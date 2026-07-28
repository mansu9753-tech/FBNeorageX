// PreviewVideo.cpp — 프리뷰 영상 소프트웨어 디코더 (FFmpeg)
#include "PreviewVideo.h"

#include <QDebug>
#include <QFileInfo>

#if HAVE_FFMPEG
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QIODevice>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// 프리뷰 오디오 출력 규격 (48kHz 스테레오 16bit)
static constexpr int kPvRate = 48000;
static constexpr int kPvCh   = 2;
#endif

PreviewVideo::PreviewVideo(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    // 짧은 주기로 깨어나 "표시할 때가 됐는지"만 확인한다 (실제 속도는 PTS 가 결정)
    connect(m_timer, &QTimer::timeout, this, [this]{ tick(); });
}

PreviewVideo::~PreviewVideo() { close(); }

void PreviewVideo::stop() {
    if (m_timer) m_timer->stop();
    close();
}

#if !HAVE_FFMPEG
// FFmpeg 없이 빌드된 경우(예: Windows 정적 빌드) — 이 클래스는 사용되지 않는다.
bool PreviewVideo::open(const QString&) { return false; }
void PreviewVideo::close() {}
void PreviewVideo::tick() {}
void PreviewVideo::flushAudio() {}
bool PreviewVideo::pumpOnce() { return false; }
qint64 PreviewVideo::clockUsec() const { return 0; }
void PreviewVideo::openAudio() {}
void PreviewVideo::feedAudio(AVPacket*) {}
void PreviewVideo::setVolume(int v) { m_volume = v; }
#else

void PreviewVideo::setVolume(int percent) {
    m_volume = qBound(0, percent, 100);
    if (m_sink) m_sink->setVolume(m_volume / 100.0);
}

// ── 오디오 스트림 준비 (없으면 무음으로 진행) ────────────────
void PreviewVideo::openAudio() {
    const AVCodec* acodec = nullptr;
    m_astream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &acodec, 0);
    if (m_astream < 0 || !acodec) return;          // 오디오 없음 — 정상

    m_adec = avcodec_alloc_context3(acodec);
    if (!m_adec) { m_astream = -1; return; }
    avcodec_parameters_to_context(m_adec, m_fmt->streams[m_astream]->codecpar);
    if (avcodec_open2(m_adec, acodec, nullptr) < 0) {
        avcodec_free_context(&m_adec); m_astream = -1; return;
    }

    // 입력 채널 레이아웃이 비어 있으면 채널 수로 기본값 설정
    AVChannelLayout inLayout;
    if (m_adec->ch_layout.nb_channels > 0 &&
        m_adec->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
        av_channel_layout_copy(&inLayout, &m_adec->ch_layout);
    } else {
        av_channel_layout_default(&inLayout,
            m_adec->ch_layout.nb_channels > 0 ? m_adec->ch_layout.nb_channels : 2);
    }
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kPvCh);

    if (swr_alloc_set_opts2(&m_swr, &outLayout, AV_SAMPLE_FMT_S16, kPvRate,
                            &inLayout, m_adec->sample_fmt, m_adec->sample_rate,
                            0, nullptr) < 0 || swr_init(m_swr) < 0) {
        if (m_swr) { swr_free(&m_swr); }
        avcodec_free_context(&m_adec); m_astream = -1;
        av_channel_layout_uninit(&inLayout); av_channel_layout_uninit(&outLayout);
        return;
    }
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);

    QAudioFormat fmt;
    fmt.setSampleRate(kPvRate);
    fmt.setChannelCount(kPvCh);
    fmt.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) {                 // 출력 장치 없음 → 무음으로 진행
        swr_free(&m_swr); avcodec_free_context(&m_adec); m_astream = -1; return;
    }
    m_sink = new QAudioSink(dev, fmt, this);
    // 버퍼 여유(약 200ms) — 프레임 준비가 잠깐 늦어도 소리가 끊기지 않게
    m_sink->setBufferSize(kPvRate * kPvCh * 2 / 5);
    m_sink->setVolume(m_volume / 100.0);
    m_sinkIo = m_sink->start();         // push 모드
    m_aframe = av_frame_alloc();
}

// ── 오디오 패킷 → PCM 변환 → 출력 ────────────────────────────
void PreviewVideo::feedAudio(AVPacket* p) {
    if (!m_adec || !m_swr || !m_aframe) return;
    if (avcodec_send_packet(m_adec, p) < 0) return;

    while (avcodec_receive_frame(m_adec, m_aframe) == 0) {
        const int maxOut = swr_get_out_samples(m_swr, m_aframe->nb_samples);
        if (maxOut <= 0) continue;

        QByteArray buf(maxOut * kPvCh * 2, Qt::Uninitialized);   // S16 스테레오
        uint8_t* out[1] = { reinterpret_cast<uint8_t*>(buf.data()) };
        const int got = swr_convert(m_swr, out, maxOut,
                                    const_cast<const uint8_t**>(m_aframe->data),
                                    m_aframe->nb_samples);
        if (got > 0) {
            buf.resize(got * kPvCh * 2);
            // ★ 여기서 바로 write() 하지 않는다. 사인크가 가득 차 있으면 일부만
            //   받아가고 나머지는 사라져 소리가 끊긴다. 큐에 모아 두고
            //   flushAudio() 가 받아주는 만큼씩 안전하게 밀어넣는다.
            m_apend.append(buf);
        }
    }
}

