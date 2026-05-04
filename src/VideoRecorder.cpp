// VideoRecorder.cpp — 플랫폼별 H264+AAC MP4 녹화
//   Windows : IMFSinkWriter (WMF) — 외부 DLL 불필요
//   Linux   : libav* (FFmpeg 공유 라이브러리)

#include "VideoRecorder.h"
#include <QDebug>

// ════════════════════════════════════════════════════════════
#ifdef _WIN32
// ════════════════════════════════════════════════════════════
//  Windows — Windows Media Foundation IMFSinkWriter
// ════════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

// COM 스마트 포인터 helper
template<class T>
static void safeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ── 생성자 / 소멸자 ──────────────────────────────────────
VideoRecorder::VideoRecorder()  {}
VideoRecorder::~VideoRecorder() { close(); }

bool VideoRecorder::isOpen() const { return m_sinkWriter != nullptr; }

// ── RGB565 / XRGB8888 → NV12 변환 ────────────────────────
// NV12: Y 플레인(전체 픽셀) + 인터리브 UV 플레인(2×다운샘플)
void VideoRecorder::convertToNV12(const void* src, int srcPitch,
                                   uint8_t* dstY, int dstStrideY,
                                   uint8_t* dstUV) const
{
    const int W = m_width;
    const int H = m_height;

    if (m_pixFmt == VPF_XRGB8888) {
        for (int y = 0; y < H; ++y) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(
                static_cast<const uint8_t*>(src) + y * srcPitch);
            uint8_t* yRow = dstY + y * dstStrideY;
            for (int x = 0; x < W; ++x) {
                uint32_t px = row[x];
                int r = (px >> 16) & 0xFF;
                int g = (px >>  8) & 0xFF;
                int b = (px      ) & 0xFF;
                yRow[x] = (uint8_t)((66*r + 129*g + 25*b + 128) >> 8) + 16;
            }
        }
        for (int y = 0; y < H/2; ++y) {
            const uint32_t* r0 = reinterpret_cast<const uint32_t*>(
                static_cast<const uint8_t*>(src) + (y*2)   * srcPitch);
            const uint32_t* r1 = reinterpret_cast<const uint32_t*>(
                static_cast<const uint8_t*>(src) + (y*2+1) * srcPitch);
            uint8_t* uvRow = dstUV + y * dstStrideY;
            for (int x = 0; x < W/2; ++x) {
                // 2×2 ブロック平均
                uint32_t p0 = r0[x*2], p1 = r0[x*2+1], p2 = r1[x*2], p3 = r1[x*2+1];
                int r = (((p0>>16)&0xFF)+((p1>>16)&0xFF)+((p2>>16)&0xFF)+((p3>>16)&0xFF))>>2;
                int g = (((p0>>8)&0xFF)+((p1>>8)&0xFF)+((p2>>8)&0xFF)+((p3>>8)&0xFF))>>2;
                int b = ((p0&0xFF)+(p1&0xFF)+(p2&0xFF)+(p3&0xFF))>>2;
                uvRow[x*2  ] = (uint8_t)((-38*r -  74*g + 112*b + 128) >> 8) + 128; // U
                uvRow[x*2+1] = (uint8_t)((112*r -  94*g -  18*b + 128) >> 8) + 128; // V
            }
        }
    } else {
        // RGB565: R=5bit G=6bit B=5bit
        for (int y = 0; y < H; ++y) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(
                static_cast<const uint8_t*>(src) + y * srcPitch);
            uint8_t* yRow = dstY + y * dstStrideY;
            for (int x = 0; x < W; ++x) {
                uint16_t px = row[x];
                int r = ((px >> 11) & 0x1F) << 3;
                int g = ((px >>  5) & 0x3F) << 2;
                int b =  (px        & 0x1F) << 3;
                yRow[x] = (uint8_t)((66*r + 129*g + 25*b + 128) >> 8) + 16;
            }
        }
        for (int y = 0; y < H/2; ++y) {
            const uint16_t* r0 = reinterpret_cast<const uint16_t*>(
                static_cast<const uint8_t*>(src) + (y*2)   * srcPitch);
            const uint16_t* r1 = reinterpret_cast<const uint16_t*>(
                static_cast<const uint8_t*>(src) + (y*2+1) * srcPitch);
            uint8_t* uvRow = dstUV + y * dstStrideY;
            for (int x = 0; x < W/2; ++x) {
                uint16_t p0=r0[x*2], p1=r0[x*2+1], p2=r1[x*2], p3=r1[x*2+1];
                int r = ((((p0>>11)&0x1F)+(  (p1>>11)&0x1F)+(  (p2>>11)&0x1F)+(  (p3>>11)&0x1F))<<3)>>2;
                int g = ((((p0>>5)&0x3F)+(  (p1>>5)&0x3F)+(  (p2>>5)&0x3F)+(  (p3>>5)&0x3F))<<2)>>2;
                int b = (((p0&0x1F)+(p1&0x1F)+(p2&0x1F)+(p3&0x1F))<<3)>>2;
                uvRow[x*2  ] = (uint8_t)((-38*r -  74*g + 112*b + 128) >> 8) + 128;
                uvRow[x*2+1] = (uint8_t)((112*r -  94*g -  18*b + 128) >> 8) + 128;
            }
        }
    }
}

