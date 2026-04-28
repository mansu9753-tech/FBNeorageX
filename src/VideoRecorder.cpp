// VideoRecorder.cpp — libav* 기반 H264+AAC MP4 녹화 구현
#include "VideoRecorder.h"

#include <QDebug>

#if !HAVE_FFMPEG
// FFmpeg 없이 빌드된 경우 — 스텁 구현 (링크 오류 방지)
VideoRecorder::VideoRecorder() {}
VideoRecorder::~VideoRecorder() {}
bool VideoRecorder::open(const QString&, int, int, double, int, int, VideoPixelFormat) {
    m_lastError = "FFmpeg not available in this build"; return false; }
void VideoRecorder::addVideoFrame(const void*, int) {}
void VideoRecorder::addAudioSamples(const int16_t*, int) {}
void VideoRecorder::close() {}
#else
// ══════════════════════════════════════════════════════
//  실제 FFmpeg 기반 구현
// ══════════════════════════════════════════════════════

// ── avutil 오류 문자열 ──────────────────────────────────────
static QString avErr(int err) {
    char buf[128] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString(buf);
}

// ════════════════════════════════════════════════════════════
//  생성자 / 소멸자
// ════════════════════════════════════════════════════════════
VideoRecorder::VideoRecorder() {}

VideoRecorder::~VideoRecorder() {
    close();
}

