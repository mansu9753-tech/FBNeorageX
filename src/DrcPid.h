#pragma once
// DrcPid.h — PID 기반 Dynamic Rate Control (헤더 전용)
// Python의 DrcPid / FractionalResampler 클래스 대응

#include <QByteArray>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>

// ── PID 컨트롤러 ──────────────────────────────────────────────
// error를 target 대비 정규화(-1~+1)하여 적분기 폭주 방지
class DrcPid {
public:
    double kp, ki, kd, maxAdj;

    explicit DrcPid(double kp    = 0.04,
                    double ki    = 0.0002,
                    double kd    = 0.008,
                    double maxAdj = 0.005)
        : kp(kp), ki(ki), kd(kd), maxAdj(maxAdj) {}

    void reset() { m_integral = 0.0; m_prevError = 0.0; }

    // current: 현재 버퍼 점유 바이트, target: 목표 바이트
    // 반환값: 리샘플 비율 (1.0 ± maxAdj)
    double update(int current, int target) {
        if (target <= 0) return 1.0;
        double error     = static_cast<double>(current - target) / target;
        m_integral       = std::clamp(m_integral + error, -20.0, 20.0);
        double deriv     = error - m_prevError;
        m_prevError      = error;
        double adj       = kp * error + ki * m_integral + kd * deriv;
        return 1.0 + std::clamp(adj, -maxAdj, maxAdj);
    }

private:
    double m_integral  = 0.0;
    double m_prevError = 0.0;
};

// ── Catmull-Rom 위상 연속 분수 리샘플러 ───────────────────────
class FractionalResampler {
public:
    void reset() {
        m_phase = 0.0;
    }

    // data: int16 스테레오 인터리브 PCM (4바이트/프레임)
    // ratio > 1.0 → 출력 감소(버퍼 소진 가속)
    // ratio < 1.0 → 출력 증가(버퍼 충전 가속)
    QByteArray process(const QByteArray& data, double ratio) {
        if (data.isEmpty() || ratio <= 0.0) return data;

        const int16_t* src = reinterpret_cast<const int16_t*>(data.constData());
        int srcFrames = data.size() / 4;

        // 출력 프레임 수 (최소 1 보장)
        int dstFrames = std::max(1, static_cast<int>(std::floor(srcFrames / ratio)));

        QByteArray out(dstFrames * 4, '\0');
        int16_t* dst = reinterpret_cast<int16_t*>(out.data());

        double pos = m_phase;
        for (int i = 0; i < dstFrames; ++i) {
            int    p0 = static_cast<int>(std::floor(pos));
            double t  = pos - static_cast<double>(p0);

            for (int ch = 0; ch < 2; ++ch) {
                auto getSample = [&](int idx) -> double {
                    if (idx < 0)          idx = 0;
                    if (idx >= srcFrames) idx = srcFrames - 1;
                    return static_cast<double>(src[idx * 2 + ch]);
                };
                double y0 = getSample(p0 - 1);
                double y1 = getSample(p0);
                double y2 = getSample(p0 + 1);
                double y3 = getSample(p0 + 2);
                double v  = catmullRom(y0, y1, y2, y3, t);
                dst[i * 2 + ch] =
                    static_cast<int16_t>(std::clamp(v, -32768.0, 32767.0));
            }
            pos += ratio;
        }

        // 위상 갱신 — 0 이상으로 클램프
        m_phase = pos - static_cast<double>(srcFrames);
        if (m_phase < 0.0) m_phase = 0.0;

        return out;
    }

private:
    double m_phase = 0.0;

    static double catmullRom(double y0, double y1,
                             double y2, double y3, double t) {
        const double t2 = t * t, t3 = t2 * t;
        return 0.5 * ((2.0 * y1)
                    + (-y0 + y2)                        * t
                    + (2.0*y0 - 5.0*y1 + 4.0*y2 - y3)  * t2
                    + (-y0 + 3.0*y1 - 3.0*y2 + y3)      * t3);
    }
};