// ── open() ───────────────────────────────────────────────
bool VideoRecorder::open(const QString& path, int width, int height, double fps,
                         int sampleRate, int channels, VideoPixelFormat pixFmt)
{
    if (m_sinkWriter) close();
    m_width      = width;
    m_height     = height;
    m_sampleRate = sampleRate;
    m_channels   = channels;
    m_pixFmt     = pixFmt;

    HRESULT hr;

    // MFStartup
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) { m_lastError = "MFStartup failed"; return false; }
    m_mfStarted = true;

    // IMFSinkWriter 생성
    IMFSinkWriter* pSW = nullptr;
    hr = MFCreateSinkWriterFromURL(path.toStdWString().c_str(), nullptr, nullptr, &pSW);
    if (FAILED(hr)) {
        m_lastError = QString("MFCreateSinkWriterFromURL hr=0x%1").arg((uint)hr, 8, 16);
        MFShutdown(); m_mfStarted = false;
        return false;
    }

    // ── 비디오 출력 스트림 (H264) ───────────────────────────
    IMFMediaType* pVOut = nullptr;
    MFCreateMediaType(&pVOut);
    pVOut->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pVOut->SetGUID (MF_MT_SUBTYPE,    MFVideoFormat_H264);
    pVOut->SetUINT32(MF_MT_AVG_BITRATE, 3000000);  // 3 Mbps
    pVOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize (pVOut, MF_MT_FRAME_SIZE, width, height);
    // fps → rational (소수점 3자리 정확도)
    UINT32 fpsNum = (UINT32)(fps * 1000 + 0.5), fpsDen = 1000;
    MFSetAttributeRatio(pVOut, MF_MT_FRAME_RATE, fpsNum, fpsDen);
    MFSetAttributeRatio(pVOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    DWORD vIdx = 0;
    hr = pSW->AddStream(pVOut, &vIdx);
    safeRelease(pVOut);
    if (FAILED(hr)) { m_lastError = "AddStream(video) failed"; safeRelease(pSW); MFShutdown(); m_mfStarted=false; return false; }
    m_videoStream = (int)vIdx;

    // 비디오 입력 타입 (NV12 — H264 인코더가 요구하는 포맷)
    IMFMediaType* pVIn = nullptr;
    MFCreateMediaType(&pVIn);
    pVIn->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pVIn->SetGUID (MF_MT_SUBTYPE,    MFVideoFormat_NV12);
    pVIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize (pVIn, MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(pVIn, MF_MT_FRAME_RATE, fpsNum, fpsDen);
    MFSetAttributeRatio(pVIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = pSW->SetInputMediaType(vIdx, pVIn, nullptr);
    safeRelease(pVIn);
    if (FAILED(hr)) { m_lastError = QString("SetInputMediaType(video) hr=0x%1").arg((uint)hr, 8, 16); safeRelease(pSW); MFShutdown(); m_mfStarted=false; return false; }

    // ── 오디오 출력 스트림 (AAC) ────────────────────────────
    IMFMediaType* pAOut = nullptr;
    MFCreateMediaType(&pAOut);
    pAOut->SetGUID  (MF_MT_MAJOR_TYPE,                 MFMediaType_Audio);
    pAOut->SetGUID  (MF_MT_SUBTYPE,                    MFAudioFormat_AAC);
    pAOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,      16);
    pAOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,   (UINT32)sampleRate);
    pAOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,         (UINT32)channels);
    pAOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);  // 128 kbps

    DWORD aIdx = 0;
    hr = pSW->AddStream(pAOut, &aIdx);
    safeRelease(pAOut);
    if (FAILED(hr)) { m_lastError = "AddStream(audio) failed"; safeRelease(pSW); MFShutdown(); m_mfStarted=false; return false; }
    m_audioStream = (int)aIdx;

    // 오디오 입력 타입 (PCM s16le)
    IMFMediaType* pAIn = nullptr;
    MFCreateMediaType(&pAIn);
    pAIn->SetGUID  (MF_MT_MAJOR_TYPE,                 MFMediaType_Audio);
    pAIn->SetGUID  (MF_MT_SUBTYPE,                    MFAudioFormat_PCM);
    pAIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,      16);
    pAIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,   (UINT32)sampleRate);
    pAIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,         (UINT32)channels);
    pAIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(sampleRate * channels * 2));
    hr = pSW->SetInputMediaType(aIdx, pAIn, nullptr);
    safeRelease(pAIn);
    if (FAILED(hr)) { m_lastError = QString("SetInputMediaType(audio) hr=0x%1").arg((uint)hr, 8, 16); safeRelease(pSW); MFShutdown(); m_mfStarted=false; return false; }

    hr = pSW->BeginWriting();
    if (FAILED(hr)) { m_lastError = QString("BeginWriting hr=0x%1").arg((uint)hr, 8, 16); safeRelease(pSW); MFShutdown(); m_mfStarted=false; return false; }

    m_sinkWriter = pSW;
    m_videoTime  = 0;
    m_audioTime  = 0;
    // 프레임 지속시간 = 10,000,000 / fps (100ns 단위)
    m_frameDur = (int64_t)(10000000.0 / fps + 0.5);

    qDebug() << "[VideoRecorder/WMF] opened:" << path << width << "x" << height << "@" << fps;
    return true;
}