// ════════════════════════════════════════════════════════════
//  open() — 출력 파일 + 코덱 초기화
// ════════════════════════════════════════════════════════════
bool VideoRecorder::open(const QString& path, int width, int height, double fps,
                         int sampleRate, int channels, VideoPixelFormat pixFmt) {
    if (m_fmtCtx) close();
    m_width  = width;
    m_height = height;
    m_pixFmt = pixFmt;

    // Windows WMF는 유니코드 경로를 못 쓰므로 ASCII path를 쓴다.
    // (이미 호출자에서 임시경로 처리를 했을 수 있지만 방어적으로 UTF-8 encode)
    QByteArray pathUtf8 = path.toUtf8();
    int ret;

    // ── 출력 컨테이너 ──────────────────────────────────────
    ret = avformat_alloc_output_context2(&m_fmtCtx, nullptr, "mp4",
                                         pathUtf8.constData());
    if (ret < 0 || !m_fmtCtx) {
        m_lastError = "avformat_alloc_output_context2: " + avErr(ret);
        return false;
    }

    // ══════════════════════════════════════════════════════
    //  비디오 스트림 (H264)
    // ══════════════════════════════════════════════════════
    const AVCodec* vcodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!vcodec) {
        m_lastError = "H264 encoder not found";
        cleanup(); return false;
    }

    m_videoStream = avformat_new_stream(m_fmtCtx, vcodec);
    if (!m_videoStream) {
        m_lastError = "avformat_new_stream (video) failed";
        cleanup(); return false;
    }
    m_videoStream->id = m_fmtCtx->nb_streams - 1;

    m_videoCtx = avcodec_alloc_context3(vcodec);
    if (!m_videoCtx) {
        m_lastError = "avcodec_alloc_context3 (video) failed";
        cleanup(); return false;
    }

    m_videoCtx->width     = width;
    m_videoCtx->height    = height;
    m_videoCtx->pix_fmt   = AV_PIX_FMT_YUV420P;
    // fps → time_base = 1/fps (정수 근사)
    int fpsNum = static_cast<int>(fps * 1000 + 0.5);
    m_videoCtx->time_base = { 1000, fpsNum };  // = 1/fps
    m_videoStream->time_base = m_videoCtx->time_base;
    m_videoCtx->framerate     = { fpsNum, 1000 };
    m_videoCtx->gop_size      = 30;
    m_videoCtx->max_b_frames  = 0;  // B-frame 없음 → 낮은 지연

    // ultrafast preset (실시간 인코딩)
    av_opt_set(m_videoCtx->priv_data, "preset",  "ultrafast", 0);
    av_opt_set(m_videoCtx->priv_data, "tune",    "zerolatency", 0);
    av_opt_set(m_videoCtx->priv_data, "crf",     "23", 0);

    if (m_fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        m_videoCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(m_videoCtx, vcodec, nullptr);
    if (ret < 0) {
        m_lastError = "avcodec_open2 (H264): " + avErr(ret);
        cleanup(); return false;
    }

    ret = avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCtx);
    if (ret < 0) {
        m_lastError = "avcodec_parameters_from_context (video): " + avErr(ret);
        cleanup(); return false;
    }

    // ── 비디오 프레임 (YUV420p) 할당 ──────────────────────
    m_videoFrame = av_frame_alloc();
    if (!m_videoFrame) { m_lastError = "av_frame_alloc (video) failed"; cleanup(); return false; }
    m_videoFrame->format = AV_PIX_FMT_YUV420P;
    m_videoFrame->width  = width;
    m_videoFrame->height = height;
    ret = av_image_alloc(m_videoFrame->data, m_videoFrame->linesize,
                         width, height, AV_PIX_FMT_YUV420P, 32);
    if (ret < 0) {
        m_lastError = "av_image_alloc (video): " + avErr(ret);
        cleanup(); return false;
    }

    // ── SwsContext: 게임 픽셀 포맷 → YUV420p ──────────────
    AVPixelFormat srcFmt = (pixFmt == VPF_XRGB8888)
                           ? AV_PIX_FMT_BGR32 : AV_PIX_FMT_RGB565LE;
    m_swsCtx = sws_getContext(width, height, srcFmt,
                               width, height, AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        m_lastError = "sws_getContext failed";
        cleanup(); return false;
    }

    // ══════════════════════════════════════════════════════
    //  오디오 스트림 (AAC)
    // ══════════════════════════════════════════════════════
    const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!acodec) {
        m_lastError = "AAC encoder not found";
        cleanup(); return false;
    }

    m_audioStream = avformat_new_stream(m_fmtCtx, acodec);
    if (!m_audioStream) {
        m_lastError = "avformat_new_stream (audio) failed";
        cleanup(); return false;
    }
    m_audioStream->id = m_fmtCtx->nb_streams - 1;

    m_audioCtx = avcodec_alloc_context3(acodec);
    if (!m_audioCtx) {
        m_lastError = "avcodec_alloc_context3 (audio) failed";
        cleanup(); return false;
    }

    m_audioCtx->sample_fmt  = AV_SAMPLE_FMT_FLTP;  // AAC 요구 포맷
    m_audioCtx->sample_rate = sampleRate;
    m_audioCtx->bit_rate    = 128000;
    av_channel_layout_default(&m_audioCtx->ch_layout, channels);
    m_audioCtx->time_base   = { 1, sampleRate };
    m_audioStream->time_base = m_audioCtx->time_base;

    if (m_fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        m_audioCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(m_audioCtx, acodec, nullptr);
    if (ret < 0) {
        m_lastError = "avcodec_open2 (AAC): " + avErr(ret);
        cleanup(); return false;
    }

    ret = avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCtx);
    if (ret < 0) {
        m_lastError = "avcodec_parameters_from_context (audio): " + avErr(ret);
        cleanup(); return false;
    }

    m_audioFrameSize = m_audioCtx->frame_size > 0 ? m_audioCtx->frame_size : 1024;

    // ── 오디오 프레임 (fltp) 할당 ──────────────────────────
    m_audioFrame = av_frame_alloc();
    if (!m_audioFrame) { m_lastError = "av_frame_alloc (audio) failed"; cleanup(); return false; }
    m_audioFrame->format      = AV_SAMPLE_FMT_FLTP;
    m_audioFrame->nb_samples  = m_audioFrameSize;
    av_channel_layout_copy(&m_audioFrame->ch_layout, &m_audioCtx->ch_layout);
    m_audioFrame->sample_rate = sampleRate;
    ret = av_frame_get_buffer(m_audioFrame, 0);
    if (ret < 0) {
        m_lastError = "av_frame_get_buffer (audio): " + avErr(ret);
        cleanup(); return false;
    }

    // ── SwrContext: s16le stereo → fltp ────────────────────
    AVChannelLayout srcChLayout = AV_CHANNEL_LAYOUT_STEREO;
    ret = swr_alloc_set_opts2(&m_swrCtx,
        &m_audioCtx->ch_layout, AV_SAMPLE_FMT_FLTP, sampleRate,
        &srcChLayout,           AV_SAMPLE_FMT_S16,  sampleRate,
        0, nullptr);
    if (ret < 0 || swr_init(m_swrCtx) < 0) {
        m_lastError = "swr_alloc/init failed: " + avErr(ret);
        cleanup(); return false;
    }

    // ── 파일 열기 + 헤더 쓰기 ─────────────────────────────
    if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_fmtCtx->pb, pathUtf8.constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            m_lastError = "avio_open: " + avErr(ret);
            cleanup(); return false;
        }
    }

    ret = avformat_write_header(m_fmtCtx, nullptr);
    if (ret < 0) {
        m_lastError = "avformat_write_header: " + avErr(ret);
        cleanup(); return false;
    }

    m_videoPts = 0;
    m_audioPts = 0;
    m_audioBuf.clear();

    qDebug() << "[VideoRecorder] opened:" << path
             << width << "x" << height << "@" << fps
             << "srate=" << sampleRate;
    return true;
}

