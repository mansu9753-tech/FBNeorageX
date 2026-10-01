// SoundMode.cpp — 사운드 모드 DSP 구현

#include "SoundMode.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

// 소프트 클립 — 임계값 위로만 부드럽게 눌러 하드 클리핑 잡음을 막는다.
inline double softClip(double x) {
    constexpr double t = 0.80;
    const double a = std::fabs(x);
    if (a <= t) return x;
    const double over = (a - t) / (1.0 - t);
    return (x < 0.0 ? -1.0 : 1.0) * (t + (1.0 - t) * std::tanh(over));
}

// 44.1kHz 기준으로 잡힌 Freeverb 지연값을 현재 샘플레이트로 환산
inline int scaleDelay(int base44k, int sr) {
    return std::max(1, static_cast<int>(std::lround(base44k * (sr / 44100.0))));
}
}  // namespace

// ════════════════════════════════════════════════════════════
//  모드 메타데이터
// ════════════════════════════════════════════════════════════
const std::vector<SoundModeId>& soundModeAll() {
    static const std::vector<SoundModeId> all = {
        SoundModeId::Original, SoundModeId::HiFi,
        SoundModeId::Cabinet,  SoundModeId::Enhanced
    };
    return all;
}

SoundModeId soundModeFromKey(const QString& key) {
    const QString k = key.trimmed().toLower();
    if (k == "hifi"     || k == "hi-fi")   return SoundModeId::HiFi;
    if (k == "cabinet"  || k == "arcade")  return SoundModeId::Cabinet;
    if (k == "enhanced")                   return SoundModeId::Enhanced;
    return SoundModeId::Original;
}

QString soundModeKey(SoundModeId id) {
    switch (id) {
    case SoundModeId::HiFi:     return "hifi";
    case SoundModeId::Cabinet:  return "cabinet";
    case SoundModeId::Enhanced: return "enhanced";
    default:                    return "original";
    }
}

QString soundModeLabel(SoundModeId id, bool english) {
    switch (id) {
    case SoundModeId::HiFi:     return english ? "HI-FI"          : "하이파이";
    case SoundModeId::Cabinet:  return english ? "ARCADE CABINET" : "아케이드 캐비닛";
    case SoundModeId::Enhanced: return english ? "ENHANCED"       : "인핸스드";
    default:                    return english ? "ORIGINAL"       : "오리지널";
    }
}

QString soundModeDesc(SoundModeId id, bool english) {
    switch (id) {
    case SoundModeId::HiFi:
        return english
            ? "Clean playback for headphones and good speakers.\n"
              "Adds low/high extension and clears the boxy midrange."
            : "헤드폰·좋은 스피커용 맑은 재생.\n"
              "저역과 고역을 살리고 답답한 중역을 덜어냅니다.";
    case SoundModeId::Cabinet:
        return english
            ? "Simulates a real arcade cabinet speaker.\n"
              "Band-limited, midrange-forward, lightly overdriven."
            : "실제 오락실 캐비닛 스피커를 흉내냅니다.\n"
              "대역이 좁고 중역이 강하며 살짝 포화된 소리.";
    case SoundModeId::Enhanced:
        return english
            ? "Big and lively. Bass boost, wide stereo,\n"
              "and a light sense of space."
            : "크고 화려하게. 저역 부스트와 넓은 스테레오,\n"
              "옅은 공간감을 더합니다.";
    default:
        return english
            ? "No processing. The board's raw output.\n"
              "Use this as the reference sound."
            : "무처리. 기판 출력 그대로입니다.\n"
              "기준이 되는 소리입니다.";
    }
}

// ════════════════════════════════════════════════════════════
//  Biquad — RBJ Audio EQ Cookbook
// ════════════════════════════════════════════════════════════
void Biquad::normalize(double b0, double b1, double b2,
                       double a0, double a1, double a2) {
    m_b0 = b0 / a0; m_b1 = b1 / a0; m_b2 = b2 / a0;
    m_a1 = a1 / a0; m_a2 = a2 / a0;
}

void Biquad::setPassthrough() {
    m_b0 = 1.0; m_b1 = m_b2 = m_a1 = m_a2 = 0.0;
}

void Biquad::setLowShelf(double sr, double freq, double q, double gainDb) {
    const double A     = std::pow(10.0, gainDb / 40.0);
    const double w0    = 2.0 * kPi * freq / sr;
    const double cosw  = std::cos(w0), sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * q);
    const double tsa   = 2.0 * std::sqrt(A) * alpha;
    normalize(A * ((A + 1) - (A - 1) * cosw + tsa),
              2 * A * ((A - 1) - (A + 1) * cosw),
              A * ((A + 1) - (A - 1) * cosw - tsa),
              (A + 1) + (A - 1) * cosw + tsa,
              -2 * ((A - 1) + (A + 1) * cosw),
              (A + 1) + (A - 1) * cosw - tsa);
}

