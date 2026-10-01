// GamepadManager.cpp — 게임패드 입력
// Windows: XInput (Xbox) 우선, 없으면 WinMM(DirectInput) 폴백
// Linux: /dev/input/js0

#include "GamepadManager.h"
#include "PadRawBits.h"
#include "PadMapping.h"
#include "EmulatorState.h"
#include "AppSettings.h"

#include <QDebug>
#include <algorithm>

#ifndef _WIN32
#  include <cerrno>
#endif

#ifdef _WIN32
#  include <windows.h>

// ── XInput 구조체/상수 (헤더 의존 없이 인라인 정의) ──────────
struct _XINPUT_GAMEPAD {
    WORD  wButtons;
    BYTE  bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
struct _XINPUT_STATE {
    DWORD dwPacketNumber;
    _XINPUT_GAMEPAD Gamepad;
};
typedef DWORD (WINAPI* PFN_XInputGetState)(DWORD, _XINPUT_STATE*);

static constexpr WORD XI_DPAD_UP    = 0x0001;
static constexpr WORD XI_DPAD_DOWN  = 0x0002;
static constexpr WORD XI_DPAD_LEFT  = 0x0004;
static constexpr WORD XI_DPAD_RIGHT = 0x0008;
static constexpr WORD XI_START      = 0x0010;
static constexpr WORD XI_BACK       = 0x0020;
static constexpr WORD XI_L3         = 0x0040;
static constexpr WORD XI_R3         = 0x0080;
static constexpr WORD XI_LB         = 0x0100;
static constexpr WORD XI_RB         = 0x0200;
static constexpr WORD XI_A          = 0x1000;
static constexpr WORD XI_B          = 0x2000;
static constexpr WORD XI_X          = 0x4000;
static constexpr WORD XI_Y          = 0x8000;
static constexpr short XI_DEAD      = 8689;
static constexpr BYTE  XI_TRIG_DEAD = 30;

// ── WinMM joyGetPosEx 구조체 (mmsystem.h 없이 인라인) ─────────
struct _JOYINFOEX {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwXpos, dwYpos, dwZpos;
    DWORD dwRpos, dwUpos, dwVpos;
    DWORD dwButtons;
    DWORD dwButtonNumber;
    DWORD dwPOV;
    DWORD dwReserved1, dwReserved2;
};
typedef MMRESULT (WINAPI* PFN_JoyGetPosEx)(UINT, _JOYINFOEX*);

static constexpr DWORD JOY_RETURNALL_FLAGS = 0x000000FF;
static constexpr DWORD JOY_POV_CENTERED    = 0xFFFF;  // JOY_POVCENTERED

#else
// ── Linux 원시 컨트롤 비트 (Windows XInput 과 동일한 "비트마스크 키" 방식) ──
//   매핑 테이블의 key 가 이 비트값이다. 캡처도 같은 값을 돌려주므로
//   D-패드/스틱까지 전부 리매핑할 수 있다.
static constexpr uint32_t RAW_L2       = 1u << 16;
static constexpr uint32_t RAW_R2       = 1u << 17;
static constexpr uint32_t RAW_DP_UP    = 1u << 20;
static constexpr uint32_t RAW_DP_DOWN  = 1u << 21;
static constexpr uint32_t RAW_DP_LEFT  = 1u << 22;
static constexpr uint32_t RAW_DP_RIGHT = 1u << 23;
static constexpr uint32_t RAW_ST_UP    = 1u << 24;
static constexpr uint32_t RAW_ST_DOWN  = 1u << 25;
static constexpr uint32_t RAW_ST_LEFT  = 1u << 26;
static constexpr uint32_t RAW_ST_RIGHT = 1u << 27;

// 조이스틱 버튼 번호(xpad 표준) → 비트
static constexpr uint32_t XI_A          = 1u << 0;   // A / Cross
static constexpr uint32_t XI_B          = 1u << 1;   // B / Circle
static constexpr uint32_t XI_X          = 1u << 2;   // X / Square
static constexpr uint32_t XI_Y          = 1u << 3;   // Y / Triangle
static constexpr uint32_t XI_LB         = 1u << 4;   // L1
static constexpr uint32_t XI_RB         = 1u << 5;   // R1
static constexpr uint32_t XI_BACK       = 1u << 6;   // Back / Select
static constexpr uint32_t XI_START      = 1u << 7;   // Start / Menu
static constexpr uint32_t XI_L3         = 1u << 9;   // L3
static constexpr uint32_t XI_R3         = 1u << 10;  // R3
static constexpr uint32_t XI_DPAD_UP    = RAW_DP_UP;
static constexpr uint32_t XI_DPAD_DOWN  = RAW_DP_DOWN;
static constexpr uint32_t XI_DPAD_LEFT  = RAW_DP_LEFT;
static constexpr uint32_t XI_DPAD_RIGHT = RAW_DP_RIGHT;

#  include <fcntl.h>
#  include <sys/ioctl.h>   // 패드 장치 이름/버튼 수 조회 (JSIOCGNAME 등)
#  include <unistd.h>
#  include <linux/joystick.h>
static constexpr int LR_B = 0, LR_Y = 1, LR_SEL = 2, LR_STA = 3;
static constexpr int LR_UP= 4, LR_DN= 5, LR_LT  = 6, LR_RT  = 7;
static constexpr int LR_A = 8, LR_X = 9, LR_L   =10, LR_R   =11;
static constexpr int LR_L2=12, LR_R2=13;
#endif

// ── 비트 정의가 어긋나지 않는지 컴파일 타임에 못 박는다 ─────
//   이름표(PadRawBits.h)와 여기 XI_*/RAW_* 가 따로 정의돼 있던 탓에
//   스팀덱에서 버튼 이름이 전부 엉뚱하게 표시됐다. 다시는 갈라지지 않게
//   두 정의가 같은 값인지 빌드가 검사한다.
static_assert(uint32_t(XI_A)     == PAD_A,     "PAD_A 불일치");
static_assert(uint32_t(XI_B)     == PAD_B,     "PAD_B 불일치");
static_assert(uint32_t(XI_X)     == PAD_X,     "PAD_X 불일치");
static_assert(uint32_t(XI_Y)     == PAD_Y,     "PAD_Y 불일치");
static_assert(uint32_t(XI_LB)    == PAD_LB,    "PAD_LB 불일치");
static_assert(uint32_t(XI_RB)    == PAD_RB,    "PAD_RB 불일치");
static_assert(uint32_t(XI_BACK)  == PAD_BACK,  "PAD_BACK 불일치");
static_assert(uint32_t(XI_START) == PAD_START, "PAD_START 불일치");
static_assert(uint32_t(XI_L3)    == PAD_L3,    "PAD_L3 불일치");
static_assert(uint32_t(XI_R3)    == PAD_R3,    "PAD_R3 불일치");
static_assert(uint32_t(XI_DPAD_UP)    == PAD_DPAD_UP,    "PAD_DPAD_UP 불일치");
static_assert(uint32_t(XI_DPAD_DOWN)  == PAD_DPAD_DOWN,  "PAD_DPAD_DOWN 불일치");
static_assert(uint32_t(XI_DPAD_LEFT)  == PAD_DPAD_LEFT,  "PAD_DPAD_LEFT 불일치");
static_assert(uint32_t(XI_DPAD_RIGHT) == PAD_DPAD_RIGHT, "PAD_DPAD_RIGHT 불일치");
#ifndef _WIN32
static_assert(RAW_L2 == PAD_L2 && RAW_R2 == PAD_R2, "트리거 비트 불일치");
static_assert(RAW_ST_UP == PAD_ST_UP && RAW_ST_RIGHT == PAD_ST_RIGHT,
              "스틱 비트 불일치");
#endif

// ── 생성자/소멸자 ─────────────────────────────────────────────
GamepadManager::GamepadManager(QObject* parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setTimerType(Qt::CoarseTimer);
    m_pollTimer->setInterval(8);  // ~120 Hz
    connect(m_pollTimer, &QTimer::timeout, this, &GamepadManager::onPoll);

    resetDefaultMapping();
    resetDefaultWinMM();
}

GamepadManager::~GamepadManager() {
    stop();
#ifdef _WIN32
    if (m_hXInput) FreeLibrary(static_cast<HMODULE>(m_hXInput));
    if (m_hWinMM)  FreeLibrary(static_cast<HMODULE>(m_hWinMM));
#else
    if (m_jsFd >= 0) ::close(m_jsFd);
#endif
}

// ── 시작/정지 ─────────────────────────────────────────────────
void GamepadManager::start() {
#ifdef _WIN32
    // inputMode 설정에 따라 우선순위 결정
    QString mode = gSettings.inputMode;
    if (mode == "winmm") {
        initWinMM();
    } else {
        initXInput();
        initWinMM();   // 폴백 준비
    }
#else
    openJoystick();
#endif
    m_pollTimer->start();
    qDebug() << "GamepadManager: 폴링 시작 (mode=" << gSettings.inputMode << ")";
}

void GamepadManager::stop() {
    m_pollTimer->stop();
}

// ── 매핑 헬퍼 ─────────────────────────────────────────────────

// ── 버튼 캡처용 raw 폴 ────────────────────────────────────────
int GamepadManager::pollRawForCapture(bool winmm) {
#ifdef _WIN32
    if (winmm) {
        if (!m_winmmAvail) return -1;
        auto joyGetPosEx = reinterpret_cast<PFN_JoyGetPosEx>(m_fnJoyGetPosEx);
        for (UINT joyId = 0; joyId < 4; ++joyId) {
            _JOYINFOEX info{};
            info.dwSize  = sizeof(info);
            info.dwFlags = JOY_RETURNALL_FLAGS;
            if (joyGetPosEx(joyId, &info) != 0) continue;
            return static_cast<int>(info.dwButtons);
        }
        return -1;
    } else {
        if (!m_fnGetState) return -1;
        auto XInputGetState = reinterpret_cast<PFN_XInputGetState>(m_fnGetState);
        for (DWORD i = 0; i < 4; ++i) {
            _XINPUT_STATE state{};
            if (XInputGetState(i, &state) != 0) continue;
            int bits = static_cast<int>(state.Gamepad.wButtons);
            if (state.Gamepad.bLeftTrigger  > XI_TRIG_DEAD) bits |= 0x10000;
            if (state.Gamepad.bRightTrigger > XI_TRIG_DEAD) bits |= 0x20000;
            return bits;
        }
        return -1;
    }
#else
    Q_UNUSED(winmm)
    // 매핑 키와 같은 도메인(원시 비트)을 돌려줘야 캡처 결과가 그대로 키가 된다.
    //   예전에는 libretro 비트를 돌려줘서 매핑이 어긋났고 D-패드는 잡히지도 않았다.
    if (m_jsFd < 0) return -1;
    readJoystick();                 // 최신 상태 반영
    return static_cast<int>(m_rawBits);
#endif
}

// ── 패드별 매핑 / 플레이어 배정 ───────────────────────────────
bool GamepadManager::padPresent(int idx) const {
#ifdef _WIN32
    return idx >= 0 && idx < 4 && idx < m_padCount;
#else
    return idx >= 0 && idx < kMaxPads && m_jsFds[idx] >= 0;
#endif
}

// 버튼 이름은 PadRawBits.cpp 한 곳에만 있다 (비트 정의와 같은 파일).
QString GamepadManager::buttonName(int rawBit) {
    return padButtonName(rawBit);
}

// 마우스 클릭을 트리거 비트로 바꿔 둔다 (게임 화면에서만 호출된다)
void GamepadManager::setMouseTriggerBits(bool left, bool right) {
    uint32_t b = 0;
    if (left)  b |= 0x20000u;   // 왼쪽 클릭 → R2
    if (right) b |= 0x10000u;   // 오른쪽 클릭 → L2
    m_mouseTrigBits = b;
}

// 패드는 어떤 경우에도 "자기 표"를 갖는다.
//   비어 있으면 현재 배치의 기본값 사본을 넣는다. (공용 표 공유 금지)
void GamepadManager::ensurePadMap(int idx) {
    if (idx < 0 || idx >= 4) return;
    if (m_padMaps[idx].isEmpty())
        m_padMaps[idx] = makeDefaultMapping(m_padLayout);
}

void GamepadManager::setPadMapping(int idx, const QHash<int,int>& m) {
    if (idx < 0 || idx >= 4) return;
    m_padMaps[idx] = m;
#ifdef _WIN32
    // Windows 는 XInput 입력을 합쳐 한 표로 읽는다 → 첫 번째 패드의 표를 그대로 쓴다
    if (idx == 0) m_xinputMapping = m;
#endif
}

QHash<int,int> GamepadManager::padMapping(int idx) const {
    if (idx < 0 || idx >= 4) return {};
    if (!m_padMaps[idx].isEmpty()) return m_padMaps[idx];
    return makeDefaultMapping(m_padLayout);   // 공용 표로 새지 않게
}

void GamepadManager::setPadPlayer(int idx, int player) {
    if (idx < 0 || idx >= 4) return;
    m_padPlayer[idx] = qBound(0, player, 4);
}

// 같은 플레이어에 여러 패드가 배정돼 있으면 합쳐서 돌려준다
uint16_t GamepadManager::playerBits(int player) const {
    uint16_t bits = 0;
    for (int i = 0; i < 4; ++i)
        if (m_padPlayer[i] == player) bits |= m_padBits[i];
    return bits;
}

// ── 기본 매핑 (XInput — Xbox/표준 게임패드) ───────────────────
// FBNeo 코어가 알려준 실제 버튼 정의 (게임 실행 중 확인):
//     id 0 = Button A,  id 8 = Button B,  id 1 = Button C,  id 9 = Button D
//     id 3 = Start, 14 = Select, 4/5/6/7 = 위/아래/왼쪽/오른쪽
//     id 11 = AB, 10 = CD, 13 = ABC, 12 = BCD (동시입력 매크로)
// NeoGeo 격투게임(KOF 등) 기준 배치:
//     A = 약손, B = 약발, C = 강손, D = 강발
// 패드 얼굴 버튼에는 격투게임 표준대로 얹는다 (Fightcade/아케이드 배치와 동일):
//     X(왼쪽)=약손A   Y(위)=강손C
//     A(아래)=약발B   B(오른쪽)=강발D
void GamepadManager::resetDefaultMapping() {
    m_xinputMapping = makeDefaultMapping(m_padLayout);
}

// 공장 기본 매핑을 만들어 돌려준다 (객체 상태와 무관)
// ── 조합 핫키 판정 (플랫폼 공통) ─────────────────────────────
//   SELECT(Back) 를 누른 채 다른 버튼을 누르면 핫키다. 조합 없이 SELECT 만 눌렀다 떼면
//   그 순간 코인 펄스를 준다 (그래서 SELECT 를 코인으로 써도 핫키와 겹치지 않는다).
void GamepadManager::updateHotkeys(uint32_t raw) {
    m_rawAll = raw;   // 메뉴 조작이 참조하는 물리 버튼 상태
    const bool sel = (raw & XI_BACK) != 0;
    const uint32_t pressed = raw & ~m_hkPrevRaw;   // 이번에 새로 눌린 것

    if (sel) {
        if (!m_selHeld) m_selCombo = false;        // 홀드 시작
        auto fire = [this](uint16_t hk) { m_hotkeyPending |= hk; m_selCombo = true; };
        if (pressed & XI_START)      fire(HK_MENU);
        if (pressed & XI_Y)          fire(HK_EXIT);
        if (pressed & XI_LB)         fire(HK_SERVICE);
        if (pressed & XI_RB)         fire(HK_FF);
        if (pressed & XI_A)          fire(HK_SAVE);
        if (pressed & XI_B)          fire(HK_LOAD);
        if (pressed & XI_X)          fire(HK_SHOT);
        if (pressed & XI_DPAD_RIGHT) fire(HK_SLOT_UP);
        if (pressed & XI_DPAD_LEFT)  fire(HK_PREVSHOT);
        if (pressed & XI_DPAD_UP)    fire(HK_FULLSCR);
        if (pressed & XI_DPAD_DOWN)  fire(HK_SWAP);
        if (pressed & (1u << 16))    fire(HK_RECORD);     // L2 (트리거 비트는 양 플랫폼 공통)
        if (pressed & (1u << 17))    fire(HK_PREVREC);    // R2
    } else if (m_selHeld) {
        // 조합 없이 그냥 뗐다 → 코인 펄스
        if (!m_selCombo) m_selPulse.restart();
        else             m_selPulse.invalidate();
        m_selCombo = false;
    }

    m_selHeld   = sel;
    m_hkPrevRaw = raw;
}

QHash<int,int> GamepadManager::makeDefaultMapping(PadLayout layout) {
    QHash<int,int> m_xinputMapping;   // 아래 기존 코드를 그대로 쓰기 위한 지역명

    // ── 6버튼 격투 배치 (스트리트파이터 계열) ────────────────
    //   아케이드 패널이 2단 3열이라 패드도 같은 모양으로 잡는다.
    //        X   Y   R1        LP  MP  HP
    //        A   B   R2        LK  MK  HK
    //   FBNeo 의 6버튼 인덱스: LP=1 MP=9 HP=10 / LK=0 MK=8 HK=11
    //   (일반 배치와 인덱스가 달라서 표를 따로 둔다 — 같은 표를 쓰면
    //    스트리트파이터에서 손발이 뒤바뀐다)
    if (layout == PadLayout::SixButton) {
        // ★ 인덱스 근거: 코어가 직접 보내는 버튼 정의(retro_input_descriptor).
        //     1=Weak Punch  9=Medium Punch  10=Strong Punch
        //     0=Weak Kick   8=Medium Kick   11=Strong Kick
        //   FBNeo 소스의 배정 규칙과도 일치한다.
        //   ※ 한때 이 표를 뒤집은 적이 있는데, 그건 저장된 프로필이 기본값을
        //     덮고 있던 문제를 인덱스 문제로 오판한 것이었다. 되돌렸다.
        //   ★ 인덱스가 아니라 "의미"로 넣는다. 실제 인덱스는 게임이 실행될 때
        //     코어 정의에서 채워진다 (게임마다 인덱스가 다르기 때문).
        m_xinputMapping[XI_X]    = padSemId(SEM_LP);   // X  → 약손
        m_xinputMapping[XI_Y]    = padSemId(SEM_MP);   // Y  → 중손
        m_xinputMapping[XI_RB]   = padSemId(SEM_HP);   // R1 → 강손
        m_xinputMapping[XI_A]    = padSemId(SEM_LK);   // A  → 약발
        m_xinputMapping[XI_B]    = padSemId(SEM_MK);   // B  → 중발
        m_xinputMapping[0x20000] = padSemId(SEM_HK);   // R2 → 강발
        // 3연타 매크로(12/13)는 어느 쪽이 펀치인지 실기 확인 전이라 기본 배정하지
        // 않는다. 컨트롤 표에 행은 있으니 원하는 버튼에 직접 지정하면 된다.
        m_xinputMapping[XI_START] = 3;
        m_xinputMapping[XI_BACK]  = 14;
        // 방향은 D-패드만 기본으로 둔다.
        //   스틱까지 같이 넣으면 같은 동작에 두 입력이 걸려 매핑 표에 어느 쪽이
        //   뜰지 들쭉날쭉해진다(스틱/D-패드가 섞여 보이던 원인).
        m_xinputMapping[XI_DPAD_UP]    = 4;
        m_xinputMapping[XI_DPAD_DOWN]  = 5;
        m_xinputMapping[XI_DPAD_LEFT]  = 6;
        m_xinputMapping[XI_DPAD_RIGHT] = 7;
        return m_xinputMapping;
    }

#ifdef _WIN32
    // Windows 는 기존 배치 유지 (사용자 요청)
    m_xinputMapping[XI_X]          = 0;   // X → Button A (약손)
    m_xinputMapping[XI_A]          = 8;   // A → Button B (약발)
    m_xinputMapping[XI_Y]          = 1;   // Y → Button C (강손)
    m_xinputMapping[XI_B]          = 9;   // B → Button D (강발)
    m_xinputMapping[XI_BACK]       = 2;   // SELECT
    m_xinputMapping[XI_START]      = 3;   // START
    m_xinputMapping[XI_LB]         = 10;  // L
    m_xinputMapping[XI_RB]         = 11;  // R
    m_xinputMapping[0x10000]       = 12;  // L2 (트리거)
    m_xinputMapping[0x20000]       = 13;  // R2
    m_xinputMapping[XI_L3]         = 14;
    m_xinputMapping[XI_R3]         = 15;
#else
    // ── 스팀덱 기본 배치 (사용자 지정) ────────────────────────
    //   코어 정의: 0=A(약손) 1=C(강손) 8=B(약발) 9=D(강발)
    //              11=AB, 13=ABC, 3=Start, 14=Select
    //     X(왼쪽) = A 약손      Y(위)     = B 약발
    //     A(아래) = C 강손      B(오른쪽) = D 강발
    //     L = ABC(13)           R = AB(11)
    //   ※ L3/R3/트리거는 게임 입력이 아니라 핫키로 쓰므로 여기서 매핑하지 않는다.
    m_xinputMapping[XI_X]          = 0;   // X → Button A (약손)
    m_xinputMapping[XI_A]          = 1;   // A → Button C (강손)
    m_xinputMapping[XI_Y]          = 8;   // Y → Button B (약발)
    m_xinputMapping[XI_B]          = 9;   // B → Button D (강발)
    m_xinputMapping[XI_LB]         = 13;  // L → ABC 동시입력
    m_xinputMapping[XI_RB]         = 11;  // R → AB 동시입력
    m_xinputMapping[XI_START]      = 3;   // Start
    m_xinputMapping[XI_BACK]       = 14;  // Select (코인)
#endif
    // 방향은 D-패드만 기본으로 (스틱은 필요하면 사용자가 직접 매핑)
    m_xinputMapping[XI_DPAD_UP]    = 4;   // UP
    m_xinputMapping[XI_DPAD_DOWN]  = 5;   // DOWN
    m_xinputMapping[XI_DPAD_LEFT]  = 6;   // LEFT
    m_xinputMapping[XI_DPAD_RIGHT] = 7;   // RIGHT
    return m_xinputMapping;
}

// ── 기본 매핑 (WinMM — 아케이드 스틱 / 일반 HID 패드) ─────────
// 버튼 번호는 0-based (dwButtons 비트 위치)
// NeoGeo 아케이드 스틱 표준 레이아웃 기준
void GamepadManager::resetDefaultWinMM() {
    m_winmmMapping.clear();
    // 버튼 1~4 = 네오지오 A/B/C/D (아케이드 스틱은 왼쪽부터 A B C D 순서)
    //   코어 정의: 0=A(약손), 8=B(약발), 1=C(강손), 9=D(강발)
    m_winmmMapping[0]  = 0;   // 버튼 1 → Button A (약손)
    m_winmmMapping[1]  = 8;   // 버튼 2 → Button B (약발)
    m_winmmMapping[2]  = 1;   // 버튼 3 → Button C (강손)
    m_winmmMapping[3]  = 9;   // 버튼 4 → Button D (강발)
    // 버튼 5~8: 숄더/추가 버튼
    m_winmmMapping[4]  = 10;  // 버튼 5 → L
    m_winmmMapping[5]  = 11;  // 버튼 6 → R
    m_winmmMapping[6]  = 12;  // 버튼 7 → L2
    m_winmmMapping[7]  = 13;  // 버튼 8 → R2
    // 버튼 9~10: 시스템 버튼
    m_winmmMapping[8]  = 2;   // 버튼 9  → SELECT
    m_winmmMapping[9]  = 3;   // 버튼 10 → START
    m_winmmMapping[10] = 14;  // 버튼 11 → L3
    m_winmmMapping[11] = 15;  // 버튼 12 → R3
}

// ── 폴링 슬롯 ─────────────────────────────────────────────────
void GamepadManager::onPoll() {
    uint16_t bits = pollPlatform();
    applyBits(bits);
}

void GamepadManager::applyBits(uint16_t bits) {
    for (int i = 0; i < 16; ++i) {
        // 게임 실행 중에만 키보드 우선 (kbHeld 가 채워져 있을 때)
        // 게임 미실행 시(메뉴 탐색)에는 gamepad 가 rawKeys 를 직접 갱신
        // → kbHeld 잔류로 인해 rawKeys 가 stuck 되는 현상 방지
        if (gState.gameLoaded && gState.kbHeld.contains(i)) continue;
        gState.rawKeys[i] = (bits >> i) & 1;
    }
}

// ════════════════════════════════════════════════════════════
//  플랫폼 폴링
// ════════════════════════════════════════════════════════════
uint16_t GamepadManager::pollPlatform() {
#ifdef _WIN32
    QString mode = gSettings.inputMode;

    if (mode == "winmm") {
        // WinMM 전용 모드
        return readWinMM();
    } else if (mode == "xinput") {
        // XInput 전용 모드
        return readXInput();
    } else {
        // Auto: XInput 우선, 없으면 WinMM
        uint16_t xi = readXInput();
        if (m_source == PadSource::XInput) return xi;
        return readWinMM();
    }
#else
    return readJoystick();
#endif
}

// ════════════════════════════════════════════════════════════
//  Windows XInput
// ════════════════════════════════════════════════════════════
#ifdef _WIN32

bool GamepadManager::initXInput() {
    static const wchar_t* dlls[] = {
        L"xinput1_4.dll", L"xinput9_1_0.dll",
        L"xinput1_3.dll", L"xinput1_2.dll", L"xinput1_1.dll",
    };
    for (auto dll : dlls) {
        m_hXInput = LoadLibraryW(dll);
        if (m_hXInput) {
            m_fnGetState = reinterpret_cast<void*>(
                GetProcAddress(static_cast<HMODULE>(m_hXInput), "XInputGetState"));
            if (m_fnGetState) {
                qDebug() << "GamepadManager: XInput 로드 —" << QString::fromWCharArray(dll);
                return true;
            }
            FreeLibrary(static_cast<HMODULE>(m_hXInput));
            m_hXInput = nullptr;
        }
    }
    qDebug() << "GamepadManager: XInput 없음";
    return false;
}

uint16_t GamepadManager::readXInput() {
    if (!m_fnGetState) {
        if (!initXInput()) {
            m_source = PadSource::None;
            return 0;
        }
    }

    auto XInputGetState = reinterpret_cast<PFN_XInputGetState>(m_fnGetState);
    // ★ 연결된 컨트롤러를 전부 확인해 입력을 합친다.
    //   하나만 쓰면 나중에 연결한(블루투스 등) 패드가 무시된다.
    WORD  allBtns = 0;
    short allLX = 0, allLY = 0;
    BYTE  allLT = 0, allRT = 0;
    bool  anyPad = false;
    for (DWORD i = 0; i < 4; ++i) {
        _XINPUT_STATE state{};
        if (XInputGetState(i, &state) != 0) continue;
        anyPad = true;
        allBtns |= state.Gamepad.wButtons;
        if (qAbs(state.Gamepad.sThumbLX) > qAbs(allLX)) allLX = state.Gamepad.sThumbLX;
        if (qAbs(state.Gamepad.sThumbLY) > qAbs(allLY)) allLY = state.Gamepad.sThumbLY;
        allLT = qMax(allLT, state.Gamepad.bLeftTrigger);
        allRT = qMax(allRT, state.Gamepad.bRightTrigger);

        if (m_source != PadSource::XInput || m_ctrlIdx != (int)i) {
            m_source    = PadSource::XInput;
            m_connected = true;
            m_ctrlIdx   = (int)i;
            emit connected(m_ctrlIdx);
            qDebug() << "GamepadManager: XInput 컨트롤러" << i << "연결";
        }

    }   // for i (모든 컨트롤러 합산)

    // 컨트롤러가 하나라도 있으면 "패드 1개" 로 본다 (Windows 는 XInput 입력을 합쳐서 쓴다).
    //   이걸 알려 줘야 컨트롤 화면이 패드 매핑을 보여 주고, 저장된 매핑이 적용된다.
    {
        const int want = anyPad ? 1 : 0;
        if (m_padCount != want) {
            m_padCount = want;
            if (want && m_padNames[0].isEmpty()) m_padNames[0] = QStringLiteral("XInput Controller");
            if (want) ensurePadMap(0);
            emit padsChanged();
        }
    }

    if (!anyPad) {
        if (m_source == PadSource::XInput) {
            m_source    = PadSource::None;
            m_connected = false;
            emit disconnected();
        }
        return 0;
    }

    WORD btns = allBtns;
    if (allLX < -XI_DEAD)  btns |= XI_DPAD_LEFT;
    if (allLX >  XI_DEAD)  btns |= XI_DPAD_RIGHT;
    if (allLY >  XI_DEAD)  btns |= XI_DPAD_UP;
    if (allLY < -XI_DEAD)  btns |= XI_DPAD_DOWN;

    int bits32 = btns;
    if (allLT > XI_TRIG_DEAD) bits32 |= 0x10000;
    if (allRT > XI_TRIG_DEAD) bits32 |= 0x20000;

    // 핫키 비트 (게임 입력과 분리)
    bits32 |= m_mouseTrigBits;   // 마우스로 대체한 트리거

    updateHotkeys(bits32);

    // 조합 핫키를 누르는 중에는 게임으로 입력을 보내지 않는다.
    //   (SELECT+R1 을 눌렀는데 R1 이 강손으로도 들어가면 안 되므로)
    if (m_selHeld) { for (int i = 0; i < 4; ++i) m_padBits[i] = 0; return 0; }
    if (coinPulseActive()) bits32 |= XI_BACK;     // SELECT 만 눌렀다 뗐다 → 코인

    uint16_t result = 0;
    for (auto it = m_xinputMapping.begin(); it != m_xinputMapping.end(); ++it)
        if ((bits32 & it.key()) && it.value() < 16)
            result |= (1 << it.value());
    m_padBits[0] = result;
    return result;
}

// ════════════════════════════════════════════════════════════
//  WinMM (DirectInput 폴백 — 아케이드 스틱 / 일반 HID 패드)
// ════════════════════════════════════════════════════════════

bool GamepadManager::initWinMM() {
    if (m_hWinMM) return m_winmmAvail;
    m_hWinMM = LoadLibraryW(L"winmm.dll");
    if (!m_hWinMM) {
        qDebug() << "GamepadManager: winmm.dll 로드 실패";
        return false;
    }
    m_fnJoyGetPosEx = reinterpret_cast<void*>(
        GetProcAddress(static_cast<HMODULE>(m_hWinMM), "joyGetPosEx"));
    m_winmmAvail = (m_fnJoyGetPosEx != nullptr);
    if (m_winmmAvail)
        qDebug() << "GamepadManager: WinMM 로드 성공";
    return m_winmmAvail;
}

uint16_t GamepadManager::readWinMM() {
    if (!m_winmmAvail) {
        if (!initWinMM()) return 0;
    }

    auto joyGetPosEx = reinterpret_cast<PFN_JoyGetPosEx>(m_fnJoyGetPosEx);

    // 조이스틱 슬롯 0~3 탐색
    for (UINT joyId = 0; joyId < 4; ++joyId) {
        _JOYINFOEX info{};
        info.dwSize  = sizeof(info);
        info.dwFlags = JOY_RETURNALL_FLAGS;

        if (joyGetPosEx(joyId, &info) != 0) continue;  // MMSYSERR_NOERROR = 0

        if (m_source != PadSource::WinMM || m_ctrlIdx != (int)joyId) {
            m_source    = PadSource::WinMM;
            m_connected = true;
            m_ctrlIdx   = (int)joyId;
            emit connected(m_ctrlIdx);
            qDebug() << "GamepadManager: WinMM 조이스틱" << joyId << "연결";
        }

        uint16_t result = 0;

        // ── 핫키: 패드와 같은 위치의 버튼을 XInput 비트로 옮겨 같은 판정을 쓴다 ──
        //   버튼 1~4 = A B X Y, 5/6 = L1/R1, 9 = SELECT, 10 = START, POV = 십자키
        {
            const DWORD b = info.dwButtons;
            uint32_t raw = 0;
            if (b & (1u << 0)) raw |= XI_A;
            if (b & (1u << 1)) raw |= XI_B;
            if (b & (1u << 2)) raw |= XI_X;
            if (b & (1u << 3)) raw |= XI_Y;
            if (b & (1u << 4)) raw |= XI_LB;
            if (b & (1u << 5)) raw |= XI_RB;
            if (b & (1u << 6)) raw |= (1u << 16);      // 버튼 7 = L2
            if (b & (1u << 7)) raw |= (1u << 17);      // 버튼 8 = R2
            if (b & (1u << 8)) raw |= XI_BACK;
            if (b & (1u << 9)) raw |= XI_START;
            const DWORD pv = info.dwPOV;
            if (pv <= 35900) {
                if (pv >= 4500  && pv <= 13500) raw |= XI_DPAD_RIGHT;
                if (pv >= 22500 && pv <= 31500) raw |= XI_DPAD_LEFT;
                if (pv >= 31500 || pv <= 4500)  raw |= XI_DPAD_UP;
                if (pv >= 13500 && pv <= 22500) raw |= XI_DPAD_DOWN;
            }
            updateHotkeys(raw);
        }
        // 조합 중에는 게임으로 보내지 않는다. SELECT 만 눌렀다 뗐으면 코인 버튼을 잠깐 눌린 것으로 본다.
        if (m_selHeld) return 0;
        DWORD buttons = info.dwButtons;
        if (coinPulseActive()) buttons |= (1u << 8);

        // ── 버튼 매핑 ──────────────────────────────────────────
        for (auto it = m_winmmMapping.begin(); it != m_winmmMapping.end(); ++it) {
            int btnIdx = it.key();  // 0-based
            if (it.value() < 16 && ((buttons >> btnIdx) & 1))
                result |= (1 << it.value());
        }

        // ── POV 해트 → D-패드 ──────────────────────────────────
        // POV: 0=북(UP), 9000=동(RIGHT), 18000=남(DOWN), 27000=서(LEFT)
        // 단위: 1/100도. 65535=중립
        DWORD pov = info.dwPOV;
        if (pov <= 35900) {  // 유효 범위 (65535=중립 제외)
            if (pov >= 31500 || pov <= 4500)   result |= (1 << 4);  // UP
            if (pov >= 4500  && pov <= 13500)  result |= (1 << 7);  // RIGHT
            if (pov >= 13500 && pov <= 22500)  result |= (1 << 5);  // DOWN
            if (pov >= 22500 && pov <= 31500)  result |= (1 << 6);  // LEFT
        }

        // ── 아날로그 스틱 → D-패드 ────────────────────────────
        // 대부분의 HID 장치는 0~65535 범위, 중앙=32767
        const DWORD CENTER = 32767;
        const DWORD DEAD   = 9000;
        if (info.dwXpos < CENTER - DEAD) result |= (1 << 6);  // LEFT
        if (info.dwXpos > CENTER + DEAD) result |= (1 << 7);  // RIGHT
        if (info.dwYpos < CENTER - DEAD) result |= (1 << 4);  // UP
        if (info.dwYpos > CENTER + DEAD) result |= (1 << 5);  // DOWN

        return result;
    }

    // 연결 없음
    if (m_source == PadSource::WinMM) {
        m_source    = PadSource::None;
        m_connected = false;
        emit disconnected();
    }
    return 0;
}

// ════════════════════════════════════════════════════════════
//  Linux joystick
// ════════════════════════════════════════════════════════════
#else

// ★ 연결된 조이스틱을 "전부" 연다.
//   예전에는 처음 열린 것 하나만 썼다. 그래서
//     · 블루투스 패드가 먼저 잡히면 스팀덱 내장 컨트롤러가 막히고
//     · 나중에 연결한 패드는 아예 인식되지 않았다.
//   지금은 여러 개를 동시에 읽어 어느 패드로도 조작할 수 있다.
//   (플레이어별 배정은 다음 단계에서 추가)
bool GamepadManager::openJoystick() {
    bool any = false;
    for (int i = 0; i < kMaxPads; ++i) {
        if (m_jsFds[i] >= 0) { any = true; continue; }      // 이미 열려 있음
        const QString dev = QString("/dev/input/js%1").arg(i);
        int fd = ::open(dev.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            m_jsFds[i] = fd;
            if (m_jsFd < 0) m_jsFd = fd;                    // 호환용 대표 fd
            // 장치 이름을 읽어 둔다 (패드별 버튼 배열이 달라 진단에 필요)
            char name[128] = {0};
            if (::ioctl(fd, JSIOCGNAME(sizeof(name)), name) >= 0)
                m_padNames[i] = QString::fromUtf8(name).trimmed();
            // 이름을 못 읽어도 비워 두지 않는다 — 이름이 비면 상위에서
            // 프로필 적용을 건너뛰어 그 패드만 설정이 안 먹는 일이 있었다.
            if (m_padNames[i].isEmpty())
                m_padNames[i] = QString("PAD %1").arg(i);
            ensurePadMap(i);
            char nbtn = 0, naxis = 0;
            ::ioctl(fd, JSIOCGBUTTONS, &nbtn);
            ::ioctl(fd, JSIOCGAXES,    &naxis);
            if (!m_connected) { m_connected = true; emit connected(i); }
            qDebug().noquote() << QString("GamepadManager: js%1 열림 — \"%2\" (버튼 %3, 축 %4)")
                                  .arg(i).arg(m_padNames[i]).arg(int(nbtn)).arg(int(naxis));
            any = true;
            emit padsChanged();          // 목록 갱신 → 프로필/배정 재적용
        }
    }
    if (!any) {
        // 폴링마다 찍히면 로그가 도배되어 정작 필요한 메시지가 묻힌다 → 한 번만
        static bool warned = false;
        if (!warned) { warned = true; qWarning() << "GamepadManager: 조이스틱 없음"; }
    }
    return any;
}

uint16_t GamepadManager::readJoystick() {
    // 새로 연결된 패드(블루투스 등)를 주기적으로 찾는다 — 핫플러그 대응
    if (++m_rescanTick >= 120) {          // 폴링 주기 기준 약 1초
        m_rescanTick = 0;
        openJoystick();
    }
    if (m_jsFd < 0 && !openJoystick()) return 0;

    // ── xpad / Steam Deck 표준 축 레이아웃 ───────────────────
    // axis 0: 왼쪽 스틱 X   axis 1: 왼쪽 스틱 Y
    // axis 2: LT 트리거     axis 5: RT 트리거
    // axis 6: D-패드 X      axis 7: D-패드 Y
    // ── 비트 구성 ─────────────────────────────────────────────
    // m_rawBits  : 버튼 + 트리거 + D-패드 + 스틱을 합친 원시 비트 (매핑의 key)
    // m_dpadBits : D-패드/스틱 방향만 추린 값 — 메뉴 네비게이션 전용
    const short DEAD = 10000;  // ~30% 데드존 (스틱 드리프트 방지 강화)

    // 연결된 모든 패드의 이벤트를 읽어 하나의 원시 비트로 합친다
    //   → 내장 컨트롤러든 블루투스 패드든 아무 것으로나 조작 가능
    for (int pad = 0; pad < kMaxPads; ++pad) {
    const int fd = m_jsFds[pad];
    if (fd < 0) continue;

    struct js_event ev;
    while (::read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type & JS_EVENT_INIT) continue;

        uint32_t& raw = m_padRaw[pad];        // 이 패드 전용 비트

        if (ev.type == JS_EVENT_BUTTON) {
            // 원시 비트만 갱신한다 (해석은 아래에서 매핑 테이블이 담당)
            if (ev.number < 16) {
                if (ev.value) raw |=  (1u << ev.number);
                else          raw &= ~(1u << ev.number);
            }

        } else if (ev.type == JS_EVENT_AXIS) {
            int   axis = ev.number;
            short val  = ev.value;

            // 축도 "원시 비트"로 바꿔 둔다 → 매핑 테이블로 리매핑 가능해진다
            auto setDir = [&](uint32_t negBit, uint32_t posBit) {
                raw &= ~(negBit | posBit);
                if (val < -DEAD) raw |= negBit;
                if (val >  DEAD) raw |= posBit;
            };
            if      (axis == 0) setDir(RAW_ST_LEFT, RAW_ST_RIGHT);  // 왼쪽 스틱 X
            else if (axis == 1) setDir(RAW_ST_UP,   RAW_ST_DOWN);   // 왼쪽 스틱 Y
            else if (axis == 6) setDir(RAW_DP_LEFT, RAW_DP_RIGHT);  // D-패드 X
            else if (axis == 7) setDir(RAW_DP_UP,   RAW_DP_DOWN);   // D-패드 Y
            else if (axis == 2) { if (val > 0) raw |= RAW_L2; else raw &= ~RAW_L2; }
            else if (axis == 5) { if (val > 0) raw |= RAW_R2; else raw &= ~RAW_R2; }
        }
    }

    // 이 패드가 빠졌으면 그 fd 만 닫는다 (다른 패드는 계속 사용)
    if (errno == ENODEV) {
        ::close(fd);
        m_jsFds[pad] = -1;
        m_padRaw[pad] = 0;
        m_padBits[pad] = 0;
        m_padNames[pad].clear();
        if (m_jsFd == fd) m_jsFd = -1;
        qDebug() << "GamepadManager: js" << pad << "연결 해제";
        emit padsChanged();
    }
    }   // for pad

