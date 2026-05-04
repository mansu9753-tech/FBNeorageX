// BorderPanel.cpp — NeoRageX 스타일 3D 베벨 테두리 패널
//
// 조명 방향: 좌상단 (top-left light source)
//   TOP  / LEFT  면 : 밝은 시안 그라데이션 (조명)
//   BOTTOM / RIGHT 면: 어두운 네이비 그라데이션 (그림자)
//   각 스트립은 QLinearGradient 로 5단계 페이드 표현
//   코너: 상하 strip이 전체 폭을 담당 → 별도 코너 장식 없이 자연스럽게 이어짐

#include "BorderPanel.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>
#include <QFontMetrics>

// ── 색상 상수 (헤더 정의용, 실제 렌더는 paintEvent 내 팔레트 사용) ──
const QColor BorderPanel::COL_A    (0,   0,   200);
const QColor BorderPanel::COL_B    (0,   0,   160);
const QColor BorderPanel::COL_HI   (74, 190,  255);
const QColor BorderPanel::COL_TITLE(255, 255, 255);

// ─────────────────────────────────────────────────────────────
//  setRoundedCorners — 코너별 라운딩 설정
// ─────────────────────────────────────────────────────────────
void BorderPanel::setRoundedCorners(int cornerFlags)
{
    m_roundedCorners = cornerFlags;
    update();
}

// ─────────────────────────────────────────────────────────────
//  코너별 라운드 직사각형 경로 생성 헬퍼
//  corners: Qt::Corner 비트마스크 (라운딩할 코너만 포함)
// ─────────────────────────────────────────────────────────────
static QPainterPath makeCornerPath(int W, int H, int r, int corners)
{
    qreal tl = (corners & BorderPanel::CornerTL) ? r : 0;
    qreal tr = (corners & BorderPanel::CornerTR) ? r : 0;
    qreal br = (corners & BorderPanel::CornerBR) ? r : 0;
    qreal bl = (corners & BorderPanel::CornerBL) ? r : 0;

    QPainterPath path;
    path.moveTo(tl, 0);
    path.lineTo(W - tr, 0);
    if (tr > 0) path.arcTo(W - 2*tr, 0,        2*tr, 2*tr,  90, -90);
    else        path.lineTo(W, 0);
    path.lineTo(W, H - br);
    if (br > 0) path.arcTo(W - 2*br, H - 2*br, 2*br, 2*br,   0, -90);
    else        path.lineTo(W, H);
    path.lineTo(bl, H);
    if (bl > 0) path.arcTo(0,        H - 2*bl, 2*bl, 2*bl, -90, -90);
    else        path.lineTo(0, H);
    path.lineTo(0, tl);
    if (tl > 0) path.arcTo(0,        0,        2*tl, 2*tl, 180, -90);
    else        path.lineTo(0, 0);
    path.closeSubpath();
    return path;
}

// ── 생성자 ────────────────────────────────────────────────
BorderPanel::BorderPanel(const QString& title, QWidget* parent)
    : QWidget(parent)
    , m_title(title.toUpper())
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(false);

    m_timer = new QTimer(this);
    m_timer->setInterval(12);   // 12ms ≈ 1.6초 완주
    connect(m_timer, &QTimer::timeout, this, &BorderPanel::onTick);

    m_layout = new QVBoxLayout(this);
    int topMargin = m_title.isEmpty() ? BW + 2 : 36;
    m_layout->setContentsMargins(BW + 4, topMargin, BW + 4, BW + 4);
    m_layout->setSpacing(3);
}

void BorderPanel::setTitle(const QString& t) {
    m_title = t.toUpper();
    update();
}

void BorderPanel::startAnim(int delayMs) {
    m_prog = 0.0;
    if (delayMs > 0)
        QTimer::singleShot(delayMs, m_timer, qOverload<>(&QTimer::start));
    else
        m_timer->start();
}

