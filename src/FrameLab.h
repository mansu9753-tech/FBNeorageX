#pragma once
// FrameLab.h — 프레임 단위 확인 / 캡처 화면 (SHOTS FACTORY → FRAME LAB)
//
//  게임을 멈춘 자리에서 앞뒤로 한 프레임씩 옮겨 가며 화면을 보고, 마음에 드는 프레임을
//  스크린샷으로 저장한다. 아래에는 앞뒤 프레임을 작은 격자로 늘어놓아 한눈에 고르게 한다.
//
//  · 뒤로: 게임을 하는 동안 쌓아 둔 최근 프레임 기록(MainWindow::m_frameHist)을 본다.
//  · 앞으로: 기록의 끝에서 더 가면 코어를 한 프레임 실행해 새 프레임을 만든다 (stepper).
//  화면 그리기와 입력만 맡고, 코어를 돌리거나 파일을 쓰는 일은 MainWindow 가 넘겨 준 함수가 한다.

#include <QImage>
#include <QVector>
#include <QWidget>
#include <functional>

#include "PixelFont.h"

class FrameLab : public QWidget {
    Q_OBJECT
public:
    enum class Cmd { Prev, Next, Prev10, Next10, First, Last, Save, Close };

    explicit FrameLab(QWidget* parent = nullptr);

    // frames: 오래된 것 → 최신 순. stepper: 코어를 한 프레임 실행하고 그 화면을 돌려준다(실패하면 널 이미지).
    // saver: 화면 한 장을 저장하고 저장한 경로를 돌려준다(실패하면 빈 문자열).
    void open(const QVector<QImage>& frames,
              std::function<QImage()> stepper,
              std::function<QString(const QImage&, int rel)> saver,
              bool korean);
    void navigate(Cmd c);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    struct Geo { QRect big, title, help; QVector<QRect> cells; int first = 0; int scale = 1; };
    Geo geometry() const;
    int  labelOf(int idx) const { return idx - m_base; }      // 0 = 열었을 때의 화면, 음수 = 그 이전

    QVector<QImage> m_frames;
    int  m_cur  = 0;
    int  m_base = 0;
    std::function<QImage()>                        m_step;
    std::function<QString(const QImage&, int)>     m_save;
    bool    m_ko = false;
    QString m_note;                    // 저장 결과 같은 안내 (잠깐 보여 준다)
    qint64  m_noteUntil = 0;
    PixelFont m_font;

    static constexpr int COLS = 8, ROWS = 2, MAX_FRAMES = 600;
};