    // 남은 패드가 하나도 없으면 연결 해제 상태로
    bool anyOpen = false;
    for (int i = 0; i < kMaxPads; ++i) if (m_jsFds[i] >= 0) { anyOpen = true; if (m_jsFd < 0) m_jsFd = m_jsFds[i]; }
    if (!anyOpen) {
        m_rawBits = 0; m_dpadBits = 0;
        if (m_connected) { m_connected = false; emit disconnected(); }
        return 0;
    }

    // ── 패드별로 매핑 테이블을 적용 ──────────────────────────
    //   패드마다 따로 계산해야 1P/2P/3P/4P 를 나눌 수 있다.
    //   m_rawBits 는 전체 합산본 — 캡처/핫키/UI 네비게이션에서 쓴다.
    m_rawBits  = 0;
    m_padCount = 0;
    uint16_t result = 0;
    uint32_t rawPad[kMaxPads] = {0, 0, 0, 0};
    uint32_t selDown = 0;                       // SELECT 를 누르고 있는 패드들
    for (int pad = 0; pad < kMaxPads; ++pad) {
        m_padBits[pad] = 0;
        if (m_jsFds[pad] < 0) continue;
        ++m_padCount;
        // 마우스로 대체한 트리거는 1P 패드에만 더한다
        //   (여러 패드가 있을 때 클릭 하나가 전원에게 들어가면 안 된다)
        rawPad[pad] = m_padRaw[pad]
                    | ((m_padPlayer[pad] == 1) ? m_mouseTrigBits : 0u);
        // 캡처용 합산본: 리매핑 대상 패드가 지정돼 있으면 그 패드만 본다
        if (m_capturePad < 0 || m_capturePad == pad) m_rawBits |= rawPad[pad];
        if (rawPad[pad] & XI_BACK) selDown |= (1u << pad);
    }