void BorderPanel::onTick() {
    m_prog = std::min(1.0, m_prog + ANIM_STEP);
    update();
    if (m_prog >= 1.0) m_timer->stop();
}

// ─────────────────────────────────────────────────────────────
//  3D 베벨 그라데이션 생성 헬퍼
//  lit=true  : 조명 받는 면 (TOP/LEFT)  — 외곽 밝고 내측 어두운
//  lit=false : 그림자 면  (BOTTOM/RIGHT) — 외곽 어둡고 내측 약간 밝음
// ─────────────────────────────────────────────────────────────
static QBrush makeBevelGrad(QPointF from, QPointF to, bool lit)
{
    QLinearGradient g(from, to);
    if (lit) {
        g.setColorAt(0.00, QColor(  0,   3,  22));   // 최외곽 — 어두운 기준선
        g.setColorAt(0.04, QColor(172, 232, 255));   // 최외곽 글로우 (흰/시안)
        g.setColorAt(0.13, QColor( 88, 190, 238));   // 밝은 페이스 피크
        g.setColorAt(0.28, QColor( 44, 138, 188));   // 밝은 → 바디 전환
        g.setColorAt(0.50, QColor( 16,  65, 128));   // 바디 중앙 (메인 파랑)
        g.setColorAt(0.72, QColor( 10,  44,  88));   // 바디 → 어두운
        g.setColorAt(0.87, QColor(  0,  15,  50));   // 내측 어두운 페이스
        g.setColorAt(0.95, QColor(  0,   5,  20));   // 내측 어두운 선
        g.setColorAt(1.00, QColor(  0,   2,  12));   // 콘텐츠 엣지
    } else {
        g.setColorAt(0.00, QColor(  0,   2,  12));   // 콘텐츠 엣지
        g.setColorAt(0.05, QColor(  0,   8,  35));   // 내측 어두운 선
        g.setColorAt(0.16, QColor(  5,  30,  65));   // 내측 바디 시작
        g.setColorAt(0.40, QColor( 10,  44,  88));   // 바디 중앙
        g.setColorAt(0.62, QColor(  5,  24,  55));   // 바디 → 어두운
        g.setColorAt(0.78, QColor(  0,   8,  28));   // 어두운 페이스
        g.setColorAt(0.93, QColor(  0,   3,  14));   // 외측 글로우 (dim)
        g.setColorAt(1.00, QColor(  0,   2,  12));   // 최외곽 어두운 기준선
    }
    return QBrush(g);
}