// ── addVideoFrame() ──────────────────────────────────────
void VideoRecorder::addVideoFrame(const void* data, int pitch)
{
    if (!m_sinkWriter || !data) return;
    IMFSinkWriter* pSW = reinterpret_cast<IMFSinkWriter*>(m_sinkWriter);

    const int W = m_width, H = m_height;
    // NV12 버퍼: Y 플레인(W×H) + UV 플레인(W×H/2) = W × H × 3/2
    const int yStride  = W;
    const int bufSize  = yStride * H + yStride * (H / 2);

    IMFMediaBuffer* pBuf = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(bufSize, &pBuf);
    if (FAILED(hr)) return;

    BYTE* pData = nullptr; DWORD maxLen = 0, curLen = 0;
    pBuf->Lock(&pData, &maxLen, &curLen);
    uint8_t* yPlane  = pData;
    uint8_t* uvPlane = pData + yStride * H;
    convertToNV12(data, pitch, yPlane, yStride, uvPlane);
    pBuf->Unlock();
    pBuf->SetCurrentLength(bufSize);

    IMFSample* pSample = nullptr;
    MFCreateSample(&pSample);
    pSample->AddBuffer(pBuf);
    pSample->SetSampleTime(m_videoTime);
    pSample->SetSampleDuration(m_frameDur);
    safeRelease(pBuf);

    pSW->WriteSample((DWORD)m_videoStream, pSample);
    safeRelease(pSample);

    m_videoTime += m_frameDur;
}

// ── addAudioSamples() ────────────────────────────────────
void VideoRecorder::addAudioSamples(const int16_t* data, int sampleCount)
{
    if (!m_sinkWriter || !data || sampleCount <= 0) return;
    IMFSinkWriter* pSW = reinterpret_cast<IMFSinkWriter*>(m_sinkWriter);

    const int bytesPerFrame = m_channels * sizeof(int16_t);
    const int totalBytes    = sampleCount * bytesPerFrame;

    IMFMediaBuffer* pBuf = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(totalBytes, &pBuf);
    if (FAILED(hr)) return;

    BYTE* pData = nullptr; DWORD maxLen = 0, curLen = 0;
    pBuf->Lock(&pData, &maxLen, &curLen);
    memcpy(pData, data, totalBytes);
    pBuf->Unlock();
    pBuf->SetCurrentLength(totalBytes);

    IMFSample* pSample = nullptr;
    MFCreateSample(&pSample);
    pSample->AddBuffer(pBuf);
    pSample->SetSampleTime(m_audioTime);
    // 지속시간 = sampleCount / sampleRate × 10,000,000
    int64_t dur = (int64_t)((double)sampleCount / m_sampleRate * 10000000.0 + 0.5);
    pSample->SetSampleDuration(dur);
    safeRelease(pBuf);

    pSW->WriteSample((DWORD)m_audioStream, pSample);
    safeRelease(pSample);

    m_audioTime += dur;
}

