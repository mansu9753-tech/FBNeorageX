// AudioManager.cpp — Pull-mode 링버퍼 오디오
// Python SDL2 audio_callback 방식과 동일한 구조:
//   하드웨어가 readData()를 직접 호출 → 에뮬 타이밍과 완전 분리 → 끊김 없음

#include "AudioManager.h"
#include "EmulatorState.h"
#include "AppSettings.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QDebug>
#include <cstring>
#include <algorithm>

// ════════════════════════════════════════════════════════════
//  AudioRingBuffer
// ════════════════════════════════════════════════════════════

AudioRingBuffer::AudioRingBuffer(QObject* parent)
    : QIODevice(parent)
{
    open(QIODevice::ReadOnly);
}

void AudioRingBuffer::resetBuffer() {
    QMutexLocker lock(&m_mutex);
    m_readPos  = 0;
    m_writePos = 0;
    m_used     = 0;
    memset(m_ring, 0, RING_BYTES);
}

// ── readData — QAudioSink 하드웨어 스레드에서 호출 ────────────
// 데이터가 있으면 반환, 없으면 무음(0) 반환 → 팝 노이즈 방지
qint64 AudioRingBuffer::readData(char* data, qint64 maxSize) {
    QMutexLocker lock(&m_mutex);

    // 4바이트(int16 스테레오) 정렬
    qint64 aligned = maxSize & ~3LL;
    if (aligned <= 0) return 0;

    qint64 toRead = std::min((qint64)m_used, aligned);

    if (toRead > 0) {
        // 링 버퍼에서 읽기 (wrap-around 처리)
        qint64 part1 = std::min(toRead, (qint64)(RING_BYTES - m_readPos));
        memcpy(data, m_ring + m_readPos, (size_t)part1);
        if (toRead > part1)
            memcpy(data + part1, m_ring, (size_t)(toRead - part1));
        m_readPos = (int)((m_readPos + toRead) % RING_BYTES);
        m_used   -= (int)toRead;
    }

    // 나머지 부분은 무음으로 채움 → 팝/클릭 제거
    if (toRead < aligned)
        memset(data + toRead, 0, (size_t)(aligned - toRead));

    return aligned;
}

// ── writeData — 에뮬 메인 스레드에서 호출 ────────────────────
// 링버퍼가 가득 차면 가장 오래된 데이터를 덮어씀 (드리프트 방지)
qint64 AudioRingBuffer::writeData(const char* data, qint64 size) {
    QMutexLocker lock(&m_mutex);

    int toWrite = (int)(size & ~3LL);  // 4바이트 정렬
    if (toWrite <= 0) return size;
    if (toWrite > RING_BYTES) {
        // 너무 큰 경우: 최신 데이터만 유지
        data   += (toWrite - RING_BYTES);
        toWrite = RING_BYTES;
    }

    // 공간 부족 시 오래된 데이터 버림
    if (m_used + toWrite > RING_BYTES) {
        int drop = m_used + toWrite - RING_BYTES;
        m_readPos = (m_readPos + drop) % RING_BYTES;
        m_used   -= drop;
    }

    // 링 버퍼에 쓰기 (wrap-around 처리)
    int part1 = std::min(toWrite, RING_BYTES - m_writePos);
    memcpy(m_ring + m_writePos, data, (size_t)part1);
    if (toWrite > part1)
        memcpy(m_ring, data + part1, (size_t)(toWrite - part1));
    m_writePos = (m_writePos + toWrite) % RING_BYTES;
    m_used    += toWrite;

    return size;
}

qint64 AudioRingBuffer::bytesAvailable() const {
    QMutexLocker lock(&m_mutex);
    return m_used + QIODevice::bytesAvailable();
}

int AudioRingBuffer::usedBytes() const {
    QMutexLocker lock(&m_mutex);
    return m_used;
}

// ════════════════════════════════════════════════════════════
//  AudioManager
// ════════════════════════════════════════════════════════════

AudioManager::AudioManager(QObject* parent)
    : QObject(parent)
{}