bool PreviewVideo::open(const QString& path) {
    close();
    if (!QFileInfo::exists(path)) return false;

    const QByteArray p = path.toUtf8();
    if (avformat_open_input(&m_fmt, p.constData(), nullptr, nullptr) < 0) {
        emit failed("파일 열기 실패");
        return false;
    }
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) {
        emit failed("스트림 정보 없음"); close(); return false;
    }

    // 비디오 스트림 선택 (오디오는 재생하지 않는다 — 프리뷰는 무음)
    const AVCodec* codec = nullptr;
    m_stream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (m_stream < 0 || !codec) {
        emit failed("비디오 스트림 없음"); close(); return false;
    }

    m_dec = avcodec_alloc_context3(codec);
    if (!m_dec) { close(); return false; }
    avcodec_parameters_to_context(m_dec, m_fmt->streams[m_stream]->codecpar);

    // ★ 소프트웨어 디코딩 전용 — 하드웨어 컨텍스트를 일절 만들지 않는다.
    //   (Qt 백엔드가 VAAPI/Vulkan 을 잡다가 죽던 문제의 원인 제거)
    m_dec->thread_count = 1;

    if (avcodec_open2(m_dec, codec, nullptr) < 0) {
        emit failed("디코더 열기 실패"); close(); return false;
    }

    m_w = m_dec->width;
    m_h = m_dec->height;
    if (m_w <= 0 || m_h <= 0) { emit failed("해상도 불명"); close(); return false; }

    m_frame = av_frame_alloc();
    m_rgb   = av_frame_alloc();
    m_pkt   = av_packet_alloc();
    if (!m_frame || !m_rgb || !m_pkt) { close(); return false; }

    const int bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, m_w, m_h, 1);
    m_rgbBuf = static_cast<unsigned char*>(av_malloc(static_cast<size_t>(bytes)));
    if (!m_rgbBuf) { close(); return false; }
    av_image_fill_arrays(m_rgb->data, m_rgb->linesize, m_rgbBuf,
                         AV_PIX_FMT_RGB24, m_w, m_h, 1);

    // ★ 색변환기는 여기서 만들지 않는다.
    //   avcodec_open2 직후의 m_dec->pix_fmt 는 아직 확정 전이거나 실제
    //   프레임 포맷과 다를 수 있어, 그 값으로 변환하면 색이 뒤틀린다
    //   (전체가 파랗고 검정이 빨갛게 보이던 증상).
    //   → 첫 프레임을 받은 뒤 그 프레임의 실제 포맷으로 생성한다.

    // 표시 시각 계산용 time_base (PTS × time_base = 초)
    m_vTimeBase = av_q2d(m_fmt->streams[m_stream]->time_base);
    if (m_vTimeBase <= 0.0) m_vTimeBase = 1.0 / 30.0;

    openAudio();      // 오디오가 있으면 함께 재생 (없으면 무음)

    m_vq.clear();
    m_apend.clear();
    m_eof = false;
    // 프레임 대기열 상한: 메모리 과다 사용 방지 (대략 24MB 이내)
    const qint64 frameBytes = static_cast<qint64>(m_w) * m_h * 3;
    const qint64 fit = (24LL << 20) / qMax<qint64>(1, frameBytes);
    m_vqMax = static_cast<int>(qBound<qint64>(4LL, fit, 16LL));

    m_elapsed.start();          // 오디오가 없을 때의 기준 시계
    m_timer->start(4);          // 4ms 마다 확인 (실제 속도는 PTS/오디오 클럭이 결정)
    return true;
}

// 재생 기준 시계(usec) — 오디오가 있으면 실제로 재생된 오디오량을 따른다.
//   오디오 하드웨어가 소비한 만큼을 기준으로 영상을 맞추므로 싱크가 벌어지지 않는다.
qint64 PreviewVideo::clockUsec() const {
    if (m_sink) return m_sink->processedUSecs();
    return m_elapsed.isValid() ? (m_elapsed.nsecsElapsed() / 1000) : 0;
}

// 사인크가 받아주는 만큼만 쓰고, 남은 것은 보관한다 (버리면 소리에 구멍이 남)
void PreviewVideo::flushAudio() {
    if (!m_sinkIo || m_apend.isEmpty()) return;
    const qint64 n = m_sinkIo->write(m_apend);
    if (n > 0) m_apend.remove(0, static_cast<int>(n));
}