// ── close() ──────────────────────────────────────────────
void VideoRecorder::close()
{
    if (!m_sinkWriter) return;
    IMFSinkWriter* pSW = reinterpret_cast<IMFSinkWriter*>(m_sinkWriter);
    pSW->Finalize();
    pSW->Release();
    m_sinkWriter  = nullptr;
    m_videoStream = -1;
    m_audioStream = -1;
    m_videoTime   = 0;
    m_audioTime   = 0;
    if (m_mfStarted) { MFShutdown(); m_mfStarted = false; }
}

// ════════════════════════════════════════════════════════════
#else   // Linux / Mac — FFmpeg libav*
// ════════════════════════════════════════════════════════════

#if HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// 편의 캐스트
#define FMT  reinterpret_cast<AVFormatContext*>(m_fmtCtx)
#define VCTX reinterpret_cast<AVCodecContext*>(m_videoCtx)
#define ACTX reinterpret_cast<AVCodecContext*>(m_audioCtx)
#define VSTR reinterpret_cast<AVStream*>(m_videoStream)
#define ASTR reinterpret_cast<AVStream*>(m_audioStream)
#define SWS  reinterpret_cast<SwsContext*>(m_swsCtx)
#define SWR  reinterpret_cast<SwrContext*>(m_swrCtx)
#define VFRM reinterpret_cast<AVFrame*>(m_videoFrame)
#define AFRM reinterpret_cast<AVFrame*>(m_audioFrame)

static QString avErr(int e) {
    char b[128]{}; av_strerror(e, b, sizeof b); return QString(b);
}

VideoRecorder::VideoRecorder()  {}
VideoRecorder::~VideoRecorder() { close(); }
bool VideoRecorder::isOpen() const { return m_fmtCtx != nullptr; }

