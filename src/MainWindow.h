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
#include <QMediaRecorder>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QAudioFormat>
#include <QAudioBuffer>
#include <QVideoFrame>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#  include <QVideoFrameInput>
#  include <QAudioBufferInput>
#endif

#include "BorderPanel.h"
#include "GameCanvas.h"
#include "LibretroCore.h"
#include "AudioManager.h"
#include "NetplayManager.h"
#include "CheatManager.h"
#include "GamepadManager.h"

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
    void onNetLoadGame(const QString& romName); // 조인: 게임 자동 로드
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

    // 게임 캔버스 전환 헬퍼 (프리뷰 정지 + 스택 전환 통합)
    void enterGameScreen();   // GUI → 게임 화면 (프리뷰 정지)
    void leaveGameScreen();   // 게임 화면 → GUI (프리뷰 재개)

    // ── 세이브스테이트 / 스크린샷 ────────────────────────
    void saveState(int slot);
    void loadState(int slot);
    void takeScreenshot();

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
    QLineEdit*       m_npIpEdit         = nullptr;
    QSpinBox*        m_npPortSpin       = nullptr;
    QLabel*          m_npStatusLabel    = nullptr;
    QPushButton*     m_npHostBtn        = nullptr;
    QPushButton*     m_npConnectBtn     = nullptr;
    QPushButton*     m_npStartBtn       = nullptr;
    QPushButton*     m_npDisconnBtn     = nullptr;
    QLabel*          m_npLocalIpLabel   = nullptr;

    // ════════════════════════════════════════════════════
    //  상태 멤버
    // ════════════════════════════════════════════════════
    QString          m_selectedGame;
    QString          m_loadedGame;   // 현재 코어에 실제 로드된 롬 이름 (isPaused 재개 판별용)
    bool             m_isFullscreen  = false;
    QPushButton*     m_swapBtn       = nullptr;  // 1P↔2P 스왑 버튼
    int              m_glFilter     = 0;  // 0=ALL, 1=FAV(즐겨찾기만), 2=☆(미즐겨찾기만)
    QSize            m_windowedSize;
    int              m_stateSlot    = 1;

    QList<QPair<QString, QString>> m_allRoms;  // {displayName, romName}

    // ── 에뮬 타이머 ─────────────────────────────────────
    QTimer*          m_timer       = nullptr;
    QElapsedTimer    m_aflClock;
    double           m_frameAccum  = 0.0;   // 프레임 누산기 (AFL 타이밍 정밀도 개선)
    QTimer*          m_cursorTimer  = nullptr; // 마우스 커서 자동 숨김 타이머
    bool             m_cursorHidden = false;  // setOverrideCursor 적용 중 여부

    // ── UI 게임패드 네비게이션 (D-패드로 게임목록 탐색) ──────────
    QTimer*          m_uiNavTimer   = nullptr;
    int              m_navDir       = 0;    // -1=UP, 0=없음, 1=DOWN
    int              m_navRepeatMs  = 0;    // 방향 유지 누적 시간(ms)
    bool             m_navAWasDown  = false; // A버튼 이전 상태 (엣지 감지)
    bool             m_navLWasDown  = false; // LEFT 버튼 이전 상태 (페이지 업)
    bool             m_navRWasDown  = false; // RIGHT 버튼 이전 상태 (페이지 다운)

    // ── 코어 / 오디오 / 치트 / 게임패드 ─────────────────
    LibretroCore*    m_core    = nullptr;
    AudioManager*    m_audio   = nullptr;
    CheatManager*    m_cheat   = nullptr;
    GamepadManager*  m_gamepad = nullptr;

    // ── 녹화 ────────────────────────────────────────────
    QMediaCaptureSession* m_captureSession = nullptr;
    QMediaRecorder*       m_recorder       = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QVideoFrameInput*     m_videoInput     = nullptr;
    QAudioBufferInput*    m_audioInput     = nullptr;
#endif

    // ── Rollback Netcode ─────────────────────────────────
    QHash<int, QByteArray> m_npStates;   // frame → run() 직전 스냅샷

    // 프레임별 입력 히스토리 (로컬 + 원격 동시 보관)
    struct NpInputState { uint16_t local = 0; uint16_t remote = 0; };
    std::map<int, NpInputState> m_npInputHistory;

    // ── No-Wait 프레임 페이싱 ───────────────────────────
    // 프레임 스톨(return) 대신 targetMs 를 ±1ms씩 조절해 부드럽게 동기화
    double m_frameDelay = 0.0;  // ms 오프셋, 양수=슬로우다운

    // ── Loading Barrier ──────────────────────────────────
    // 양쪽 준비 판단을 MainWindow에서 직접 관리 (NetplayManager 플래그 경쟁 회피)
    bool   m_npPeerReady   = false; // 상대가 MSG_READY 보냈는지
    bool   m_npSelfLoaded  = false; // 내가 loadRomInternal() 완료했는지
    QTimer* m_npReadyRetry = nullptr; // READY 재전송 타이머 (300ms)
};
