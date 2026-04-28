#pragma once
// VideoRecorder.h — libav* 기반 H264+AAC MP4 녹화
// Qt Multimedia 백엔드에 의존하지 않으므로 WMF/GStreamer 제약 없음.

#include <QString>
#include <QByteArray>
#include <cstdint>

#if HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#endif

// libretro pixel formats (EmulatorState.h 와 동일한 값)
enum VideoPixelFormat {
    VPF_RGB565  = 0,
    VPF_XRGB8888 = 1,
    VPF_RGB5A3  = 2,   // 미사용
};

class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();

    // ── 녹화 시작 ───────────────────────────────────────────
    // path       : 출력 파일 경로 (ASCII/UTF-8)
    // width/height: 게임 해상도
    // fps        : 코어 FPS (예: 59.97)
    // sampleRate : 오디오 샘플레이트 (예: 44100)
    // channels   : 오디오 채널수 (보통 2 = stereo)
    // pixFmt     : VPF_RGB565 또는 VPF_XRGB8888
    bool open(const QString& path, int width, int height, double fps,
              int sampleRate = 44100, int channels = 2,
              VideoPixelFormat pixFmt = VPF_RGB565);

    // ── 프레임 추가 ─────────────────────────────────────────
    // data  : retro_video_refresh 에서 받은 픽셀 버퍼
    // pitch : 행당 바이트 수
    void addVideoFrame(const void* data, int pitch);

    // ── 오디오 추가 ─────────────────────────────────────────
    // data        : s16le stereo PCM
    // sampleCount : 채널당 샘플 수
    void addAudioSamples(const int16_t* data, int sampleCount);

    // ── 녹화 종료 ───────────────────────────────────────────
    void close();

    bool isOpen() const {
#if HAVE_FFMPEG
        return m_fmtCtx != nullptr;
#else
        return false;
#endif
    }

    // 마지막 오류 메시지
    QString lastError() const { return m_lastError; }

private:
#if HAVE_FFMPEG
    bool writePacket(AVCodecContext* ctx, AVStream* stream);
    void cleanup();

    int               m_width       = 0;
    int               m_height      = 0;
    VideoPixelFormat  m_pixFmt      = VPF_RGB565;

    AVFormatContext*  m_fmtCtx       = nullptr;
    AVCodecContext*   m_videoCtx     = nullptr;
    AVCodecContext*   m_audioCtx     = nullptr;
    AVStream*         m_videoStream  = nullptr;
    AVStream*         m_audioStream  = nullptr;
    SwsContext*       m_swsCtx       = nullptr;
    SwrContext*       m_swrCtx       = nullptr;
    AVFrame*          m_videoFrame   = nullptr;  // YUV420p, encoder로 보낼 프레임
    AVFrame*          m_audioFrame   = nullptr;  // fltp, encoder로 보낼 프레임
    int64_t           m_videoPts     = 0;
    int64_t           m_audioPts     = 0;
    int               m_audioFrameSize = 1024;   // AAC 고정 프레임 크기
    QByteArray        m_audioBuf;                // s16le 오디오 누적 버퍼
#endif

    QString           m_lastError;
};