bool VideoRecorder::open(const QString& path, int width, int height, double fps,
                         int sampleRate, int channels, VideoPixelFormat pixFmt)
{
    if (m_fmtCtx) close();
    m_width = width; m_height = height;
    m_sampleRate = sampleRate; m_channels = channels; m_pixFmt = pixFmt;

    QByteArray pathUtf8 = path.toUtf8();
    int ret;

    ret = avformat_alloc_output_context2(
        reinterpret_cast<AVFormatContext**>(&m_fmtCtx),
        nullptr, "mp4", pathUtf8.constData());
    if (ret < 0) { m_lastError = "avformat_alloc: " + avErr(ret); return false; }

    // ── 비디오 (H264) ────────────────────────────────────
    const AVCodec* vc = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!vc) { m_lastError = "H264 encoder not found"; cleanupFFmpeg(); return false; }
    m_videoStream = avformat_new_stream(FMT, vc);
    if (!m_videoStream) { m_lastError = "new_stream(video) failed"; cleanupFFmpeg(); return false; }
    m_videoCtx = avcodec_alloc_context3(vc);
    if (!m_videoCtx) { m_lastError = "alloc_context3(video) failed"; cleanupFFmpeg(); return false; }
    VCTX->width     = width; VCTX->height    = height;
    VCTX->pix_fmt   = AV_PIX_FMT_YUV420P;
    int fpsN = (int)(fps*1000+.5);
    VCTX->time_base = {1000, fpsN};
    VSTR->time_base = VCTX->time_base;
    VCTX->framerate = {fpsN, 1000};
    VCTX->gop_size = 30; VCTX->max_b_frames = 0;
    av_opt_set(VCTX->priv_data, "preset", "ultrafast", 0);
    av_opt_set(VCTX->priv_data, "tune",   "zerolatency", 0);
    av_opt_set(VCTX->priv_data, "crf",    "23", 0);
    if (FMT->oformat->flags & AVFMT_GLOBALHEADER) VCTX->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ret = avcodec_open2(VCTX, vc, nullptr);
    if (ret < 0) { m_lastError = "avcodec_open2(H264): " + avErr(ret); cleanupFFmpeg(); return false; }
    avcodec_parameters_from_context(VSTR->codecpar, VCTX);

    m_videoFrame = av_frame_alloc();
    VFRM->format = AV_PIX_FMT_YUV420P; VFRM->width = width; VFRM->height = height;
    ret = av_image_alloc(VFRM->data, VFRM->linesize, width, height, AV_PIX_FMT_YUV420P, 32);
    if (ret < 0) { m_lastError = "av_image_alloc: " + avErr(ret); cleanupFFmpeg(); return false; }

    AVPixelFormat srcFmt = (pixFmt == VPF_XRGB8888) ? AV_PIX_FMT_BGR32 : AV_PIX_FMT_RGB565LE;
    m_swsCtx = sws_getContext(width, height, srcFmt, width, height, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) { m_lastError = "sws_getContext failed"; cleanupFFmpeg(); return false; }

    // ── 오디오 (AAC) ─────────────────────────────────────
    const AVCodec* ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!ac) { m_lastError = "AAC encoder not found"; cleanupFFmpeg(); return false; }
    m_audioStream = avformat_new_stream(FMT, ac);
    m_audioCtx    = avcodec_alloc_context3(ac);
    ACTX->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    ACTX->sample_rate = sampleRate; ACTX->bit_rate = 128000;
    av_channel_layout_default(&ACTX->ch_layout, channels);
    ACTX->time_base = {1, sampleRate}; ASTR->time_base = ACTX->time_base;
    if (FMT->oformat->flags & AVFMT_GLOBALHEADER) ACTX->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ret = avcodec_open2(ACTX, ac, nullptr);
    if (ret < 0) { m_lastError = "avcodec_open2(AAC): " + avErr(ret); cleanupFFmpeg(); return false; }
    avcodec_parameters_from_context(ASTR->codecpar, ACTX);
    m_audioFrameSize = ACTX->frame_size > 0 ? ACTX->frame_size : 1024;

    m_audioFrame = av_frame_alloc();
    AFRM->format = AV_SAMPLE_FMT_FLTP; AFRM->nb_samples = m_audioFrameSize;
    av_channel_layout_copy(&AFRM->ch_layout, &ACTX->ch_layout);
    AFRM->sample_rate = sampleRate;
    av_frame_get_buffer(AFRM, 0);

    AVChannelLayout srcCh = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(reinterpret_cast<SwrContext**>(&m_swrCtx),
        &ACTX->ch_layout, AV_SAMPLE_FMT_FLTP, sampleRate,
        &srcCh,           AV_SAMPLE_FMT_S16,  sampleRate, 0, nullptr);
    swr_init(SWR);

    if (!(FMT->oformat->flags & AVFMT_NOFILE))
        avio_open(&FMT->pb, pathUtf8.constData(), AVIO_FLAG_WRITE);
    avformat_write_header(FMT, nullptr);

    m_videoPts = 0; m_audioPts = 0; m_audioBuf.clear();
    return true;
}