    // 핫키 비트 (게임 입력과 분리 — 매핑 테이블을 거치지 않는다)
    updateHotkeys(m_rawBits);
    if (m_selHeld) m_selPads |= uint8_t(selDown);        // 코인 펄스를 줄 패드 기억
    const bool pulse = coinPulseActive();
    if (!m_selHeld && !pulse) m_selPads = 0;

    for (int pad = 0; pad < kMaxPads; ++pad) {
        if (m_jsFds[pad] < 0) continue;
        uint32_t raw = rawPad[pad];
        if (m_selHeld) continue;                          // 조합 중에는 게임으로 보내지 않는다
        if (pulse && (m_selPads & (1u << pad))) raw |= XI_BACK;   // SELECT 만 눌렀다 뗐다 → 코인

        // ★ 패드별 매핑을 쓴다 (장치마다 버튼 번호가 다르므로)
        // ★ 공용 표로 폴백하지 않는다.
        //   예전에는 패드별 표가 비면 공용 표(m_xinputMapping)를 썼는데,
        //   그 공용 표가 "전역 저장" 값으로 덮여 있어 배치 기본값이 영영
        //   적용되지 않았다. 패드는 열릴 때 항상 자기 표를 갖는다(ensurePadMap).
        const QHash<int,int>& map = m_padMaps[pad];
        uint16_t bits = 0;
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            if ((raw & uint32_t(it.key())) && it.value() < 16)
                bits |= (1u << it.value());
        m_padBits[pad] = bits;
    }
    // 1P 에 배정된 패드들의 합 (UI 네비게이션·단일 플레이 경로용)
    result = m_selHeld ? uint16_t(0) : playerBits(1);