void Biquad::setHighShelf(double sr, double freq, double q, double gainDb) {
    const double A     = std::pow(10.0, gainDb / 40.0);
    const double w0    = 2.0 * kPi * freq / sr;
    const double cosw  = std::cos(w0), sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * q);
    const double tsa   = 2.0 * std::sqrt(A) * alpha;
    normalize(A * ((A + 1) + (A - 1) * cosw + tsa),
              -2 * A * ((A - 1) + (A + 1) * cosw),
              A * ((A + 1) + (A - 1) * cosw - tsa),
              (A + 1) - (A - 1) * cosw + tsa,
              2 * ((A - 1) - (A + 1) * cosw),
              (A + 1) - (A - 1) * cosw - tsa);
}

void Biquad::setPeaking(double sr, double freq, double q, double gainDb) {
    const double A     = std::pow(10.0, gainDb / 40.0);
    const double w0    = 2.0 * kPi * freq / sr;
    const double cosw  = std::cos(w0), sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * q);
    normalize(1 + alpha * A, -2 * cosw, 1 - alpha * A,
              1 + alpha / A, -2 * cosw, 1 - alpha / A);
}

void Biquad::setHighPass(double sr, double freq, double q) {
    const double w0    = 2.0 * kPi * freq / sr;
    const double cosw  = std::cos(w0), sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * q);
    normalize((1 + cosw) / 2, -(1 + cosw), (1 + cosw) / 2,
              1 + alpha, -2 * cosw, 1 - alpha);
}

void Biquad::setLowPass(double sr, double freq, double q) {
    const double w0    = 2.0 * kPi * freq / sr;
    const double cosw  = std::cos(w0), sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * q);
    normalize((1 - cosw) / 2, 1 - cosw, (1 - cosw) / 2,
              1 + alpha, -2 * cosw, 1 - alpha);
}

// ════════════════════════════════════════════════════════════
//  콤 / 올패스
// ════════════════════════════════════════════════════════════
void CombFilter::setSize(int samples) {
    m_buf.assign(std::max(1, samples), 0.0);
    m_idx = 0; m_store = 0.0;
}
void CombFilter::reset() {
    std::fill(m_buf.begin(), m_buf.end(), 0.0);
    m_idx = 0; m_store = 0.0;
}
void AllPassFilter::setSize(int samples) {
    m_buf.assign(std::max(1, samples), 0.0);
    m_idx = 0;
}
void AllPassFilter::reset() {
    std::fill(m_buf.begin(), m_buf.end(), 0.0);
    m_idx = 0;
}

// ════════════════════════════════════════════════════════════
//  SoundProcessor
// ════════════════════════════════════════════════════════════
void SoundProcessor::setSampleRate(int sr) {
    if (sr <= 0 || sr == m_sr) return;
    m_sr = sr;
    rebuild();
}

void SoundProcessor::setMode(SoundModeId id) {
    if (id == m_mode) return;
    m_mode = id;
    rebuild();
    reset();     // 이전 모드의 필터 상태가 남아 첫 프레임에 튀는 것 방지
}

void SoundProcessor::reset() {
    for (int c = 0; c < 2; ++c) {
        m_lowShelf[c].reset(); m_midPeak[c].reset(); m_highShelf[c].reset();
        m_hpf[c].reset();      m_lpf[c].reset();
        for (int i = 0; i < kCombs;   ++i) m_comb[c][i].reset();
        for (int i = 0; i < kAllPass; ++i) m_ap  [c][i].reset();
    }
}

