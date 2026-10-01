#pragma once
// SoundMode.h — 사운드 모드 (게임별로 고르는 음색 프리셋)
//
//  아케이드 기판의 출력은 대역이 좁고 밋밋하다. 어떤 환경에서 듣느냐에 따라
//  원하는 소리가 달라지므로, 출력단에 프리셋 이펙트 체인을 하나 둔다.
//
//    ORIGINAL  — 무처리(바이패스). 기판 출력 그대로. 기준음.
//    HI-FI     — 헤드폰/좋은 스피커용. 저역·고역을 살리고 중역 답답함을 덜어냄.
//    CABINET   — 실제 오락실 캐비닛 흉내. 대역제한 + 중역 강조 + 살짝 포화.
//    ENHANCED  — 넓고 화려하게. 저역 부스트 + 스테레오 확장 + 옅은 공간감.
//
//  모든 처리는 순수 C++ 이라 Windows/스팀덱(Linux) 동일하게 동작한다.
//  리샘플링이 끝난 하드웨어 샘플레이트 기준으로 적용된다.

#include <QString>
#include <cstdint>
#include <vector>

// ── 모드 정의 ────────────────────────────────────────────────
enum class SoundModeId { Original = 0, HiFi, Cabinet, Enhanced };

// 설정 파일에 저장되는 문자열 ↔ enum 변환
SoundModeId soundModeFromKey(const QString& key);
QString     soundModeKey    (SoundModeId id);
// UI 표시용 이름 / 설명 (한글·영문)
QString     soundModeLabel  (SoundModeId id, bool english);
QString     soundModeDesc   (SoundModeId id, bool english);
// 콤보박스 구성을 위한 전체 목록 (표시 순서)
const std::vector<SoundModeId>& soundModeAll();

// ── 2차 IIR (RBJ 쿡북) ───────────────────────────────────────
//  전치형 direct form II — 계수 변경 시에도 상태가 튀지 않는다.
class Biquad {
public:
    void  reset() { m_z1 = m_z2 = 0.0; }
    void  setPassthrough();
    void  setLowShelf (double sr, double freq, double q, double gainDb);
    void  setHighShelf(double sr, double freq, double q, double gainDb);
    void  setPeaking  (double sr, double freq, double q, double gainDb);
    void  setHighPass (double sr, double freq, double q);
    void  setLowPass  (double sr, double freq, double q);

    inline double process(double x) {
        const double y = m_b0 * x + m_z1;
        m_z1 = m_b1 * x - m_a1 * y + m_z2;
        m_z2 = m_b2 * x - m_a2 * y;
        return y;
    }

private:
    void normalize(double b0, double b1, double b2,
                   double a0, double a1, double a2);
    double m_b0 = 1.0, m_b1 = 0.0, m_b2 = 0.0;
    double m_a1 = 0.0, m_a2 = 0.0;
    double m_z1 = 0.0, m_z2 = 0.0;
};

// ── 감쇠 콤 필터 (Freeverb 계열) ───────────────────────────
class CombFilter {
public:
    void  setSize(int samples);
    void  setParams(double feedback, double damp) { m_fb = feedback; m_damp = damp; }
    void  reset();
    inline double process(double x) {
        if (m_buf.empty()) return 0.0;
        const double out = m_buf[m_idx];
        m_store = out * (1.0 - m_damp) + m_store * m_damp;
        m_buf[m_idx] = x + m_store * m_fb;
        if (++m_idx >= static_cast<int>(m_buf.size())) m_idx = 0;
        return out;
    }
private:
    std::vector<double> m_buf;
    int    m_idx   = 0;
    double m_store = 0.0;
    double m_fb    = 0.5;
    double m_damp  = 0.2;
};

// ── 올패스 (공간감 확산) ─────────────────────────────────────
class AllPassFilter {
public:
    void  setSize(int samples);
    void  reset();
    inline double process(double x) {
        if (m_buf.empty()) return x;
        const double buf = m_buf[m_idx];
        const double out = -x + buf;
        m_buf[m_idx] = x + buf * m_g;
        if (++m_idx >= static_cast<int>(m_buf.size())) m_idx = 0;
        return out;
    }
private:
    std::vector<double> m_buf;
    int    m_idx = 0;
    double m_g   = 0.5;
};

// ── 사운드 프로세서 ──────────────────────────────────────────
//  오디오 스레드가 아니라 에뮬 루프(processDrc)에서 호출된다.
//  ORIGINAL 일 때는 process() 가 즉시 반환하므로 부하가 0 이다.
class SoundProcessor {
public:
    // 샘플레이트가 바뀌면 계수를 다시 만든다 (같은 값이면 아무 것도 안 함)
    void setSampleRate(int sr);
    void setMode(SoundModeId id);
    SoundModeId mode() const { return m_mode; }

    // 필터 상태만 비운다 (게임 전환·일시정지 복귀 시 잔향/잡음 제거)
    void reset();

    // 인터리브 int16 스테레오를 제자리에서 처리
    void process(int16_t* pcm, int frames);

private:
    void rebuild();

    SoundModeId m_mode = SoundModeId::Original;
    int         m_sr   = 48000;

    // 채널별 필터 체인 (0=L, 1=R)
    Biquad m_lowShelf[2], m_midPeak[2], m_highShelf[2], m_hpf[2], m_lpf[2];

    // 리버브 (ENHANCED 전용)
    static constexpr int kCombs   = 4;
    static constexpr int kAllPass = 2;
    CombFilter    m_comb[2][kCombs];
    AllPassFilter m_ap  [2][kAllPass];

    bool   m_useReverb = false;
    double m_reverbWet = 0.0;

    double m_width     = 1.0;   // 1.0=원본, >1 확장, <1 모노화
    double m_drive     = 1.0;   // 소프트 새츄레이션 강도 (1.0=없음)
    double m_outGain   = 1.0;   // 최종 게인
};
