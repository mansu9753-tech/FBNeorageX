#pragma once
// PreviewVideo.h — 프리뷰 영상 전용 소프트웨어 디코더 (Linux/FFmpeg)
//
// 왜 QMediaPlayer 를 안 쓰는가:
//   Qt6 의 FFmpeg 멀티미디어 백엔드는 재생 시작 시 하드웨어 디코더를
//   VAAPI → CUDA → VDPAU → Vulkan 순서로 탐색한다. 스팀덱에서는
//     · VAAPI  : 번들 FFmpeg(우분투 빌드) ↔ 시스템 Mesa 드라이버 불일치
//     · Vulkan : GPU 가 VK_KHR_video_decode_queue 미지원
//   인데도 Vulkan 컨텍스트를 선택해 버려, 프레임 텍스처 생성 단계에서
//   프로세스가 그대로 죽었다(실측 로그로 확인).
//   드라이버 차단 환경변수로 우회하려 했으나, 환경에 따라 OpenGL 이
//   Vulkan 위에서 도는 경우(Zink) 게임 화면 렌더링까지 깨질 위험이 있었다.
//
//   → 프리뷰 영상은 작고 짧으므로 직접 소프트웨어 디코딩한다.
//     하드웨어 탐색 경로가 아예 없어져 드라이버 문제에서 자유롭다.
//
// 사용법: open() 으로 재생 시작, frameReady() 로 QImage 를 받아 그리면 된다.
//         파일 끝에 도달하면 처음으로 되감아 반복 재생한다(무음).

#include <QObject>
#include <QImage>
#include <QString>
#include <QTimer>
#include <QPointer>
#include <QElapsedTimer>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;
class QAudioSink;
class QIODevice;

class PreviewVideo : public QObject {
    Q_OBJECT
public:
    explicit PreviewVideo(QObject* parent = nullptr);
    ~PreviewVideo() override;

    // 재생 시작. 실패 시 false (지원하지 않는 파일/빌드에 FFmpeg 없음 등)
    bool open(const QString& path);
    void stop();
    void setVolume(int percent);   // 0~100 (앱 볼륨 설정과 연동)
    bool isPlaying() const { return m_timer && m_timer->isActive(); }

signals:
    void frameReady(const QImage& img);
    void finished();                       // 끝까지 재생 완료 (반복은 호출측이 결정)
    void failed(const QString& reason);

private:
    void close();
    void tick();             // 오디오 공급 + 표시 타이밍 확인
    void flushAudio();       // 사인크가 받아주는 만큼만 쓰고 나머지는 보관
    bool pumpOnce();         // 패킷 하나 처리 (false = 더 읽을 것 없음)
    qint64 clockUsec() const;// 재생 기준 시계 (오디오가 있으면 오디오 클럭)

    QTimer*          m_timer   = nullptr;
    AVFormatContext* m_fmt     = nullptr;
    AVCodecContext*  m_dec     = nullptr;
    AVFrame*         m_frame   = nullptr;
    AVFrame*         m_rgb     = nullptr;
    AVPacket*        m_pkt     = nullptr;
    SwsContext*      m_sws     = nullptr;
    int              m_swsFmt  = -1;    // 변환기를 만든 기준 픽셀 포맷
    unsigned char*   m_rgbBuf  = nullptr;
    int              m_stream  = -1;
    int              m_w = 0, m_h = 0;

    // ── 재생 타이밍 ────────────────────────────────────────
    //   고정 간격으로 프레임을 뿌리면 실제 재생 속도와 미세하게 어긋나
    //   시간이 갈수록 소리와 영상이 벌어진다(싱크 밀림).
    //   → 각 프레임의 PTS 를 기준 시계와 비교해 "때가 됐을 때만" 표시한다.
    //     오디오가 있으면 오디오 클럭이 기준(오디오 마스터), 없으면 경과 시간.
    QElapsedTimer    m_elapsed;
    double           m_vTimeBase  = 0.0;   // 비디오 스트림 time_base (초)

    // 디코딩해 둔 프레임 대기열 (표시 시각 순).
    //   오디오를 미리 채우려면 패킷을 앞서 읽어야 하고, 그 과정에서 나온
    //   영상 프레임을 담아둘 곳이 필요하다.
    struct QueuedFrame { qint64 ptsUsec; QImage img; };
    QList<QueuedFrame> m_vq;
    int              m_vqMax = 12;         // 프레임 크기에 따라 open() 에서 조정
    bool             m_eof   = false;      // 파일 끝까지 읽음

    // 사인크가 아직 받아가지 못한 PCM. write() 는 요청보다 적게 쓸 수 있으므로
    // 남은 것을 버리지 않고 여기 보관했다가 다음 기회에 마저 쓴다.
    //   (이걸 버리면 그 길이만큼 소리에 구멍이 나 '뚝뚝 끊기는' 소리가 된다)
    QByteArray       m_apend;

    // ── 오디오 (있으면 함께 재생) ──────────────────────────
    //   비디오와 같은 파일에서 오디오 스트림을 디코딩해 QAudioSink 로 흘린다.
    //   프리뷰용 짧은 반복 영상이므로 정밀 동기화 대신 단순 스트리밍 방식.
    AVCodecContext*  m_adec    = nullptr;
    SwrContext*      m_swr     = nullptr;
    AVFrame*         m_aframe  = nullptr;
    QAudioSink*      m_sink    = nullptr;
    QPointer<QIODevice> m_sinkIo;
    int              m_astream = -1;
    int              m_volume  = 100;

    void openAudio();            // 오디오 스트림 준비 (없으면 조용히 통과)
    void feedAudio(AVPacket* p); // 오디오 패킷 디코딩 → 재생 버퍼로
};
