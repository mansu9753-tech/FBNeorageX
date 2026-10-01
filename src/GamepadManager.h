#pragma once
// GamepadManager.h — 게임패드 입력
// Windows: XInput (Xbox) + WinMM/DirectInput (아케이드 스틱, 일반 HID 패드) 자동 감지
// Linux: /dev/input/js0 (Linux joystick API)
// gState.rawKeys 를 직접 업데이트

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QElapsedTimer>
#include "ArcadeLayout.h"

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

    void resetDefaultMapping();   // XInput 기본 매핑
    // 공장 기본값 생성 — 게임의 버튼 배치에 따라 표가 달라진다
    static QHash<int,int> makeDefaultMapping(PadLayout layout = PadLayout::Standard);

    // ── 게임 버튼 배치 ───────────────────────────────────────
    //   FBNeo 가 6버튼 격투게임에 다른 retropad 인덱스를 쓰기 때문에,
    //   "기본값"의 의미가 게임마다 달라진다. 저장된 사용자 매핑은
    //   그대로 우선하고, 기본값만 이 배치를 따른다.
    void      setPadLayout(PadLayout l) { m_padLayout = l; }
    PadLayout padLayout() const { return m_padLayout; }
    void resetDefaultWinMM();     // WinMM 기본 매핑 (아케이드 스틱)


    // ── 게임패드 핫키 (게임 입력과 분리) ─────────────────────
    //   RetroArch 처럼 SELECT(Back/View) 를 "핫키 버튼" 으로 쓰고 다른 버튼을 함께 누른다.
    //   L3/R3 같은 스틱 버튼은 쓰지 않는다. 매핑 테이블을 거치지 않으므로 게임 버튼과 섞이지 않는다.
    //       SELECT + START  → 게임 ↔ 메뉴          SELECT + Y      → 게임 종료
    //       SELECT + L1     → 서비스(TEST)          SELECT + R1     → 패스트포워드
    //       SELECT + A      → 스테이트 저장         SELECT + B      → 스테이트 불러오기
    //       SELECT + X      → 스크린샷              SELECT + →      → 저장 슬롯 다음 (1→8 순환)
    //       SELECT + ←      → 프리뷰 이미지 저장    SELECT + ↑      → 전체화면
    //       SELECT + ↓      → 1P ↔ 2P 스왑          SELECT + L2     → 녹화
    //       SELECT + R2     → 프리뷰 영상 녹화
    //   키보드에 있는 핫키는 전부 여기에도 있다.
    //   SELECT 를 누르고 있는 동안 다른 버튼은 게임으로 전달되지 않는다.
    //   SELECT 만 눌렀다 떼면(다른 버튼 없이) 그때 코인으로 전달한다 (짧은 펄스).
    //   아케이드 스틱(WinMM)은 같은 위치의 버튼을 쓴다:
    //       버튼 9 = SELECT, 10 = START, 1~4 = A B X Y, 5/6 = L1/R1, 7/8 = L2/R2, POV = 십자키
    static constexpr uint16_t HK_MENU      = 0x001;   // 게임 ↔ 메뉴 전환
    static constexpr uint16_t HK_EXIT      = 0x002;   // 게임 종료
    static constexpr uint16_t HK_SERVICE   = 0x004;   // 서비스(TEST) 입력
    static constexpr uint16_t HK_FF        = 0x008;   // 패스트포워드
    static constexpr uint16_t HK_SAVE      = 0x010;   // 스테이트 저장
    static constexpr uint16_t HK_LOAD      = 0x020;   // 스테이트 불러오기
    static constexpr uint16_t HK_SHOT      = 0x040;   // 스크린샷
    static constexpr uint16_t HK_SLOT_UP   = 0x080;   // 저장 슬롯 다음
    static constexpr uint16_t HK_PREVSHOT  = 0x100;   // 프리뷰 이미지 저장
    static constexpr uint16_t HK_FULLSCR   = 0x200;   // 전체화면
    static constexpr uint16_t HK_SWAP      = 0x400;   // 1P ↔ 2P 스왑
    static constexpr uint16_t HK_RECORD    = 0x800;   // 녹화
    static constexpr uint16_t HK_PREVREC   = 0x1000;  // 프리뷰 영상 녹화

    // 쌓인 핫키 이벤트를 가져가고 비운다 (한 번 발생 = 한 번만 처리)
    //   레벨이 아니라 이벤트라서 폴링 주기가 달라도 놓치지 않는다.
    uint16_t takeHotkeyEvents() { uint16_t e = m_hotkeyPending; m_hotkeyPending = 0; return e; }
    // SELECT 를 누르고 있는 중인가 (게임 입력 차단 상태 표시용)
    bool     hotkeyModifierHeld() const { return m_selHeld; }

    // 매핑을 거치지 않은 "물리 버튼" 비트 (메뉴 조작용).
    //   메뉴에서 게임을 실행하는 버튼은 게임 매핑과 무관해야 하므로
    //   libretro 인덱스가 아니라 이 값을 본다.
    uint32_t rawBits() const { return m_rawAll; }

    // ── 패드별 입력 (로컬 1P~4P) ─────────────────────────────
    //   패드 하나가 플레이어 한 명. 연결 순서대로 1P, 2P, 3P, 4P 가 된다.
    //   예전에는 모든 패드 입력을 합쳐서 전부 1P 로 잡혔다.
    uint16_t padBits(int idx) const
    { return (idx >= 0 && idx < 4) ? m_padBits[idx] : 0; }
    int      padCount() const { return m_padCount; }
    QString  padName(int idx) const
    { return (idx >= 0 && idx < 4) ? m_padNames[idx] : QString(); }
    bool     padPresent(int idx) const;

    // ── 장치별 매핑 프로필 ───────────────────────────────────
    //   패드마다 버튼 번호가 달라(8BitDo 16버튼 / Xbox 11버튼) 공용 매핑으로는
    //   맞출 수 없다. 패드별로 매핑을 따로 들고 해석한다.
    void            setPadMapping(int idx, const QHash<int,int>& m);
    QHash<int,int>  padMapping(int idx) const;

    // ── 플레이어 배정 ────────────────────────────────────────
    //   0 = 사용 안 함, 1~4 = 해당 플레이어. 기본은 감지 순서대로 1P,2P,…
    void     setPadPlayer(int idx, int player);
    int      padPlayer(int idx) const
    { return (idx >= 0 && idx < 4) ? m_padPlayer[idx] : 0; }
    // 해당 플레이어에 배정된 패드들의 입력 합계
    uint16_t playerBits(int player) const;

    // ── 마우스 버튼 → 트리거 대체 입력 ───────────────────────
    //   스팀덱은 스팀 입력에서 L2/R2 에 마우스 우/좌클릭을 걸어 두면
    //   트리거가 조이스틱으로는 보고되지 않아 게임 버튼으로 쓸 수 없다.
    //   게임 화면에서는 마우스 클릭을 그대로 트리거 입력으로 넣어 준다.
    //   (GUI 에서는 호출하지 않으므로 마우스는 평소대로 동작한다)
    //     왼쪽 클릭 → R2,  오른쪽 클릭 → L2
    void setMouseTriggerBits(bool left, bool right);

    // ── 원시 버튼 비트 → 사람이 읽는 이름 ────────────────────
    //   ★ 플랫폼마다 비트 의미가 완전히 다르다. 예전에는 Windows XInput
    //     비트표를 리눅스에도 그대로 써서, 스팀덱에서 SELECT 가 "L3",
    //     START 가 "R3", A/B/X/Y 가 "D-Pad ..." 로 표시됐다.
    //     이름표는 반드시 이 함수 하나만 쓴다.
    static QString buttonName(int rawBit);

    // 캡처 대상 패드 지정 (리매핑할 패드를 고를 때 사용)
    void     setCapturePad(int idx) { m_capturePad = idx; }

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
    void padsChanged();          // 패드 목록이 바뀜 (연결/해제)