AudioManager::~AudioManager() {
    shutdown();
}

// ── init ─────────────────────────────────────────────────────
bool AudioManager::init(int sampleRate, int bufferMs) {
    shutdown();

    m_coreSampleRate = sampleRate;   // 코어가 출력하는 샘플레이트 (e.g. 44100)
    m_bufferMs       = bufferMs;

    QAudioDevice dev = QMediaDevices::defaultAudioOutput();

    // ── 하드웨어 native rate 탐지 ─────────────────────────────
    // PipeWire / PulseAudio 는 preferredFormat()으로 실제 HW 레이트를 알려줌
    // (isFormatSupported 는 모든 레이트에 true 를 반환하므로 신뢰 불가)
    // Steam Deck: preferredFormat = 48000 Hz
    int hwRate = dev.preferredFormat().sampleRate();
    if (hwRate <= 0) hwRate = 48000;          // 기본값
    m_hwSampleRate = hwRate;

    // SRC 기본비율: coreSampleRate / hwSampleRate
    //   ratio < 1.0 → resampler 가 출력 프레임 수를 늘림 (업샘플)
    //   ratio > 1.0 → resampler 가 출력 프레임 수를 줄임 (다운샘플)
    m_baseRatio = (double)m_coreSampleRate / (double)m_hwSampleRate;

    QAudioFormat fmt;
    fmt.setSampleRate(hwRate);   // HW native rate 사용 → PipeWire 내부 리샘플 없음
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    if (!dev.isFormatSupported(fmt)) {
        qWarning() << "AudioManager: HW 포맷 미지원 → preferredFormat 사용";
        fmt = dev.preferredFormat();
        m_hwSampleRate = fmt.sampleRate();
        m_baseRatio    = (double)m_coreSampleRate / (double)m_hwSampleRate;
    }

    // 링버퍼 생성
    m_ringBuf = std::make_unique<AudioRingBuffer>(this);
    m_ringBuf->resetBuffer();

    // QAudioSink 생성 (HW native rate)
    m_sink = std::make_unique<QAudioSink>(dev, fmt);

    // 하드웨어 버퍼: 최소 40ms (PipeWire < 40ms 요청 시 불안정)
    // HW 샘플레이트 기준으로 계산
    int hwMs = std::max(m_bufferMs, 40);
    qsizetype hwBufBytes = static_cast<qsizetype>(
        m_hwSampleRate * 2 * 2 * hwMs / 1000);
    m_sink->setBufferSize(hwBufBytes);

    // DRC 목표: bufferMs × 1.5 (HW 샘플레이트 기준)
    m_targetBytes = m_hwSampleRate * 4 * (m_bufferMs * 3 / 2) / 1000;
    if (m_targetBytes < 4096)  m_targetBytes = 4096;
    if (m_targetBytes > AudioRingBuffer::RING_BYTES / 2)
        m_targetBytes = AudioRingBuffer::RING_BYTES / 2;
    m_pid.reset();
    m_resampler.reset();  // 이전 게임의 리샘플러 위상이 남으면 첫 프레임 노이즈 발생

    // 링버퍼 선채움 (start() 전 — race-free)
    // SRC로 생산속도 ≒ 소비속도가 맞춰지므로 pre-fill 후 안정 유지
    {
        QByteArray silence(m_targetBytes, '\0');
        m_ringBuf->pushData(silence.constData(), silence.size());
    }

    // 풀모드 시작
    m_sink->start(m_ringBuf.get());

    if (m_sink->state() == QAudio::StoppedState) {
        qWarning() << "AudioManager: QAudioSink pull-mode 시작 실패";
        m_sink.reset();
        m_ringBuf.reset();
        return false;
    }

    setVolume(gSettings.audioVolume / 100.0);

    qDebug() << "AudioManager: SRC+DRC 초기화 완료 —"
             << "core=" << m_coreSampleRate << "Hz"
             << "hw=" << m_hwSampleRate << "Hz"
             << "baseRatio=" << m_baseRatio
             << "buf=" << m_bufferMs << "ms (hw=" << hwMs << "ms)"
             << "hwBuf=" << hwBufBytes << "B"
             << "target=" << m_targetBytes << "B";
    return true;
}

