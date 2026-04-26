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

    m_sampleRate = sampleRate;
    m_bufferMs   = bufferMs;

    QAudioFormat fmt;
    fmt.setSampleRate(sampleRate);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (!dev.isFormatSupported(fmt)) {
        qWarning() << "AudioManager: 요청 포맷 미지원 → 기본 포맷 사용";
        fmt = dev.preferredFormat();
        m_sampleRate = fmt.sampleRate();
    }

    // 링버퍼 생성 및 초기화
    m_ringBuf = std::make_unique<AudioRingBuffer>(this);
    m_ringBuf->resetBuffer();

    // QAudioSink — pull 모드: 링버퍼를 직접 소스로 사용
    m_sink = std::make_unique<QAudioSink>(dev, fmt);

    // 하드웨어 버퍼 크기: bufferMs 만큼만 — 너무 크면 지연, 너무 작으면 언더런
    qsizetype hwBufBytes = static_cast<qsizetype>(
        m_sampleRate * 2 * 2 * m_bufferMs / 1000);
    m_sink->setBufferSize(hwBufBytes);

    // 풀모드 시작: QAudioSink가 readData()를 직접 호출
    m_sink->start(m_ringBuf.get());

    if (m_sink->state() == QAudio::StoppedState) {
        qWarning() << "AudioManager: QAudioSink pull-mode 시작 실패";
        m_sink.reset();
        m_ringBuf.reset();
        return false;
    }

    setVolume(gSettings.audioVolume / 100.0);

    // DRC 목표: bufferMs × 2 분량의 오디오를 링버퍼에 유지
    m_targetBytes = m_sampleRate * 4 * (m_bufferMs * 2) / 1000;
    if (m_targetBytes < 4096)  m_targetBytes = 4096;
    if (m_targetBytes > AudioRingBuffer::RING_BYTES / 2)
        m_targetBytes = AudioRingBuffer::RING_BYTES / 2;
    m_pid.reset();

    qDebug() << "AudioManager: pull-mode 초기화 완료 —"
             << m_sampleRate << "Hz,"
             << m_bufferMs << "ms, hwBuf=" << hwBufBytes << "bytes"
             << "DRC target=" << m_targetBytes << "bytes";
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

// ── processDrc — 매 프레임 호출 ──────────────────────────────
// 1) 링버퍼 점유율 → PID → 리샘플 비율 계산
// 2) Catmull-Rom 분수 리샘플러로 audio 미세 조정 (±0.4%)
// 3) 조정된 PCM을 링버퍼에 push
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

    // ── DRC 비율 계산 ────────────────────────────────────────
    int curUsed = m_ringBuf->usedBytes();
    double ratio = m_pid.update(curUsed, m_targetBytes);

    // ── Catmull-Rom 분수 리샘플 ──────────────────────────────
    // 작은 청크(< 8프레임)는 리샘플 건너뜀 — 짧은 효과음 손실 방지
    const int MIN_CHUNK_FRAMES = 8;
    QByteArray chunk = gState.audioPending.left(rawSize);
    gState.audioPending.remove(0, rawSize);

    QByteArray resampled;
    if (rawSize < MIN_CHUNK_FRAMES * 4) {
        resampled = chunk;
    } else {
        resampled = m_resampler.process(chunk, ratio);
    }
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