private slots:
    void onPoll();

private:
    QTimer*         m_pollTimer    = nullptr;
    bool            m_connected    = false;
    int             m_ctrlIdx      = 0;
    uint16_t        m_hotkeyPending = 0;  // 아직 처리되지 않은 HK_* 이벤트
    bool            m_selHeld       = false;  // SELECT(핫키 버튼) 누르는 중
    bool            m_selCombo      = false;  // 이번 홀드에서 조합이 발동했는가
    QElapsedTimer   m_selPulse;               // SELECT 를 조합 없이 뗐을 때 코인 펄스를 주는 시간
    uint8_t         m_selPads       = 0;      // 이번 홀드에서 SELECT 를 누른 패드들 (Linux)
    uint32_t        m_hkPrevRaw     = 0;      // 조합 버튼 에지 검출용
    uint32_t        m_rawAll        = 0;      // 매핑 전 물리 버튼 비트
    uint32_t        m_mouseTrigBits = 0;      // 마우스로 대체한 L2/R2 비트
    uint16_t        m_padBits[4]   = {0, 0, 0, 0};   // 패드별 매핑 결과
    QString         m_padNames[4];                   // 패드 장치 이름
    int             m_padCount     = 0;
    QHash<int,int>  m_padMaps[4];                    // 패드별 버튼 매핑
    int             m_padPlayer[4] = {1, 2, 3, 4};   // 패드 → 플레이어 (0=사용안함)
    int             m_capturePad   = -1;             // 리매핑 대상 패드 (-1=아무거나)
    PadLayout       m_padLayout    = PadLayout::Standard;

    QHash<int,int>  m_xinputMapping;
    QHash<int,int>  m_winmmMapping;

    // SELECT 를 조합 없이 뗀 직후 짧게 코인을 전달하는 중인가
    bool     coinPulseActive() const { return m_selPulse.isValid() && m_selPulse.elapsed() < 70; }
    void     ensurePadMap(int idx);   // 패드별 표가 비지 않도록 보장
    // 원시 입력에서 조합 핫키를 판정한다 (플랫폼 공통)
    void     updateHotkeys(uint32_t raw);
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
    uint32_t m_padRaw[kMaxPads] = { 0, 0, 0, 0 };   // 패드별 원시 비트
    uint16_t m_dpadBits   = 0;  // UI 네비게이션용 방향 비트
#endif
};
