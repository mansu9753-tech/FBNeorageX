#pragma once
// BorderPanel.h — 아케이드 스타일 3D 파이프 테두리 패널
//
// CSS 스펙 기반 구현:
//   파이프 두께:      18px (기준 720p 기준, DPI 비례 스케일)
//   외곽 라운드:      28px
//   내측 라운드:      12px
//   메인 컬러:        #3b82f6 (Vivid Blue)
//   inset 그림자:     4종 (어두운 엣지, TL 하이라이트, BR 그림자, 내측 비네트)
//   외부 드롭섀도우:  6px 6px 15px rgba(0,0,0,0.7)

#include <QWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QColor>
#include <QLinearGradient>
#include <QCursor>

class BorderPanel : public QWidget {
    Q_OBJECT
public:
    explicit BorderPanel(const QString& title = {}, QWidget* parent = nullptr);

    void setTitle(const QString& t);
    QString title() const { return m_title; }

    // 등장 애니메이션 시작 (delay ms 후)
    void startAnim(int delayMs = 0);

    // 내부 레이아웃 접근 (자식 위젯 추가용)
    QVBoxLayout* innerLayout() const { return m_layout; }

    // 라운드 코너 제어
    static constexpr int CornerTL  = 0x1;
    static constexpr int CornerTR  = 0x2;
    static constexpr int CornerBL  = 0x4;
    static constexpr int CornerBR  = 0x8;
    static constexpr int CornerAll = 0xF;

    void setRoundedCorners(int cornerFlags);

    // CSS 기준 수치 (기본값 — 스케일링 전)
    static constexpr int BASE_BW     = 18;  // 파이프 두께
    static constexpr int BASE_OUTER_R = 28; // 외곽 라운드
    static constexpr int BASE_INNER_R = 12; // 내측 라운드

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTick();

private:
    QString      m_title;
    double       m_prog   = 0.0;
    QTimer*      m_timer  = nullptr;
    QVBoxLayout* m_layout = nullptr;
    int          m_roundedCorners = CornerAll;

    // 현재 해상도에 맞게 스케일된 값 업데이트
    void updateMargins();

    static constexpr double ANIM_STEP = 0.025;
};
