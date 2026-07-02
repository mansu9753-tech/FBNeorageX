// BorderPanel.cpp — 아케이드 스타일 3D 파이프 테두리 패널 (CSS 스펙 구현)
//
// CSS 원본 스펙:
//   파이프 두께:      18px  (BASE_BW)
//   외곽 라운드:      28px  (BASE_OUTER_R)
//   내측 라운드:      12px  (BASE_INNER_R)
//   메인 컬러:        #3b82f6 (Vivid Blue)
//   외부 드롭섀도우:  6px 6px 15px rgba(0,0,0,0.7)
//   inset 그림자 4종:
//     1) 어두운 엣지   inset 0  0  0   1px  rgba(0,0,0,0.8)
//     2) TL 하이라이트 inset 3px 3px 12px rgba(255,255,255,0.6)
//     3) BR 그림자    inset -3px -3px 10px rgba(0,0,0,0.4)
//     4) 내측 비네트   inset 0  0  20px  8px  rgba(0,0,0,0.6)

#include "BorderPanel.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>
#include <QFontMetrics>
#include <QResizeEvent>
#include <algorithm>

// ─────────────────────────────────────────────────────────────
//  라운드 직사각형 경로 헬퍼 (x,y 오프셋 지원, 코너별 선택적 라운딩)
// ─────────────────────────────────────────────────────────────
static QPainterPath makeRoundedRectPath(qreal x, qreal y, qreal w, qreal h,
                                         qreal r, int corners)
{
    qreal tl = (corners & BorderPanel::CornerTL) ? r : 0;
    qreal tr = (corners & BorderPanel::CornerTR) ? r : 0;
    qreal br = (corners & BorderPanel::CornerBR) ? r : 0;
    qreal bl = (corners & BorderPanel::CornerBL) ? r : 0;

    QPainterPath path;
    path.moveTo(x + tl,     y);
    path.lineTo(x + w - tr, y);
    if (tr > 0) path.arcTo(x + w - 2*tr, y,             2*tr, 2*tr,  90, -90);
    else        path.lineTo(x + w,        y);
    path.lineTo(x + w,      y + h - br);
    if (br > 0) path.arcTo(x + w - 2*br, y + h - 2*br,  2*br, 2*br,   0, -90);
    else        path.lineTo(x + w,        y + h);
    path.lineTo(x + bl,     y + h);
    if (bl > 0) path.arcTo(x,             y + h - 2*bl,  2*bl, 2*bl, -90, -90);
    else        path.lineTo(x,             y + h);
    path.lineTo(x,          y + tl);
    if (tl > 0) path.arcTo(x,             y,             2*tl, 2*tl, 180, -90);
    else        path.lineTo(x,             y);
    path.closeSubpath();
    return path;
}

// ─────────────────────────────────────────────────────────────
//  생성자
// ─────────────────────────────────────────────────────────────
BorderPanel::BorderPanel(const QString& title, QWidget* parent)
    : QWidget(parent)
    , m_title(title.toUpper())
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(false);

    m_timer = new QTimer(this);
    m_timer->setInterval(12);          // 12ms ≈ 1.6초 완주
    connect(m_timer, &QTimer::timeout, this, &BorderPanel::onTick);

    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(3);
    updateMargins();
}

// ─────────────────────────────────────────────────────────────
//  공개 인터페이스
// ─────────────────────────────────────────────────────────────
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

void BorderPanel::setRoundedCorners(int cornerFlags) {
    m_roundedCorners = cornerFlags;
    update();
}

// ─────────────────────────────────────────────────────────────
//  내부 슬롯 / 이벤트
// ─────────────────────────────────────────────────────────────
void BorderPanel::onTick() {
    m_prog = std::min(1.0, m_prog + ANIM_STEP);
    update();
    if (m_prog >= 1.0) m_timer->stop();
}

void BorderPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateMargins();
    update();
}

