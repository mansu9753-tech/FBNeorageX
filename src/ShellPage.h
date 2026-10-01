#pragma once
// ShellPage.h — OPTIONS 메뉴의 카테고리 하나를 나타내는 페이지 인터페이스
//
//  ── 왜 이렇게 나눴나 ─────────────────────────────────────
//   예전에는 MainWindow 에 `if (id == "video") switch (index) { case 3: … }` 식의
//   분기가 쌓였다. 항목을 하나 끼워 넣으면 뒤의 번호가 전부 밀리고, 번호로 맞춘
//   표시 코드와 동작 코드가 조용히 어긋난다.
//
//   그래서 카테고리 하나 = 클래스 하나로 나누고, 동작을 "행" 자체에 붙였다.
//       Row{ 라벨, 값, adjust(±1), activate() }
//   행을 만드는 자리에서 동작도 같이 만들므로 번호 매칭이 필요 없다.
//   항목을 끼워 넣어도, 순서를 바꿔도 다른 곳을 고칠 일이 없다.
//
//  ── 규칙 ────────────────────────────────────────────────
//   · 페이지는 MainWindow 를 직접 만지지 않는다. 앱에 부탁할 일은 ShellHost 로만.
//   · rows() 는 "지금 상태" 를 읽어 새로 만든다. 값이 바뀌면 다시 불린다.
//     그래서 페이지가 값을 따로 들고 있을 필요가 없다(진실은 gSettings/gState).
//   · rows() 는 가볍게 유지한다. 값 하나 바꿀 때마다 호출된다.

#include <QString>
#include <QVector>
#include <functional>

class GameCanvas;
class AudioManager;
class CheatManager;
class QWidget;

// 페이지가 앱에 요청할 수 있는 것의 좁은 창구.
//   MainWindow 를 통째로 넘기면 페이지가 뭐든 건드릴 수 있게 되어 다시 꼬인다.
//   필요한 것만 여기에 이름을 붙여 열어 둔다.
// CONTROLS 카테고리가 앱에 부탁하는 것들.
//   매핑을 저장·해석하는 로직(PadMapping/ArcadeLayout 등)은 MainWindow 에 그대로 있고,
//   페이지는 "보여 주기 + 무엇을 눌렀는지 전달" 만 한다. 장치 번호: 0=키보드 1=게임패드 2=아케이드스틱.
struct ControlSlot { QString action; QString bound; };          // 표의 한 줄
struct ControlPad  { int index = 0; QString name; int player = 0; };
struct ControlsApi {
    std::function<QVector<ControlSlot>(int dev)> bindings;        // 액션 목록 + 현재 배정
    std::function<void(int dev, int slot)>       remap;          // 입력 캡처창을 열어 다시 배정
    std::function<void(int dev)>                 resetDevice;    // 그 장치 매핑을 기본값으로
    std::function<QVector<ControlPad>()>         pads;           // 연결된 패드
    std::function<int()>                         targetPad;
    std::function<void(int)>                     setTargetPad;
    std::function<void(int pad, int player)>     setPadPlayer;   // 0=사용 안 함
    std::function<QString()>                     sourceText;     // "지금 적용 중: ..."
    std::function<void()>                        savePlatform;   // 지금 컨트롤·터보를 이 기종 전체에 저장
    std::function<QString()>                     platformLabel;  // "NEOGEO" / "CPS" / "OTHER"
    std::function<void()>                        forgetGame;     // 이 게임에 저장된 컨트롤 지우기
    std::function<void()>                        clearPadProfiles;
    std::function<QString()>                     inputMode;      // auto / xinput / winmm
    std::function<void(const QString&)>          setInputMode;
    std::function<QVector<QPair<int,QString>>()> turboButtons; // 터보를 걸 수 있는 버튼 (인덱스, 이름)
    std::function<bool(int idx)>                 turbo;
    std::function<void(int idx, bool on)>        setTurbo;
    std::function<int()>                         turboPeriod;
    std::function<void(int)>                     setTurboPeriod;
};

