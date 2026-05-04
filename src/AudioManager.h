#pragma once
// AudioManager.h — Pull-mode 링버퍼 오디오 (Python SDL2 콜백 방식과 동일한 구조)

#include <QObject>
#include <QAudioSink>
#include <QIODevice>
#include <QAudioFormat>
#include <QMutex>
#include <memory>
#include <cstdint>
#include "DrcPid.h"

// ── 풀모드 링버퍼 ─────────────────────────────────────────────
// QAudioSink가 하드웨어 타이밍에 맞춰 readData()를 직접 호출.
// 에뮬레이터 루프와 오디오 하드웨어를 완전히 분리하여 끊김 제거.
class AudioRingBuffer : public QIODevice {
    Q_OBJECT
public:
    // 링버퍼 크기: ~341ms @ 48kHz 스테레오 int16
    static constexpr int RING_FRAMES = 16384;
    static constexpr int RING_BYTES  = RING_FRAMES * 4;  // int16 스테레오 × 2ch

    explicit AudioRingBuffer(QObject* parent = nullptr);

    void resetBuffer();

    // QIODevice 가상 함수
    qint64 readData (char*       data, qint64 maxSize) override;
    qint64 writeData(const char* data, qint64 size)    override;
    bool   isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

    // 현재 링버퍼에 쌓인 바이트 수 (메인 스레드 참고용)
    int usedBytes() const;

    // 에뮬 스레드 → 링버퍼로 직접 쓰기
    void pushData(const char* data, qint64 size) { writeData(data, size); }

private:
    mutable QMutex m_mutex;
    char m_ring[RING_BYTES]{};
    int  m_readPos  = 0;
    int  m_writePos = 0;
    int  m_used     = 0;
};

// ── 오디오 매니저 ─────────────────────────────────────────────
class AudioManager : public QObject {
    Q_OBJECT
public:
    explicit AudioManager(QObject* parent = nullptr);
    ~AudioManager() override;

    // ── 초기화 / 정리 ────────────────────────────────────────
    bool init(int sampleRate = 48000, int bufferMs = 80);
    void shutdown();
    bool isReady() const { return m_sink != nullptr; }

    // 링버퍼·PID·리샘플러 리셋 + 선채움 (QAudioSink 재시작 없음)
    // 일시정지 재개 / 게임 전환 후 audio 이상 방지용
    void flush();

    // ── 볼륨 ──────────────────────────────────────────────────
    void   setVolume(double v);   // 0.0 ~ 1.0
    double volume() const;

    // ── 에뮬 루프에서 매 프레임 호출 ─────────────────────────
    void processDrc(int preAudioSize);

    // ── 오디오 콜백에서 직접 PCM 추가 ────────────────────────
    void appendSamples(const int16_t* data, size_t frames);
    void appendSample (int16_t left, int16_t right);

private:
    std::unique_ptr<QAudioSink>      m_sink;
    std::unique_ptr<AudioRingBuffer> m_ringBuf;

    int    m_coreSampleRate = 44100; // 코어(libretro)가 실제로 출력하는 샘플레이트
    int    m_hwSampleRate   = 48000; // 하드웨어(PipeWire native) 샘플레이트
    int    m_bufferMs       = 80;
    int    m_targetBytes    = 0;

    // SRC 기본비율: coreSampleRate / hwSampleRate
    //   예) 44100 / 48000 = 0.91875 — resampler 에 전달하면 업샘플 발생
    // DRC 미세조정이 이 위에 곱해짐
    double m_baseRatio = 1.0;

    // DRC: SRC 기본비율 위에 ±maxAdj(0.5%) 미세 보정
    DrcPid              m_pid  { 0.015, 0.00008, 0.003, 0.005 };
    FractionalResampler m_resampler;
};