// ─────────────────────────────────────────────────────────────
//  paintEvent  — 3D 베벨 테두리 + 애니메이션
// ─────────────────────────────────────────────────────────────
void BorderPanel::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter p(this);
    // 라운드 코너를 위해 Antialiasing 활성화
    p.setRenderHint(QPainter::Antialiasing, true);

    const int W  = width();
    const int H  = height();
    const int bw = BW;  // 20
    // 외곽 라운드 반지름 (bw/2 이하 → 코너 처리가 테두리 strip 안에서만)
    constexpr int r = 9;

    // ── 외곽 경로: 지정 코너만 라운드, 나머지는 직각 ────────
    QPainterPath outerPath = makeCornerPath(W, H, r, m_roundedCorners);

    // ── 애니메이션: 시계방향 드로잉 진행률 계산 ────────────
    const bool full  = (m_prog >= 1.0);
    const int  perim = 2 * (W + H);
    const int  drawn = full ? perim : static_cast<int>(perim * m_prog);

    // 각 변의 픽셀 드로잉 길이 (시계방향: 상→우→하→좌)
    auto segLen = [&](int start, int len) -> int {
        if (full) return len;
        int rem = drawn - start;
        return (rem <= 0) ? 0 : std::min(rem, len);
    };

    const int dTop   = segLen(0,           W);
    const int dRight = segLen(W,           H);
    const int dBot   = segLen(W + H,       W);
    const int dLeft  = segLen(W + H + W,   H);

    // ── 1. 내부 콘텐츠 배경 (클립 없이 — 항상 먼저 표시) ───
    // r=9 < bw=20 이므로 콘텐츠 rect(bw,bw ~ W-bw,H-bw)는
    // outerPath 안쪽에 완전히 포함 → 별도 클립 불필요
    p.setClipping(false);
    p.fillRect(bw, bw, W - 2*bw, H - 2*bw, QColor(0, 0, 8, 215));

    // ── 2. 클립 설정 ─────────────────────────────────────
    // 애니메이션 중: 시계방향 sweep 직사각형 ∩ 라운드 외곽
    // 완료 후: 라운드 외곽만 (코너가 부드럽게 잘림)
    if (!full) {
        QPainterPath cp;
        if (dTop   > 0) cp.addRect(QRectF(0,       0,       dTop,  bw));   // 상단 →
        if (dRight > 0) cp.addRect(QRectF(W-bw,    0,       bw,    dRight));// 우측 ↓
        if (dBot   > 0) cp.addRect(QRectF(W-dBot,  H-bw,   dBot,  bw));   // 하단 ←
        if (dLeft  > 0) cp.addRect(QRectF(0,       H-dLeft, bw,    dLeft));// 좌측 ↑
        // intersected: sweep 진행 범위 AND 라운드 외곽 → 코너가 곡선으로 등장
        p.setClipPath(cp.intersected(outerPath));
    } else {
        p.setClipPath(outerPath);
    }

    // ── 3. 4면 베벨 strip ─────────────────────────────────
    // 상단·하단: 전체 폭(코너 포함) — outerPath 클립이 코너를 라운드로 잘라줌
    // 좌측·우측: 코너를 제외한 중간 구간 — 상하 strip과 자연스럽게 이어짐

    // 상단 strip (조명 받는 면)
    p.fillRect(0, 0, W, bw,
               makeBevelGrad({0.0, 0.0}, {0.0, (qreal)bw}, true));

    // 하단 strip (그림자 면)
    p.fillRect(0, H-bw, W, bw,
               makeBevelGrad({0.0, (qreal)(H-bw)}, {0.0, (qreal)H}, false));

    // 좌측 strip (조명 받는 면)
    p.fillRect(0, bw, bw, H-2*bw,
               makeBevelGrad({0.0, 0.0}, {(qreal)bw, 0.0}, true));

    // 우측 strip (그림자 면)
    p.fillRect(W-bw, bw, bw, H-2*bw,
               makeBevelGrad({(qreal)(W-bw), 0.0}, {(qreal)W, 0.0}, false));

    // ── 4. 타이틀 ─────────────────────────────────────────
    QFont font("Press Start 2P", 8, QFont::Bold);
    if (!font.exactMatch())
        font = QFont("Courier New", 11, QFont::Bold);
    p.setFont(font);
    QFontMetrics fm(font);

    const QString titleStr = m_title.isEmpty()
                             ? QString()
                             : (' ' + m_title + ' ');
    const int tw = fm.horizontalAdvance(titleStr);
    const int th = fm.height();
    const int tx = bw;

    if (!m_title.isEmpty() && m_prog > 0.05 && dTop > tx + 4) {
        const int alpha = std::min(255, (int)(m_prog * 400));

        p.setClipping(false);

        // 타이틀 배경: 상단 strip 위에 검정 블록으로 텍스트 공간 확보
        p.fillRect(tx - 2, 0, tw + 4, bw, QColor(0, 0, 10));

        // 글로우 그림자
        QColor glowCol(0, 100, 180, alpha / 4);
        p.setPen(glowCol);
        p.drawText(tx + 1, th, titleStr);

        // 메인 텍스트 (밝은 시안)
        QColor textCol(80, 195, 245);
        textCol.setAlpha(alpha);
        p.setPen(textCol);
        p.drawText(tx, th - 1, titleStr);
    }
}
