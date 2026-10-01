// NeoRageXSkin.cpp — 측정값을 목표 해상도로 푸는 코드

#include "NeoRageXSkin.h"

#include <QtMath>

namespace nrx {

Layout layout(int W, int H, int scale) {
    const Base& B = BASE;
    if (W <= 0) W = B.W;
    if (H <= 0) H = B.H;
    int s = scale > 0 ? scale : qMax(1, qRound(double(W) / B.W));

    Layout L;
    L.W = W; L.H = H; L.s = s;
    L.border = B.border * s;

    const int sbW = B.scrollW * s, sbArrow = B.scrollArrow * s;
    int statusY;

    if (W == B.W && H == B.H) {
        // 원본 해상도 — 측정한 사각형을 그대로 쓴다
        L.gamelist = B.gamelist;
        L.options  = B.options;
        L.preview  = B.preview;
        L.events   = B.events;
        static const char* ids[3]    = { "launch", "import", "exit" };
        static const char* labels[3] = { "LAUNCH", "IMPORT", "EXIT" };
        for (int i = 0; i < 3; ++i)
            L.buttons.append({ QString::fromLatin1(ids[i]), QString::fromLatin1(labels[i]),
                               QRect(B.buttonX[i], B.buttonY, B.buttonW, B.buttonH) });
        statusY = B.statusY;
    } else {
        // 가로·세로 모두 실제 크기에 맞춰 다시 나눈다.
        //   예전에는 가로를 정수배로만 키웠는데, 화면 폭이 640 의 배수가 아니면
        //   오른쪽에 그만큼 빈 띠가 남았다(레터박스). 비율을 지키면서 남는 폭을
        //   두 박스가 나눠 가지면 어떤 해상도에서도 화면이 꽉 찬다.
        const int M = B.margin * s, GY = B.gapY * s;
        const int BH = B.buttonH * s, SH = B.statusH * s, SG = B.statusGap * s;
        const int avail = H - M - GY - SG - SH;
        const int topH = qRound(double(avail) * B.topShare / (B.topShare + B.botShare));
        const int botH = avail - topH;
        const int y0 = M, y1 = y0 + topH + GY;

        const int GX = B.gapX * s, MR = B.marginR * s;
        const int availW = qMax(1, W - M - GX - MR);
        const int gw = qRound(double(availW) * B.leftShare / (B.leftShare + B.rightShare));
        const int ow = availW - gw;
        const int gx = M, ox = M + gw + GX;

        L.gamelist = QRect(gx, y0, gw, topH);
        L.options  = QRect(ox, y0, ow, topH - GY - BH);
        L.preview  = QRect(gx, y1, gw, botH);
        L.events   = QRect(ox, y1, ow, botH);

        // 버튼도 OPTIONS 박스 안에서의 비율을 지켜 배치한다
        static const char* ids[3]    = { "launch", "import", "exit" };
        static const char* labels[3] = { "LAUNCH", "IMPORT", "EXIT" };
        const double bw = double(B.buttonW) / B.options.width();
        for (int i = 0; i < 3; ++i) {
            const double fx = double(B.buttonX[i] - B.options.x()) / B.options.width();
            L.buttons.append({ QString::fromLatin1(ids[i]), QString::fromLatin1(labels[i]),
                               QRect(ox + qRound(ow * fx), y0 + topH - BH,
                                     qMax(1, qRound(ow * bw)), BH) });
        }
        statusY = y1 + botH + SG;
    }

    const QRect& gl = L.gamelist;
    L.scrollbar.x        = gl.x() + gl.width() - L.border - sbW;
    L.scrollbar.w        = sbW;
    L.scrollbar.top      = gl.y() + L.border;
    L.scrollbar.bottom   = gl.y() + gl.height() - L.border;
    L.scrollbar.arrow    = sbArrow;
    L.scrollbar.minThumb = 16 * s;

    L.text.cell     = ATLAS_CELL  * s;
    L.text.cellH    = ATLAS_CELLH * s;
    L.text.titleDX  = B.titleDX * s;
    L.text.titleDY  = B.titleDY * s;
    L.text.bodyDX   = B.bodyDX  * s;
    L.text.bodyDY   = B.bodyDY  * s;
    L.text.rowPitch = B.rowPitch * s;

    L.options_.pitch  = B.optPitch * s;
    L.options_.scaleY = B.optScaleY;

    L.status.y      = statusY;
    L.status.leftX  = B.statusLeftX * s;
    L.status.rightX = (W == B.W && H == B.H) ? B.statusRightX
                                             : W - B.marginR * s;

    return L;
}

}  // namespace nrx
