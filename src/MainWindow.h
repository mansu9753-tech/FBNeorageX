#pragma once
// MainWindow.h — 메인 윈도우
#include <map>
#include <algorithm>

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>
#include <QCursor>
#include <QTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QFrame>
#include <QHeaderView>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMediaPlayer>
#include <QVideoWidget>

#include "BorderPanel.h"
#include "GameCanvas.h"
#include "VideoRecorder.h"
#include "LibretroCore.h"
#include "AudioManager.h"
#include "NetplayManager.h"
#include "UPnpMapper.h"
#include "CheatManager.h"
#include "GamepadManager.h"

class QNetworkAccessManager;   // 공유 릴레이 QNAM (전방 선언)

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void log(const QString& msg);

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event)   override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onEmuTimer();

    void onNetConnected(bool isHost);
    void onNetDisconnected();
    void onNetError(const QString& msg);
    void onNetStateChanged(NetplayManager::State s);

    // 게임 흐름 (상태머신 기반)
    void onNetLoadGame(const QString& romName, int inputDelay); // 조인: 게임 자동 로드
    void onNetReady();                          // 호스트: 상대 READY 수신
    void onNetStart();                          // 양쪽: 동시 시작 신호
    void onNetGameOver();                       // 양쪽: 게임 종료 복귀