// 현재 해상도에 맞춰 레이아웃 마진 재계산
void BorderPanel::updateMargins() {
    if (!m_layout) return;
    const double scale    = qMax(0.3, qMin(width(), height()) / 720.0);
    const int    bw       = qMax(4, qRound(BASE_BW * scale));
    const int    topMargin = m_title.isEmpty()
                             ? bw + 2
                             : qMax(bw + 2, qRound(28.0 * scale));
    m_layout->setContentsMargins(bw + 4, topMargin, bw + 4, bw + 4);
}

// ─────────────────────────────────────────────────────────────
//  paintEvent  — CSS 스펙 아케이드 파이프 테두리
// ─────────────────────────────────────────────────────────────
void BorderPanel::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing,       true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const int W = width();
    const int H = height();
    if (W < 10 || H < 10) return;

    // ── 스케일 계산 (720p 기준 비례) ──────────────────────
    const double scale  = qMax(0.3, qMin(W, H) / 720.0);
    const int    bw     = qMax(4,  qRound(BASE_BW      * scale));
    const int    outerR = qMax(2,  qRound(BASE_OUTER_R * scale));
    const int    innerR = qMax(1,  qRound(BASE_INNER_R * scale));

    // ── 내측 콘텐츠 영역 계산 ─────────────────────────────
    const qreal ix = bw,    iy = bw;
    const qreal iW = W - 2.0*bw,  iH = H - 2.0*bw;

    // ── 경로 생성 ─────────────────────────────────────────
    const QPainterPath outerPath =
        makeRoundedRectPath(0, 0, W, H, outerR, m_roundedCorners);

    QPainterPath innerPath;
    if (iW > 0 && iH > 0)
        innerPath = makeRoundedRectPath(ix, iy, iW, iH, innerR, m_roundedCorners);

    // 파이프 링 = 외곽 - 내측 구멍
    const QPainterPath pipeBase = outerPath.subtracted(innerPath);

    // ── 애니메이션 클립 ───────────────────────────────────
    const bool full  = (m_prog >= 1.0);
    const int  perim = 2 * (W + H);
    const int  drawn = full ? perim : static_cast<int>(perim * m_prog);

    auto segLen = [&](int start, int len) -> int {
        if (full) return len;
        int rem = drawn - start;
        return (rem <= 0) ? 0 : std::min(rem, len);
    };
    const int dTop   = segLen(0,         W);
    const int dRight = segLen(W,         H);
    const int dBot   = segLen(W + H,     W);
    const int dLeft  = segLen(W + H + W, H);

    QPainterPath pipePath = pipeBase;
    if (!full) {
        QPainterPath sweep;
        if (dTop   > 0) sweep.addRect(QRectF(0,       0,        dTop,   bw));
        if (dRight > 0) sweep.addRect(QRectF(W - bw,  0,        bw,     dRight));
        if (dBot   > 0) sweep.addRect(QRectF(W - dBot, H - bw,  dBot,   bw));
        if (dLeft  > 0) sweep.addRect(QRectF(0,        H - dLeft, bw,   dLeft));
        pipePath = pipeBase.intersected(sweep.intersected(outerPath));
    }

    // ═══════════════════════════════════════════════════════
    //  [1] 외부 드롭섀도우 — 알파를 약하게 + 콘텐츠 영역에 그리지 않음
    //      sb 번 누적되면 콘텐츠가 검게 덮이는 문제 회피.
    // ═══════════════════════════════════════════════════════
    {
        const int sx = qRound(6  * scale);
        const int sy = qRound(6  * scale);
        const int sb = qMax(1, qRound(15 * scale));

        p.setClipping(false);
        for (int i = sb; i >= 1; --i) {
            const double t     = static_cast<double>(i) / sb;
            // 알파를 30%로 낮춰 누적 마스크가 콘텐츠를 가리지 않게 함
            const int    alpha = qRound(53.0 * (1.0 - t * 0.75));
            const double ox    = sx + sb * t * 0.35;
            const double oy    = sy + sb * t * 0.35;
            p.save();
            p.translate(ox, oy);
            p.fillPath(outerPath, QColor(0, 0, 0, alpha));
            p.restore();
        }
    }

    // ═══════════════════════════════════════════════════════
    //  [2] 콘텐츠 배경 — #0f172a (반투명: 배경 이미지가 비치도록)
    //  alpha 80 ≈ 31% 불투명 → 배경 이미지가 더 잘 비침.
    //  자식 위젯(QListWidget viewport 등) 의 autoFillBackground 가
    //  false 로 설정되어 있을 때만 효과가 보임.
    // ═══════════════════════════════════════════════════════
    p.setClipping(false);
    if (!innerPath.isEmpty())
        p.fillPath(innerPath, QColor(0x0f, 0x17, 0x2a, 80));

    // ═══════════════════════════════════════════════════════
    //  [3] 파이프 베이스 그라데이션 — #3b82f6 Vivid Blue TL→BR
    // ═══════════════════════════════════════════════════════
    {
        QLinearGradient baseGrad(0, 0, W, H);
        baseGrad.setColorAt(0.00, QColor(0x93, 0xc5, 0xfd));  // blue-300  (TL 밝음)
        baseGrad.setColorAt(0.35, QColor(0x60, 0xa5, 0xfa));  // blue-400
        baseGrad.setColorAt(0.55, QColor(0x3b, 0x82, 0xf6));  // blue-500  (메인)
        baseGrad.setColorAt(0.75, QColor(0x25, 0x63, 0xeb));  // blue-600
        baseGrad.setColorAt(1.00, QColor(0x1d, 0x4e, 0xd8));  // blue-700  (BR 어둠)

        p.setClipPath(pipePath);
        p.fillPath(pipePath, QBrush(baseGrad));
    }

    // ═══════════════════════════════════════════════════════
    //  [4] inset 그림자 4종 — 파이프 링 위에 오버레이
    // ═══════════════════════════════════════════════════════

    // ── 4a. 어두운 외곽 엣지 — inset 0 0 0 1px rgba(0,0,0,0.8) ──
    {
        p.setClipPath(pipePath);
        QPen edgePen(QColor(0, 0, 0, 204), qMax(1.0, 1.5 * scale));
        p.setPen(edgePen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(outerPath);
    }

    // ── 4b. TL 하이라이트 — inset 3px 3px 12px rgba(255,255,255,0.6) ──
    {
        p.setClipPath(pipePath);
        const qreal hb = qRound(12 * scale);

        // 좌측 → 오른쪽 (왼쪽 파이프 면이 밝게)
        QLinearGradient hlLeft(0, 0, hb, 0);
        hlLeft.setColorAt(0.0, QColor(255, 255, 255, 153));
        hlLeft.setColorAt(1.0, QColor(255, 255, 255,   0));
        p.fillPath(pipePath, QBrush(hlLeft));

        // 위 → 아래 (상단 파이프 면이 밝게)
        QLinearGradient hlTop(0, 0, 0, hb);
        hlTop.setColorAt(0.0, QColor(255, 255, 255, 153));
        hlTop.setColorAt(1.0, QColor(255, 255, 255,   0));
        p.fillPath(pipePath, QBrush(hlTop));
    }

    // ── 4c. BR 그림자 — inset -3px -3px 10px rgba(0,0,0,0.4) ──
    {
        p.setClipPath(pipePath);
        const qreal bb = qRound(10 * scale);

        // 오른쪽 → 왼쪽 (오른쪽 파이프 면이 어둡게)
        QLinearGradient shRight(W - bb, 0, W, 0);
        shRight.setColorAt(0.0, QColor(0, 0, 0,   0));
        shRight.setColorAt(1.0, QColor(0, 0, 0, 102));
        p.fillPath(pipePath, QBrush(shRight));

        // 아래 → 위 (하단 파이프 면이 어둡게)
        QLinearGradient shBot(0, H - bb, 0, H);
        shBot.setColorAt(0.0, QColor(0, 0, 0,   0));
        shBot.setColorAt(1.0, QColor(0, 0, 0, 102));
        p.fillPath(pipePath, QBrush(shBot));
    }

    // ── 4d. 내측 비네트 — inset 0 0 20px 8px rgba(0,0,0,0.6) ──
    //    파이프 내부 엣지(콘텐츠 경계) 근처를 어둡게
    {
        p.setClipPath(pipePath);
        const qreal vs = qRound(8  * scale);   // spread
        const qreal vb = qRound(20 * scale);   // blur
        const qreal vr = vs + vb;              // 총 범위

        // 내측 상단 엣지 (iy에서 아래로 vr px)
        QLinearGradient vTop(0, iy, 0, iy + vr);
        vTop.setColorAt(0.0, QColor(0, 0, 0, 153));
        vTop.setColorAt(1.0, QColor(0, 0, 0,   0));
        p.fillPath(pipePath, QBrush(vTop));

        // 내측 하단 엣지 (iy+iH에서 위로 vr px)
        QLinearGradient vBot(0, iy + iH - vr, 0, iy + iH);
        vBot.setColorAt(0.0, QColor(0, 0, 0,   0));
        vBot.setColorAt(1.0, QColor(0, 0, 0, 153));
        p.fillPath(pipePath, QBrush(vBot));

        // 내측 좌측 엣지 (ix에서 오른쪽으로 vr px)
        QLinearGradient vLeft(ix, 0, ix + vr, 0);
        vLeft.setColorAt(0.0, QColor(0, 0, 0, 153));
        vLeft.setColorAt(1.0, QColor(0, 0, 0,   0));
        p.fillPath(pipePath, QBrush(vLeft));

        // 내측 우측 엣지 (ix+iW에서 왼쪽으로 vr px)
        QLinearGradient vRight(ix + iW - vr, 0, ix + iW, 0);
        vRight.setColorAt(0.0, QColor(0, 0, 0,   0));
        vRight.setColorAt(1.0, QColor(0, 0, 0, 153));
        p.fillPath(pipePath, QBrush(vRight));
    }

    // ═══════════════════════════════════════════════════════
    //  [5] 타이틀 텍스트
    // ═══════════════════════════════════════════════════════
    if (!m_title.isEmpty() && m_prog > 0.05 && dTop > bw + 4) {
        p.setClipping(false);

        const int    alpha     = std::min(255, static_cast<int>(m_prog * 400));
        const double fontScale = qMax(0.6, scale);

        QFont font("Press Start 2P", qRound(7 * fontScale), QFont::Bold);
        if (!font.exactMatch())
            font = QFont("Courier New", qRound(10 * fontScale), QFont::Bold);
        p.setFont(font);
        QFontMetrics fm(font);

        const QString titleStr = ' ' + m_title + ' ';
        const int tw = fm.horizontalAdvance(titleStr);
        const int th = fm.height();
        const int tx = bw;
        const int ty = qMax(th, bw - 1);   // 상단 strip 안에 들어오도록

        // 타이틀 배경 블록 (콘텐츠 배경색으로 텍스트 공간 확보)
        p.fillRect(tx - 2, 0, tw + 4, bw, QColor(0x0f, 0x17, 0x2a, 220));

        // 글로우 레이어 (파란 그림자)
        p.setPen(QColor(0x3b, 0x82, 0xf6, alpha / 3));
        p.drawText(tx + 1, ty, titleStr);

        // 메인 텍스트 (밝은 흰색/시안)
        QColor textCol(220, 235, 255);
        textCol.setAlpha(alpha);
        p.setPen(textCol);
        p.drawText(tx, ty - 1, titleStr);
    }
}