    // UI 네비게이션용 방향 비트 (D-패드 + 스틱 모두 인정)
    m_dpadBits = 0;
    if (m_rawBits & (RAW_DP_UP    | RAW_ST_UP))    m_dpadBits |= (1u << LR_UP);
    if (m_rawBits & (RAW_DP_DOWN  | RAW_ST_DOWN))  m_dpadBits |= (1u << LR_DN);
    if (m_rawBits & (RAW_DP_LEFT  | RAW_ST_LEFT))  m_dpadBits |= (1u << LR_LT);
    if (m_rawBits & (RAW_DP_RIGHT | RAW_ST_RIGHT)) m_dpadBits |= (1u << LR_RT);

    return result;
}

uint16_t GamepadManager::dpadBits() const {
#ifdef _WIN32
    return 0;  // Windows는 UI 네비가 rawKeys 기반이므로 미사용
#else
    return m_dpadBits;
#endif
}

#endif  // !_WIN32 (Linux section end)

// ── 게임 전환 시 입력 누산기 초기화 (플랫폼 공통) ─────────────────
// 이전 게임의 잔류 버튼/스틱 상태 제거 + Linux: fd 보류 이벤트 드레인
void GamepadManager::clearState() {
#ifndef _WIN32
    m_rawBits    = 0;
    m_dpadBits   = 0;
    m_hotkeyPending = 0;
    // 이전 게임 중 발생한 미처리 이벤트 드레인 (열린 패드 전부)
    for (int i = 0; i < kMaxPads; ++i) {
        if (m_jsFds[i] < 0) continue;
        struct js_event ev;
        while (::read(m_jsFds[i], &ev, sizeof(ev)) == sizeof(ev)) {}
        // errno == EAGAIN/EWOULDBLOCK → 정상 (O_NONBLOCK)
    }
    // rawKeys 와 kbHeld 는 MainWindow 에서 직접 초기화
#endif
    // Windows: rawKeys/kbHeld 초기화는 MainWindow 에서 수행 (GamepadManager는 비트 없음)
}
