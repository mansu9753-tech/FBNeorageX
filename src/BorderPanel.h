#pragma once
// BorderPanel.h — NeoRageX 스타일 애니메이션 테두리 패널

#include <QWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QColor>
#include <QLinearGradient>

class BorderPanel : public QWidget {
    Q_OBJECT
public:
    static constexpr int BW = 20;  // 테두리 두께

    explicit BorderPanel(const QString& title = {}, QWidget* parent = nullptr);

    void setTitle(const QString& t);
    QString title() const { return m_title; }

    // 등장 애니메이션 시작 (delay ms 후)
    void startAnim(int delayMs = 0);

    // 내부 레이아웃 접근 (자식 위젯 추가용)
    QVBoxLayout* innerLayout() const { return m_layout; }

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onTick();

private:
    QString      m_title;
    double       m_prog   = 0.0;   // 0.0 ~ 1.0
    QTimer*      m_timer  = nullptr;
    QVBoxLayout* m_layout = nullptr;

    // 색상 상수
    static const QColor COL_A;     // 메인 테두리 좌상
    static const QColor COL_B;     // 메인 테두리 우하
    static const QColor COL_HI;    // 안쪽 하이라이트 선
    static const QColor COL_TITLE; // 타이틀 글자색

    static constexpr int    HI_THICKNESS = 2;
    static constexpr double ANIM_STEP    = 0.025;
};