// ════════════════════════════════════════════════════════════
//  addVideoFrame() — 게임 프레임 → H264 인코딩
// ════════════════════════════════════════════════════════════
void VideoRecorder::addVideoFrame(const void* data, int pitch) {
    if (!m_fmtCtx || !m_videoCtx || !data) return;

    // 게임 픽셀 → YUV420p 변환
    const uint8_t* srcPlane[1] = { static_cast<const uint8_t*>(data) };
    int srcStride[1] = { pitch };
    sws_scale(m_swsCtx, srcPlane, srcStride, 0, m_height,
              m_videoFrame->data, m_videoFrame->linesize);

    m_videoFrame->pts = m_videoPts++;
    m_videoFrame->key_frame = 0;
    m_videoFrame->pict_type = AV_PICTURE_TYPE_NONE;

    int ret = avcodec_send_frame(m_videoCtx, m_videoFrame);
    if (ret < 0) return;
    writePacket(m_videoCtx, m_videoStream);
}

// ════════════════════════════════════════════════════════════
//  addAudioSamples() — s16le PCM → AAC 인코딩
// ════════════════════════════════════════════════════════════
void VideoRecorder::addAudioSamples(const int16_t* data, int sampleCount) {
    if (!m_fmtCtx || !m_audioCtx || !data || sampleCount <= 0) return;

    // s16le stereo: 4 bytes per sample-pair
    int appendBytes = sampleCount * 2 * sizeof(int16_t);
    int oldSize = m_audioBuf.size();
    m_audioBuf.resize(oldSize + appendBytes);
    memcpy(m_audioBuf.data() + oldSize, data, appendBytes);

    int bytesPerSamplePair = 2 * sizeof(int16_t);
    int frameBytes = m_audioFrameSize * bytesPerSamplePair;

    while (m_audioBuf.size() >= frameBytes) {
        const int16_t* src = reinterpret_cast<const int16_t*>(m_audioBuf.constData());

        // s16le → fltp 변환
        int ret = av_frame_make_writable(m_audioFrame);
        if (ret < 0) break;

        const uint8_t* srcArr[1] = { reinterpret_cast<const uint8_t*>(src) };
        ret = swr_convert(m_swrCtx,
                          m_audioFrame->data, m_audioFrameSize,
                          srcArr, m_audioFrameSize);
        if (ret <= 0) break;

        m_audioFrame->pts      = m_audioPts;
        m_audioFrame->nb_samples = m_audioFrameSize;
        m_audioPts += m_audioFrameSize;

        ret = avcodec_send_frame(m_audioCtx, m_audioFrame);
        if (ret >= 0) writePacket(m_audioCtx, m_audioStream);

        // 처리된 부분 제거
        m_audioBuf.remove(0, frameBytes);
    }
}