void AudioManager::shutdown() {
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    if (m_ringBuf) {
        m_ringBuf->resetBuffer();
        m_ringBuf.reset();
    }
}

// ── 볼륨 ─────────────────────────────────────────────────────
void AudioManager::setVolume(double v) {
    if (m_sink) m_sink->setVolume(static_cast<float>(std::clamp(v, 0.0, 1.0)));
}

double AudioManager::volume() const {
    return m_sink ? static_cast<double>(m_sink->volume()) : 0.0;
}

// ── flush ─────────────────────────────────────────────────────
// QAudioSink 재시작 없이 링버퍼/PID/리샘플러만 초기화 + 선채움
// 일시정지 재개: 링버퍼가 비어있어 DRC 재수렴에 ~25초 걸리는 문제 해결
// 게임 전환:    리샘플러 phase 누적으로 인한 초기 노이즈 해결
void AudioManager::flush() {
    if (!m_ringBuf) return;
    m_ringBuf->resetBuffer();
    m_pid.reset();
    m_resampler.reset();
    gState.audioPending.clear();   // 재개 전 잔류 오디오 제거

    // 목표량만큼 무음 선채움 → 재개 직후 안정적인 오디오 공급
    if (m_targetBytes > 0) {
        QByteArray silence(m_targetBytes, '\0');
        m_ringBuf->pushData(silence.constData(), silence.size());
    }
}

// ── processDrc — 매 프레임 호출 ──────────────────────────────
// SRC + DRC 통합:
//   ratio = baseRatio × pidFactor
//   baseRatio   = coreSampleRate / hwSampleRate  (샘플레이트 변환)
//   pidFactor   = 1.0 ± maxAdj                  (링버퍼 레벨 미세 보정)
//
//   ratio < 1.0 → 출력 프레임 수 증가 (업샘플 / 버퍼 충전)
//   ratio > 1.0 → 출력 프레임 수 감소 (다운샘플 / 버퍼 소진)
void AudioManager::processDrc(int preAudioSize) {
    Q_UNUSED(preAudioSize)
    if (!m_ringBuf) return;

    // 넷플레이 재시뮬 중 무음
    if (gState.netplayResim) {
        gState.audioPending.clear();
        return;
    }

    if (gState.audioPending.isEmpty()) return;

    // 4바이트(int16 스테레오) 정렬
    int rawSize = gState.audioPending.size() & ~3;
    if (rawSize <= 0) return;

    // ── SRC + DRC 통합 비율 ──────────────────────────────────
    // PID 는 링버퍼 레벨 기반 미세 보정(±0.5%)
    // baseRatio 가 SRC 핵심: 44100→48000 업샘플이면 0.91875
    int    curUsed   = m_ringBuf->usedBytes();
    double pidFactor = m_pid.update(curUsed, m_targetBytes); // 1.0 ± maxAdj
    double ratio     = m_baseRatio * pidFactor;              // SRC + DRC

    // ── Catmull-Rom 분수 리샘플 (SRC + DRC 동시 처리) ────────
    QByteArray chunk = gState.audioPending.left(rawSize);
    gState.audioPending.remove(0, rawSize);

    QByteArray resampled = m_resampler.process(chunk, ratio);
    if (resampled.isEmpty()) return;

    m_ringBuf->pushData(resampled.constData(), resampled.size());
}

// ── 직접 PCM 추가 ────────────────────────────────────────────
void AudioManager::appendSamples(const int16_t* data, size_t frames) {
    gState.audioPending.append(
        reinterpret_cast<const char*>(data),
        static_cast<int>(frames * 4));
}

void AudioManager::appendSample(int16_t left, int16_t right) {
    int16_t buf[2] = {left, right};
    gState.audioPending.append(reinterpret_cast<const char*>(buf), 4);
}
