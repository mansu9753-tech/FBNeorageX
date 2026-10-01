#pragma once
#include "ShellPage.h"
// MainWindow.h — 메인 윈도우
#include <map>
#include <algorithm>

#include <QMainWindow>
#include <QPointer>
#include <QVector>
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
#include <QAbstractSpinBox>   // 휠 가드 (QSpinBox/QDoubleSpinBox 공통 기반)
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QAudioSink>
#include <QBuffer>

#include "AppSettings.h"   // gSettings (isEn() 인라인에서 사용)
#include "GameCanvas.h"
#include "SoftCanvas.h"
#include "VideoRecorder.h"
#include "FrameLab.h"
#include "IntroSplash.h"
#include "PreviewVideo.h"   // 프리뷰 영상 자체 디코더 (Linux/FFmpeg)
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
    void buildShell();              // 메뉴 셸 생성 + 신호 연결
    void syncShellList();           // 게임 목록을 셸로 복사
    void syncShellPreview();        // 선택된 게임의 프리뷰 이미지를 셸로
    void buildShellMenu();                       // OPTIONS 메뉴 페이지 등록
    void applyPathSettings();                    // ROM/프리뷰 폴더 변경 반영 (코어 폴더 + 재스캔)
    void applyLiveSettings();                    // 저장된 화면·음량 설정을 지금 화면에 반영
    void refreshNativeCheatFlag();               // 네이티브 치트가 있으면 수동 엔진을 끈다
    void syncShellFilters();        // 필터 단추 줄 갱신

    // OPTIONS 패널 내부 페이지 빌더


    // ── 공통 스타일 헬퍼 ────────────────────────────────

    // ── 게임 관리 ────────────────────────────────────────
    void scanRoms();
    struct ListRow { QString rom; QString label; bool fav = false; };
    QString          m_searchText;            // 검색어 (게임 이름·롬 이름에 포함되는 것만 보인다)
    QTimer*          m_searchDebounce = nullptr;
    QVector<ListRow> m_rows;                 // 화면에 보이는 게임 목록 (필터 적용 후)
    void filterRoms();                       // 필터를 적용해 m_rows 를 만들고 셸에 넘긴다
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
    void startCompositingWatch();
    void checkCompositing();
    void attachCanvas();      // 빈 자리 위젯 → 게임 캔버스 (처음 한 번)
    void enterGameScreen();   // GUI → 게임 화면 (프리뷰 정지)
    void leaveGameScreen();   // 게임 화면 → GUI (프리뷰 재개)

    // ── 세이브스테이트 / 스크린샷 ────────────────────────
    void saveState(int slot);
    void loadState(int slot);
    QImage currentFrameImage() const;   // 코어의 마지막 화면 (32비트). 없으면 널
    void pushFrameHistory();            // FRAME LAB 용 최근 프레임 기록
    QImage stepOneFrame();              // 멈춘 게임을 한 프레임만 진행
    void openFrameLab();
    void takeScreenshot();
    void savePreviewShot();        // 현재 프레임을 previews/{rom}.png 로 저장
    void togglePreviewRecord();    // previews/{rom}.mp4 녹화 시작/정지

    // ── 설정 적용 ────────────────────────────────────────
    void openShaderParams();   // 셰이더 파라미터 편집 창

    // ── GUI 한/영 전환 ───────────────────────────────────
    //   위젯을 다시 만들지 않고 텍스트만 갈아끼운다 → 레이아웃/시그널 연결에
    //   전혀 영향을 주지 않으므로 기존 동작(멀티플레이 등)이 그대로 유지된다.
    void retranslateUi();      // 등록된 모든 위젯을 현재 언어로 갱신
    void toggleLanguage();     // ko ↔ en 전환 + 저장
    static bool isEn() { return gSettings.uiLanguage == "en"; }

    // ── 즐겨찾기 ────────────────────────────────────────
    void toggleFavorite(const QString& romName);
    bool isFavorite(const QString& romName) const;

    // ── 게임목록 필터 바 (ALL / ★FAV / ☆ + 기종별 탭) ────
    //   기종 탭은 실제로 가지고 있는 ROM 기준으로만 만들어진다.
    //   (없는 기종 버튼이 자리만 차지하지 않도록)
    QString      m_hwFilter;                // 선택된 기종 id (빈 값 = 전체)

    // 옵션 패널의 콤보/스핀/슬라이더가 휠을 가로채 스크롤을 막는 것 방지.
    //   동적으로 다시 만들어지는 페이지(머신세팅/치트)는 갱신 후 다시 호출해야 한다.

    // ── 게임패드: 장치별 프로필 / 플레이어 배정 ──────────────
    // ── 베젤 ─────────────────────────────────────────────
    void       applyBezel();          // 현재 게임의 베젤을 캔버스에 적용
    // ── VIDEO 셸 페이지 창구 (셰이더·베젤 이미지) ──
    QString bezelKeyFor(const QString& scope) const;
    QString bezelScopeLabel(const QString& scope) const;
    QString bezelInfoText() const;
    void    assignBezel(const QString& key, const QString& value);
    void    clearBezelAssign(const QString& key);
    bool    applyShaderFile(const QString& path);
    void    clearShaderFile();
    void    saveTurboSettings();

    void applyPadProfiles();     // 연결된 패드에 저장된 프로필·배정 적용
    // 패드 매핑 — 저장소 하나(gSettings.padMaps), 해석 한 곳
    QString padScopeKeyDev (const QString& device) const;
    QString currentPadRom() const;
    QHash<int,int> layoutDefaultMap() const;   // 배치 기본값 (코어 정의 우선)
    void    applyCoreButtonDefs();             // 코어 버튼 정의로 기본값 확정
    CoreSixButtons m_coreBtns;                 // 코어가 알려준 6버튼 인덱스
    QHash<int,int> m_padUiMap[4];              // 화면에 보여줄 표 (의미 포함)
    int  keyActionIndex(int stored) const;     // 의미 → 이 게임의 실제 인덱스
    QHash<int,int> resolvePadMap(const QString& device, const QString& rom,
                                 QString* sourceOut = nullptr) const;
    QString m_padMapSource[4];   // 패드별 "적용 출처" 표시용
    void    clearPadScope(const QString& scope);
    void logPadMappingSummary(); // 실제 적용된 매핑을 로그로 확인
    int          m_remapPad        = 0;   // 리매핑 대상 패드 인덱스

    // FBNeo 네이티브 치트 엔진 연동: <system_dir>/fbneo/cheats/ 로 ini 복사
    void syncCheatsToSystemDir(const QString& romName);
    // 코어가 치트를 옵션으로 등록했는가 → true 면 수동 RAM 쓰기 엔진을 끈다
    bool m_nativeCheatsActive = false;

    // ── 치트 UI 갱신 ─────────────────────────────────────

    // ── 프리뷰 ──────────────────────────────────────────
    void buildPreviewPlayers();                 // 프리뷰 영상 재생기 (프레임은 셸/화면보호기로)
    void onPreviewFrame(const QImage& img);
    void stopGame();                            // STOP GAME
    void resetGame();                           // RESET
    QString tateLabel() const;                  // TATE 상태 문구
    void loadPreview(const QString& romName);
    // 프리뷰 표시 (비율 유지, 크롭 없음)
    // 프리뷰 박스 폭을 원본 비율에 맞춰 조정 → 잘림도 여백도 없게
    // 프리뷰 영상은 게임당 한 번만 재생한다 (끝나면 이미지로 돌아가고 반복 없음)
    bool    m_previewVideoDone = false;

    // ── 화면보호기 ───────────────────────────────────────
    //   메뉴에서 1분간 조작이 없으면 프리뷰 영상을 무작위로 전체화면 재생하고,
    //   한 편이 끝나면 또 다른 영상을 무작위로 이어서 재생한다.
    //   아무 조작이나 하면 즉시 빠져나온다. (번인 방지)
    QWidget* m_ssPage      = nullptr;   // 전체화면 표시용 페이지 (스택 index 2)
    QLabel*  m_ssLabel     = nullptr;   // FFmpeg 프레임을 그리는 라벨
    QTimer*  m_ssIdle      = nullptr;   // 무조작 감시 타이머 (1초 틱)
    int      m_ssIdleTicks = 0;         // 연속 무조작 초
    bool     m_ssActive    = false;
    QString  m_ssLastFile;              // 직전 재생 파일 (연속 중복 방지)
    uint32_t m_ssPadPrev   = 0;         // 패드 조작 감지용 이전 상태
    bool     m_ssWasWindowed = false;   // 시작 전 창모드였는지 (해제 시 원복)
    void     startScreensaver();
    void     stopScreensaver();
    void     playRandomScreensaverVideo();
    void     resetIdleTimer();          // 입력이 있으면 대기시간 초기화
    void     revealGame(const QString& rom);   // 목록 커서를 그 게임으로 (필터에 가려져 있으면 필터를 푼다)
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
    static QHash<int, int> buildDefaultKeymap(bool six);
    void reloadKeymap();
    // ── 컨트롤 저장 범위: 게임별(자동) > 기종별(직접 저장) > 전역 > 기본 ──
    QString platScopeKey(const QString& rom) const;      // "plat:neogeo#std" 처럼 기종+배치
    QString gameScopeKey(const QString& rom) const { return rom.isEmpty() ? QString() : "game:" + rom; }
    void    autoSaveKeyboard();                          // 지금 키보드 표를 이 게임 범위에 저장
    void    autoSavePad(int padIdx);                     // 그 패드의 표를 이 게임 범위에 저장
    void    autoSaveStick();                             // 아케이드 스틱 표
    void    saveControlsForPlatform();                   // 지금 컨트롤·터보를 이 기종 전체에 저장
    void    forgetGameControls();                        // 이 게임에 저장된 컨트롤을 지운다
    QString platformSaveLabel() const;                   // "NEOGEO" / "CPS" / "OTHER"
    void    applyTurboFor(const QString& rom);           // 게임 > 기종 > 전역 순으로 터보 적용
    QString turboToString() const;                               // 지금 배치·게임에 맞는 키보드 표를 다시 고른다
    bool sixLayoutNow() const { return m_gamepad && m_gamepad->padLayout() == PadLayout::SixButton; }
    QVector<QPair<int,QString>> turboButtons() const;  // 터보 표: (libretro 인덱스, 이름)
    // ── CONTROLS 셸 페이지가 ShellHost::controls 로 부르는 창구 ──
    //   dev: 0=키보드 1=게임패드(XInput) 2=아케이드 스틱(WinMM)
    void refreshControlsUi();                          // 열려 있는 CONTROLS 화면을 다시 그린다
    QVector<ControlSlot> controlSlots(int dev);        // 액션 목록 + 현재 배정
    void remapControl(int dev, int slot);              // 캡처창을 열어 다시 배정
    void resetControlDevice(int dev);                  // 그 장치 매핑을 기본값으로
    QString padSourceText() const;                     // "지금 적용 중: ..." 안내
    QHash<int, int> m_keymap;      // Qt key → libretro button

    // ── 기종 분류 + 핫키 시스템 ─────────────────────────────────
    static QString gamePlatform(const QString& rom);   // 기종 분류
    static int  hotkeyEncode(int key, int mods);
    static void hotkeyDecode(int enc, int& key, int& mods);

    // ── 사운드 모드 ──────────────────────────────────────
    void applyResolvedSoundMode();  // 우선순위대로 골라 오디오에 반영
    int  hotkeyOf(const QString& action);              // 현재 핫키(설정>기본)
    bool hotkeyMatch(const QString& action, int key, int qtMods);

    // ── 컨트롤 스코프 해석/적용 (게임별 > 기종별 > 전역 > 기본) ──
    QHash<int,int> resolveCtrlMap(const QHash<QString,QHash<int,int>>& scoped,
                                  const QHash<int,int>& global,
                                  const QHash<int,int>& dflt,
                                  const QString& rom);
    void resolveAndApplyControls(const QString& rom);  // 게임 로드 시 적용
    QString m_ctrlScopeRom;        // 현재 컨트롤 테이블이 대상으로 하는 게임 (게임별 저장용)

    // 핫키 정의 테이블
    struct HotkeyDef { const char* action; const char* label; const char* labelEn; int key; int mods; };
    static const HotkeyDef* hotkeyDefs(int* count);

    // ════════════════════════════════════════════════════
    //  위젯 멤버
    // ════════════════════════════════════════════════════

    // ── 화면 전환 스택 ───────────────────────────────────
    QStackedWidget*  m_stack      = nullptr;  // 0=GUI, 1=게임화면
    QWidget*         m_guiWidget  = nullptr;
    GameViewIface*   m_canvas     = nullptr;   // 게임 화면 (GameCanvas 또는 SoftCanvas)
    QWidget*         m_canvasW    = nullptr;   // 그 위젯

    // ── NeoRageX 0.6b 메뉴 셸 ────────────────────────────
    //   GUI 화면(게임 목록·옵션 메뉴·프리뷰·이벤트)을 통째로 이 위젯이 그린다.
    class NeoRageXShell* m_shell     = nullptr;
    class ShellMenu*     m_menu      = nullptr;   // OPTIONS 메뉴 컨트롤러



    // 프리뷰 스택 (0=이미지, 1=비디오)
    // ── 마우스 좌클릭 효과음 ─────────────────────────────
    //   QSoundEffect 는 두어 번 재생하면 무음이 되는 문제가 있었다.
    //   클릭마다 sink 를 stop/start 하는 방식도 불안정했다(10회 중 7회만 재생).
    //   → push 모드: sink 를 한 번만 열어두고 클릭할 때마다 PCM 을 write 한다.
    //     (검증: 10회 중 10회 재생, 총 541ms ≈ 57ms × 10)
    QByteArray       m_sfxPcm;                     // WAV 본문 PCM
    QAudioSink*      m_sfxSink        = nullptr;
    QBuffer*         m_sfxBuf         = nullptr;   // 재생용 PCM 리더
    QAudioFormat     m_sfxFmt;                     // 클릭음 포맷
    quint64          m_lastClickTs    = 0;         // 같은 클릭 중복 처리 방지
    qint64           m_sfxLastPlay    = 0;         // 연타 제한
    double           m_sfxBytesPerMs  = 0.0;       // 재생 길이 계산용
    void             loadClickSound();
    void             playClickSound();
    QVideoSink*      m_videoSink     = nullptr;   // Windows: 프리뷰 영상 프레임을 받는다
    QMediaPlayer*    m_mediaPlayer    = nullptr;   // Windows 경로에서만 사용
    PreviewVideo*    m_previewVideo   = nullptr;   // Linux: 자체 소프트웨어 디코더
    QTimer*          m_previewVidTimer= nullptr;


    // ── CONTROLS 페이지 ──────────────────────────────────

    // ── DIRECTORIES / SETTINGS 위젯 ─────────────────────

    // ── VIDEO 위젯 ───────────────────────────────────────

    // ── AUDIO 위젯 ───────────────────────────────────────

    // ── CHEATS 페이지 위젯 ───────────────────────────────

    // ── NETPLAY 페이지 위젯 ──────────────────────────────
    // ── MULTIPLAYER 셸 페이지 창구 ──
    NetState         m_net;                                 // 화면에 보일 상태 (문구·버튼 가능 여부)
    int              m_netDelay = 2;                        // 입력 지연 (프레임)
    void netHost();
    void netJoin();
    void netDisconnect();
    void netSetRelay(const QString& typed);
    void netCopyRoomCode();
    void fetchPublicIp();
    void refreshNetUi();
    NetState netState() const;
    QString          m_publicIp;                            // 외부 공개 IP (api.ipify.org)
    QHash<uint32_t, uint16_t> m_npDelayQueue;              // 입력 지연 큐 frame→bits

    // ════════════════════════════════════════════════════
    //  상태 멤버
    // ════════════════════════════════════════════════════
    QString          m_selectedGame;
    QString          m_loadedGame;   // 현재 코어에 실제 로드된 롬 이름 (isPaused 재개 판별용)
    bool             m_isFullscreen  = false;
    bool             fullscreenNow() const;
    QTimer*          m_gfxWatch = nullptr;       // OpenGL 합성 실패 감시
    int              m_gfxBad = 0, m_gfxChecks = 0;
    QWidget*         m_canvasHolder = nullptr;   // 캔버스를 처음 끼우기 전까지 스택 1번 자리를 지킨다
    FrameLab*        m_lab = nullptr;            // 스택 4번 페이지
    QList<QImage>    m_frameHist;                // 최근 프레임 (오래된 것 -> 최신)
    QString          m_histGame;                 // 기록이 어느 게임의 것인지
    IntroSplash*     m_intro = nullptr;          // 시작 오프닝 (스택 3번 페이지)
    QLabel*          m_playerOverlay = nullptr;  // 게임 화면 내 플레이어 표시 오버레이
    QTimer*          m_overlayTimer  = nullptr;  // 1P 복귀 시 오버레이 자동 숨김 타이머
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
    bool             m_navBWasDown  = false;  // B버튼 (메뉴 닫기)
    bool             m_navXWasDown  = false;  // X버튼 (즐겨찾기)
    bool             m_navYWasDown  = false;  // Y버튼 (검색)
    bool             m_navLBWasDown = false;  // L 버튼 (커서 → 게임 목록)
    bool             m_navRBWasDown = false;  // R 버튼 (커서 → 옵션 메뉴)

    // ── 게임패드 메뉴 진입 홀드 카운터 ─────────────────────────
    // SELECT+START 동시 홀드 120프레임(~2초) → togglePause (메인 GUI)
    // Start 단독은 게임으로 그대로 전달 (KOF 보스선택 커맨드 정상 작동)
    // START 연속 홀드 프레임 카운터 (서비스 메뉴 우발 진입 방지)
    // serviceMode=false 상태에서 90프레임 초과 → keys[3]=0 강제
    int  m_startHoldFrames = 0;
    // ── 전용 서비스(TEST) 입력 펄스 ──────────────────────
    //   전용 핫키를 누르면 이 카운터가 세팅되고, 그 프레임 수만큼 코어에
    //   서비스/테스트 입력(L2, index 12)을 직접 assert 한다. START 홀드·게임패드
    //   L2 와 완전히 분리된 "마메식 전용 서비스 키"를 모든 기종에 제공.
    int  m_serviceHoldFrames = 0;
    // 게임패드 핫키(L3/R3/트리거) 눌림 상태 — 눌린 순간만 처리하기 위한 이전 값
    // 리매핑 캡처 중 여부 — 이 동안에는 메뉴 조작·핫키를 멈춘다
    bool    m_captureActive = false;

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
    static QString redactRelayUrl(const QString& s);   // 로그에서 릴레이 주소 마스킹
    QTimer* m_relayPollTimer = nullptr;
    // 릴레이 폴링 '세대' — 새 HOST/JOIN 또는 DISCONNECT 마다 증가시킨다.
    // 진행 중인 폴링 루프(1초 재시도 + in-flight GET 응답)는 자신이 시작될 때의
    // 세대를 기억했다가, 현재 세대와 다르면 스스로 종료한다. 이렇게 해야
    // 디스커넥트 후에도 옛 루프가 살아남아 재연결을 방해하는 일이 없다.
    int  m_relayPollGen = 0;
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