// MULTIPLAYER 카테고리가 앱에 부탁하는 것들. 연결 절차(STUN/릴레이/UPnP)는 MainWindow 에 그대로 있다.
struct NetState {
    QString status = QStringLiteral("● OFFLINE");
    QString rtt;
    QString publicIp;
    QString roomCode;                    // 내가 호스트일 때 만든 코드
    QString joinCode;                    // 참가할 때 입력한 코드
    QString joinIp = QStringLiteral("127.0.0.1");
    int     port = 7845;
    int     delay = 2;
    bool    relayBuiltin = true;
    bool    canHost = true, canJoin = true, canStart = false, canDisc = false;
};
struct NetApi {
    std::function<NetState()>              state;
    std::function<void()>                  host, join, start, disconnect, copyCode;
    std::function<void(int)>               setPort, setDelay;
    std::function<void(const QString&)>    setJoinCode, setJoinIp, setRelay;
};

// VIDEO 카테고리 중 셰이더·베젤 이미지처럼 앱 상태(캔버스, 선택한 게임)가 필요한 부분.
//   scope: "game" / "plat" / "all"  (베젤을 적용할 범위)
struct VideoApi {
    std::function<bool(const QString& path)> setShader;      // false = 컴파일/링크 실패
    std::function<void()>                    clearShader;
    std::function<QString(const QString& scope)> bezelKey;   // 저장 키 ("" = 게임 미선택)
    std::function<QString(const QString& scope)> bezelScopeLabel;
    std::function<QString()>                 bezelInfo;      // "현재 적용: ..." 안내
    std::function<void(const QString& key, const QString& value)> assignBezel;
    std::function<void(const QString& key)>  clearBezel;
};

struct ShellHost {
    // ── 객체 ─────────────────────────────────────────────
    GameCanvas*   canvas = nullptr;
    AudioManager* audio  = nullptr;
    CheatManager* cheat  = nullptr;
    QWidget*      window = nullptr;     // 파일 선택창의 부모

    // ── 알림 ─────────────────────────────────────────────
    std::function<void(const QString&)> log;
    std::function<bool()>               english;          // GUI 언어가 영어인가

    // ── 게임 정보 ────────────────────────────────────────
    std::function<QString()>               loadedGame;    // 실행 중인 롬 (없으면 "")
    std::function<QString()>               selectedGame;  // 목록에서 고른 롬
    std::function<QString(const QString&)> platformOf;    // 롬 → 기종

    // ── 동작 ─────────────────────────────────────────────
    std::function<void()> applyBezel;
    std::function<void()> applySoundMode;
    std::function<void()> applyPaths;          // ROM/프리뷰 폴더가 바뀐 뒤: 코어 폴더 갱신 + 재스캔
    std::function<void()> applyLiveSettings;   // 저장된 화면·음량 설정을 지금 화면에 반영
    std::function<void()> toggleFullscreen;
    std::function<bool()> isFullscreen;
    std::function<void()> openShaderParams;
    std::function<void()> toggleLanguage;

    // 게임 조작 (옛 하단 버튼 줄에 있던 것들)
    std::function<void()>    stopGame;
    std::function<void()>    resetGame;
    std::function<bool()>    swapPlayers;        // true = 2P 포트
    std::function<void()>    toggleSwap;
    std::function<QString()> tateLabel;
    std::function<void()>    toggleTate;

    // 세이브스테이트 / 녹화 / 스크린샷
    std::function<int()>     stateSlot;
    std::function<void(int)> setStateSlot;
    std::function<void()>    saveState;          // 현재 슬롯에 저장
    std::function<void()>    loadState;
    std::function<void()>    takeScreenshot;
    std::function<void()>    openFrameLab;       // 프레임 단위 확인 / 캡처 화면
    std::function<void()>    savePreviewShot;    // Ctrl+F12
    std::function<void()>    togglePreviewRecord;// Ctrl+F9
    std::function<void()>    toggleRecording;
    std::function<bool()>    isRecording;

