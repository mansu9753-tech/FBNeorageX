#pragma once
// GamepadManager.h — 게임패드 입력
// Windows: XInput (Xbox) + WinMM/DirectInput (아케이드 스틱, 일반 HID 패드) 자동 감지
// Linux: /dev/input/js0 (Linux joystick API)
// gState.rawKeys 를 직접 업데이트

#include <QObject>
#include <QTimer>
#include <QHash>

class GamepadManager : public QObject {
    Q_OBJECT
public:
    explicit GamepadManager(QObject* parent = nullptr);
    ~GamepadManager() override;

    void start();
    void stop();

    bool isConnected()   const { return m_connected; }
    int  controllerIdx() const { return m_ctrlIdx;   }

    // XInput 버튼 매핑 (비트마스크 → libretro idx)
    void setXInputMapping(const QHash<int,int>& m) { m_xinputMapping = m; }
    QHash<int,int> getXInputMapping() const { return m_xinputMapping; }

    // WinMM 버튼 매핑 (0-based 버튼 인덱스 → libretro idx)
    void setWinMMMapping(const QHash<int,int>& m) { m_winmmMapping = m; }
    QHash<int,int> getWinMMMapping() const { return m_winmmMapping; }

    // 하위 호환: 기존 setMapping/getMapping 은 현재 활성 디바이스에 적용
    void setMapping(const QHash<int,int>& m);
    QHash<int,int> getMapping() const;

    void resetDefaultMapping();   // XInput 기본 매핑
    void resetDefaultWinMM();     // WinMM 기본 매핑 (아케이드 스틱)

    // 현재 활성 입력 소스 이름
    QString activeSource() const;

    // 버튼 캡처 다이얼로그용 — 현재 눌린 버튼의 raw 상태를 반환
    // XInput: wButtons | (LT→bit16) | (RT→bit17) 값, 없으면 -1
    // WinMM : dwButtons 비트마스크, 없으면 -1
    // Linux : m_jsBits 누적값, 없으면 -1
    int pollRawForCapture(bool winmm = false);

    // Linux D-패드 전용 비트 (UI 네비게이션용 — 아날로그 드리프트 무관)
    // Windows에서는 사용 안 함 (항상 0 반환)
    uint16_t dpadBits() const;

    // 게임 전환 시 입력 누산기 초기화 (잔류 상태 제거 + 보류 이벤트 드레인)
    void clearState();


signals:
    void connected(int index);
    void disconnected();

private slots:
    void onPoll();

private:
    QTimer*         m_pollTimer    = nullptr;
    bool            m_connected    = false;
    int             m_ctrlIdx      = 0;

    QHash<int,int>  m_xinputMapping;
    QHash<int,int>  m_winmmMapping;

    void     applyBits(uint16_t bits);
    uint16_t pollPlatform();

#ifdef _WIN32
    // ── XInput ───────────────────────────────────────────
    bool     initXInput();
    uint16_t readXInput();
    void*    m_hXInput    = nullptr;
    void*    m_fnGetState = nullptr;

    // ── WinMM (DirectInput 폴백) ──────────────────────────
    bool     initWinMM();
    uint16_t readWinMM();
    bool     m_winmmAvail    = false;
    void*    m_hWinMM        = nullptr;  // winmm.dll HMODULE
    void*    m_fnJoyGetPosEx = nullptr;  // joyGetPosEx 함수 포인터

    enum class PadSource { None, XInput, WinMM };
    PadSource m_source = PadSource::None;
#else
    bool     openJoystick();
    uint16_t readJoystick();

    int      m_jsFd       = -1;
    // ── 입력 누산기 (비트 간섭 방지를 위해 소스별 분리) ──────
    // readJoystick() 반환 = m_buttonBits | m_stickBits | m_dpadBits
    uint16_t m_buttonBits = 0;  // JS_EVENT_BUTTON → libretro 비트
    uint16_t m_stickBits  = 0;  // axis 0/1 (왼쪽 스틱) → 방향 비트
    uint16_t m_dpadBits   = 0;  // axis 6/7 (D-패드 hat) → 방향 비트 (UI 네비용)
#endif
};
