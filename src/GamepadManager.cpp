// GamepadManager.cpp — 게임패드 입력
// Windows: XInput (Xbox) 우선, 없으면 WinMM(DirectInput) 폴백
// Linux: /dev/input/js0

#include "GamepadManager.h"
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
// ── Linux joystick 버튼 번호 (표준 Xbox 호환 패드 / Steam Deck) ─
// /dev/input/js0 button number → libretro 인덱스 매핑에 사용
// resetDefaultMapping() 이 이 상수들을 Windows/Linux 공통으로 사용
static constexpr uint16_t XI_A          =  0;  // A  / Cross
static constexpr uint16_t XI_B          =  1;  // B  / Circle
static constexpr uint16_t XI_X          =  2;  // X  / Square
static constexpr uint16_t XI_Y          =  3;  // Y  / Triangle
static constexpr uint16_t XI_LB         =  4;  // L1
static constexpr uint16_t XI_RB         =  5;  // R1
static constexpr uint16_t XI_BACK       =  6;  // Back / Select
static constexpr uint16_t XI_START      =  7;  // Start / Menu
static constexpr uint16_t XI_L3         =  9;  // L3 (left stick click)
static constexpr uint16_t XI_R3         = 10;  // R3 (right stick click)
static constexpr uint16_t XI_DPAD_UP    = 13;  // D-pad Up
static constexpr uint16_t XI_DPAD_DOWN  = 14;  // D-pad Down
static constexpr uint16_t XI_DPAD_LEFT  = 11;  // D-pad Left
static constexpr uint16_t XI_DPAD_RIGHT = 12;  // D-pad Right

#  include <fcntl.h>
#  include <unistd.h>
#  include <linux/joystick.h>
static constexpr int LR_B = 0, LR_Y = 1, LR_SEL = 2, LR_STA = 3;
static constexpr int LR_UP= 4, LR_DN= 5, LR_LT  = 6, LR_RT  = 7;
static constexpr int LR_A = 8, LR_X = 9, LR_L   =10, LR_R   =11;
static constexpr int LR_L2=12, LR_R2=13;
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
void GamepadManager::setMapping(const QHash<int,int>& m) {
#ifdef _WIN32
    if (m_source == PadSource::WinMM) m_winmmMapping = m;
    else                               m_xinputMapping = m;
#else
    m_xinputMapping = m;
#endif
}

QHash<int,int> GamepadManager::getMapping() const {
#ifdef _WIN32
    return (m_source == PadSource::WinMM) ? m_winmmMapping : m_xinputMapping;
#else
    return m_xinputMapping;
#endif
}

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
    return (m_jsFd >= 0) ? static_cast<int>(m_jsBits) : -1;
#endif
}

QString GamepadManager::activeSource() const {
#ifdef _WIN32
    switch (m_source) {
    case PadSource::XInput: return "XInput";
    case PadSource::WinMM:  return "WinMM";
    default:                return "None";
    }
#else
    return m_jsFd >= 0 ? "Joystick" : "None";
#endif
}

// ── 기본 매핑 (XInput — Xbox/표준 게임패드) ───────────────────
void GamepadManager::resetDefaultMapping() {
    m_xinputMapping.clear();
    m_xinputMapping[XI_A]          = 0;   // B  (NeoGeo A)
    m_xinputMapping[XI_B]          = 8;   // A  (NeoGeo B)
    m_xinputMapping[XI_X]          = 1;   // Y  (NeoGeo C)
    m_xinputMapping[XI_Y]          = 9;   // X  (NeoGeo D)
    m_xinputMapping[XI_BACK]       = 2;   // SELECT
    m_xinputMapping[XI_START]      = 3;   // START
    m_xinputMapping[XI_DPAD_UP]    = 4;   // UP
    m_xinputMapping[XI_DPAD_DOWN]  = 5;   // DOWN
    m_xinputMapping[XI_DPAD_LEFT]  = 6;   // LEFT
    m_xinputMapping[XI_DPAD_RIGHT] = 7;   // RIGHT
    m_xinputMapping[XI_LB]         = 10;  // L
    m_xinputMapping[XI_RB]         = 11;  // R
    m_xinputMapping[0x10000]       = 12;  // L2 (트리거)
    m_xinputMapping[0x20000]       = 13;  // R2
    m_xinputMapping[XI_L3]         = 14;
    m_xinputMapping[XI_R3]         = 15;
}