void SoundProcessor::rebuild() {
    const double sr = m_sr;

    // 기본값: 전부 통과, 효과 없음
    for (int c = 0; c < 2; ++c) {
        m_lowShelf[c].setPassthrough(); m_midPeak[c].setPassthrough();
        m_highShelf[c].setPassthrough();
        m_hpf[c].setPassthrough();      m_lpf[c].setPassthrough();
    }
    m_useReverb = false; m_reverbWet = 0.0;
    m_width = 1.0; m_drive = 1.0; m_outGain = 1.0;

    switch (m_mode) {
    case SoundModeId::HiFi:
        // 저역·고역을 넓히고, 좁은 스피커에서 생기는 중역 뭉침을 살짝 덜어낸다.
        for (int c = 0; c < 2; ++c) {
            m_lowShelf [c].setLowShelf (sr,   90.0, 0.707,  3.5);
            m_midPeak  [c].setPeaking  (sr,  500.0, 1.0,   -1.5);
            m_highShelf[c].setHighShelf(sr, 7000.0, 0.707,  4.0);
        }
        m_width   = 1.25;
        m_outGain = 1.02;      // 1kHz 를 원음과 같은 크기로 (피크는 리미터가 처리)
        break;

    case SoundModeId::Cabinet:
        // 캐비닛의 작은 스피커: 저역이 안 나오고 고역도 일찍 잘린다.
        // 중역(호른 대역)이 앞으로 나오고, 값싼 앰프라 살짝 포화된다.
        for (int c = 0; c < 2; ++c) {
            m_hpf    [c].setHighPass(sr,  160.0, 0.707);
            m_midPeak[c].setPeaking (sr, 1600.0, 0.9,   3.0);
            m_lpf    [c].setLowPass (sr, 7500.0, 0.707);
        }
        m_width   = 0.35;      // 스피커가 붙어 있어 거의 모노로 들린다
        m_drive   = 1.45;
        m_outGain = 0.72;      // 중역 강조분을 상쇄 — 모드 간 음량차 최소화
        break;

    case SoundModeId::Enhanced:
        // 저역을 키우고 존재감을 더한 뒤 스테레오를 넓히고 옅은 잔향을 섞는다.
        for (int c = 0; c < 2; ++c) {
            m_lowShelf [c].setLowShelf (sr,    70.0, 0.707, 5.0);
            m_midPeak  [c].setPeaking  (sr,  3000.0, 1.2,   2.5);
            m_highShelf[c].setHighShelf(sr, 10000.0, 0.707, 3.0);
        }
        m_width     = 1.5;
        m_useReverb = true;
        m_reverbWet = 0.12;    // 게임 효과음이 뭉개지지 않을 만큼만
        m_outGain   = 0.95;    // 저역 부스트분만 살짝 양보

        // Freeverb 표준 지연값. 오른쪽은 조금 밀어 스테레오감을 만든다.
        {
            static const int combBase[kCombs]   = {1116, 1188, 1277, 1356};
            static const int apBase  [kAllPass] = { 556,  441};
            constexpr int spread = 23;
            for (int c = 0; c < 2; ++c) {
                const int off = (c == 1) ? spread : 0;
                for (int i = 0; i < kCombs; ++i) {
                    m_comb[c][i].setSize(scaleDelay(combBase[i] + off, m_sr));
                    m_comb[c][i].setParams(0.78, 0.35);
                }
                for (int i = 0; i < kAllPass; ++i)
                    m_ap[c][i].setSize(scaleDelay(apBase[i] + off, m_sr));
            }
        }
        break;

    case SoundModeId::Original:
    default:
        break;     // 바이패스
    }
}

void SoundProcessor::process(int16_t* pcm, int frames) {
    if (m_mode == SoundModeId::Original || !pcm || frames <= 0) return;

    for (int i = 0; i < frames; ++i) {
        double l = pcm[i * 2]     / 32768.0;
        double r = pcm[i * 2 + 1] / 32768.0;

        // ── EQ 체인 ──────────────────────────────────────────
        l = m_hpf[0].process(l); r = m_hpf[1].process(r);
        l = m_lowShelf[0].process(l);  r = m_lowShelf[1].process(r);
        l = m_midPeak[0].process(l);   r = m_midPeak[1].process(r);
        l = m_highShelf[0].process(l); r = m_highShelf[1].process(r);
        l = m_lpf[0].process(l); r = m_lpf[1].process(r);

        // ── 소프트 새츄레이션 (캐비닛 앰프 느낌) ─────────────
        if (m_drive > 1.0) {
            l = std::tanh(l * m_drive) / std::tanh(m_drive);
            r = std::tanh(r * m_drive) / std::tanh(m_drive);
        }

        // ── 잔향 (ENHANCED) ──────────────────────────────────
        if (m_useReverb) {
            const double in = (l + r) * 0.5 * 0.6;
            double wl = 0.0, wr = 0.0;
            for (int k = 0; k < kCombs; ++k) {
                wl += m_comb[0][k].process(in);
                wr += m_comb[1][k].process(in);
            }
            wl /= kCombs; wr /= kCombs;
            for (int k = 0; k < kAllPass; ++k) {
                wl = m_ap[0][k].process(wl);
                wr = m_ap[1][k].process(wr);
            }
            l += wl * m_reverbWet;
            r += wr * m_reverbWet;
        }

        // ── 스테레오 폭 (Mid/Side) ───────────────────────────
        if (m_width != 1.0) {
            const double mid  = (l + r) * 0.5;
            const double side = (l - r) * 0.5 * m_width;
            l = mid + side;
            r = mid - side;
        }

        // ── 출력 게인 + 소프트 리미터 ────────────────────────
        l = softClip(l * m_outGain);
        r = softClip(r * m_outGain);

        pcm[i * 2]     = static_cast<int16_t>(
            std::lround(std::clamp(l, -1.0, 1.0) * 32767.0));
        pcm[i * 2 + 1] = static_cast<int16_t>(
            std::lround(std::clamp(r, -1.0, 1.0) * 32767.0));
    }
}
