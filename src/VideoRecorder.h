#pragma once
// VideoRecorder.h — 플랫폼별 H264+AAC MP4 녹화
//   Windows : IMFSinkWriter (WMF 직접 — 외부 DLL 불필요, 단일 EXE)
//   Linux   : libav* (FFmpeg, 시스템 공유 라이브러리)
//
// 플랫폼 세부 타입은 VideoRecorder.cpp 에서만 include → 헤더는 Qt/STL만 의존

#include <QString>
#include <QByteArray>
#include <cstdint>

// libretro pixel format (EmulatorState.h 와 동일한 값)
enum VideoPixelFormat {
    VPF_RGB565   = 0,
    VPF_XRGB8888 = 1,
};

class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();

    // ── 녹화 시작 ──────────────────────────────────────────
    // path      : 출력 파일 경로
    // fps       : 코어 FPS
    // sampleRate: 오디오 샘플레이트
    bool open(const QString& path, int width, int height, double fps,
              int sampleRate = 44100, int channels = 2,
              VideoPixelFormat pixFmt = VPF_RGB565);

    // ── 프레임 추가 ────────────────────────────────────────
    // data : retro_video_refresh 픽셀 버퍼 / pitch : 행당 바이트
    void addVideoFrame(const void* data, int pitch);

    // ── 오디오 추가 ────────────────────────────────────────
    // data : s16le stereo PCM / sampleCount : 채널당 샘플 수
    void addAudioSamples(const int16_t* data, int sampleCount);

    // ── 녹화 종료 ──────────────────────────────────────────
    void close();

    bool    isOpen()    const;
    QString lastError() const { return m_lastError; }

private:
    // ── 공통 ──────────────────────────────────────────────
    QString          m_lastError;
    int              m_width      = 0;
    int              m_height     = 0;
    int              m_sampleRate = 44100;
    int              m_channels   = 2;
    VideoPixelFormat m_pixFmt     = VPF_RGB565;

#ifdef _WIN32
    // ── Windows WMF (IMFSinkWriter) ────────────────────────
    // 실제 COM 포인터는 .cpp 에서만 사용; 여기선 void* 로 보관
    void*   m_sinkWriter  = nullptr;  // IMFSinkWriter*
    int     m_videoStream = -1;
    int     m_audioStream = -1;
    int64_t m_videoTime   = 0;   // 100ns 단위 PTS
    int64_t m_audioTime   = 0;
    int64_t m_frameDur    = 0;   // 프레임 하나의 100ns 지속시간
    bool    m_mfStarted   = false;

    void    convertToNV12(const void* src, int srcPitch,
                          uint8_t* dst, int dstStrideY,
                          uint8_t* dstUV) const;
#else
    // ── Linux/Mac: FFmpeg (libav*) ─────────────────────────
    // 실제 AVCodecContext 등은 .cpp 에서만 include
    void*    m_fmtCtx      = nullptr;  // AVFormatContext*
    void*    m_videoCtx    = nullptr;  // AVCodecContext*
    void*    m_audioCtx    = nullptr;  // AVCodecContext*
    void*    m_videoStream = nullptr;  // AVStream*
    void*    m_audioStream = nullptr;  // AVStream*
    void*    m_swsCtx      = nullptr;  // SwsContext*
    void*    m_swrCtx      = nullptr;  // SwrContext*
    void*    m_videoFrame  = nullptr;  // AVFrame*
    void*    m_audioFrame  = nullptr;  // AVFrame*
    int64_t  m_videoPts    = 0;
    int64_t  m_audioPts    = 0;
    int      m_audioFrameSize = 1024;
    QByteArray m_audioBuf;

    bool writePacket(void* ctx, void* stream);
    void cleanupFFmpeg();
#endif
};