// ── 기본 매핑 (WinMM — 아케이드 스틱 / 일반 HID 패드) ─────────
// 버튼 번호는 0-based (dwButtons 비트 위치)
// NeoGeo 아케이드 스틱 표준 레이아웃 기준
void GamepadManager::resetDefaultWinMM() {
    m_winmmMapping.clear();
    // 버튼 1~4: 주 공격 버튼 (NeoGeo A/B/C/D)
    m_winmmMapping[0]  = 0;   // 버튼 1 → JOYPAD_B  (NeoGeo A)
    m_winmmMapping[1]  = 8;   // 버튼 2 → JOYPAD_A  (NeoGeo B)
    m_winmmMapping[2]  = 1;   // 버튼 3 → JOYPAD_Y  (NeoGeo C)
    m_winmmMapping[3]  = 9;   // 버튼 4 → JOYPAD_X  (NeoGeo D)
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
        if (gState.kbHeld.contains(i)) continue;  // 키보드 우선
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
    for (DWORD i = 0; i < 4; ++i) {
        _XINPUT_STATE state{};
        if (XInputGetState(i, &state) != 0) continue;

        if (m_source != PadSource::XInput || m_ctrlIdx != (int)i) {
            m_source    = PadSource::XInput;
            m_connected = true;
            m_ctrlIdx   = (int)i;
            emit connected(m_ctrlIdx);
            qDebug() << "GamepadManager: XInput 컨트롤러" << i << "연결";
        }

        WORD btns = state.Gamepad.wButtons;
        short lx = state.Gamepad.sThumbLX;
        short ly = state.Gamepad.sThumbLY;
        if (lx < -XI_DEAD)  btns |= XI_DPAD_LEFT;
        if (lx >  XI_DEAD)  btns |= XI_DPAD_RIGHT;
        if (ly >  XI_DEAD)  btns |= XI_DPAD_UP;
        if (ly < -XI_DEAD)  btns |= XI_DPAD_DOWN;

        int bits32 = btns;
        if (state.Gamepad.bLeftTrigger  > XI_TRIG_DEAD) bits32 |= 0x10000;
        if (state.Gamepad.bRightTrigger > XI_TRIG_DEAD) bits32 |= 0x20000;

        uint16_t result = 0;
        for (auto it = m_xinputMapping.begin(); it != m_xinputMapping.end(); ++it)
            if ((bits32 & it.key()) && it.value() < 16)
                result |= (1 << it.value());
        return result;
    }

    // 연결 없음
    if (m_source == PadSource::XInput) {
        m_source    = PadSource::None;
        m_connected = false;
        emit disconnected();
    }
    return 0;
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

        // ── 버튼 매핑 ──────────────────────────────────────────
        for (auto it = m_winmmMapping.begin(); it != m_winmmMapping.end(); ++it) {
            int btnIdx = it.key();  // 0-based
            if (it.value() < 16 && ((info.dwButtons >> btnIdx) & 1))
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

bool GamepadManager::openJoystick() {
    for (int i = 0; i < 4; ++i) {
        QString dev = QString("/dev/input/js%1").arg(i);
        int fd = ::open(dev.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            m_jsFd = fd; m_connected = true;
            emit connected(i);
            qDebug() << "GamepadManager:" << dev << "열림";
            return true;
        }
    }
    qWarning() << "GamepadManager: 조이스틱 없음";
    return false;
}

uint16_t GamepadManager::readJoystick() {
    if (m_jsFd < 0) {
        if (!openJoystick()) return 0;
    }
    struct js_event ev;
    while (::read(m_jsFd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type & JS_EVENT_INIT) continue;
        if (ev.type == JS_EVENT_BUTTON) {
            auto it = m_xinputMapping.find(ev.number);
            if (it != m_xinputMapping.end() && it.value() < 16) {
                if (ev.value) m_jsBits |=  (1 << it.value());
                else          m_jsBits &= ~(1 << it.value());
            }
        } else if (ev.type == JS_EVENT_AXIS) {
            int   axis = ev.number;
            short val  = ev.value;
            // ── xpad / Steam Deck 표준 축 레이아웃 ──────────────────
            // axis 0: 왼쪽 스틱 X  axis 1: 왼쪽 스틱 Y
            // axis 2: LT 트리거   axis 3: 오른쪽 스틱 X
            // axis 4: 오른쪽 스틱 Y  axis 5: RT 트리거
            // axis 6: D-패드 X    axis 7: D-패드 Y
            const short DEAD = 8192;  // ~25% 데드존

            if (axis == 0) {
                // 왼쪽 스틱 X → LEFT / RIGHT (게임 입력용)
                m_jsBits &= ~((1 << LR_LT) | (1 << LR_RT));
                if (val < -DEAD) m_jsBits |= (1 << LR_LT);
                if (val >  DEAD) m_jsBits |= (1 << LR_RT);
            } else if (axis == 1) {
                // 왼쪽 스틱 Y → UP / DOWN (게임 입력용, 아날로그 드리프트 가능)
                m_jsBits &= ~((1 << LR_UP) | (1 << LR_DN));
                if (val < -DEAD) m_jsBits |= (1 << LR_UP);
                if (val >  DEAD) m_jsBits |= (1 << LR_DN);
            } else if (axis == 6) {
                // D-패드 X → LEFT / RIGHT (게임 + UI 네비용)
                m_jsBits   &= ~((1 << LR_LT) | (1 << LR_RT));
                m_dpadBits &= ~((1 << LR_LT) | (1 << LR_RT));
                if (val < -DEAD) { m_jsBits |= (1 << LR_LT); m_dpadBits |= (1 << LR_LT); }
                if (val >  DEAD) { m_jsBits |= (1 << LR_RT); m_dpadBits |= (1 << LR_RT); }
            } else if (axis == 7) {
                // D-패드 Y → UP / DOWN (게임 + UI 네비용 — 디지털값만, 드리프트 없음)
                m_jsBits   &= ~((1 << LR_UP) | (1 << LR_DN));
                m_dpadBits &= ~((1 << LR_UP) | (1 << LR_DN));
                if (val < -DEAD) { m_jsBits |= (1 << LR_UP); m_dpadBits |= (1 << LR_UP); }
                if (val >  DEAD) { m_jsBits |= (1 << LR_DN); m_dpadBits |= (1 << LR_DN); }
            } else if (axis == 2) {
                // LT 트리거 → L2  (쉬는 상태=-32767, 완전 누름=+32767)
                if (val > 0) m_jsBits |=  (1 << LR_L2);
                else         m_jsBits &= ~(1 << LR_L2);
            } else if (axis == 5) {
                // RT 트리거 → R2
                if (val > 0) m_jsBits |=  (1 << LR_R2);
                else         m_jsBits &= ~(1 << LR_R2);
            }
        }
    }
    if (errno == ENODEV) {
        ::close(m_jsFd); m_jsFd = -1; m_jsBits = 0; m_dpadBits = 0;
        if (m_connected) { m_connected = false; emit disconnected(); }
        return 0;
    }
    return m_jsBits;
}

uint16_t GamepadManager::dpadBits() const {
#ifdef _WIN32
    return 0;  // Windows는 UI 네비가 rawKeys 기반이므로 미사용
#else
    return m_dpadBits;
#endif
}

#endif
