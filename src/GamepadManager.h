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

    // ── 게임패드 핫키 (게임 입력과 분리) ─────────────────────
    //   L3 / R3 / L트리거 / R트리거 는 게임에 전달하지 않고
    //   메뉴 전환·게임종료·서비스·패스트포워드로 쓴다.
    //   매핑 테이블을 거치지 않으므로 게임 버튼과 섞이지 않는다.
    static constexpr uint8_t HK_L3 = 0x1;   // 게임 ↔ 메뉴 전환
    static constexpr uint8_t HK_R3 = 0x2;   // 게임 종료
    static constexpr uint8_t HK_LT = 0x4;   // 서비스(TEST) 입력
    static constexpr uint8_t HK_RT = 0x8;   // 패스트포워드
    uint8_t hotkeyBits() const { return m_hotkeyBits; }

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
    uint8_t         m_hotkeyBits   = 0;   // 위 HK_* 조합 (플랫폼 공통)

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
    // ── Linux 원시 입력 비트 ────────────────────────────────
    //  ★ Windows(XInput) 와 같은 방식으로 통일했다.
    //    예전에는 버튼만 매핑을 거치고 D-패드/스틱은 축(axis)이라 매핑을 건너뛰어
    //      · 리매핑 화면에서 십자키가 아예 잡히지 않고
    //      · 캡처가 돌려주는 값의 의미가 매핑 키와 달라 매핑이 어긋났다.
    //    이제 "원시 컨트롤 비트" 하나로 모아 두고 매핑 테이블로 해석한다.
    //      비트 0~15 : 조이스틱 버튼 번호
    //      16/17     : L2 / R2 트리거
    //      20~23     : D-패드 상/하/좌/우
    //      24~27     : 왼쪽 스틱 상/하/좌/우
    uint32_t m_rawBits    = 0;
    // 연결된 조이스틱을 전부 연다 (내장 + 블루투스 등). 하나만 열면 나중에
    // 연결한 패드가 무시되고, 먼저 잡힌 패드가 다른 패드를 막아버린다.
    static constexpr int kMaxPads = 4;
    int      m_jsFds[kMaxPads] = { -1, -1, -1, -1 };
    int      m_rescanTick = 0;      // 주기적 재탐색(핫플러그) 카운터
    uint16_t m_buttonBits = 0;  // (호환 유지용, 미사용)
    uint16_t m_stickBits  = 0;
    uint16_t m_dpadBits   = 0;  // UI 네비게이션용 방향 비트
#endif
};
