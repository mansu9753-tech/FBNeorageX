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

void BorderPanel::setClassicStyle(bool on) {
    if (m_classic == on) return;
    m_classic = on;
    // ★ 애니메이션은 그대로 둔다 — 클래식에서도 선이 그려지는 연출을 쓴다.
    updateMargins();
    update();
}

// ── 클래식(NeoRageX 0.6b) 수치 ───────────────────────────────
//   원본은 640x480 기준 2px 테두리 + 좌상단 제목이다.
//   해상도가 커져도 같은 인상을 주도록 살짝만 스케일한다.
static int classicBorderW(int w, int h) {
    const double scale = qMax(1.0, qMin(w, h) / 480.0);
    // 기본 12px — 얇아서 티가 안 난다고 하여 기존(4px)의 3배로 키움
    return qBound(10, qRound(12.0 * scale), 26);
}
static int classicTitleH(int w, int h) {
    const double scale = qMax(1.0, qMin(w, h) / 480.0);
    return qBound(14, qRound(15.0 * scale), 26);
}

// 현재 해상도에 맞춰 레이아웃 마진 재계산
void BorderPanel::updateMargins() {
    if (!m_layout) return;

    if (m_classic) {
        // 얇은 테두리 + 제목 줄만큼만 여백 (원본처럼 내용이 넓게 쓰이도록)
        const int bw = classicBorderW(width(), height());
        const int th = m_title.isEmpty() ? 0 : classicTitleH(width(), height());
        m_layout->setContentsMargins(bw + 3, bw + th, bw + 3, bw + 3);
        return;
    }

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

    // ════════════════════════════════════════════════════════
    //  클래식 스타일 — NeoRageX 0.6b
    //   · 각진 얇은 파란 테두리 (라운드 없음)
    //   · 좌상단에 제목, 그 자리만 테두리를 비워 라벨처럼 보이게
    //   · 안쪽은 칠하지 않는다 → 배경 이미지가 그대로 비쳐 보임
    // ════════════════════════════════════════════════════════
    if (m_classic) {
        const int bw = classicBorderW(W, H);
        const int th = m_title.isEmpty() ? 0 : classicTitleH(W, H);

        p.setRenderHint(QPainter::Antialiasing, false);   // 각진 픽셀 느낌 유지

        // 내부를 살짝 어둡게 깔아 글자 가독성 확보 (배경은 여전히 비쳐 보임)
        p.fillRect(QRect(bw, bw, W - 2*bw, H - 2*bw), QColor(0, 0, 16, 120));

        // ── 등장 애니메이션 ──────────────────────────────────
        //   한쪽 모서리에서 선이 뻗어나가 사각형을 그려 나간다.
        //   (원본 NeoRageX 의 박스가 그려지는 느낌)
        const bool full  = (m_prog >= 1.0);
        const int  perim = 2 * (W + H);
        const int  drawn = full ? perim : int(perim * m_prog);
        auto seg = [&](int start, int len) {
            if (full) return len;
            const int rem = drawn - start;
            return (rem <= 0) ? 0 : std::min(rem, len);
        };
        const int dTop   = seg(0,         W);
        const int dRight = seg(W,         H);
        const int dBot   = seg(W + H,     W);
        const int dLeft  = seg(W + H + W, H);

        // ── 입체 파이프 테두리 ───────────────────────────────
        //   ★ 변마다 따로 칠하면 모서리에서 그라디언트가 끊겨 이어지지 않아
        //     보였다. 사각 링(바깥-안쪽) 하나를 만들어 대각선 그라디언트로
        //     한 번에 칠한다 → 네 모서리가 자연스럽게 연결된다.
        //   애니메이션은 "그려진 구간"만 클리핑해서 보여주는 방식으로 처리.
        QRegion drawnRegion;
        if (full) {
            drawnRegion = QRegion(0, 0, W, H);
        } else {
            if (dTop   > 0) drawnRegion += QRect(0, 0, dTop, bw);
            if (dRight > 0) drawnRegion += QRect(W - bw, 0, bw, dRight);
            if (dBot   > 0) drawnRegion += QRect(W - dBot, H - bw, dBot, bw);
            if (dLeft  > 0) drawnRegion += QRect(0, H - dLeft, bw, dLeft);
        }

        if (!drawnRegion.isEmpty()) {
            p.save();
            p.setClipRegion(drawnRegion);

            // ── 관(파이프) 단면 음영 ──────────────────────────
            //   테두리 두께를 가로지르며 색을 바꾼다.
            //     바깥 = 어두움 → 살짝 안쪽에 하이라이트(빛 반사)
            //     → 중간 본색 → 안쪽으로 갈수록 다시 어두움
            //   이렇게 하면 단면이 둥근 관처럼 보이고, 사각형을 한 겹씩
            //   그리는 방식이라 네 모서리에서 음영이 자연스럽게 이어진다.
            auto tubeColor = [](double t) -> QColor {
                struct Stop { double t; int r, g, b; };
                static const Stop stops[] = {
                    { 0.00, 0x0a, 0x18, 0x5e },   // 바깥 가장자리 (어두움)
                    { 0.14, 0x3f, 0x6d, 0xf5 },
                    { 0.28, 0xdc, 0xe9, 0xff },   // 하이라이트 (빛 반사)
                    { 0.45, 0x59, 0x8a, 0xff },
                    { 0.62, 0x22, 0x4e, 0xe6 },   // 본색
                    { 0.82, 0x12, 0x2c, 0x9c },
                    { 1.00, 0x05, 0x0f, 0x4a },   // 안쪽 가장자리 (어두움)
                };
                const int n = int(sizeof(stops) / sizeof(stops[0]));
                t = qBound(0.0, t, 1.0);
                for (int i = 1; i < n; ++i) {
                    if (t <= stops[i].t) {
                        const Stop& a = stops[i - 1];
                        const Stop& b = stops[i];
                        const double f = (b.t - a.t) < 1e-6 ? 0.0 : (t - a.t) / (b.t - a.t);
                        return QColor(int(a.r + (b.r - a.r) * f),
                                      int(a.g + (b.g - a.g) * f),
                                      int(a.b + (b.b - a.b) * f));
                    }
                }
                return QColor(stops[n - 1].r, stops[n - 1].g, stops[n - 1].b);
            };

            // 사각 테두리를 한 겹씩 안쪽으로 그린다 (모서리 음영이 이어짐)
            for (int i = 0; i < bw; ++i) {
                const double t = (bw <= 1) ? 0.0 : double(i) / (bw - 1);
                p.setPen(QPen(tubeColor(t), 1));
                p.drawRect(i, i, W - 2*i - 1, H - 2*i - 1);
            }
            p.restore();
        }

        // 제목 — 좌상단, 테두리 바로 안쪽에 배치
        //   (테두리를 굵게 했으므로 시작 위치를 테두리 두께만큼 더 띄운다)
        if (th > 0) {
            QFont f("Courier New", qMax(8, qRound(th * 0.60)), QFont::Bold);
            p.setFont(f);
            const QRect tr(bw + 6, bw + 1, W - 2*bw - 12, th);
            // 글자 뒤를 살짝 눌러 테두리와 겹쳐도 읽히게
            p.fillRect(tr.adjusted(-4, 0, 0, 0), QColor(0, 0, 24, 190));
            p.setPen(QColor(0xcc, 0xdd, 0xff));
            p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, m_title);
        }
        return;
    }

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
