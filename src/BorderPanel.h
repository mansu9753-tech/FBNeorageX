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

    // 라운드 코너 제어 — 인접 패널과 맞닿는 안쪽 코너는 직각(square)으로 설정
    // CornerXxx 상수를 OR 조합해서 사용
    // 예) 좌측 패널: CornerTL | CornerBL
    //     우측 패널: CornerTR | CornerBR
    static constexpr int CornerTL  = 0x1;   // Top-Left
    static constexpr int CornerTR  = 0x2;   // Top-Right
    static constexpr int CornerBL  = 0x4;   // Bottom-Left
    static constexpr int CornerBR  = 0x8;   // Bottom-Right
    static constexpr int CornerAll = 0xF;   // 모든 코너 (기본값)

    void setRoundedCorners(int cornerFlags);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onTick();

private:
    QString      m_title;
    double       m_prog   = 0.0;   // 0.0 ~ 1.0
    QTimer*      m_timer  = nullptr;
    QVBoxLayout* m_layout = nullptr;
    int          m_roundedCorners = CornerAll;

    // 색상 상수
    static const QColor COL_A;     // 메인 테두리 좌상
    static const QColor COL_B;     // 메인 테두리 우하
    static const QColor COL_HI;    // 안쪽 하이라이트 선
    static const QColor COL_TITLE; // 타이틀 글자색

    static constexpr int    HI_THICKNESS = 2;
    static constexpr double ANIM_STEP    = 0.025;
};