// ════════════════════════════════════════════════════════════
//  writePacket() — 인코더 출력 패킷을 컨테이너에 쓰기
// ════════════════════════════════════════════════════════════
bool VideoRecorder::writePacket(AVCodecContext* ctx, AVStream* stream) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return false;
    bool ok = true;
    while (true) {
        int ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) { ok = false; break; }

        av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        av_interleaved_write_frame(m_fmtCtx, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return ok;
}

// ════════════════════════════════════════════════════════════
//  close() — 인코더 플러시 + 트레일러 쓰기
// ════════════════════════════════════════════════════════════
void VideoRecorder::close() {
    if (!m_fmtCtx) return;

    // 비디오 플러시
    if (m_videoCtx) {
        avcodec_send_frame(m_videoCtx, nullptr);
        writePacket(m_videoCtx, m_videoStream);
    }

    // 오디오 플러시 (남은 버퍼 처리)
    if (m_audioCtx) {
        // 남은 audioBuf 가 있으면 패딩해서 마지막 프레임 인코딩
        int bytesPerPair = 2 * sizeof(int16_t);
        int frameBytes   = m_audioFrameSize * bytesPerPair;
        if (!m_audioBuf.isEmpty()) {
            m_audioBuf.resize(frameBytes, 0);  // 패딩
            const int16_t* src = reinterpret_cast<const int16_t*>(m_audioBuf.constData());
            const uint8_t* srcArr[1] = { reinterpret_cast<const uint8_t*>(src) };
            if (av_frame_make_writable(m_audioFrame) >= 0) {
                swr_convert(m_swrCtx, m_audioFrame->data, m_audioFrameSize,
                            srcArr, m_audioFrameSize);
                m_audioFrame->pts      = m_audioPts;
                m_audioFrame->nb_samples = m_audioFrameSize;
                avcodec_send_frame(m_audioCtx, m_audioFrame);
                writePacket(m_audioCtx, m_audioStream);
            }
        }
        avcodec_send_frame(m_audioCtx, nullptr);
        writePacket(m_audioCtx, m_audioStream);
    }

    av_write_trailer(m_fmtCtx);
    cleanup();
}

// ════════════════════════════════════════════════════════════
//  cleanup() — 모든 avXxx 자원 해제
// ════════════════════════════════════════════════════════════
void VideoRecorder::cleanup() {
    if (m_videoFrame) {
        av_freep(&m_videoFrame->data[0]);
        av_frame_free(&m_videoFrame);
    }
    if (m_audioFrame)  av_frame_free(&m_audioFrame);
    if (m_videoCtx)    avcodec_free_context(&m_videoCtx);
    if (m_audioCtx)    avcodec_free_context(&m_audioCtx);
    if (m_swsCtx)      sws_freeContext(m_swsCtx);     m_swsCtx = nullptr;
    if (m_swrCtx)      swr_free(&m_swrCtx);
    if (m_fmtCtx) {
        if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE) && m_fmtCtx->pb)
            avio_closep(&m_fmtCtx->pb);
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
    }
    m_videoCtx    = nullptr;
    m_audioCtx    = nullptr;
    m_videoStream = nullptr;
    m_audioStream = nullptr;
    m_videoFrame  = nullptr;
    m_audioFrame  = nullptr;
    m_swrCtx      = nullptr;
    m_videoPts    = 0;
    m_audioPts    = 0;
    m_audioBuf.clear();
}

#endif  // HAVE_FFMPEG
