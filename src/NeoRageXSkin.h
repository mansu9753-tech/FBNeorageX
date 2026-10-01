#pragma once
#ifndef FBNRX_VERSION
#define FBNRX_VERSION "0"   // CMake 의 project VERSION 이 정의한다
#endif
// NeoRageXSkin.h — NeoRageX 0.6b 메뉴 셸의 측정값 (neoragex-skin.js 이식)
//
//  여기 있는 숫자는 전부 원본 스크린샷(640x480, 8비트 DirectDraw)의 픽셀을
//  직접 읽어서 얻은 값이다. 추정치가 아니다. 값은 여기서만 고치고
//  그리는 코드(NeoRageXShell.cpp)에는 숫자를 박아 넣지 않는다.
//
//  layout() 이 목표 해상도에 맞춰 좌표를 푼다. 640x480 은 측정값을 그대로
//  돌려주고, 나머지는 크롬을 정수배로 키운 뒤 세로만 재배치한다.
//  원본이 4:3 이고 요즘 타겟은 아니기 때문이다(스팀덱 16:10, 1080p 16:9).
//  균일 배율로 키우면 양옆에 바가 생긴다.

#include <QColor>
#include <QRect>
#include <QString>
#include <QVector>

namespace nrx {

// ── 파이프 테두리 단면 ───────────────────────────────────────
//   순수 블루: R=0, G=0, B만 변한다. 좌우 대칭이라 테두리가
//   빛을 받은 원통처럼 읽힌다. 10칸 = 1배율에서 10px.
inline const int PIPE[10] = { 107, 156, 198, 231, 247, 247, 231, 198, 156, 107 };
constexpr int PIPE_N = 10;

// 글리프 아틀라스 셀. 항상 1배율이고, 블릿할 때 정수배로 확대한다.
constexpr int ATLAS_CELL  = 8;
constexpr int ATLAS_CELLH = 10;

struct Palette {
    QColor black   {   0,   0,   0 };
    QColor text    { 247, 243, 247 };   // 목록 행, 제목, 로그
    QColor textDim { 148, 146, 148 };   // 비활성 항목
    QColor pure    { 255, 255, 255 };   // 스크롤바 화살표 글리프
    QColor btnFill {   0,   0, 247 };
    QColor btnTop  {   0, 162, 247 };
    QColor btnBot  {   0,   0, 115 };
    QColor barFill {   0,  81, 247 };   // 스크롤바 썸 + 화살표 버튼
    QColor barTop  {   0, 162, 247 };
    QColor barBot  {   0,   0, 115 };
    QColor track   { 247, 243, 247 };   // ★ 트랙이 흰색이고 썸이 파랑이다
    QColor selFill {   0,   0, 247 };
    // 박스 안쪽 바탕. 완전한 검정이면 배경 그림이 전혀 안 보이고, 완전 투명이면 글자가
    // 배경에 묻힌다. 0(투명)~255(불투명) 중 글자가 또렷하게 읽히는 선에서 배경을 살린다.
    int   panelAlpha = 185;
};
inline const Palette COLOR;

// ── 측정한 원본 (1배율) ──────────────────────────────────────
struct Base {
    int W = 640, H = 480;
    int border = 10;

    QRect gamelist {   5,   5, 211, 306 };
    QRect options  { 225,   5, 411, 281 };
    QRect preview  {   5, 315, 211, 156 };
    QRect events   { 225, 315, 411, 156 };

    int buttonY = 290, buttonH = 20, buttonW = 80;
    int buttonX[3] = { 254, 392, 530 };

    int scrollW = 12, scrollArrow = 12;
    int rowPitch = 15;
    int titleDX = 20, titleDY = 1;
    int bodyDX  = 16, bodyDY  = 12;
    int optPitch = 30, optScaleY = 2;

    int statusY = 471, statusLeftX = 10, statusRightX = 636;

    // 다른 해상도로 재배치할 때 쓰는 리듬
    //   세로: margin / gapY / 상태줄
    int margin = 5, gapY = 5, statusH = 12, statusGap = 1;
    int topShare = 306, botShare = 156;
    //   가로: 왼쪽 여백 5, GAMELIST 211, 사이 9, OPTIONS 411, 오른쪽 여백 4
    //   (원본 640 기준 측정값. 이 비율을 지키면 어떤 폭에서도 구도가 같다)
    int gapX = 9, marginR = 4;
    int leftShare = 211, rightShare = 411;
};
inline const Base BASE;

struct Preset { int width, height, scale; };
inline const Preset PRESET_ORIGINAL {  640,  480, 1 };
inline const Preset PRESET_DECK     { 1280,  800, 2 };   // 스팀덱 16:10
inline const Preset PRESET_FULLHD   { 1920, 1080, 3 };   // 16:9

// ── 풀린 레이아웃 ────────────────────────────────────────────
struct Button { QString id, label; QRect rect; };

struct Layout {
    int W = 640, H = 480, s = 1;
    int border = 10;

    QRect gamelist, options, preview, events;
    QVector<Button> buttons;

    struct {
        int x = 0, w = 0, top = 0, bottom = 0, arrow = 0, minThumb = 0;
    } scrollbar;

    struct {
        int cell = 8, cellH = 10;
        int titleDX = 20, titleDY = 1;
        int bodyDX = 16, bodyDY = 12;
        int rowPitch = 15;
    } text;

    struct { int pitch = 30; int scaleY = 2; } options_;

    struct {
        int y = 471, leftX = 10, rightX = 636;
        QString left  = QStringLiteral("Ver " FBNRX_VERSION);
        QString right = QStringLiteral("FBNeoRageX 2026");
    } status;
};

// 목표 해상도에 맞춰 레이아웃을 푼다.
//   scale 이 0 이면 width/BASE.W 를 반올림해 쓴다.
Layout layout(int W, int H, int scale = 0);

// ── 부팅 + 메뉴 타임라인 (60fps 기준 프레임 수) ─────────────
//   해상도와 무관하다.
struct Timing {
    int backdrop = 8;
    int box      = 240;   // 약 4초: 박스마다 펜 하나가 좌상단에서 출발
    int hold     = 18;    // 내용이 나오기 전 정적
    int titles   = 3;
    int btnLead  = 4;
    int optLead  = 6,  optStag  = 4;
    int listLead = 10, listStag = 2;
    int evtLead  = 16, evtStag  = 8;
    int rise     = 10;    // 고른 OPTIONS 항목이 맨 위로 올라가는 시간, 선형
    int riseGap  = 4;
    int subStag  = 3;
};
inline const Timing TIMING;

// ── 시대를 규정하는 렌더 규칙 ────────────────────────────────
struct Rules {
    int  paletteSteps = 6;      // 알파 블렌딩이 없었다. 페이드는 계단으로 스냅
    bool subpixel     = false;  // 모든 좌표를 반올림
    bool gTo9         = true;   // 8x8 face 는 소문자 g 를 9 처럼 그린다
    bool penCcw       = true;   // 왼변 먼저 (false 면 윗변 먼저)
};
inline const Rules RULES;

}  // namespace nrx