    ControlsApi controls;
    NetApi      net;
    VideoApi    video;

};

class ShellPage {
public:
    // 화면에 그릴 한 줄 + 그 줄의 동작
    struct Row {
        QString label;
        QString value;                          // 비면 값 자리를 그리지 않는다
        std::function<void(int delta)> adjust;  // 있으면 ◀ ▶ 단추가 붙는다 (delta = -1/+1)
        std::function<void()>          activate;// 있으면 눌렀을 때 실행한다
        bool info = false;                      // 안내 줄: 흐리게, 선택·동작 없음
        bool elideLeft = false;                 // 길면 앞을 자른다 (경로 표시용)
        bool isHeading = false;                 // 구역 제목 줄 (밝은 색, 선택·동작 없음)

        // 값 조절형 행
        static Row value_(const QString& l, const QString& v,
                          std::function<void(int)> adj) {
            Row r; r.label = l; r.value = v; r.adjust = std::move(adj); return r;
        }
        // 선택지가 딱 둘인 값(켬/끔 등). 좌우 화살표는 의미가 없으므로 붙이지 않고,
        //   누르면(클릭·A 버튼·Enter) 다른 쪽으로 넘어간다. fn 의 인자는 항상 +1.
        static Row flip(const QString& l, const QString& v, std::function<void(int)> fn) {
            Row r; r.label = l; r.value = v;
            r.activate = [fn = std::move(fn)] { fn(+1); };
            return r;
        }
        // 선택지가 둘이면 flip, 셋 이상이면 화살표(value_)
        static Row pick(bool twoChoices, const QString& l, const QString& v, std::function<void(int)> fn) {
            return twoChoices ? flip(l, v, std::move(fn)) : value_(l, v, std::move(fn));
        }
        // 누르면 뭔가 실행하는 행 (v 는 현재 상태를 보여주고 싶을 때만)
        static Row action(const QString& l, std::function<void()> act,
                          const QString& v = QString()) {
            Row r; r.label = l; r.value = v; r.activate = std::move(act); return r;
        }
        // 안내 줄
        static Row note(const QString& l, const QString& v = QString()) {
            Row r; r.label = l; r.value = v; r.info = true; return r;
        }
        // 구역 제목: 긴 목록을 몇 덩어리로 나눌 때 쓴다 (앞에 빈 줄을 두려면 note("") 를 하나 넣는다)
        static Row heading(const QString& l) {
            Row r; r.label = l; r.info = true; r.isHeading = true; return r;
        }
        // 경로 안내 줄: 길면 끝부분이 보이게 앞을 자른다
        static Row path(const QString& p) {
            Row r; r.label = p; r.info = true; r.elideLeft = true; return r;
        }
    };

    explicit ShellPage(const ShellHost& host) : m_h(host) {}
    virtual ~ShellPage() = default;

    virtual QString id() const = 0;

    // 지금 상태로 행 목록을 만든다.
    virtual QVector<Row> rows() = 0;

    // 행이 하나도 없는 페이지(아직 옛 창으로 여는 카테고리)를 골랐을 때.
    virtual void openStandalone() {}

protected:
    void say(const QString& s) const { if (m_h.log) m_h.log(s); }
    bool en() const { return m_h.english && m_h.english(); }

    // 순환 이동: (cur + delta) 를 [0, n) 로 감싼다
    static int wrap(int cur, int delta, int n) {
        return n > 0 ? ((cur + delta) % n + n) % n : 0;
    }
    // 범위 이동: 끝에서 멈춘다
    static int clampStep(int cur, int delta, int step, int lo, int hi) {
        int v = cur + delta * step;
        return v < lo ? lo : (v > hi ? hi : v);
    }

    ShellHost m_h;
};
