#pragma once
// PadRawBits.h — 게임패드 "원시 입력 비트" 정의 (플랫폼 공통 어휘)
//
//  매핑 테이블의 key 가 바로 이 비트값이다. 캡처도 같은 값을 돌려준다.
//
//  ★ 이 비트들은 플랫폼마다 값이 전혀 다르다.
//      Windows : XInput 의 wButtons 비트마스크
//      Linux   : 조이스틱 버튼 번호를 비트로 올린 값 (xpad 표준 순서)
//    즉 같은 숫자가 다른 버튼을 뜻한다. 예전에는 이 정의가 GamepadManager.cpp
//    안에만 있고, 이름표는 MainWindow.cpp 에 Windows 기준으로 따로 적혀 있어서
//    스팀덱에서 SELECT 가 "L3", START 가 "R3", A/B/X/Y 가 "D-Pad" 로 표시됐다.
//    정의와 이름표를 한 파일에 묶어 두 번 다시 어긋나지 않게 한다.

#include <QString>
#include <cstdint>

// 트리거는 양 플랫폼 공통 비트를 쓴다 (XInput 은 아날로그라 별도 비트로 올린다)
static constexpr uint32_t PAD_L2 = 1u << 16;
static constexpr uint32_t PAD_R2 = 1u << 17;

#ifdef _WIN32
// ── Windows: XInput wButtons 비트마스크 ──────────────────────
static constexpr uint32_t PAD_DPAD_UP    = 0x0001;
static constexpr uint32_t PAD_DPAD_DOWN  = 0x0002;
static constexpr uint32_t PAD_DPAD_LEFT  = 0x0004;
static constexpr uint32_t PAD_DPAD_RIGHT = 0x0008;
static constexpr uint32_t PAD_START      = 0x0010;
static constexpr uint32_t PAD_BACK       = 0x0020;
static constexpr uint32_t PAD_L3         = 0x0040;
static constexpr uint32_t PAD_R3         = 0x0080;
static constexpr uint32_t PAD_LB         = 0x0100;
static constexpr uint32_t PAD_RB         = 0x0200;
static constexpr uint32_t PAD_A          = 0x1000;
static constexpr uint32_t PAD_B          = 0x2000;
static constexpr uint32_t PAD_X          = 0x4000;
static constexpr uint32_t PAD_Y          = 0x8000;
// Windows 는 스틱을 D-패드 비트에 합쳐 처리하므로 별도 비트가 없다
static constexpr uint32_t PAD_ST_UP      = 0;
static constexpr uint32_t PAD_ST_DOWN    = 0;
static constexpr uint32_t PAD_ST_LEFT    = 0;
static constexpr uint32_t PAD_ST_RIGHT   = 0;
static constexpr uint32_t PAD_GUIDE      = 0x0400;
#else
// ── Linux: 조이스틱 버튼 번호 비트 (xpad 표준) ───────────────
static constexpr uint32_t PAD_A     = 1u << 0;    // A / Cross
static constexpr uint32_t PAD_B     = 1u << 1;    // B / Circle
static constexpr uint32_t PAD_X     = 1u << 2;    // X / Square
static constexpr uint32_t PAD_Y     = 1u << 3;    // Y / Triangle
static constexpr uint32_t PAD_LB    = 1u << 4;    // L1
static constexpr uint32_t PAD_RB    = 1u << 5;    // R1
static constexpr uint32_t PAD_BACK  = 1u << 6;    // Back / Select
static constexpr uint32_t PAD_START = 1u << 7;    // Start / Menu
static constexpr uint32_t PAD_GUIDE = 1u << 8;    // Guide / 홈
static constexpr uint32_t PAD_L3    = 1u << 9;
static constexpr uint32_t PAD_R3    = 1u << 10;

static constexpr uint32_t PAD_DPAD_UP    = 1u << 20;
static constexpr uint32_t PAD_DPAD_DOWN  = 1u << 21;
static constexpr uint32_t PAD_DPAD_LEFT  = 1u << 22;
static constexpr uint32_t PAD_DPAD_RIGHT = 1u << 23;
static constexpr uint32_t PAD_ST_UP      = 1u << 24;
static constexpr uint32_t PAD_ST_DOWN    = 1u << 25;
static constexpr uint32_t PAD_ST_LEFT    = 1u << 26;
static constexpr uint32_t PAD_ST_RIGHT   = 1u << 27;
#endif

// 원시 비트 → 사람이 읽는 이름. 화면에 뜨는 버튼 이름은 전부 이 함수만 쓴다.
QString padButtonName(int rawBit);