private:
    // ── UI 빌드 ─────────────────────────────────────────
    void buildUi();
    void buildMainTab();

    // OPTIONS 패널 내부 페이지 빌더
    void buildControlsPage(QWidget* page);
    void buildDirectoriesPage(QWidget* page);
    void buildVideoPage(QWidget* page);
    void buildAudioPage(QWidget* page);
    void buildMachinePage(QWidget* page);
    void buildShotsPage(QWidget* page);
    void buildCheatsPage(QWidget* page);
    void buildNetplayPage(QWidget* page);

    void rebuildMachineSettings();

    // ── 공통 스타일 헬퍼 ────────────────────────────────
    static QString btnStyle(bool accent = false);
    static QString editStyle();
    static QString labelStyle();
    static QString groupStyle();

    // ── 게임 관리 ────────────────────────────────────────
    void scanRoms();
    void filterRoms(const QString& text);
    void selectGame(const QString& romName);
    bool loadRomInternal();
    void startEmu();
    void launchGame();
    void netplayStartGame();
    void cleanupNetplay();  // 소켓 유지 + 게임 상태만 초기화 → Lobby 복귀

    void togglePause();
    void toggleFullscreen();
    void toggleFastForward(bool on);
    void toggleSwapPlayers();
    void toggleTate();          // TATE 회전: auto→90°CCW→90°CW→off→auto 순환
    void applyTate(int rot);    // -1=auto, 0=off, 1=90°CCW, 3=90°CW

    // 게임 캔버스 전환 헬퍼 (프리뷰 정지 + 스택 전환 통합)
    void enterGameScreen();   // GUI → 게임 화면 (프리뷰 정지)
    void leaveGameScreen();   // 게임 화면 → GUI (프리뷰 재개)

    // ── 세이브스테이트 / 스크린샷 ────────────────────────
    void saveState(int slot);
    void loadState(int slot);
    void takeScreenshot();
    void savePreviewShot();        // 현재 프레임을 previews/{rom}.png 로 저장
    void togglePreviewRecord();    // previews/{rom}.mp4 녹화 시작/정지

    // ── 설정 적용 ────────────────────────────────────────
    void applySettings();
    void refreshSettingsUi();

    // ── 즐겨찾기 ────────────────────────────────────────
    void toggleFavorite(const QString& romName);
    bool isFavorite(const QString& romName) const;

    // ── 치트 UI 갱신 ─────────────────────────────────────
    void refreshCheatList();

    // ── 프리뷰 ──────────────────────────────────────────
    void loadPreview(const QString& romName);
    void loadPreviewVideo(const QString& romName);

    // ── 마우스 커서 자동 숨김 ───────────────────────────
    void resetCursorTimer();   // 타이머 재시작 + 커서 표시
    void hideCursor();         // 게임 화면 중에만 숨김

    // ── 녹화 ────────────────────────────────────────────
    void startRecording();
    void stopRecording();
    void toggleRecording();

    // ── 키 매핑 ──────────────────────────────────────────
    void applyKeyPress(int qtKey);
    // 넷플레이 입력 합성: lb=로컬, rb=원격 → gState.keys / p2Keys
    void npApplyInput(uint16_t lb, uint16_t rb);
    void applyKeyRelease(int qtKey);
    static QHash<int, int> buildDefaultKeymap();
    void refreshControlsTable();   // 키보드 테이블 갱신
    void refreshPadTable();        // 게임패드(XInput) 테이블 갱신
    void refreshWinMMTable();      // 아케이드스틱(WinMM) 테이블 갱신
    QHash<int, int> m_keymap;      // Qt key → libretro button

    // ── 기종 분류 + 핫키 시스템 ─────────────────────────────────
    static QString gamePlatform(const QString& rom);   // 기종 분류
    static int  hotkeyEncode(int key, int mods);
    static void hotkeyDecode(int enc, int& key, int& mods);
    static QString hotkeyText(int enc);                // 표시용 ("Ctrl+F9" 등)
    void rebuildHotkeyTable();                         // 핫키 테이블 갱신
    QTableWidget* m_hotkeyTable = nullptr;
    QString m_machineScope = "game";   // 머신세팅 저장 범위: "game"/"plat"
    int  hotkeyOf(const QString& action);              // 현재 핫키(설정>기본)
    bool hotkeyMatch(const QString& action, int key, int qtMods);

    // ── 컨트롤 스코프 해석/적용 (게임별 > 기종별 > 전역 > 기본) ──
    QHash<int,int> resolveCtrlMap(const QHash<QString,QHash<int,int>>& scoped,
                                  const QHash<int,int>& global,
                                  const QHash<int,int>& dflt,
                                  const QString& rom);
    void resolveAndApplyControls(const QString& rom);  // 게임 로드 시 적용
    void saveControlsToScope(const QString& scope);    // "global"/"plat"/"game"
    QString m_ctrlScopeRom;        // 현재 컨트롤 테이블이 대상으로 하는 게임 (게임별 저장용)
    QString m_hotkeyCapture;       // 핫키 캡처 중인 action (빈 문자열=비캡처)

    // 핫키 정의 테이블
    struct HotkeyDef { const char* action; const char* label; int key; int mods; };
    static const HotkeyDef* hotkeyDefs(int* count);

    // ════════════════════════════════════════════════════
    //  위젯 멤버
    // ════════════════════════════════════════════════════

    // ── 화면 전환 스택 ───────────────────────────────────
    QStackedWidget*  m_stack      = nullptr;  // 0=GUI, 1=게임화면
    QWidget*         m_guiWidget  = nullptr;
    QWidget*         m_mainTab    = nullptr;  // GUI 루트 위젯
    GameCanvas*      m_canvas     = nullptr;

    // ── OPTIONS 패널 내부 스택 ───────────────────────────
    QStackedWidget*  m_optionsStack   = nullptr;
    // Machine 페이지용 스크롤
    QScrollArea*     m_machineScroll  = nullptr;
    QWidget*         m_machineContent = nullptr;

    // ── 메인 패널 ────────────────────────────────────────
    BorderPanel*     m_gamelistPanel  = nullptr;
    BorderPanel*     m_optionsPanel   = nullptr;
    BorderPanel*     m_previewPanel   = nullptr;
    BorderPanel*     m_eventsPanel    = nullptr;

    QLineEdit*       m_searchEdit     = nullptr;
    QListWidget*     m_gameList       = nullptr;

    // 프리뷰 스택 (0=이미지, 1=비디오)
    QStackedWidget*  m_previewStack   = nullptr;
    QLabel*          m_previewLabel   = nullptr;
    QVideoWidget*    m_videoWidget    = nullptr;
    QMediaPlayer*    m_mediaPlayer    = nullptr;
    QTimer*          m_previewVidTimer= nullptr;

    QTextEdit*       m_logEdit        = nullptr;

    // ── CONTROLS 페이지 ──────────────────────────────────
    QTableWidget*    m_ctrlTable      = nullptr;   // 키보드
    QTableWidget*    m_padTable       = nullptr;   // 게임패드(XInput)
    QTableWidget*    m_winmmTable     = nullptr;   // 아케이드스틱(WinMM)

    // ── DIRECTORIES / SETTINGS 위젯 ─────────────────────
    QLineEdit*       m_romPathEdit        = nullptr;
    QLineEdit*       m_previewPathEdit    = nullptr;
    QLineEdit*       m_screenshotPathEdit = nullptr;
    QLineEdit*       m_savePathEdit       = nullptr;
    QLineEdit*       m_recordPathEdit     = nullptr;

    // ── VIDEO 위젯 ───────────────────────────────────────
    QComboBox*       m_scaleCombo       = nullptr;
    QCheckBox*       m_smoothCheck      = nullptr;
    QCheckBox*       m_crtCheck         = nullptr;
    QSlider*         m_crtSlider        = nullptr;
    QCheckBox*       m_vsyncCheck       = nullptr;
    QSpinBox*        m_frameskipSpin    = nullptr;
    QCheckBox*       m_flashGuardCheck  = nullptr;   // 플래시 감소 on/off
    QSlider*         m_flashSlider      = nullptr;   // 플래시 감소 강도

    // ── AUDIO 위젯 ───────────────────────────────────────
    QSlider*         m_volumeSlider     = nullptr;
    QLabel*          m_volumeLabel      = nullptr;
    QComboBox*       m_sampleRateCombo  = nullptr;
    QSpinBox*        m_bufferMsSpin     = nullptr;
    QComboBox*       m_regionCombo      = nullptr;

    // ── CHEATS 페이지 위젯 ───────────────────────────────
    QScrollArea*     m_cheatScroll      = nullptr;   // 치트 목록 스크롤 영역
    QWidget*         m_cheatRows        = nullptr;   // 행 컨테이너 (동적 재생성)
    QLabel*          m_cheatStatusLabel = nullptr;

    // ── NETPLAY 페이지 위젯 ──────────────────────────────
    QLineEdit*       m_npIpEdit          = nullptr;
    QSpinBox*        m_npPortSpin        = nullptr;
    QLabel*          m_npStatusLabel     = nullptr;
    QPushButton*     m_npHostBtn         = nullptr;
    QPushButton*     m_npConnectBtn      = nullptr;
    QPushButton*     m_npStartBtn        = nullptr;
    QPushButton*     m_npDisconnBtn      = nullptr;
    QLabel*          m_npLocalIpLabel    = nullptr;
    QLabel*          m_npPublicIpLabel   = nullptr;
    QLabel*          m_npRoomCodeLabel   = nullptr;
    QLineEdit*       m_npRoomCodeEdit    = nullptr;
    QSpinBox*        m_npDelaySpinBox    = nullptr;
    QLabel*          m_npRttLabel        = nullptr;
    QLineEdit*       m_npRelayUrlEdit    = nullptr;         // 릴레이 URL 입력창
    QString          m_publicIp;                            // 외부 공개 IP (api.ipify.org)
    QHash<uint32_t, uint16_t> m_npDelayQueue;              // 입력 지연 큐 frame→bits

    // ════════════════════════════════════════════════════
    //  상태 멤버
    // ════════════════════════════════════════════════════
    QString          m_selectedGame;
    QString          m_loadedGame;   // 현재 코어에 실제 로드된 롬 이름 (isPaused 재개 판별용)
    bool             m_isFullscreen  = false;
    QPushButton*     m_swapBtn       = nullptr;  // 1P↔2P 스왑 버튼
    QLabel*          m_playerOverlay = nullptr;  // 게임 화면 내 플레이어 표시 오버레이
    QTimer*          m_overlayTimer  = nullptr;  // 1P 복귀 시 오버레이 자동 숨김 타이머
    QPushButton*     m_tateBtn       = nullptr;  // TATE 회전 버튼 (세로형 게임)
    int              m_glFilter     = 0;  // 0=ALL, 1=FAV(즐겨찾기만), 2=☆(미즐겨찾기만)
    QSize            m_windowedSize;
    int              m_stateSlot    = 1;

    QList<QPair<QString, QString>> m_allRoms;  // {displayName, romName}

    // ── 에뮬 타이머 ─────────────────────────────────────
    QTimer*          m_timer       = nullptr;
    QElapsedTimer    m_aflClock;
    double           m_frameAccum  = 0.0;   // 프레임 누산기 (AFL 타이밍 정밀도 개선)
    QTimer*          m_cursorTimer  = nullptr; // 마우스 커서 자동 숨김 타이머
    bool             m_cursorHidden = false;  // BlankCursor 적용 중 여부
    QCursor          m_customCursor;          // mousepoint.png 커서 (미리 생성)
    QCursor          m_blankCursor;           // 숨김용 BlankCursor (미리 생성)

    // ── UI 게임패드 네비게이션 (D-패드로 게임목록 탐색) ──────────
    QTimer*          m_uiNavTimer   = nullptr;
    int              m_navDir       = 0;    // 수직: -1=UP, 0=없음, 1=DOWN
    int              m_navRepeatMs  = 0;    // 수직 방향 유지 누적 시간(ms)
    int              m_navHDir      = 0;    // 수평: -1=LEFT, 0=없음, 1=RIGHT
    int              m_navHRepeatMs = 0;    // 수평 방향 유지 누적 시간(ms)
    bool             m_navAWasDown  = false;  // A버튼 이전 상태 (엣지 감지)

    // ── 게임패드 메뉴 진입 홀드 카운터 ─────────────────────────
    // SELECT+START 동시 홀드 120프레임(~2초) → togglePause (메인 GUI)
    // Start 단독은 게임으로 그대로 전달 (KOF 보스선택 커맨드 정상 작동)
    int  m_menuHoldCount   = 0;
    // START 연속 홀드 프레임 카운터 (서비스 메뉴 우발 진입 방지)
    // serviceMode=false 상태에서 90프레임 초과 → keys[3]=0 강제
    int  m_startHoldFrames = 0;

    // ── 코어 / 오디오 / 치트 / 게임패드 ─────────────────
    LibretroCore*    m_core    = nullptr;
    AudioManager*    m_audio   = nullptr;
    CheatManager*    m_cheat   = nullptr;
    GamepadManager*  m_gamepad = nullptr;
    UPnpMapper*      m_upnp    = nullptr;

    // ── 릴레이 홀펀칭 ────────────────────────────────────
    void relayRegister(const QString& code, const QString& role,
                       const QString& ip, int port);
    void relayPollPeer(const QString& code, const QString& myRole, int tries = 0);
    QTimer* m_relayPollTimer = nullptr;
    // 피어 발견 처리 중복 방지: relayRegister(POST) 와 relayPollPeer(GET) 가
    // 둘 다 워커에서 피어를 받아 clientStartHandshake/probe 를 이중 실행하던 문제 차단.
    bool m_relayPeerHandled = false;
    // ★ 공유 QNetworkAccessManager: 요청마다 새로 만들고 파괴하면 정적 빌드의
    //   Schannel TLS 컨텍스트가 동시 파괴되며 충돌 → 크래시. 하나만 만들어 재사용.
    QNetworkAccessManager* relayNam();
    QNetworkAccessManager* m_relayNam = nullptr;

    // ── 녹화 (libav* 기반 VideoRecorder) ────────────────────
    VideoRecorder*        m_videoRecorder  = nullptr;

    // ── Rollback Netcode ─────────────────────────────────
    QHash<int, QByteArray> m_npStates;   // frame → run() 직전 스냅샷

    // 프레임별 입력 히스토리 (로컬 + 원격 동시 보관)
    struct NpInputState { uint16_t local = 0; uint16_t remote = 0; };
    std::map<int, NpInputState> m_npInputHistory;

    // ── No-Wait 프레임 페이싱 ───────────────────────────
    // 프레임 스톨(return) 대신 targetMs 를 ±1ms씩 조절해 부드럽게 동기화
    double m_frameDelay = 0.0;  // ms 오프셋, 양수=슬로우다운

    // ── 청크 재시뮬레이션 (대형 롤백 분산 처리) ─────────────
    // stateReceived 에서 MAX_RESIM_PER_TICK 프레임씩 나눠 처리
    // → 이벤트 루프 블로킹 방지 + 클라이언트 스터터 감소
    int  m_pendingResimTo  = -1;    // 목표 프레임(-1=없음)
    static constexpr int MAX_RESIM_PER_TICK = 8;

    // ── 호스트 스냅샷 큐 (소켓 시그널 핸들러 내 run() 방지) ─────
    // stateReceived 는 데이터만 저장 → onEmuTimer 에서 안전하게 적용
    // (순수 GGPO 에선 desync 복구용 풀스테이트 재동기에서만 사용 — 평소엔 안 옴)
    int        m_pendingSyncSf  = -1;  // 호스트 스냅샷 프레임 번호 (-1=없음)
    int        m_pendingSyncCur = -1;  // 수신 시점 로컬 프레임 번호
    QByteArray m_pendingSyncData;      // 스냅샷 페이로드

    // ── GGPO desync 감지 (체크섬) ────────────────────────────
    // 확정 프레임마다 상태 CRC 를 교환. 양쪽 같은 프레임 CRC 불일치 → desync.
    // desync 시 클라가 호스트에 풀스테이트 재동기를 1회 요청 → 복구.
    std::map<uint32_t,uint32_t> m_localChecksums;   // frame → 내 상태 CRC
    std::map<uint32_t,uint32_t> m_remoteChecksums;  // frame → 상대 상태 CRC
    uint32_t m_lastChecksumFrame = 0;               // 마지막 체크섬 계산 프레임
    bool     m_resyncPending     = false;           // 재동기 요청 진행 중
    void onNetChecksum(quint32 frame, quint32 crc); // 상대 체크섬 수신 핸들러
    void onNetResyncReq(quint32 frame);             // 호스트: 재동기 요청 받음
    void checkDesync(uint32_t frame);               // 같은 프레임 CRC 비교
    static uint32_t npChecksum(const QByteArray& data);  // 상태 CRC (FNV-1a)

    // ── Loading Barrier ──────────────────────────────────
    // 양쪽 준비 판단을 MainWindow에서 직접 관리 (NetplayManager 플래그 경쟁 회피)
    bool   m_npPeerReady   = false; // 상대가 MSG_READY 보냈는지
    bool   m_npSelfLoaded  = false; // 내가 loadRomInternal() 완료했는지
    bool   m_npStarted     = false; // onNetStart() 이미 호출됐는지 (이중 시작 방지)
    QTimer* m_npReadyRetry = nullptr; // READY 재전송 타이머 (300ms)

    // ── onEmuTimer 재진입 방지 ───────────────────────────────
    // FBNeo DLL이 run() 중 DirectSound/WinMM 등 Windows API를 호출하면
    // Qt 이벤트 루프가 재진입하여 onEmuTimer가 중간에 다시 불릴 수 있음
    // → m_core 상태 충돌 → 크래시. 이 플래그로 완전히 차단
    bool m_emuTimerBusy = false;
};
