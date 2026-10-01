#pragma once
// IntroSplash.h — 프로그램을 켤 때 처음 나오는 오프닝 화면
//
//  assets/intro/ 폴더에 intro.mp4 (또는 webm / mkv / avi / mov / wmv) 가 있으면 그 영상을
//  재생하고, 없으면 내장 샘플 애니메이션(로고가 그려지는 약 4초)을 보여 준다.
//  영상은 그냥 파일만 바꿔 넣으면 된다 (다시 빌드할 필요 없음).
//  아무 키·마우스·패드 버튼이나 누르면 건너뛴다.
//
//  재생 방식은 프리뷰 영상과 같다: Linux 는 자체 FFmpeg 디코더(PreviewVideo),
//  Windows 는 QMediaPlayer. 재생이 안 되는 파일이면 샘플로 대신한다.

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include "PixelFont.h"

class QMediaPlayer;
class QVideoSink;
class PreviewVideo;
class QTimer;

class IntroSplash : public QWidget {
    Q_OBJECT
public:
    explicit IntroSplash(QWidget* parent = nullptr);
    ~IntroSplash() override;

    // 재생할 영상 파일을 찾는다 (없으면 빈 문자열 = 샘플 애니메이션)
    static QString findVideo(const QString& baseDir);

    void start(const QString& videoPath);     // 빈 문자열이면 샘플
    void skip();                              // 바로 끝낸다
    bool running() const { return m_running; }
    int  elapsedMs() const { return m_running ? int(m_clock.elapsed()) : 0; }

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    void begin(bool sample);
    void end();
    void paintSample(QPainter& p);
    void band(QPainter& p, const QRect& b, double t, int thick) const;

    bool          m_running = false;
    bool          m_video   = false;      // 영상 재생 중인가 (아니면 샘플)
    QElapsedTimer m_clock;
    QTimer*       m_tick = nullptr;
    QImage        m_frame;                // 마지막 영상 프레임
    QImage        m_canvas;               // 샘플 애니메이션이 그려지는 낮은 해상도 면
    PixelFont     m_font;

    QMediaPlayer* m_player = nullptr;
    QVideoSink*   m_sink   = nullptr;
    PreviewVideo* m_decoder = nullptr;
};