bool VideoRecorder::writePacket(void* vctx, void* vstream)
{
    AVCodecContext* ctx    = reinterpret_cast<AVCodecContext*>(vctx);
    AVStream*       stream = reinterpret_cast<AVStream*>(vstream);
    AVPacket* pkt = av_packet_alloc();
    bool ok = true;
    while (true) {
        int r = avcodec_receive_packet(ctx, pkt);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
        if (r < 0) { ok = false; break; }
        av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        av_interleaved_write_frame(FMT, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return ok;
}

void VideoRecorder::addVideoFrame(const void* data, int pitch)
{
    if (!m_fmtCtx || !data) return;
    const uint8_t* sp[1] = { static_cast<const uint8_t*>(data) };
    int ss[1] = { pitch };
    sws_scale(SWS, sp, ss, 0, m_height, VFRM->data, VFRM->linesize);
    VFRM->pts = m_videoPts++;
    VFRM->pict_type = AV_PICTURE_TYPE_NONE;
    avcodec_send_frame(VCTX, VFRM);
    writePacket(m_videoCtx, m_videoStream);
}

void VideoRecorder::addAudioSamples(const int16_t* data, int sampleCount)
{
    if (!m_fmtCtx || !data || sampleCount <= 0) return;
    int appendBytes = sampleCount * 2 * (int)sizeof(int16_t);
    int oldSize = m_audioBuf.size();
    m_audioBuf.resize(oldSize + appendBytes);
    memcpy(m_audioBuf.data() + oldSize, data, appendBytes);

    int frameBytes = m_audioFrameSize * 2 * (int)sizeof(int16_t);
    while (m_audioBuf.size() >= frameBytes) {
        if (av_frame_make_writable(AFRM) < 0) break;
        const uint8_t* sa[1] = { reinterpret_cast<const uint8_t*>(m_audioBuf.constData()) };
        int got = swr_convert(SWR, AFRM->data, m_audioFrameSize, sa, m_audioFrameSize);
        if (got <= 0) break;
        AFRM->pts = m_audioPts; AFRM->nb_samples = m_audioFrameSize;
        m_audioPts += m_audioFrameSize;
        avcodec_send_frame(ACTX, AFRM);
        writePacket(m_audioCtx, m_audioStream);
        m_audioBuf.remove(0, frameBytes);
    }
}

void VideoRecorder::close()
{
    if (!m_fmtCtx) return;
    if (m_videoCtx) { avcodec_send_frame(VCTX, nullptr); writePacket(m_videoCtx, m_videoStream); }
    if (m_audioCtx) {
        int frameBytes = m_audioFrameSize * 2 * (int)sizeof(int16_t);
        if (!m_audioBuf.isEmpty()) {
            m_audioBuf.resize(frameBytes, 0);
            const uint8_t* sa[1] = { reinterpret_cast<const uint8_t*>(m_audioBuf.constData()) };
            if (av_frame_make_writable(AFRM) >= 0) {
                swr_convert(SWR, AFRM->data, m_audioFrameSize, sa, m_audioFrameSize);
                AFRM->pts = m_audioPts; AFRM->nb_samples = m_audioFrameSize;
                avcodec_send_frame(ACTX, AFRM);
                writePacket(m_audioCtx, m_audioStream);
            }
        }
        avcodec_send_frame(ACTX, nullptr); writePacket(m_audioCtx, m_audioStream);
    }
    av_write_trailer(FMT);
    cleanupFFmpeg();
}

void VideoRecorder::cleanupFFmpeg()
{
    if (m_videoFrame) { av_freep(&VFRM->data[0]); av_frame_free(reinterpret_cast<AVFrame**>(&m_videoFrame)); }
    if (m_audioFrame) { AVFrame* f = AFRM; av_frame_free(&f); m_audioFrame = nullptr; }
    if (m_videoCtx)   { AVCodecContext* c = VCTX; avcodec_free_context(&c); m_videoCtx = nullptr; }
    if (m_audioCtx)   { AVCodecContext* c = ACTX; avcodec_free_context(&c); m_audioCtx = nullptr; }
    if (m_swsCtx)     { sws_freeContext(SWS); m_swsCtx = nullptr; }
    if (m_swrCtx)     { SwrContext* s = SWR; swr_free(&s); m_swrCtx = nullptr; }
    if (m_fmtCtx) {
        if (!(FMT->oformat->flags & AVFMT_NOFILE) && FMT->pb)
            avio_closep(&FMT->pb);
        AVFormatContext* f = FMT;
        avformat_free_context(f);
        m_fmtCtx = nullptr;
    }
    m_videoStream = m_audioStream = nullptr;
    m_videoPts = m_audioPts = 0;
    m_audioBuf.clear();
}

#undef FMT
#undef VCTX
#undef ACTX
#undef VSTR
#undef ASTR
#undef SWS
#undef SWR
#undef VFRM
#undef AFRM

#else  // !HAVE_FFMPEG

VideoRecorder::VideoRecorder()  {}
VideoRecorder::~VideoRecorder() {}
bool VideoRecorder::isOpen() const { return false; }
bool VideoRecorder::open(const QString&, int, int, double, int, int, VideoPixelFormat) {
    m_lastError = "FFmpeg not available in this build"; return false; }
void VideoRecorder::addVideoFrame(const void*, int) {}
void VideoRecorder::addAudioSamples(const int16_t*, int) {}
void VideoRecorder::close() {}
void VideoRecorder::cleanupFFmpeg() {}
bool VideoRecorder::writePacket(void*, void*) { return false; }

#endif  // HAVE_FFMPEG
#endif  // _WIN32