void PreviewVideo::tick() {
    if (!m_fmt) return;

    // 1) 밀린 오디오부터 사인크에 밀어넣는다
    flushAudio();

    // 2) 오디오 여유분과 프레임 대기열을 채운다.
    //    ★ 영상 표시를 기다리는 동안에도 계속 읽어야 오디오가 굶지 않는다.
    //      (이 선행 읽기가 없어 소리가 끊겼다)
    const int kAudioBacklog = 96 * 1024;          // ~0.5초 분량까지 미리 확보
    for (int guard = 0; guard < 512; ++guard) {
        if (m_apend.size() >= kAudioBacklog) break;   // 오디오 충분
        if (m_vq.size() >= m_vqMax)            break;   // 프레임 대기열 가득
        if (!pumpOnce())                       break;   // 파일 끝
    }
    flushAudio();

    // 3) 표시할 때가 된 프레임을 표시 (늦은 프레임은 건너뛰어 싱크 유지)
    const qint64 now = clockUsec();
    int show = -1;
    while (!m_vq.isEmpty() && m_vq.first().ptsUsec <= now) {
        show = 0;
        if (m_vq.size() >= 2 && m_vq.at(1).ptsUsec <= now) { m_vq.removeFirst(); continue; }
        break;
    }
    if (show == 0) {
        emit frameReady(m_vq.first().img);
        m_vq.removeFirst();
    }

    // 4) 끝까지 재생했으면 완료 통지 (반복 여부는 호출측이 결정)
    if (m_eof && m_vq.isEmpty() && m_apend.isEmpty()) {
        m_timer->stop();
        emit finished();
    }
}

// 패킷/프레임을 하나 처리한다. 더 읽을 것이 없으면 false.
bool PreviewVideo::pumpOnce() {
    if (!m_dec || !m_pkt || !m_frame) return false;

    for (int guard = 0; guard < 64; ++guard) {
        int ret = avcodec_receive_frame(m_dec, m_frame);
        if (ret == 0) {
            // 실제 프레임의 포맷으로 변환기 준비 (바뀌면 다시 만든다)
            if (!m_sws || m_frame->format != m_swsFmt) {
                if (m_sws) sws_freeContext(m_sws);
                m_swsFmt = m_frame->format;
                m_sws = sws_getContext(
                    m_w, m_h, static_cast<AVPixelFormat>(m_frame->format),
                    m_w, m_h, AV_PIX_FMT_RGB24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!m_sws) { emit failed("색변환기 생성 실패"); return false; }
            }
            sws_scale(m_sws, m_frame->data, m_frame->linesize, 0, m_h,
                      m_rgb->data, m_rgb->linesize);

            // QImage 는 버퍼를 참조만 하므로 copy() 로 소유권을 넘긴다
            QImage img(m_rgb->data[0], m_w, m_h, m_rgb->linesize[0],
                       QImage::Format_RGB888);

            // 표시 시각 = PTS × time_base (초) → usec
            int64_t ts = m_frame->best_effort_timestamp;
            if (ts == AV_NOPTS_VALUE) ts = m_frame->pts;
            const qint64 ptsUsec = (ts == AV_NOPTS_VALUE)
                ? clockUsec()                                  // PTS 없음 → 즉시
                : static_cast<qint64>(ts * m_vTimeBase * 1000000.0);

            m_vq.append({ ptsUsec, img.copy() });
            return true;
        }
        if (ret == AVERROR_EOF)    { m_eof = true; return false; }  // 디코더 소진
        if (ret != AVERROR(EAGAIN)) { m_eof = true; return false; } // 디코드 오류

        if (av_read_frame(m_fmt, m_pkt) < 0) {
            avcodec_send_packet(m_dec, nullptr);               // 남은 프레임 flush
            continue;
        }
        if (m_pkt->stream_index == m_stream)
            avcodec_send_packet(m_dec, m_pkt);
        else if (m_pkt->stream_index == m_astream)
            feedAudio(m_pkt);
        av_packet_unref(m_pkt);
    }
    return true;   // 상한 도달 — 다음 틱에서 계속
}

void PreviewVideo::close() {
    if (m_timer) m_timer->stop();
    // 오디오 먼저 정리 (출력 장치 점유 해제)
    if (m_sink)   { m_sink->stop(); m_sink->deleteLater(); m_sink = nullptr; }
    m_sinkIo = nullptr;
    if (m_swr)    { swr_free(&m_swr); }
    if (m_aframe) { av_frame_free(&m_aframe); }
    if (m_adec)   { avcodec_free_context(&m_adec); }
    m_astream = -1;
    if (m_sws)    { sws_freeContext(m_sws); m_sws = nullptr; }
    m_swsFmt = -1;
    m_vq.clear();
    m_apend.clear();
    m_eof = false;
    if (m_rgbBuf) { av_free(m_rgbBuf);      m_rgbBuf = nullptr; }
    if (m_pkt)    { av_packet_free(&m_pkt); }
    if (m_frame)  { av_frame_free(&m_frame); }
    if (m_rgb)    { av_frame_free(&m_rgb); }
    if (m_dec)    { avcodec_free_context(&m_dec); }
    if (m_fmt)    { avformat_close_input(&m_fmt); }
    m_stream = -1; m_w = m_h = 0;
}

#endif  // HAVE_FFMPEG
