// MainWindow.cpp — 메인 윈도우 (Phase 3 완전 구현)

#include "MainWindow.h"

#include <QProcess>
#include <QScreen>
#include <QWindow>
#include <QRegularExpression>
#include "ShaderParamDialog.h"
#include "NeoRageXShell.h"
#include "NativeCheats.h"
#include "ShellMenu.h"
#include "ShellPages.h"
#include "AppSettings.h"
#include "EmulatorState.h"
#include "GameNamesDb.h"
#include "GameHardware.h"   // 게임목록 기종별 탭 분류
#include "SoundMode.h"      // 사운드 모드 프리셋
#include "ArcadeLayout.h"   // 게임별 아케이드 버튼 배치 (6버튼 격투 판별)
#include "PadMapping.h"     // 패드 매핑 해석 (저장소 하나, 우선순위 하나)
#include "PadRawBits.h"     // 물리 버튼 비트 (메뉴 조작용)

#include <QApplication>
#include <QEvent>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSplitter>
#include <QScrollArea>
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QFileInfo>
#include <QStandardPaths>
#include <QIcon>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTimer>
#include <QDateTime>
#include <QPixmap>
#include <QImage>
#include <QDebug>
#include <QPushButton>
#include <QUrl>
#include <QAudioBuffer>
#include <QAudioFormat>
#include <QMediaDevices>   // 클릭음 출력 장치
#include <QAudioOutput>
#include <QVideoFrame>
#include <QMediaFormat>
#include <QDialog>
#include <QKeySequence>
#include <QHeaderView>
#include <QTableWidget>
#include <QMessageBox>
#include <QInputDialog>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonDocument>
#include <QClipboard>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QScopeGuard>

// ── 배경 이미지 위젯 (paintEvent로 직접 렌더링) ─────────────────
//
//  ★ 주의: 부모(QStackedWidget) 또는 자기 자신에 stylesheet 가 적용되어 있어도
//    custom paintEvent 가 항상 동작하도록 다음 속성 강제:
//      - WA_StyledBackground = false  → Qt styling engine 의 background 그리기 비활성화
//      - WA_OpaquePaintEvent = true   → Qt 가 paint 전 영역을 clear 하지 않음
//      - autoFillBackground = false   → palette 색 자동 채움 방지
//    이 셋이 모두 false/true 일 때만 paintEvent 가 화면 픽셀의 단독 책임을 가진다.
//
#include <QPainter>
class BgWidget : public QWidget {
    QPixmap m_bg;
public:
    explicit BgWidget(QWidget* parent = nullptr) : QWidget(parent) {
        m_bg = QPixmap(":/assets/background.png");
        if (m_bg.isNull())
            qWarning("BgWidget: ':/assets/background.png' 로드 실패 — "
                     "resources.qrc 또는 assets/background.png 확인 필요");
        else
            qDebug("BgWidget: background.png %dx%d 로드 완료",
                   m_bg.width(), m_bg.height());

        setAttribute(Qt::WA_StyledBackground, false);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setAutoFillBackground(false);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        if (m_bg.isNull()) {
            // 폴백: 어두운 단색으로 채워 깨진 화면 방지
            p.fillRect(rect(), QColor(0, 4, 16));
            return;
        }
        // KeepAspectRatioByExpanding: 화면 비율에 맞춰 잘라서 가득 채우기
        QSize scaled = m_bg.size().scaled(rect().size(),
                                           Qt::KeepAspectRatioByExpanding);
        QRect target((rect().width()  - scaled.width())  / 2,
                     (rect().height() - scaled.height()) / 2,
                     scaled.width(), scaled.height());
        p.drawPixmap(target, m_bg, m_bg.rect());
    }
};

// 리매핑 캡처 중임을 표시하는 스코프 가드.
//   캡처 창이 떠 있는 동안 메뉴 조작·핫키가 함께 동작하면
//   방향키를 지정하다 목록이 움직이고, 실행 버튼을 지정하다 게임이 실행된다.
struct CaptureGuard {
    bool* flag;
    explicit CaptureGuard(bool* f) : flag(f) { if (flag) *flag = true; }
    ~CaptureGuard() { if (flag) *flag = false; }
};

// ── KEY CAPTURE DIALOG (non-Q_OBJECT) ──────────────────────────
class KeyCaptureDialog : public QDialog {
public:
    int  capturedKey  = 0;
    int  capturedMods = 0;        // 핫키 캡처 시 Shift/Ctrl/Alt 비트 (1/2/4)
    bool captureMods  = false;    // true 면 모디파이어도 함께 캡처
    explicit KeyCaptureDialog(const QString& action, QWidget* parent,
                              bool withMods = false)
        : captureMods(withMods), QDialog(parent) {
        setWindowTitle("Key Remap");
        setFixedSize(320, 100);
        setStyleSheet("QDialog{background:#000820;border:1px solid #334488;}");
        auto* lbl = new QLabel(
            QString("<center><b style='color:#aaccff;font-family:Courier New;font-size:13px;'>"
                    "[ %1 ]</b><br>"
                    "<span style='color:#668899;font-family:Courier New;font-size:10px;'>"
                    "%2</span></center>")
                .arg(action,
                     withMods ? "키 조합을 누르세요 (예: Ctrl+F9) / Esc = 취소"
                              : "Press any key... (Esc = cancel)"),
            this);
        lbl->setTextFormat(Qt::RichText);
        lbl->setAlignment(Qt::AlignCenter);
        auto* lay = new QVBoxLayout(this);
        lay->addWidget(lbl);
        setModal(true);
    }
protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->isAutoRepeat()) return;      // 캡처창을 연 Enter 를 계속 누르고 있어도 배정되지 않는다
        int k = e->key();
        if (k == Qt::Key_Escape) { reject(); return; }
        // 모디파이어 키 단독 입력은 무시 (실제 키를 기다림)
        if (k == Qt::Key_Shift || k == Qt::Key_Control ||
            k == Qt::Key_Alt   || k == Qt::Key_Meta) return;
        capturedKey = k;
        if (captureMods) {
            int m = e->modifiers();
            capturedMods = ((m & Qt::ShiftModifier)   ? 1 : 0)
                         | ((m & Qt::ControlModifier) ? 2 : 0)
                         | ((m & Qt::AltModifier)     ? 4 : 0);
        }
        accept();
    }
};

// ── GAMEPAD BUTTON CAPTURE DIALOG ──────────────────────────────
// 게임패드/아케이드스틱의 버튼을 실제로 눌러서 캡처하는 다이얼로그
// Q_OBJECT 없이 QTimer 람다로 폴링 (KeyCaptureDialog와 동일한 패턴)
class GamepadCaptureDialog : public QDialog {
public:
    int capturedBtn = -1;  // XInput: bitmask / WinMM: 0-based index / -1=취소
    int prevState   = 0;   // 직전 눌림 상태 (멤버여야 람다에서 안전하게 유지됨)

    explicit GamepadCaptureDialog(const QString& action, GamepadManager* gpad,
                                   bool winmm, QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle("Button Remap");
        setFixedSize(320, 120);
        setStyleSheet("QDialog{background:#000820;border:1px solid #334488;}");
        QString inputType = winmm ? "아케이드 스틱" : "게임패드";
        auto* lbl = new QLabel(
            QString("<center>"
                    "<b style='color:#aaccff;font-family:Courier New;font-size:12px;'>[ %1 ]</b><br><br>"
                    "<span style='color:#668899;font-family:Courier New;font-size:10px;'>"
                    "%2의 버튼을 눌러주세요...</span><br>"
                    "<span style='color:#334455;font-family:Courier New;font-size:9px;'>"
                    "(Esc = 취소)</span></center>").arg(action, inputType), this);
        lbl->setTextFormat(Qt::RichText);
        lbl->setAlignment(Qt::AlignCenter);
        auto* lay = new QVBoxLayout(this);
        lay->addWidget(lbl);
        setModal(true);

        // 다이얼로그 열릴 때 이미 눌린 버튼은 무시 (초기 상태 저장)
        //  ★ prevState 는 반드시 멤버여야 한다.
        //    예전에는 생성자의 지역 변수를 람다가 참조(&prevState)로 캡처했는데,
        //    생성자가 끝나면 그 변수가 사라져 타이머가 돌 때는 쓰레기값을 읽었다.
        //    → 눌러도 "새로 눌린 버튼"이 계산되지 않아 캡처가 전혀 안 됐다.
        int init = gpad->pollRawForCapture(winmm);
        prevState = (init >= 0) ? init : 0;

        // ~60Hz 폴링으로 새 버튼 입력 감지
        auto* timer = new QTimer(this);
        timer->setInterval(16);
        connect(timer, &QTimer::timeout, this, [=]() mutable {
            int raw = gpad->pollRawForCapture(winmm);
            if (raw < 0) {
                lbl->setText("<center><span style='color:#ff6644;"
                             "font-family:Courier New;font-size:10px;'>"
                             "컨트롤러가 연결되지 않았습니다</span></center>");
                return;
            }
            int newBtns = raw & ~prevState;
            if (newBtns != 0) {
                if (winmm) {
                    for (int b = 0; b < 32; ++b)
                        if (newBtns & (1 << b)) { capturedBtn = b; break; }
                } else {
                    capturedBtn = newBtns & (-newBtns);  // lowest set bit
                }
                accept();
                return;
            }
            prevState = raw;
        });
        timer->start();
    }

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) { reject(); return; }
    }
};

// ════════════════════════════════════════════════════════════
//  ROOM CODE 헬퍼 (토큰 방식 — IP 미포함)
//  포맷: XXXXXX (6자 base-36 영숫자 대문자)
//
//  ★ 토큰 자체에는 IP/포트가 없음. 워커(Cloudflare KV)가 토큰 →
//    {ip, port} 매핑을 관리. HOST/JOIN 모두 토큰을 키로 워커에 등록.
//
//  헷갈리기 쉬운 문자(0/O, 1/I) 제외한 32문자 알파벳 사용.
// ════════════════════════════════════════════════════════════
static const QString kTokenChars = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";  // 32자
static constexpr int kTokenLen   = 6;

static QString generateRoomCode() {
    QString code;
    for (int i = 0; i < kTokenLen; ++i) {
        int v = QRandomGenerator::global()->bounded(kTokenChars.size());
        code += kTokenChars[v];
    }
    return code;  // 예: "AB3K7M"
}

static bool isValidRoomCode(const QString& raw) {
    QString s = raw.toUpper().trimmed();
    if (s.length() != kTokenLen) return false;
    for (QChar c : s)
        if (kTokenChars.indexOf(c) < 0) return false;
    return true;
}

// ── 생성자 ─────────────────────────────────────────────────
static constexpr int kScreensaverIdleSec = 300;      // 이 시간(초) 동안 조작이 없으면 화면보호기가 켜진다

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("FBNEORAGEX Core Edition " FBNRX_VERSION);
    m_windowedSize = QSize(1360, 840);
    resize(m_windowedSize);
#ifdef _WIN32
    setMinimumSize(900, 640);
#else
    // 스팀덱: 창모드가 의미 없으므로 항상 전체화면으로 시작한다.
    //   (게임 모드는 gamescope 가 어차피 전체화면으로 띄우고,
    //    데스크톱 모드에서도 창 테두리 없이 바로 쓰도록)
    //   최소 크기는 두지 않는다 — 전체화면 크기를 그대로 따르게.
    m_isFullscreen = true;
    QTimer::singleShot(0, this, [this]{ showFullScreen(); });
#endif

    // 아이콘 — 탐색기/작업표시줄/타이틀바 모두 동일한 아이콘 적용
    // icon.ico : ICO 파일 내부에 16~256px 다중 해상도가 내장되어 있어 품질 최적
    // icon.png : ICO 로드 실패 시 폴백 (PNG 스케일링)
    {
        // 1순위: QRC 내장 ICO (다중 해상도, 탐색기 아이콘과 동일한 원본)
        QIcon appIcon(":/assets/icon.ico");
        if (appIcon.isNull()) {
            // 2순위: 파일시스템 ICO
            QString icoPath = QCoreApplication::applicationDirPath() + "/assets/icon.ico";
            if (QFile::exists(icoPath))
                appIcon = QIcon(icoPath);
        }
        if (appIcon.isNull()) {
            // 3순위: PNG 스케일링 폴백
            QPixmap pm(":/assets/icon.png");
            if (!pm.isNull()) {
                for (int sz : {16, 24, 32, 48, 64, 128, 256})
                    appIcon.addPixmap(pm.scaled(sz, sz,
                        Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
        if (!appIcon.isNull())
            setWindowIcon(appIcon);
    }

    // 키맵 — 저장된 매핑이 있으면 복원, 없으면 기본값
    m_keymap = buildDefaultKeymap(false);
    if (!gSettings.keyboardMapping.isEmpty())
        m_keymap = gSettings.keyboardMapping;

    // 코어 / 오디오 / 치트 / 게임패드
    m_core    = new LibretroCore(this);
    m_audio   = new AudioManager(this);
    m_cheat   = new CheatManager(this);
    m_gamepad = new GamepadManager(this);

    // 치트 시그널
    connect(m_cheat, &CheatManager::cheatsLoaded, this, [this](int cnt, const QString& path){
        log(QString("🔓 치트 %1개 로드 — %2").arg(cnt).arg(QFileInfo(path).fileName()));
        // 치트 화면은 셸 메뉴가 열릴 때마다 지금 상태를 읽으므로 따로 갱신할 게 없다
        if (m_menu) m_menu->refreshOpen();
    });

    // ── 게임패드 조합 핫키 (SELECT + 버튼) ───────────────────
    //   게임 입력과 분리된 경로라 게임에 그대로 전달되지 않는다.
    //   SELECT+START=메뉴, +Y=종료, +L1=서비스, +R1=패스트포워드,
    //   +A=상태 저장, +B=상태 불러오기, +X=스크린샷, +→=슬롯, +←=프리뷰 이미지, +↑=전체화면,
    //   +↓=1P/2P 스왑, +L2=녹화, +R2=프리뷰 영상 녹화 (키보드 핫키와 같은 기능 전부)
    {
        auto* hk = new QTimer(this);
        hk->setInterval(33);                       // ~30Hz 로 눌림 변화만 감시
        connect(hk, &QTimer::timeout, this, [this]{
            if (!m_gamepad || m_captureActive) return;   // 리매핑 중에는 핫키도 중지
            // 패드 조작도 "조작 있음"으로 쳐서 화면보호기를 초기화/해제한다
            {
                uint32_t act = m_gamepad->hotkeyModifierHeld() ? 1u : 0u;
                for (int i = 0; i < 4; ++i) act |= m_gamepad->padBits(i);
                if (act && act != m_ssPadPrev) resetIdleTimer();
                m_ssPadPrev = act;
            }

            // 조합 핫키는 이벤트로 온다 (레벨이 아니라서 에지 검출이 필요 없다)
            const uint16_t down = m_gamepad->takeHotkeyEvents();
            if (!down) return;
            resetIdleTimer();
            if (m_stack && m_stack->currentIndex() > 1) return;   // 오프닝·프레임 랩·화면보호기 중에는 무시

            using GM = GamepadManager;
            const bool playing = gState.gameLoaded && !gState.isPaused;
            if (down & GM::HK_MENU) {                   // SELECT + START → 게임 ↔ 메뉴
                if (gState.gameLoaded) togglePause();
            }
            if (down & GM::HK_EXIT) {                   // SELECT + Y → 게임 종료
                if (gState.gameLoaded || gState.isPaused) {
                    m_timer->stop();
                    gState.isPaused = false;
                    if (m_core) m_core->unloadGame();
                    m_loadedGame.clear();
                    leaveGameScreen();
                    log("■ 게임 종료 (패드 SELECT+Y)");
                }
            }
            if (down & GM::HK_SERVICE) {                // SELECT + L1 → 서비스(TEST)
                if (playing) {
                    m_serviceHoldFrames = 12;
                    log("🔧 서비스(TEST) 입력 — 패드 SELECT+L1");
                }
            }
            if (down & GM::HK_FF) {                     // SELECT + R1 → 패스트포워드
                if (playing) toggleFastForward(!gState.fastForward);
            }
            if (gState.gameLoaded) {
                if (down & GM::HK_SAVE)      saveState(m_stateSlot);
                if (down & GM::HK_LOAD)      loadState(m_stateSlot);
                if (down & GM::HK_SHOT)      takeScreenshot();
                if (down & GM::HK_SLOT_UP) {
                    m_stateSlot = m_stateSlot % 8 + 1;
                    log(QString("💾 저장 슬롯 %1").arg(m_stateSlot));
                }
                if (down & GM::HK_PREVSHOT)  savePreviewShot();
                if (down & GM::HK_FULLSCR)   toggleFullscreen();
                if (down & GM::HK_SWAP)      toggleSwapPlayers();
                if (down & GM::HK_RECORD)    toggleRecording();
                if (down & GM::HK_PREVREC)   togglePreviewRecord();
            }
        });
        hk->start();
    }

    // 구버전 패드 설정을 정리했으면 알려 준다
    if (gSettings.padMapsMigrated) {
        QTimer::singleShot(0, this, [this]{
            log(isEn()
                ? "🎮 Pad mappings were reset once - the storage layout changed."
                : "🎮 패드 매핑을 1회 초기화했습니다 — 저장 구조가 바뀌었습니다.");
        });
    }

    // ── 잘못 저장된 6버튼 프로필 정리 (1회) ────────────────────
    //   6버튼 배치를 처음 넣었을 때 인덱스가 뒤집혀 있었고, 그 상태에서 패드를
    //   리매핑하면 그 표가 장치 프로필로 저장됐다. 이제 프로필은 배치별로
    //   나뉘므로, 그 표가 남아 있으면 "일반 배치" 프로필로 오인되어 네오지오까지
    //   망가진다. 특징(R2 → 11)으로 찾아 지운다.
    {
        QStringList bad;
        for (auto it = gSettings.padProfiles.begin();
             it != gSettings.padProfiles.end(); ) {
            if (it.value().value(0x20000, -1) == 11) {
                bad << it.key();
                it = gSettings.padProfiles.erase(it);
            } else {
                ++it;
            }
        }
        if (!bad.isEmpty()) {
            gSettings.save();
            // UI 가 아직 없는 시점이라 바로 log() 하면 메시지가 버려진다 → 뒤로 미룬다
            QTimer::singleShot(0, this, [this, bad]{
                log("🎮 잘못 저장된 패드 프로필 정리: " + bad.join(", ")
                    + " → 기본값으로 되돌림");
            });
        }
    }

    // 패드 목록이 바뀌면 장치별 프로필/배정을 다시 적용하고 UI 갱신
    connect(m_gamepad, &GamepadManager::padsChanged, this, [this]{
        applyPadProfiles();
        refreshControlsUi();
    });

    // 게임패드 시그널
    connect(m_gamepad, &GamepadManager::connected,    this, [this](int idx){
        log(QString("🎮 게임패드 %1 연결됨").arg(idx));
    });
    connect(m_gamepad, &GamepadManager::disconnected, this, [this]{
        log("🎮 게임패드 분리됨");
    });

    // ── UI 게임패드 네비게이션 타이머 ─────────────────────────────
    // GUI 모드에서 D-패드로 게임목록을 탐색하고 A버튼으로 실행
    m_uiNavTimer = new QTimer(this);
    m_uiNavTimer->setInterval(16);  // ~60Hz
    connect(m_uiNavTimer, &QTimer::timeout, this, [this] {
        // ★ 키/버튼 리매핑 중에는 메뉴 조작을 멈춘다.
        //   방향키를 지정하려고 누르면 게임목록이 같이 움직이고,
        //   실행 버튼을 지정하려 하면 게임이 실행돼 버리던 문제.
        if (m_captureActive) {
            m_navDir = 0; m_navHDir = 0; m_navRepeatMs = 0;
            // ★ 캡처가 끝나는 순간 "새로 눌림"으로 오인되던 문제.
            //   방금 배정한 버튼이 아직 눌려 있으면 (예: B 를 배정) 그 B 가 "뒤로 가기"로,
            //   A 가 "실행"으로 처리돼 버렸다. 모든 버튼을 눌린 상태로 표시해 두면
            //   손을 뗐다 다시 눌러야 동작한다.
            m_navAWasDown = m_navBWasDown = m_navXWasDown = m_navYWasDown = true;
            m_navLBWasDown = m_navRBWasDown = true;
            return;
        }
        // 오프닝 중에는 아무 패드 버튼이나 누르면 건너뛴다 (시작 직후 눌려 있던 버튼은 무시)
        if (m_stack && m_stack->currentIndex() == 3 && m_intro) {
            bool any = false;
            for (int i = 0; i < 16; ++i) any = any || gState.rawKeys[i];
            if (any && m_intro->elapsedMs() > 500) m_intro->skip();
            m_navDir = 0; m_navRepeatMs = 0;
            m_navAWasDown = m_navBWasDown = m_navXWasDown = m_navYWasDown = true;
            m_navLBWasDown = m_navRBWasDown = true;
            return;
        }
        // FRAME LAB: 좌우 = 프레임(길게 누르면 반복), L/R = 10프레임, A = 저장, B = 닫기
        if (m_stack && m_stack->currentIndex() == 4 && m_lab) {
            using Cmd = FrameLab::Cmd;
            const uint32_t bits = m_gamepad ? m_gamepad->rawBits() : 0;
            const int dir = (gState.rawKeys[7] && !gState.rawKeys[6]) ? 1
                          : (gState.rawKeys[6] && !gState.rawKeys[7]) ? -1 : 0;
            if (dir != m_navHDir) {
                m_navHDir = dir; m_navHRepeatMs = 0;
                if (dir) m_lab->navigate(dir < 0 ? Cmd::Prev : Cmd::Next);
            } else if (dir) {
                m_navHRepeatMs += 16;
                if (m_navHRepeatMs >= 380 && ((m_navHRepeatMs - 380) % 60) < 16)
                    m_lab->navigate(dir < 0 ? Cmd::Prev : Cmd::Next);
            }
            const bool a = bits & PAD_A, b = bits & PAD_B, lb = bits & PAD_LB, rb = bits & PAD_RB;
            if (a && !m_navAWasDown)   m_lab->navigate(Cmd::Save);
            if (b && !m_navBWasDown)   m_lab->navigate(Cmd::Close);
            if (lb && !m_navLBWasDown) m_lab->navigate(Cmd::Prev10);
            if (rb && !m_navRBWasDown) m_lab->navigate(Cmd::Next10);
            m_navAWasDown = a; m_navBWasDown = b; m_navLBWasDown = lb; m_navRBWasDown = rb;
            return;
        }
        // 게임 화면(스택 인덱스 1)이면 네비 비활성화
        if (!m_stack || m_stack->currentIndex() != 0) {
            m_navDir = 0; m_navRepeatMs = 0; return;
        }
        if (!m_shell) return;
        // 모달 창(폴더·파일 선택, 키 캡처)이 떠 있는 동안에는 그 뒤의 셸을 조작하지 않는다
        if (QApplication::activeModalWidget()) {
            m_navDir = 0; m_navHDir = 0;
            m_navAWasDown = m_navBWasDown = m_navXWasDown = m_navYWasDown = true;
            m_navLBWasDown = m_navRBWasDown = true;
            return;
        }
        using Nav = NeoRageXShell::Nav;

        static constexpr int REPEAT_INIT = 380; // 첫 반복 딜레이 (ms)
        static constexpr int REPEAT_RATE =  90; // 반복 간격 (ms)
        static constexpr int TICK        =  16; // 타이머 주기

        // Linux(스팀덱): D-패드 전용 비트 사용 — 아날로그 스틱 드리프트로 인한 오작동 방지
        // Windows: rawKeys 사용 (D-패드 + 아날로그 스틱 모두 포함)
#ifdef Q_OS_LINUX
        uint16_t dp = m_gamepad ? m_gamepad->dpadBits() : 0;
        int up   = (dp >> 4) & 1;  // LR_UP = bit 4
        int down = (dp >> 5) & 1;  // LR_DN = bit 5
#else
        int up   = gState.rawKeys[4]; // libretro UP
        int down = gState.rawKeys[5]; // libretro DOWN
#endif
        int newDir = (up && !down) ? -1 : (down && !up) ? 1 : 0;

        bool moved = false;
        if (newDir != m_navDir) {
            // 방향 변경 → 즉시 이동 + 딜레이 초기화
            m_navDir      = newDir;
            m_navRepeatMs = 0;
            moved = (newDir != 0);
        } else if (m_navDir != 0) {
            m_navRepeatMs += TICK;
            if (m_navRepeatMs >= REPEAT_INIT) {
                // 초기 딜레이 이후 일정 주기로 반복
                int phase = (m_navRepeatMs - REPEAT_INIT) % REPEAT_RATE;
                moved = (phase < TICK);
            }
        }

        // 셸이 목록이든 열린 메뉴든 알아서 처리한다 (키보드와 같은 경로)
        if (moved) m_shell->navigate(m_navDir < 0 ? Nav::Up : Nav::Down);

        // ★ 실행은 "패드의 물리 A 버튼"으로 한다.
        //   예전에는 libretro 인덱스 8 을 봤는데, 그 인덱스에 배정된 물리 버튼은
        //   배치/게임에 따라 달라서 스팀덱에서는 Y 버튼이 실행이 돼 버렸다.
        const bool aDown = m_gamepad && (m_gamepad->rawBits() & PAD_A);
        if (aDown && !m_navAWasDown) m_shell->navigate(Nav::Accept);
        m_navAWasDown = aDown;

        // B: 열린 메뉴 닫기 / X: 즐겨찾기 (스팀덱엔 키보드가 없다)
        const bool bDown = m_gamepad && (m_gamepad->rawBits() & PAD_B);
        if (bDown && !m_navBWasDown) m_shell->navigate(Nav::Back);
        m_navBWasDown = bDown;
        const bool xDown = m_gamepad && (m_gamepad->rawBits() & PAD_X);
        if (xDown && !m_navXWasDown) m_shell->navigate(Nav::Favorite);
        m_navXWasDown = xDown;

        // L / R: 커서를 게임 목록 ↔ 옵션 메뉴로 옮긴다 (L3 조합 핫키 중에는 쓰지 않는다)
        const bool l3 = m_gamepad && m_gamepad->hotkeyModifierHeld();
        const bool lbDown = m_gamepad && !l3 && (m_gamepad->rawBits() & PAD_LB);
        const bool rbDown = m_gamepad && !l3 && (m_gamepad->rawBits() & PAD_RB);
        if (lbDown && !m_navLBWasDown) m_shell->navigate(Nav::ZoneLeft);
        if (rbDown && !m_navRBWasDown) m_shell->navigate(Nav::ZoneRight);
        m_navLBWasDown = lbDown;
        m_navRBWasDown = rbDown;

        // Y: 검색어 입력 (화면 키보드가 뜨는 입력창)
        const bool yDown = m_gamepad && (m_gamepad->rawBits() & PAD_Y);
        if (yDown && !m_navYWasDown) m_shell->navigate(Nav::Search);
        m_navYWasDown = yDown;

        // ── LEFT / RIGHT D-패드 → 페이지 업/다운 (길게 누르면 반복) ──
#ifdef Q_OS_LINUX
        bool lDown = (dp >> 6) & 1;  // LR_LT = bit 6
        bool rDown = (dp >> 7) & 1;  // LR_RT = bit 7
#else
        bool lDown = (gState.rawKeys[6] != 0);  // libretro LEFT
        bool rDown = (gState.rawKeys[7] != 0);  // libretro RIGHT
#endif
        // 상/하와 동일한 반복 타이밍: 첫 입력 즉시 → 380ms 후 90ms 간격 반복
        int newHDir = (lDown && !rDown) ? -1 : (rDown && !lDown) ? 1 : 0;
        bool hMoved = false;
        if (newHDir != m_navHDir) {
            m_navHDir      = newHDir;
            m_navHRepeatMs = 0;
            hMoved = (newHDir != 0);
        } else if (m_navHDir != 0) {
            m_navHRepeatMs += TICK;
            if (m_navHRepeatMs >= REPEAT_INIT) {
                int phase = (m_navHRepeatMs - REPEAT_INIT) % REPEAT_RATE;
                hMoved = (phase < TICK);
            }
        }
        // 목록에서는 페이지 이동, 열린 메뉴에서는 ◀ ▶ 값 조절
        if (hMoved) m_shell->navigate(m_navHDir < 0 ? Nav::Left : Nav::Right);
    });
    m_uiNavTimer->start();

    connect(m_core, &LibretroCore::logMessage, this, &MainWindow::log);

    // 넷플레이 시그널
    connect(&gNetplay(), &NetplayManager::connected,       this, &MainWindow::onNetConnected);
    connect(&gNetplay(), &NetplayManager::disconnected,    this, &MainWindow::onNetDisconnected);
    connect(&gNetplay(), &NetplayManager::error,           this, &MainWindow::onNetError);
    connect(&gNetplay(), &NetplayManager::stateChanged,    this, &MainWindow::onNetStateChanged);
    connect(&gNetplay(), &NetplayManager::loadGameReceived,
            this, &MainWindow::onNetLoadGame);
    connect(&gNetplay(), &NetplayManager::readyReceived,   this, &MainWindow::onNetReady);
    connect(&gNetplay(), &NetplayManager::startReceived,   this, &MainWindow::onNetStart);
    connect(&gNetplay(), &NetplayManager::gameOverReceived,this, &MainWindow::onNetGameOver);
    // GGPO desync 감지
    connect(&gNetplay(), &NetplayManager::checksumReceived, this, &MainWindow::onNetChecksum);
    connect(&gNetplay(), &NetplayManager::resyncRequested,  this, &MainWindow::onNetResyncReq);

    // ── 하드 싱크 수신 (클라이언트 측) ────────────────────────
    // ★ 소켓 시그널 핸들러 안에서 m_core->unserialize() / m_core->run() 을
    //   절대 호출하지 않는다 — onEmuTimer 에서 안전하게 적용 (크래시 방지)
    connect(&gNetplay(), &NetplayManager::stateReceived,
            this, [this](quint32 frame, QByteArray data) {
        // 호스트는 수신 무시, 코어 미로드 상태도 무시
        if (gNetplay().isHost() || !m_core || !gState.gameLoaded) return;

        int sf  = static_cast<int>(frame);
        int cur = gState.frameCount;

        // [SYNC] 진단 — 30회마다 1회 (플러드 방지)
        static int s_recvCount = 0;
        if ((++s_recvCount % 30) == 1)
            qDebug("[SYNC] recv #%d frame=%d size=%d cur=%d (diff=%d)",
                   s_recvCount, sf, (int)data.size(), cur, cur - sf);

        // ★ 호스트 상태는 항상 권위(authoritative) — 절대 폐기하지 않는다.
        //   이전엔 sf 가 MAX_ROLLBACK 밖이면 폐기 → 한 번 격차가 벌어지면
        //   영원히 거부 → drift 보정도 멈춤 → 영구 desync(죽음의 소용돌이).
        //   이제는 큐의 더 오래된/중복 상태만 거르고, 최신 상태는 무조건 보관.
        if (m_pendingSyncSf >= sf) return;

        // 데이터만 큐에 저장 — 실제 적용은 onEmuTimer 에서
        m_pendingSyncSf  = sf;
        m_pendingSyncCur = cur;
        m_pendingSyncData = std::move(data);
    });

    // 에뮬 타이머 (1ms → AFL 로 조절)
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onEmuTimer);

    // 커서 미리 생성 (런타임 allocation 없이 빠른 전환 — Wayland 렉 방지)
    {
        QPixmap curPx(":/assets/mousepoint.png");
        m_customCursor = curPx.isNull() ? QCursor(Qt::ArrowCursor) : QCursor(curPx, 0, 0);
        m_blankCursor  = QCursor(Qt::BlankCursor);
        // setOverrideCursor 대신 widget-level setCursor 사용
        // → Wayland에서 포인터 진입 시점에만 실제 갱신, 렌더링 루프 간섭 없음
        setCursor(m_customCursor);
    }

    // UI 빌드
    buildUi();

    // 코어 설정
    QString base = AppSettings::baseDir();
    // BIOS 파일은 ROM 폴더에 함께 두는 것이 일반적 — ROM 경로를 우선 사용
    m_core->setSystemDir(gSettings.romPath.isEmpty() ? base : gSettings.romPath);
    m_core->setSaveDir(gSettings.savePath);

    QString coreLib = base + "/"
#ifdef _WIN32
        + "fbneo_libretro.dll";
#else
        + "fbneo_libretro.so";
#endif
    if (QFile::exists(coreLib)) {
        if (m_core->load(coreLib)) log("✔ 코어 로드 완료");
        else                       log("✖ 코어 로드 실패: " + coreLib);
    } else {
        log("⚠ 코어 없음: " + coreLib);
    }

    scanRoms();
    // NeoRageX 부팅 시퀀스 시작 (테두리를 펜으로 그리고 내용이 차례로 등장)
    if (gSettings.showIntro) {
        m_stack->setCurrentIndex(3);
        m_intro->setFocus();
        m_intro->start(IntroSplash::findVideo(base));
    } else {
        m_shell->setFocus();
        m_shell->boot();
    }
    m_audio->init(gSettings.audioSampleRate, gSettings.audioBufferMs);
    applyResolvedSoundMode();   // 저장해 둔 사운드 모드 복원

    // 터보 설정 복원
    gState.turboPeriod = gSettings.turboPeriod;
    for (const QString& s : gSettings.turboButtons.split(',', Qt::SkipEmptyParts)) {
        bool ok; int idx = s.trimmed().toInt(&ok);
        if (ok && idx >= 0 && idx < 16) gState.turboBtns[idx] = true;
    }

    // 게임패드 매핑 복원
    if (!gSettings.winmmMapping.isEmpty())
        m_gamepad->setWinMMMapping(gSettings.winmmMapping);

    // 게임패드 시작
    m_gamepad->start();

    // 앱 전역 이벤트 필터 (탭키 전환, 마우스 클릭음 등)
    qApp->installEventFilter(this);

    // ── 마우스 클릭음 (원본 NeoRageX 느낌) ────────────────────
    loadClickSound();

    // ── 화면보호기 대기 타이머 (5분 무조작) ────────────────────
    //   1초마다 확인해서 300초 연속 무조작이면 켠다. 매 틱마다 "지금 켜도 되는
    //   상태인지"를 다시 보므로 게임 실행·리매핑 등 화면 전환에 자동으로 따라간다.
    m_ssIdle = new QTimer(this);
    m_ssIdle->setInterval(1000);
    connect(m_ssIdle, &QTimer::timeout, this, [this]{
        if (m_ssActive) return;
        const bool eligible = m_stack && m_stack->currentIndex() == 0
                              && !gState.gameLoaded && !gState.isPaused
                              && !m_captureActive && !gNetplay().playing();
        if (!eligible) { m_ssIdleTicks = 0; return; }
        if (++m_ssIdleTicks >= kScreensaverIdleSec) { m_ssIdleTicks = 0; startScreensaver(); }
    });
    m_ssIdle->start();

    // 마우스 커서 자동 숨김 타이머 (3초 비입력 시 숨김)
    m_cursorTimer = new QTimer(this);
    m_cursorTimer->setSingleShot(true);
    m_cursorTimer->setInterval(3000);
    connect(m_cursorTimer, &QTimer::timeout, this, &MainWindow::hideCursor);

    // Netplay READY 재전송 타이머 (300ms 간격, 상대방 READY 수신 전까지)
    m_npReadyRetry = new QTimer(this);
    m_npReadyRetry->setInterval(300);
    connect(m_npReadyRetry, &QTimer::timeout, this, [this] {
        if (gNetplay().netState() == NetplayManager::State::Ready) {
            gNetplay().sendReady();
        } else {
            m_npReadyRetry->stop();
        }
    });

}

MainWindow::~MainWindow() {}

// ════════════════════════════════════════════════════════════
//  buildUi — 최상위 레이아웃: 탭 위젯 + 게임 캔버스
// ════════════════════════════════════════════════════════════
void MainWindow::buildUi() {
    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    // ── GUI 화면 (index 0) — BgWidget으로 배경 이미지 렌더링 ──
    m_guiWidget = new BgWidget;
    m_guiWidget->setObjectName("guiRoot");
    m_guiWidget->setStyleSheet(
        "QWidget{background:transparent;}"
        "QListWidget{background:rgba(10,15,40,120);}"
        "QListWidget::item:selected{background:rgba(0,30,120,200);}"
        "QListWidget::item:hover{background:rgba(0,20,80,150);}");

    QVBoxLayout* guiV = new QVBoxLayout(m_guiWidget);
    guiV->setContentsMargins(0, 0, 0, 0);
    guiV->setSpacing(0);

    buildPreviewPlayers();
    applyPadProfiles();     // 이미 연결돼 있는 패드에 프로필·배정을 적용한다

    buildShell();
    guiV->addWidget(m_shell);

    m_stack->addWidget(m_guiWidget);

    // ── 게임 캔버스 (index 1) ───────────────────────────────
    //   ★ 캔버스(QOpenGLWidget)는 창에 붙는 순간 Qt 가 창 전체를 GPU(RHI)로 합성하기 시작한다.
    //     일부 PC(내장 그래픽·구형 드라이버)에서는 그 합성이 실패해 메뉴까지 검은/빨간 화면이 된다.
    //     그래서 게임을 처음 켤 때까지는 자리만 차지하는 빈 위젯을 두고, 그때 캔버스를 끼운다.
    //     (메뉴는 평범한 소프트웨어 그리기로 나오므로 어떤 PC에서도 보인다)
    //   VIDEO OPTIONS → RENDERER 가 SOFTWARE 면 OpenGL 없이 CPU 로 그리는 SoftCanvas 를 쓴다.
    if (gSettings.videoRenderer == QLatin1String("software")) {
        auto* sc = new SoftCanvas;
        connect(sc, &SoftCanvas::glLogMessage, this, &MainWindow::log);
        m_canvas = sc;
        qDebug("[gfx] 렌더러: SOFTWARE (CPU)");
    } else {
        auto* gc = new GameCanvas;
        connect(gc, &GameCanvas::glLogMessage, this, &MainWindow::log);
        m_canvas = gc;
        qDebug("[gfx] 렌더러: OPENGL");
    }
    m_canvasW = m_canvas->widget();
    m_canvasW->setMouseTracking(true);   // 버튼 안 눌러도 마우스 이동 감지
    m_canvasHolder = new QWidget;
    m_stack->addWidget(m_canvasHolder);

    buildShellMenu();   // 캔버스·오디오·치트가 모두 준비된 뒤에 만든다

    // ── 화면보호기 (index 2) ────────────────────────────────
    //   메뉴에서 일정 시간 조작이 없으면 프리뷰 영상을 무작위로 전체화면 재생.
    //   같은 화면이 오래 떠 있어 생기는 번인을 줄이기 위한 기능.
    //   두 플랫폼 모두 디코딩한 프레임을 라벨에 직접 그린다 (onPreviewFrame).
    m_ssPage = new QWidget;
    m_ssPage->setStyleSheet("background:#000000;");
    m_ssPage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_ssPage->setMinimumSize(0, 0);
    auto* ssLay = new QVBoxLayout(m_ssPage);
    ssLay->setContentsMargins(0, 0, 0, 0);
    ssLay->setSpacing(0);
    m_ssLabel = new QLabel;
    m_ssLabel->setAlignment(Qt::AlignCenter);
    m_ssLabel->setStyleSheet("background:#000000;");
    m_ssLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_ssLabel->setMinimumSize(0, 0);
    ssLay->addWidget(m_ssLabel);
    m_stack->addWidget(m_ssPage);

    // ── 시작 오프닝 (index 3) ────────────────────────────────
    m_intro = new IntroSplash;
    m_stack->addWidget(m_intro);
    // ── FRAME LAB (index 4) ─────────────────────────────────
    m_lab = new FrameLab;
    m_stack->addWidget(m_lab);
    connect(m_lab, &FrameLab::closed, this, [this] {
        m_stack->setCurrentIndex(0);
        m_shell->setFocus();
    });
    connect(m_intro, &IntroSplash::finished, this, [this] {
        m_stack->setCurrentIndex(0);
        m_shell->setFocus();
        m_shell->boot();                 // 오프닝이 끝나면 메뉴 부팅(테두리 그리기)이 시작된다
    });

    // ── 1P↔2P 게임 화면 오버레이 ─────────────────────────────
    m_playerOverlay = new QLabel(m_canvasW);
    m_playerOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_playerOverlay->setStyleSheet(
        "QLabel{"
        "  background:rgba(0,60,0,200);"
        "  color:#44ff88;"
        "  font-family:'Courier New';"
        "  font-size:13px;"
        "  font-weight:bold;"
        "  padding:5px 12px;"
        "  border:1px solid #00cc44;"
        "  border-radius:5px;"
        "}");
    m_playerOverlay->hide();
    m_playerOverlay->raise();

    m_overlayTimer = new QTimer(this);
    m_overlayTimer->setSingleShot(true);
    m_overlayTimer->setInterval(1500);
    connect(m_overlayTimer, &QTimer::timeout, this, [this]{
        m_playerOverlay->hide();
    });

    m_canvasW->installEventFilter(this);
}


// ════════════════════════════════════════════════════════════
//  NeoRageX 0.6b 메뉴 셸
// ════════════════════════════════════════════════════════════
//  GUI 화면을 셸이 통째로 그린다 (게임 목록, 옵션 메뉴, 프리뷰, 이벤트 로그).
void MainWindow::buildShell() {
    m_shell = new NeoRageXShell;
    m_shell->setPointerCursor(m_customCursor);      // 제공받은 포인터 그림 (mousepoint.png)

    // 배경 아트워크 — 기존 background.png 를 그대로 쓴다
    {
        QImage bg(QStringLiteral(":/assets/background.png"));
        if (!bg.isNull()) m_shell->setBackdrop(bg);
    }

    connect(m_shell, &NeoRageXShell::launchRequested, this,
            [this](const QString&) { launchGame(); });

    connect(m_shell, &NeoRageXShell::importRequested, this, [this] {
        log(QStringLiteral("ROM 폴더를 다시 읽습니다..."));
        scanRoms();
    });

    connect(m_shell, &NeoRageXShell::exitRequested, this, [this] { close(); });

    // 검색: 글자를 칠 때마다 목록을 다시 거르면 선택 게임이 바뀌며 로그가 쏟아지므로
    //   입력이 잠시 멈춘 뒤에 한 번만 적용한다.
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(150);
    connect(m_searchDebounce, &QTimer::timeout, this, [this] { filterRoms(); });
    connect(m_shell, &NeoRageXShell::searchChanged, this, [this](const QString& t) {
        m_searchText = t;
        m_searchDebounce->start();
    });
    // 게임패드(Y): OS 입력창 (스팀덱에서는 화면 키보드가 뜬다)
    connect(m_shell, &NeoRageXShell::searchRequested, this, [this] {
        bool ok = false;
        const QString t = QInputDialog::getText(this, QStringLiteral("SEARCH"),
            isEn() ? QStringLiteral("Game name or ROM name") : QStringLiteral("게임 이름 또는 롬 이름"),
            QLineEdit::Normal, m_searchText, &ok);
        if (!ok) return;
        m_searchText = t.trimmed();
        m_shell->setSearchText(m_searchText);
        filterRoms();
    });

    // 셸에서 고른 행 → 선택 게임
    auto pick = [this](int index) {
        if (index < 0 || index >= m_rows.size()) return;
        selectGame(m_rows.at(index).rom);
        syncShellPreview();
    };
    connect(m_shell, &NeoRageXShell::gameSelected, this,
            [pick](const QString&, int i) { pick(i); });
    connect(m_shell, &NeoRageXShell::gameHighlighted, this,
            [pick](const QString&, int i) { pick(i); });

    // 즐겨찾기 토글 (목록에서 Space)
    connect(m_shell, &NeoRageXShell::favoriteToggled, this, [this](int index) {
        if (index < 0 || index >= m_rows.size()) return;
        toggleFavorite(m_rows.at(index).rom);
    });

    // 필터 단추 (ALL / FAV / NOFAV / 기종)
    connect(m_shell, &NeoRageXShell::filterChosen, this, [this](const QString& fid) {
        if      (fid == QLatin1String("all"))   { m_glFilter = 0; m_hwFilter.clear(); }
        else if (fid == QLatin1String("fav"))   { m_glFilter = 1; m_hwFilter.clear(); }
        else if (fid == QLatin1String("nofav")) { m_glFilter = 2; m_hwFilter.clear(); }
        else if (fid.startsWith(QLatin1String("hw:"))) {
            m_glFilter = 0;
            m_hwFilter = fid.mid(3);
        }
        filterRoms();
    });
}

// 필터 단추 줄을 셸에 넘긴다.
//   기종 목록은 실제로 보유한 것만, 개수 없이 짧은 이름으로 만든다.
void MainWindow::syncShellFilters() {
    if (!m_shell) return;
    using Tab = NeoRageXShell::FilterTab;
    QVector<Tab> tabs;
    tabs << Tab{ QStringLiteral("all"),   QStringLiteral("ALL")   }
         << Tab{ QStringLiteral("fav"),   QStringLiteral("FAV")   }
         << Tab{ QStringLiteral("nofav"), QStringLiteral("NOFAV") };

    QHash<QString, int> hwCount;
    for (const auto& pair : m_allRoms)
        ++hwCount[gameHardwareGroup(gameHardwareOf(pair.second))];
    for (const auto& d : gameHardwareList()) {
        const QString id = QString::fromLatin1(d.id);
        if (hwCount.value(id, 0) <= 0) continue;
        tabs << Tab{ QStringLiteral("hw:") + id,
                     QString::fromLatin1(d.label).toUpper() };
    }

    QString cur = QStringLiteral("all");
    if (!m_hwFilter.isEmpty())    cur = QStringLiteral("hw:") + m_hwFilter;
    else if (m_glFilter == 1)     cur = QStringLiteral("fav");
    else if (m_glFilter == 2)     cur = QStringLiteral("nofav");
    m_shell->setFilterTabs(tabs, cur);
}

// 화면에 보일 목록(m_rows)을 셸로 옮긴다.
void MainWindow::syncShellList() {
    if (!m_shell) return;
    QStringList labels;
    QVector<bool> favs;
    labels.reserve(m_rows.size());
    favs.reserve(m_rows.size());
    int sel = -1;
    for (int r = 0; r < m_rows.size(); ++r) {
        labels << m_rows[r].label;
        favs   << m_rows[r].fav;
        if (m_rows[r].rom == m_selectedGame) sel = r;
    }
    m_shell->setGames(labels, favs);
    if (sel >= 0) m_shell->setSelectedIndex(sel);
    syncShellFilters();
    syncShellPreview();
}
// 선택한 게임의 프리뷰 이미지를 셸에 넘긴다 (previews/<rom>.png 등)
void MainWindow::syncShellPreview() {
    if (!m_shell) return;
    if (m_selectedGame.isEmpty()) { m_shell->setPreview(QImage()); return; }
    static const char* exts[] = { ".png", ".jpg", ".jpeg", ".bmp", ".gif" };
    for (const char* e : exts) {
        const QString path = gSettings.previewPath + QLatin1Char('/')
                           + m_selectedGame + QLatin1String(e);
        if (QFileInfo::exists(path)) {
            QImage img(path);
            if (!img.isNull()) { m_shell->setPreview(img); return; }
        }
    }
    m_shell->setPreview(QImage());
}


// OPTIONS 메뉴를 만든다. 카테고리 하나 = 페이지 하나.
//   페이지는 ShellHost 로만 앱과 이야기한다. MainWindow 의 나머지를 만지지 않는다.
void MainWindow::buildShellMenu() {
    ShellHost h;
    h.canvas = m_canvas;
    h.softwareRenderer = [this]() { return qobject_cast<SoftCanvas*>(m_canvasW) != nullptr; };
    h.audio  = m_audio;
    h.cheat  = m_cheat;
    h.window = this;

    h.log     = [this](const QString& s) { log(s); };
    h.english = [this]() { return isEn(); };

    h.loadedGame   = [this]() { return m_loadedGame; };
    h.selectedGame = [this]() { return m_selectedGame; };
    h.platformOf   = [this](const QString& rom) { return gamePlatform(rom); };

    h.applyBezel        = [this]() { applyBezel(); };
    h.applySoundMode    = [this]() { applyResolvedSoundMode(); };
    h.applyLiveSettings = [this]() { applyLiveSettings(); };
    h.applyPaths        = [this]() { applyPathSettings(); };
    h.toggleFullscreen  = [this]() { toggleFullscreen(); };
    h.isFullscreen      = [this]() { return fullscreenNow(); };
    h.openShaderParams  = [this]() { openShaderParams(); };
    h.toggleLanguage    = [this]() { toggleLanguage(); };
    h.stopGame          = [this]() { stopGame(); };
    h.resetGame         = [this]() { resetGame(); };
    h.swapPlayers       = []() { return gState.swapPlayers; };
    h.toggleSwap        = [this]() { toggleSwapPlayers(); };
    h.tateLabel         = [this]() { return tateLabel(); };
    h.toggleTate        = [this]() { toggleTate(); };

    h.stateSlot       = [this]() { return m_stateSlot; };
    h.setStateSlot    = [this](int s) { m_stateSlot = s; };
    h.saveState       = [this]() { saveState(m_stateSlot); };
    h.loadState       = [this]() { loadState(m_stateSlot); };
    h.takeScreenshot  = [this]() { takeScreenshot(); };
    h.openFrameLab    = [this]() { openFrameLab(); };
    h.toggleRecording = [this]() { toggleRecording(); };
    h.savePreviewShot = [this]() { savePreviewShot(); };
    h.togglePreviewRecord = [this]() { togglePreviewRecord(); };
    h.isRecording     = []() { return gState.isRecording.load(); };

    {
        ControlsApi& k = h.controls;
        k.bindings     = [this](int d) { return controlSlots(d); };
        k.remap        = [this](int d, int s) { remapControl(d, s); };
        k.resetDevice  = [this](int d) { resetControlDevice(d); };
        k.pads         = [this]() {
            QVector<ControlPad> v;
            if (!m_gamepad) return v;
            for (int i = 0; i < 4; ++i)
                if (m_gamepad->padPresent(i))
                    v.append(ControlPad{i, m_gamepad->padName(i), m_gamepad->padPlayer(i)});
            return v;
        };
        k.targetPad    = [this]() { return m_remapPad; };
        k.setTargetPad = [this](int i) {
            m_remapPad = i;
            log(QString("🎮 리매핑 대상: %1").arg(m_gamepad ? m_gamepad->padName(i) : QString()));
        };
        k.setPadPlayer = [this](int i, int player) {
            if (!m_gamepad) return;
            const QString name = m_gamepad->padName(i);
            m_gamepad->setPadPlayer(i, player);
            if (!name.isEmpty()) { gSettings.padAssign[name] = player; gSettings.save(); }
            log(QString("🎮 %1 → %2").arg(name,
                    player == 0 ? QString(isEn() ? "Off" : "사용 안 함") : QString("%1P").arg(player)));
        };
        k.sourceText       = [this]() { return padSourceText(); };
        k.savePlatform     = [this]() { saveControlsForPlatform(); };
        k.platformLabel    = [this]() { return platformSaveLabel(); };
        k.forgetGame       = [this]() { forgetGameControls(); };
        k.clearPadProfiles = [this]() { clearPadScope("all"); };
        k.inputMode        = []() { return gSettings.inputMode; };
        k.setInputMode     = [this](const QString& m) {
            gSettings.inputMode = m;
            gSettings.save();
            log("게임패드 모드: " + m);
        };
        k.turboButtons   = [this]() { return turboButtons(); };
        k.turbo          = [](int idx) { return gState.turboBtns.value(idx, false); };
        k.setTurbo       = [this](int idx, bool on) { gState.turboBtns[idx] = on; saveTurboSettings(); };
        k.turboPeriod    = []() { return gState.turboPeriod; };
        k.setTurboPeriod = [this](int v) { gState.turboPeriod = v; saveTurboSettings(); };
    }

    {
        NetApi& n = h.net;
        n.state       = [this]() { return netState(); };
        n.host        = [this]() { netHost(); };
        n.join        = [this]() { netJoin(); };
        n.start       = [this]() { netplayStartGame(); };
        n.disconnect  = [this]() { netDisconnect(); };
        n.setPort     = [this](int p) { gSettings.netplayPort = p; gSettings.save(); };
        n.setDelay    = [this](int d) { m_netDelay = d; gSettings.netplayInputDelay = d; gSettings.save(); };
        n.setJoinCode = [this](const QString& s) { m_net.joinCode = s.trimmed().toUpper(); };
        n.setJoinIp   = [this](const QString& s) { m_net.joinIp = s.trimmed(); };
        n.copyCode    = [this]() { netCopyRoomCode(); };
        n.setRelay    = [this](const QString& s) { netSetRelay(s); };
    }

    {
        VideoApi& v = h.video;
        v.setShader       = [this](const QString& p) { return applyShaderFile(p); };
        v.clearShader     = [this]() { clearShaderFile(); };
        v.bezelKey        = [this](const QString& sc) { return bezelKeyFor(sc); };
        v.bezelScopeLabel = [this](const QString& sc) { return bezelScopeLabel(sc); };
        v.bezelInfo       = [this]() { return bezelInfoText(); };
        v.assignBezel     = [this](const QString& k, const QString& val) { assignBezel(k, val); };
        v.clearBezel      = [this](const QString& k) { clearBezelAssign(k); };
    }

    m_netDelay = gSettings.netplayInputDelay;
    fetchPublicIp();

    m_menu = new ShellMenu(m_shell, this);

    // 등록 순서 = 화면의 카테고리 순서
    m_menu->addPage(makeControlsPage(h),                       "CONTROLS");
    m_menu->addPage(makeDirectoriesPage(h),                    "DIRECTORIES");
    m_menu->addPage(makeVideoPage(h),                          "VIDEO OPTIONS");
    m_menu->addPage(makeAudioPage(h),                          "AUDIO OPTIONS");
    m_menu->addPage(makeMachinePage(h),                        "MACHINE SETTINGS");
    m_menu->addPage(makeShotsPage(h),                          "SHOTS FACTORY");
    m_menu->addPage(makeCheatsPage(h),                         "CHEATS");
    m_menu->addPage(makeNetplayPage(h),                        "MULTIPLAYER");
    m_menu->addPage(makeSystemPage(h),                         "SYSTEM");
    m_menu->setKoreanProvider([]() { return !isEn(); });
    m_menu->install();
    m_menu->retranslate();
}

// 프리뷰 영상 재생기.
//   PREVIEW 박스는 셸이 그린다. 여기서는 영상을 디코딩해 프레임을 셸(또는 화면보호기)로
//   넘기기만 한다. Linux 는 자체 소프트웨어 디코더, Windows 는 QMediaPlayer 를 쓰고
//   둘 다 같은 onPreviewFrame() 으로 모인다.
void MainWindow::buildPreviewPlayers() {
#if HAVE_FFMPEG
    // Linux(스팀덱): Qt 멀티미디어 대신 자체 소프트웨어 디코더 사용.
    //   Qt FFmpeg 백엔드가 VAAPI/Vulkan 하드웨어 디코더를 잡다가 죽는 문제를
    //   근본 차단한다(하드웨어 탐색 경로 자체가 없음). 프리뷰는 무음 재생.
    m_previewVideo = new PreviewVideo(this);
    connect(m_previewVideo, &PreviewVideo::frameReady, this,
            [this](const QImage& img) { onPreviewFrame(img); });
    connect(m_previewVideo, &PreviewVideo::failed, this, [this](const QString& why){
        log("⚠ 프리뷰 영상 재생 불가: " + why);
    });
    // 영상이 끝나면 프리뷰 이미지로 돌아가고, 다시 재생하지는 않는다.
    connect(m_previewVideo, &PreviewVideo::finished, this, [this]{
        if (m_ssActive) { playRandomScreensaverVideo(); return; }  // 다음 영상 무작위
        m_previewVideoDone = true;          // 이 게임은 이미 한 번 재생함
        if (!m_selectedGame.isEmpty()) loadPreview(m_selectedGame);
    });
#endif
    m_mediaPlayer = new QMediaPlayer(this);
    m_videoSink   = new QVideoSink(this);
    m_mediaPlayer->setVideoSink(m_videoSink);
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& f) {
        if (f.isValid()) onPreviewFrame(f.toImage());
    });
    { auto* ao = new QAudioOutput(this); m_mediaPlayer->setAudioOutput(ao); }
    // 영상이 끝나면 프리뷰 이미지로 복귀
    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus s){
        if (s != QMediaPlayer::EndOfMedia) return;
        if (m_ssActive) { playRandomScreensaverVideo(); return; }  // 다음 영상 무작위
        if (!m_selectedGame.isEmpty()) loadPreview(m_selectedGame);
    });
    // 재생 오류는 조용히 죽지 않고 이벤트 로그로 알린다.
    //   (멀티미디어 백엔드 플러그인 누락/코덱 문제 진단용)
    connect(m_mediaPlayer, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& msg){
        log("⚠ 프리뷰 영상 재생 오류: " + msg);
        if (m_mediaPlayer) m_mediaPlayer->stop();
        // 화면보호기 중이면 검은 화면으로 멈추지 않게 해제한다
        if (m_ssActive) { stopScreensaver(); return; }
        if (!m_selectedGame.isEmpty()) syncShellPreview();      // 이미지로 복귀
    });
    m_previewVidTimer = new QTimer(this);
    m_previewVidTimer->setSingleShot(true);
    m_previewVidTimer->setInterval(3000);
    connect(m_previewVidTimer, &QTimer::timeout, this, [this]{ loadPreviewVideo(m_selectedGame); });
}

// 재생 중인 프리뷰 영상의 한 프레임. 화면보호기 중이면 전체화면 페이지에, 아니면 셸의
//   PREVIEW 박스에 그린다.
void MainWindow::onPreviewFrame(const QImage& img) {
    if (img.isNull()) return;
    if (m_ssActive && m_ssLabel) {
        const QSize t = m_ssLabel->size();
        if (t.width() > 4 && t.height() > 4)
            m_ssLabel->setPixmap(QPixmap::fromImage(img).scaled(
                t, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        return;
    }
    if (m_shell) m_shell->setPreview(img);
}

// 게임을 멈추고 메뉴로 돌아온다 (STOP GAME).
void MainWindow::stopGame() {
    if (!gState.gameLoaded && !gState.isPaused) return;
    m_timer->stop();
    gState.isPaused = false;
    if (m_core) m_core->unloadGame();
    m_loadedGame.clear();
    leaveGameScreen();
    log("■ 게임 종료");
}

void MainWindow::resetGame() {
    if (m_core) m_core->reset();
}

// TATE(세로형 화면 회전) 상태를 짧은 문구로.
QString MainWindow::tateLabel() const {
    if (!m_canvas) return QStringLiteral("AUTO");
    switch (m_canvas->rotation()) {
    case  1: return QStringLiteral("90 CCW");
    case  3: return QStringLiteral("90 CW");
    case  0: return QStringLiteral("OFF");
    default: return QStringLiteral("AUTO");
    }
}
// ════════════════════════════════════════════════════════════
//  OPTIONS 패널 페이지 빌더들
// ════════════════════════════════════════════════════════════

// ── 액션 목록 — 코어가 알려준 정의로 만든다 ─────────────────
//   ★ libretro 인덱스의 뜻은 게임마다 다르다. 그래서 이름을 코드에 고정하면
//     반드시 어긋난다(실제로 UI 이름과 인게임 동작이 따로 놀았다).
//     게임이 실행 중이면 코어 정의로 이름을 만들고, 6버튼 격투는 인덱스가
//     아니라 "의미"(약손/중손/…)를 행 id 로 쓴다.
struct CtrlAction {
    int     id;                 // 행 id (일반 버튼 = libretro 인덱스, 6버튼 격투 = 의미 id)
    QString name;               // 컨트롤 표에 보이는 이름
    QString shortName;          // 터보 표 같은 데 쓰는 짧은 이름 ("A", "약손" ...)
    int     libIdx = -1;        // 실제 libretro 인덱스 (얼굴 버튼일 때)
};

// 얼굴 버튼의 표시 순서: A B C D 다음에 동시 입력 매크로.
//   libretro 인덱스 순으로 늘어놓으면 A(0) C(1) B(8) D(9) 로 뒤섞여 보인다.
static int faceRank(int id) {
    static const int order[] = {0, 8, 1, 9, 10, 11, 12, 13};
    for (int i = 0; i < 8; ++i) if (order[i] == id) return i;
    return 100 + id;
}
// CPS 2~3버튼 게임에서 얼굴 버튼에 붙이는 이름 (인덱스 0,8,1,9 = A,B,C,D)
static QString faceLetter(int id) {
    switch (id) {
    case 0: return QStringLiteral("A");
    case 8: return QStringLiteral("B");
    case 1: return QStringLiteral("C");
    case 9: return QStringLiteral("D");
    default: return QString();
    }
}
static bool isChordName(const QString& n) {
    return n.contains(QLatin1String("Buttons"), Qt::CaseInsensitive) || n.contains(QLatin1String("3x"));
}

// 6버튼 격투의 인덱스 찾기.
//   코어가 "Weak Punch" 같은 이름을 보내면 그대로 쓴다. 이름이 다르게 오는 기판(CPS 체인저, CPS3,
//   일부 클론)이라도 6버튼 격투로 분류된 롬이고 여섯 인덱스가 다 있으면 FBNeo 의 표준 배치를 쓴다:
//   약손=1 중손=9 강손=10 / 약발=0 중발=8 강발=11.
static CoreSixButtons resolveSixButtons(const QHash<int,QString>& d, bool romIsSix) {
    CoreSixButtons cb = parseCoreSixButtons(d);
    if (cb.valid || !romIsSix) return cb;
    for (int id : {0, 1, 8, 9, 10, 11})
        if (!d.contains(id)) return cb;
    cb.lp = 1; cb.mp = 9; cb.hp = 10; cb.lk = 0; cb.mk = 8; cb.hk = 11;
    cb.valid = true;
    return cb;
}

// 게임 실행 전(메뉴)에 쓰는 표.
//   이때는 코어 정의가 없어 버튼의 진짜 뜻을 알 수 없다. 그래서 의미(약손 등)로
//   보여주지 않고 인덱스 그대로 보여준다 — 저장된 키보드 설정도 그대로 보인다.
//   게임을 실행하면 코어 정의로 실제 이름이 채워진다.
static QVector<CtrlAction> fallbackActions(bool /*six*/, bool en) {
    QVector<CtrlAction> v;
    v << CtrlAction{4, "UP", "UP"} << CtrlAction{5, "DOWN", "DOWN"}
      << CtrlAction{6, "LEFT", "LEFT"} << CtrlAction{7, "RIGHT", "RIGHT"}
      << CtrlAction{0, "BTN 1", "A", 0} << CtrlAction{8, "BTN 2", "B", 8}
      << CtrlAction{1, "BTN 3", "C", 1} << CtrlAction{9, "BTN 4", "D", 9}
      << CtrlAction{10, "BTN 5", "CD", 10} << CtrlAction{11, "BTN 6", "AB", 11}
      << CtrlAction{3, "START", "START"}
      << CtrlAction{2, en ? "SELECT / COIN" : "SELECT / 코인", "SELECT"};
    return v;
}

// 코어 정의(있으면)로 실제 액션 목록을 만든다
//   cps: CPS 기판 게임이면 true. CPS 는 두 버튼 동시 입력 매크로(CD, AB, 3연타)가 먹지 않으므로
//        표에서 뺀다. 6버튼 격투는 의미(약손…)로, 2~3버튼 게임은 그냥 A B C 로 보여 준다.
static QVector<CtrlAction> buildCtrlActions(PadLayout lay, bool en, bool cps) {
    const QHash<int,QString>& d = gState.inputDesc;
    const CoreSixButtons cb = resolveSixButtons(d, lay == PadLayout::SixButton);
    if (d.isEmpty()) return fallbackActions(lay == PadLayout::SixButton, en);

    QVector<CtrlAction> v;
    v << CtrlAction{4, "UP", "UP"} << CtrlAction{5, "DOWN", "DOWN"}
      << CtrlAction{6, "LEFT", "LEFT"} << CtrlAction{7, "RIGHT", "RIGHT"};

    QSet<int> used = {2, 3, 4, 5, 6, 7};
    if (cb.valid) {
        // 6버튼 격투 — 약손 중손 강손 / 약발 중발 강발 순서로 의미를 보여 주고 실제 인덱스를 괄호로 붙인다
        const int idx[SEM_COUNT] = {cb.lp, cb.mp, cb.hp, cb.lk, cb.mk, cb.hk};
        for (int i = 0; i < SEM_COUNT; ++i) {
            v << CtrlAction{padSemId(i),
                            QString("%1  (%2)").arg(padSemName(i, en)).arg(idx[i]),
                            en ? padSemName(i, en).section(QLatin1String("  "), 0, 0)      // "LP"
                               : padSemName(i, en).section(QLatin1String("  "), -1),      // "약손"
                            idx[i]};
            used.insert(idx[i]);
        }
    }
    // 나머지 버튼: A B C D 순서로, 그 밖에는 인덱스 순
    QList<int> ids = d.keys();
    std::sort(ids.begin(), ids.end(), [](int x, int y) { return faceRank(x) < faceRank(y); });
    for (int id : ids) {
        if (used.contains(id) || id >= 16) continue;
        const QString core = d.value(id);
        if ((cps || cb.valid) && isChordName(core)) continue;   // CPS·6버튼 격투: 동시 입력 매크로는 뺀다
        const QString letter = cps ? faceLetter(id) : QString();
        if (!letter.isEmpty()) {                                // CPS 2~3버튼: 그냥 A B C
            v << CtrlAction{id, letter, letter, id};
            continue;
        }
        QString shortName = core;
        shortName.remove(QRegularExpression(QStringLiteral("^Buttons?\\s+")));
        v << CtrlAction{id, QString("%1  (%2)").arg(core).arg(id), shortName, id};
    }
    v << CtrlAction{3, "START", "START"} << CtrlAction{2, en ? "SELECT / COIN" : "SELECT / 코인", "SELECT"};
    return v;
}

// 터보를 걸 수 있는 버튼 (libretro 인덱스, 이름). 컨트롤 표와 같은 이름·순서를 쓴다.
QVector<QPair<int,QString>> MainWindow::turboButtons() const {
    QVector<QPair<int,QString>> out;
    const bool cps = gamePlatform(m_loadedGame) == QLatin1String("cps");
    const QVector<CtrlAction> acts = buildCtrlActions(
        m_gamepad ? m_gamepad->padLayout() : PadLayout::Standard, isEn(), cps);
    for (const CtrlAction& a : acts) {
        if (a.libIdx < 0) continue;
        // 방향·START·SELECT 는 터보 대상이 아니다. 얼굴 버튼(0,1,8,9,10,11)과 6버튼 격투의 여섯 개만.
        const int i = a.libIdx;
        if (i == 0 || i == 1 || i == 8 || i == 9 || i == 10 || i == 11)
            out.append({i, a.shortName});
    }
    return out;
}

// 버튼 이름은 GamepadManager::buttonName() 하나만 쓴다.
//   (플랫폼마다 원시 비트 의미가 달라 여기서 따로 표를 두면 반드시 어긋난다)
static QString xinputBtnName(int bitmask) {
    return GamepadManager::buttonName(bitmask);
}

// ── CONTROLS 셸 페이지 창구 ──────────────────────────────────
//   표를 그리는 일은 셸 페이지(ShellPageControls)가 하고, 여기는 데이터와 저장만 다룬다.
//   저장·해석 로직(PadMapping/PadRawBits/ArcadeLayout)은 그대로다.
void MainWindow::refreshControlsUi() {
    if (m_menu) m_menu->refreshOpen();
}

QVector<ControlSlot> MainWindow::controlSlots(int dev) {
    QVector<ControlSlot> out;
    const QVector<CtrlAction> acts = buildCtrlActions(
        m_gamepad ? m_gamepad->padLayout() : PadLayout::Standard, isEn(),
        gamePlatform(m_loadedGame) == QLatin1String("cps"));

    // 이 장치의 매핑 (키/버튼 → libretro id)
    QHash<int,int> mapping;
    if (dev == 0) {
        mapping = m_keymap;
    } else if (dev == 1 && m_gamepad) {
        // ★ "대상"으로 고른 패드의 표를 보여 준다 (장치마다 번호 체계가 다르다).
        //   패드가 없으면 공용 표가 아니라 배치 기본값을 미리보기로 보여 준다.
        mapping = m_gamepad->padPresent(m_remapPad) ? m_padUiMap[m_remapPad]
                                                    : layoutDefaultMap();
    } else if (dev == 2 && m_gamepad) {
        mapping = m_gamepad->getWinMMMapping();
    }

    for (const CtrlAction& a : acts) {
        int cur = -1;
        for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it)
            if (it.value() == a.id) { cur = it.key(); break; }

        QString bound = QStringLiteral("---");
        if (cur >= 0) {
            if (dev == 0)      bound = QKeySequence(cur).toString();
            else if (dev == 1) bound = xinputBtnName(cur);
            else               bound = QString("Button %1").arg(cur + 1);   // 1부터 표시
        }
        out.append(ControlSlot{a.name, bound});
    }
    return out;
}

void MainWindow::remapControl(int dev, int slot) {
    const QVector<CtrlAction> acts = buildCtrlActions(
        m_gamepad ? m_gamepad->padLayout() : PadLayout::Standard, isEn(),
        gamePlatform(m_loadedGame) == QLatin1String("cps"));
    if (slot < 0 || slot >= acts.size()) return;
    const int     libId = acts[slot].id;
    const QString act   = acts[slot].name;

    if (dev == 0) {
        // 캡처가 끝난 뒤에만 기존 배정을 바꾼다 (취소하면 그대로 둔다)
        CaptureGuard cg(&m_captureActive);
        KeyCaptureDialog dlg(act, this);
        if (dlg.exec() == QDialog::Accepted && dlg.capturedKey != 0) {
            for (auto it = m_keymap.begin(); it != m_keymap.end(); )
                it.value() == libId ? it = m_keymap.erase(it) : ++it;
            m_keymap.remove(dlg.capturedKey);          // 충돌 제거
            m_keymap.insert(dlg.capturedKey, libId);
            autoSaveKeyboard();
            log(QString("키 재설정: %1 → %2").arg(act, QKeySequence(dlg.capturedKey).toString()));
        }
    } else if (dev == 1 && m_gamepad) {
        // ★ 리매핑은 "대상으로 고른 패드"의 프로필에만 적용한다.
        //   게임용(인덱스로 바뀐) 표가 아니라 "저장용 표"를 편집해야 6버튼의 의미가 보존된다.
        const QString padName = m_gamepad->padName(m_remapPad);
        QHash<int,int> map2 = m_padUiMap[m_remapPad].isEmpty()
                              ? layoutDefaultMap() : m_padUiMap[m_remapPad];
        for (auto it = map2.begin(); it != map2.end(); )
            it.value() == libId ? it = map2.erase(it) : ++it;

        m_gamepad->setCapturePad(m_remapPad);          // 그 패드 입력만 캡처
        CaptureGuard cg(&m_captureActive);
        GamepadCaptureDialog dlg(act, m_gamepad, false, this);
        const bool ok = (dlg.exec() == QDialog::Accepted && dlg.capturedBtn >= 0);
        m_gamepad->setCapturePad(-1);
        if (ok) {
            map2.remove(dlg.capturedBtn);              // 충돌 제거
            map2.insert(dlg.capturedBtn, libId);
            // 이 패드의 프로필에만 저장한다 (공용 매핑을 덮어쓰면 다른 패드까지 바뀐다).
            m_padUiMap[m_remapPad] = map2;
            m_gamepad->setPadMapping(m_remapPad, materializePadMap(map2, m_coreBtns));
            autoSavePad(m_remapPad);
            log(QString("게임패드 재설정 [%1]: %2 → %3")
                .arg(padName.isEmpty() ? QString("PAD") : padName,
                     act, xinputBtnName(dlg.capturedBtn)));
        }
    } else if (dev == 2 && m_gamepad) {
        QHash<int,int> map2 = m_gamepad->getWinMMMapping();
        for (auto it = map2.begin(); it != map2.end(); )
            it.value() == libId ? it = map2.erase(it) : ++it;

        CaptureGuard cg(&m_captureActive);
        GamepadCaptureDialog dlg(act, m_gamepad, true, this);
        if (dlg.exec() == QDialog::Accepted && dlg.capturedBtn >= 0) {
            map2.remove(dlg.capturedBtn);              // 충돌 제거
            map2.insert(dlg.capturedBtn, libId);
            m_gamepad->setWinMMMapping(map2);
            autoSaveStick();
            log(QString("아케이드 스틱 재설정: %1 → Button %2").arg(act).arg(dlg.capturedBtn + 1));
        }
    }
    refreshControlsUi();
}

void MainWindow::resetControlDevice(int dev) {
    // 기본값으로 되돌린 표를 이 게임 범위에 그대로 저장한다 (자동 저장과 같은 규칙).
    //   기종별로 저장해 둔 표가 있어도 이 게임은 기본값이 된다.
    if (dev == 0) {
        m_keymap = buildDefaultKeymap(sixLayoutNow());
        autoSaveKeyboard();
        log("키보드 매핑 기본값으로 초기화됨");
    } else if (dev == 1 && m_gamepad) {
        if (m_gamepad->padPresent(m_remapPad)) {
            m_padUiMap[m_remapPad] = layoutDefaultMap();
            m_gamepad->setPadMapping(m_remapPad, materializePadMap(m_padUiMap[m_remapPad], m_coreBtns));
            autoSavePad(m_remapPad);
            applyPadProfiles();
        }
        log("게임패드 매핑 기본값으로 초기화됨");
    } else if (dev == 2 && m_gamepad) {
        m_gamepad->resetDefaultWinMM();
        autoSaveStick();
        log("아케이드 스틱(WinMM) 매핑 기본값으로 초기화됨");
    }
    refreshControlsUi();
}

// "지금 무엇이 적용 중인가" 안내
QString MainWindow::padSourceText() const {
    if (!m_gamepad || !m_gamepad->padPresent(m_remapPad))
        return isEn() ? "NO PAD CONNECTED - SHOWING THE DEFAULT LAYOUT"
                      : "패드 없음 - 기본 배치 미리보기";
    const QString dev = m_gamepad->padName(m_remapPad);
    const QString src = m_padMapSource[m_remapPad];
    const QString lay = padLayoutLabel(m_gamepad->padLayout(), isEn());
    return isEn() ? QString("APPLIED: %1 | %2 | %3").arg(dev, src, lay)
                  : QString("적용 중: %1 | 출처 %2 | 배치 %3").arg(dev, src, lay);
}


// ── 핫키 테이블 갱신 ─────────────────────────────────────────

// ── 우선순위대로 모드를 골라 오디오에 반영 ───────────────────
void MainWindow::applyResolvedSoundMode() {
    if (!m_audio) return;
    const QString rom  = m_loadedGame.isEmpty() ? m_selectedGame : m_loadedGame;
    const QString key  = gSettings.resolvedSoundMode(rom, gamePlatform(rom));
    const SoundModeId id = soundModeFromKey(key);
    if (m_audio->soundMode() == id) return;   // 같은 모드면 상태를 건드리지 않는다
    m_audio->setSoundMode(id);
    log(QString("🎚 사운드 모드: %1").arg(soundModeLabel(id, isEn())));
}

// ════════════════════════════════════════════════════════════
//  MULTIPLAYER 셸 페이지 창구
//   화면은 셸 페이지(ShellPageNetplay)가 그리고, 여기는 연결 동작과 상태만 가진다.
//   상태 문자열·버튼 가능 여부는 m_net 하나에 모아 두고, 바뀔 때마다 refreshNetUi().
// ════════════════════════════════════════════════════════════
void MainWindow::refreshNetUi() {
    if (m_menu) m_menu->refreshOpen();
}

NetState MainWindow::netState() const {
    NetState s = m_net;
    s.port  = gSettings.netplayPort;
    s.delay = m_netDelay;
    s.relayBuiltin = gSettings.netplayRelayUrl.isEmpty() ||
                     gSettings.netplayRelayUrl == AppSettings::builtinRelayUrl();
    return s;
}

// 릴레이 주소는 계정 ID 를 포함하므로 화면에 실제 값을 절대 보여 주지 않는다.
//   비워서 저장하면 내장 서버로 복귀한다.
void MainWindow::netSetRelay(const QString& typed) {
    const QString t = typed.trimmed();
    gSettings.netplayRelayUrl = t.isEmpty() ? AppSettings::builtinRelayUrl() : t;
    gSettings.save();
}

void MainWindow::netCopyRoomCode() {
    if (!m_net.roomCode.isEmpty()) QApplication::clipboard()->setText(m_net.roomCode);
}

// 공개 IP 비동기 조회 (화면에 "YOUR IP" 로 보여 준다)
void MainWindow::fetchPublicIp() {
            auto* nam = new QNetworkAccessManager(this);
        connect(nam, &QNetworkAccessManager::finished, this,
                [this, nam](QNetworkReply* reply){
            if (reply->error() == QNetworkReply::NoError) {
                m_publicIp = reply->readAll().trimmed();
                m_net.publicIp = m_publicIp;
            } else {
                m_publicIp = gNetplay().localIp();
                m_net.publicIp = m_publicIp + " (local)";
            }
            refreshNetUi();
            reply->deleteLater();
            nam->deleteLater();

            // 토큰 방식: 코드 갱신 불필요 (IP가 코드에 포함되지 않음).
            // ipify 결과는 STUN 실패 시 폴백용으로만 사용.
        });
        nam->get(QNetworkRequest(QUrl("https://api.ipify.org")));
    
}

void MainWindow::netHost() {
                // 중복 클릭 방지 (룸코드 갱신되어 매칭 깨지는 문제 차단)
            // 재시도하려면 DISCONNECT 후 다시 HOST GAME.
            m_net.canHost = false;
            m_net.canJoin = false;
            m_net.canDisc = true;
            // 버튼 비활성화 시 포커스가 Relay URL 로 튀는 것 방지

            int port  = gSettings.netplayPort;
            int delay = m_netDelay;
            gSettings.netplayPort       = port;
            gSettings.netplayInputDelay = delay;
            gSettings.save();

            // 1. UDP 소켓 바인드 (호스트 대기)
            gNetplay().hostListen(port);
            m_relayPeerHandled = false;   // 새 연결 — 피어 처리 플래그 리셋

            // 2. 토큰 룸 코드 생성 (워커가 토큰→IP:Port 매핑 관리)
            QString code = generateRoomCode();
            m_net.roomCode = code;
            log("🎫 룸 코드: " + code + "  (상대에게 공유하세요)");
            log(QString("포트 %1 대기 중 (딜레이 %2f)").arg(port).arg(delay));

            // 3. STUN 으로 외부 IP:Port 정확히 발견 → 릴레이 등록
            //    이전 연결 정리 (중복 클릭 방지)
            disconnect(&gNetplay(), &NetplayManager::externalAddressDiscovered,
                       this, nullptr);
            disconnect(&gNetplay(), &NetplayManager::stunFailed,
                       this, nullptr);

            connect(&gNetplay(), &NetplayManager::externalAddressDiscovered, this,
                [this, code](const QString& extIp, int extPort){
                    log(QString("✓ STUN: 내 외부 주소 %1:%2").arg(extIp).arg(extPort));
                    if (gSettings.netplayRelayUrl.isEmpty()) {
                        log("⚠ 릴레이 URL 미설정 — 직접 IP 접속만 가능");
                        return;
                    }
                    // 소켓 read 핸들러 재진입 방지 — 다음 이벤트 루프로 지연
                    QTimer::singleShot(0, this, [this, code, extIp, extPort]{
                        relayRegister(code, "host", extIp, extPort);
                        relayPollPeer(code, "host");
                    });
                }, Qt::SingleShotConnection);

            connect(&gNetplay(), &NetplayManager::stunFailed, this,
                [this, code, port](const QString& reason){
                    log("⚠ STUN 실패(" + reason + ") → ipify 폴백");
                    QString ip = m_publicIp.isEmpty() ? gNetplay().localIp() : m_publicIp;
                    if (gSettings.netplayRelayUrl.isEmpty()) return;
                    QTimer::singleShot(0, this, [this, code, ip, port]{
                        relayRegister(code, "host", ip, port);
                        relayPollPeer(code, "host");
                    });
                }, Qt::SingleShotConnection);

            log("STUN 외부 주소 조회 중...");
            gNetplay().discoverExternalAddress();

            // 4. UPnP 는 보조 (성공/실패와 무관하게 릴레이 등록은 위에서 진행)
            log("UPnP 포트 개방 시도 (보조)...");
            if (m_upnp) { m_upnp->cancel(); m_upnp->deleteLater(); }
            m_upnp = new UPnpMapper(this);
            connect(m_upnp, &UPnpMapper::mapped, this, [this, port](int){
                log(QString("✓ UPnP: %1/UDP 개방 — 직접 IP 접속도 가능").arg(port));
                m_net.status = QString("● 대기 중 (UPnP %1)").arg(port);
            });
            connect(m_upnp, &UPnpMapper::failed, this, [this](const QString& reason){
                log("· UPnP 미지원: " + reason.split('\n').first()
                    + "  (릴레이 홀펀칭으로 진행)");
            });
            m_upnp->map(port, gNetplay().localIp());
        
}

void MainWindow::netJoin() {
                // 중복 클릭 방지 (재시도는 DISCONNECT 후 다시 JOIN GAME)
            m_net.canJoin = false;
            m_net.canHost = false;
            m_net.canDisc = true;
            // 버튼 비활성화 시 Qt 가 포커스를 다음 위젯(Relay URL)으로 옮기는 것 방지



            QString rawCode = m_net.joinCode.trimmed();
            QString code    = rawCode.toUpper();
            qDebug().noquote() << QString("[join] 룸코드 %1").arg(code);

            // ── 직접 IP 입력 모드 (룸 코드 비어있음) ──
            // 동일 LAN 테스트 / 포트포워딩 직결 환경용. 릴레이/STUN 미사용.
            if (code.isEmpty()) {
                QString ip = m_net.joinIp.trimmed();
                int port   = gSettings.netplayPort;
                qDebug().noquote() << QString("[join] 직접 연결 %1:%2").arg(ip).arg(port);
                gNetplay().clientConnect(ip, port);
                log(QString("(직접) %1:%2 연결 중...").arg(ip).arg(port));
                return;
            }

            // ── 룸 코드 모드 ──
            if (!isValidRoomCode(code)) {
                log("❌ 잘못된 룸 코드 (6자 영숫자)");
                return;
            }
            if (gSettings.netplayRelayUrl.isEmpty()) {
                log("❌ 릴레이 URL 미설정 — Relay URL 항목을 입력하세요");
                return;
            }

            // 1. UDP 소켓 바인드만 (HELLO 미발사 — 호스트 주소를 아직 모름)

            if (!gNetplay().clientPrepare()) {
                log("❌ UDP 소켓 바인드 실패");
                return;
            }
            m_relayPeerHandled = false;   // 새 연결 — 피어 처리 플래그 리셋
            log(QString("🎫 룸 코드 '%1' 워커에 조회 시작").arg(code));

            // 2. STUN → 릴레이 등록 → 호스트 폴링 (relayPollPeer 가 처리)
            disconnect(&gNetplay(), &NetplayManager::externalAddressDiscovered,
                       this, nullptr);
            disconnect(&gNetplay(), &NetplayManager::stunFailed,
                       this, nullptr);

            connect(&gNetplay(), &NetplayManager::externalAddressDiscovered, this,
                [this, code](const QString& extIp, int extPort){
                    log(QString("✓ STUN: 내 외부 주소 %1:%2").arg(extIp).arg(extPort));

                    // ★ 네트워크 호출을 다음 이벤트 루프 틱으로 지연 (재진입 차단)
                    QTimer::singleShot(0, this, [this, code, extIp, extPort]{

                        relayRegister(code, "client", extIp, extPort);
                        relayPollPeer(code, "client");
                    });
                }, Qt::SingleShotConnection);

            connect(&gNetplay(), &NetplayManager::stunFailed, this,
                [this, code](const QString& reason){
                    log("⚠ STUN 실패(" + reason + ") → ipify 폴백");
                    QString ip   = m_publicIp.isEmpty() ? gNetplay().localIp() : m_publicIp;
                    int     port = static_cast<int>(gNetplay().localPort());
                    QTimer::singleShot(0, this, [this, code, ip, port]{
                        relayRegister(code, "client", ip, port);
                        relayPollPeer(code, "client");
                    });
                }, Qt::SingleShotConnection);

            log("STUN 외부 주소 조회 중...");
            gNetplay().discoverExternalAddress();
        
}

void MainWindow::netDisconnect() {
                log("✖ DISCONNECT — 연결 완전 해제 중...");
            // 게임 중이면 상대에게도 종료 통지
            if (gNetplay().playing()) gNetplay().sendGameOver();
            cleanupNetplay();               // 릴레이 폴링·게임 상태 완전 정리
            gNetplay().shutdown();          // 소켓 완전 종료
            m_relayPeerHandled = false;     // 다음 연결을 위해 피어 플래그 리셋
            m_net.roomCode.clear();
            m_net.rtt.clear();
            // 상태 라벨도 OFFLINE 으로 되돌린다 (안 하면 'CONNECTED' 로 남음)
            m_net.status = "● OFFLINE";
            // 버튼 상태 복구 (HOST/JOIN 재시도 가능하게)
            m_net.canHost = true;
            m_net.canJoin = true;
            m_net.canStart = false;
            m_net.canDisc = false;
            log("✖ 연결 해제됨");
        
}

void MainWindow::scanRoms() {
    m_rows.clear();
    m_allRoms.clear();

    QDir dir(gSettings.romPath);
    if (!dir.exists()) {
        log("⚠ ROM 폴더 없음: " + gSettings.romPath);
        syncShellList();          // 비어 있는 목록을 셸에도 반영한다
        return;
    }

    for (const QFileInfo& fi :
         dir.entryInfoList({"*.zip","*.7z","*.rar"}, QDir::Files, QDir::Name)) {
        QString romName = fi.completeBaseName();
        // 게임명 DB 우선, 없으면 대문자 romName
        QString display = getGameDisplayName(romName);
        m_allRoms.append({display, romName});
    }

    // 즐겨찾기가 위로 오도록 정렬
    std::stable_sort(m_allRoms.begin(), m_allRoms.end(),
        [this](const QPair<QString,QString>& a, const QPair<QString,QString>& b){
            bool fa = isFavorite(a.second), fb = isFavorite(b.second);
            if (fa != fb) return fa;
            return a.first < b.first;
        });

    // 선택 게임이 아직 없으면(=프로그램 시작 직후) 마지막 플레이 게임을 복원.
    //   filterRoms() 가 m_selectedGame 기준으로 목록 위치를 잡아 주므로
    //   목록이 맨 위로 초기화되지 않는다.
    bool restoredLast = false;
    if (m_selectedGame.isEmpty() && !gSettings.lastGame.isEmpty()) {
        for (const auto& [disp, rom] : m_allRoms) {
            if (rom == gSettings.lastGame) { m_selectedGame = rom; restoredLast = true; break; }
        }
    }

    filterRoms();

    // 복원된 게임의 프리뷰도 함께 표시 (선택 상태와 화면을 일치시킴)
    if (restoredLast && m_loadedGame.isEmpty()) {
        loadPreview(m_selectedGame);
        qDebug().noquote() << "[list] 마지막 게임 복원 " + m_selectedGame;
        // 복원 경로는 selectGame() 을 거치지 않으므로 직접 갱신한다.
        resolveAndApplyControls(m_selectedGame);   // 버튼 배치·매핑도 함께
        applyBezel();                              // 이 게임에 맞는 베젤
    }

    log(QString("ROM %1개 검색됨 (%2)").arg(m_allRoms.size()).arg(gSettings.romPath));
}
// 필터(ALL/FAV/NOFAV/기종)를 적용해 화면에 보일 목록(m_rows)을 만들고 셸에 넘긴다.
void MainWindow::filterRoms() {
    m_rows.clear();
    int shown = 0;
    const QString q = m_searchText.trimmed().toLower();
    for (const auto& [disp, rom] : m_allRoms) {
        const bool fav = isFavorite(rom);

        // ── 탭 필터 ──────────────────────────────────────
        if (m_glFilter == 1 && !fav) continue;  // FAV: 즐겨찾기만
        if (m_glFilter == 2 &&  fav) continue;  // NOFAV: 미즐겨찾기만

        // ── 기종 필터 ────────────────────────────────────
        //   FAV 탭에서는 기종을 무시한다 (즐겨찾기는 기종 상관없이 전부 표시)
        if (m_glFilter != 1 && !m_hwFilter.isEmpty()
            && gameHardwareGroup(gameHardwareOf(rom)) != m_hwFilter) continue;

        // ── 검색어 필터 (게임 이름 또는 롬 이름에 포함) ──
        if (!q.isEmpty() && !disp.toLower().contains(q) && !rom.toLower().contains(q)) continue;

        m_rows.append(ListRow{rom, disp, fav});
        ++shown;
    }

    // 로그: 필터 적용 시 결과 개수 확인용
    if (m_glFilter != 0 || !m_hwFilter.isEmpty())
        qDebug().noquote() << QString("[list] %1개 표시").arg(shown);

    // 필터 후 선택 복원: 이전에 선택된 게임이 목록에 있으면 유지, 없으면 첫 번째 게임을 고른다
    bool selRestored = false;
    for (const ListRow& r : m_rows)
        if (r.rom == m_selectedGame) { selRestored = true; break; }
    if (!selRestored && !m_rows.isEmpty())
        selectGame(m_rows.first().rom);

    syncShellList();
}
void MainWindow::selectGame(const QString& romName) {
    if (m_selectedGame == romName) return;
    m_selectedGame = romName;
    m_previewVideoDone = false;      // 새 게임을 고르면 영상 1회 재생 다시 허용

    // 게임명 DB 표시명
    QString displayName = getGameDisplayName(romName);
    log(QString("선택: %1 (%2)").arg(displayName).arg(romName));

    // 선택이 바뀌면 그 게임에 맞는 컨트롤 매핑을 불러온다 (게임 실행 중이 아닐 때만).
    //   예전에는 게임을 "실행"할 때만 해석해서, 메뉴에서 게임을 바꿔도 컨트롤
    //   화면이 이전 게임 설정을 계속 보여줬다.
    if (!gState.gameLoaded && !gState.isPaused)
        resolveAndApplyControls(romName);

    // 선택이 바뀌면 사운드 모드도 그 게임 기준으로 다시 해석·표시한다
    applyResolvedSoundMode();
    applyBezel();            // 선택 게임에 맞는 베젤로 갱신

    loadPreview(romName);
}

void MainWindow::loadPreview(const QString& romName) {
    Q_UNUSED(romName);          // 선택 게임(m_selectedGame)의 그림을 셸이 찾는다
    // 이전 영상 정지 + 이미지로 복귀
#if HAVE_FFMPEG
    if (m_previewVideo) m_previewVideo->stop();
#endif
    if (m_mediaPlayer)     m_mediaPlayer->stop();
    if (m_previewVidTimer) m_previewVidTimer->stop();

    syncShellPreview();

    // 영상을 자동 재생할 상황인지 판단한다.
    //   · 이미 이 게임의 영상을 한 번 재생했으면 다시 틀지 않는다 (1회만)
    //   · 게임이 로드된 상태(플레이 중 메뉴로 나온 경우)에는 이미지만 보여준다
    const bool inGame  = gState.gameLoaded || gState.isPaused;
    const bool playVid = !m_previewVideoDone && !inGame;
    if (playVid && m_previewVidTimer) m_previewVidTimer->start();
}
void MainWindow::loadPreviewVideo(const QString& romName) {
    if (romName.isEmpty()) return;
    for (const QString ext : {"mp4","avi","mkv","webm","mov"}) {
        QString path = gSettings.previewPath + "/" + romName + "." + ext;
        if (!QFile::exists(path)) continue;

#if HAVE_FFMPEG
        // Linux: 자체 소프트웨어 디코더로 QLabel 에 직접 그린다.
        //   QVideoWidget 을 쓰지 않으므로 스택 전환도 필요 없다.
        if (m_previewVideo) {
            m_previewVideo->setVolume(gSettings.audioVolume);   // 앱 볼륨 연동
            if (m_previewVideo->open(path)) {
                qDebug().noquote() << "[preview] 영상 " + romName;
                return;
            }
        }
        log("⚠ 프리뷰 영상 열기 실패 — 이미지 유지: " + romName);
        return;
#else
        if (!m_mediaPlayer) return;
        m_mediaPlayer->setSource(QUrl::fromLocalFile(path));
        m_mediaPlayer->setLoops(1);       // 한 번만 재생 → 끝나면 이미지로 복귀
        m_mediaPlayer->play();
        qDebug().noquote() << "[preview] 영상 " + romName;
        return;
#endif
    }
    // 영상 없음 → 이미지 모드 유지
}

// ════════════════════════════════════════════════════════════
//  치트 파일을 FBNeo 코어가 읽는 위치로 동기화
//  ─ FBNeo(libretro) 는 치트를 자체 엔진으로 처리하며, 파일을
//      [system_dir]/fbneo/cheats/{rom}.ini
//    에서만 읽는다 (szAppCheatsPath). 읽은 치트는 코어 옵션
//    "fbneo-cheat-<n>-<드라이버>-<옵션>" 으로 등록되고, 그 값을 바꾸면
//    코어가 CheatEnable() 로 적용한다 → RetroArch 와 100% 동일 동작.
//  ─ 이 앱의 치트는 <앱>/cheats/ 에 있어 위치가 달라 코어가 못 읽었다.
//    게임 로드 직전에 복사해 두면 코어가 자동으로 인식한다.
//  ★ 반드시 core->loadGame() 이전에 호출해야 한다(치트 로드는 게임 초기화 시점).
void MainWindow::syncCheatsToSystemDir(const QString& romName) {
    if (romName.isEmpty()) return;
    const QString sysDir = gSettings.romPath.isEmpty()
        ? AppSettings::baseDir() : gSettings.romPath;
    const QString dstDir = sysDir + "/fbneo/cheats";
    QDir().mkpath(dstDir);

    // {rom}.ini 우선, 없으면 부모롬(접미사 제거) ini 도 같은 이름으로 복사
    QString src = gSettings.cheatPath + "/" + romName + ".ini";
    if (!QFile::exists(src)) {
        QString stem = romName;
        while (stem.size() > 2 && !QFile::exists(src)) {
            stem.chop(1);
            src = gSettings.cheatPath + "/" + stem + ".ini";
        }
    }
    if (!QFile::exists(src)) return;   // 치트 없음 — 조용히 통과

    const QString dst = dstDir + "/" + romName + ".ini";
    // 원본이 더 새로우면 갱신 (매번 복사하지 않도록)
    if (QFile::exists(dst)) {
        if (QFileInfo(src).lastModified() <= QFileInfo(dst).lastModified()) return;
        QFile::remove(dst);
    }
    if (QFile::copy(src, dst))
        qDebug().noquote() << "[cheat] 코어 연동: " + romName + ".ini";
}

bool MainWindow::loadRomInternal() {
    if (!m_core || m_selectedGame.isEmpty()) return false;

    // FBNeo 네이티브 치트 엔진이 읽을 수 있도록 먼저 복사 (loadGame 이전 필수)
    syncCheatsToSystemDir(m_selectedGame);

    // 로딩 커서 적용
    // processEvents() 제거: loadGame() 전에 이벤트 처리 시 MSG_START 등이
    // 재진입(re-entrant)으로 처리되어 이중 startEmu() 발생 → 크래시 원인
    {
        QPixmap loadPx(":/assets/loading.png");
        if (!loadPx.isNull())
            QApplication::setOverrideCursor(QCursor(loadPx, 0, 0));
        else
            QApplication::setOverrideCursor(Qt::WaitCursor);
    }

    // .zip 우선, 없으면 확장자 없이 시도
    QString romPath = gSettings.romPath + "/" + m_selectedGame + ".zip";
    if (!QFile::exists(romPath)) {
        for (const QString ext : {"7z","rar",""}) {
            QString candidate = ext.isEmpty()
                ? gSettings.romPath + "/" + m_selectedGame
                : gSettings.romPath + "/" + m_selectedGame + "." + ext;
            if (QFile::exists(candidate)) { romPath = candidate; break; }
        }
    }

    // 이전 게임 변수 초기화 후 저장된 머신 세팅 복원
    // (코어가 SET_VARIABLES를 호출하기 전에 미리 세팅 → 기본값 덮어쓰기 방지)
    gState.variables.clear();
    gState.variableOptions.clear();
    gState.variableDescriptions.clear();
    // 머신 세팅 복원: 기종별(기본 베이스) → 게임별(우선 덮어쓰기)
    {
        const QString plat = gamePlatform(m_selectedGame);
        const auto& platVars = gSettings.machineVarsByPlatform.value(plat);
        for (auto it = platVars.begin(); it != platVars.end(); ++it)
            gState.variables[it.key()] = it.value();
        const auto& gameVars = gSettings.machineVars.value(m_selectedGame);
        for (auto it = gameVars.begin(); it != gameVars.end(); ++it)
            gState.variables[it.key()] = it.value();   // 게임별이 기종별을 덮어씀
    }

    // 컨트롤 매핑 해석·적용 (게임별 > 기종별 > 전역 > 기본)
    resolveAndApplyControls(m_selectedGame);

    qDebug("[load] loadGame() path=%s npActive=%d",
           romPath.toUtf8().constData(), gNetplay().active() ? 1 : 0);
    bool ok = m_core->loadGame(romPath);
    qDebug("[load] loadGame() returned %d", ok ? 1 : 0);

    // 치트 로드 (게임 실행 시점에만 — 선택 시 로드하면 목록 이동마다 파일I/O 발생)
    if (ok && m_cheat) {
        m_cheat->autoLoad(m_selectedGame, gSettings.cheatPath);
        if (m_cheat->count() == 0)
            qDebug().noquote() << "[cheat] 없음 " + m_selectedGame;
        else
            qDebug().noquote() << QString("[cheat] %1개 로드").arg(m_cheat->count());
    }

    // 로딩 커서 해제 후 커스텀 커서 복원
    // restoreOverrideCursor: 로딩용 setOverrideCursor 1회만 팝
    QApplication::restoreOverrideCursor();
    setCursor(m_customCursor);  // widget-level 커스텀 커서 (스택 누적 없음)

    return ok;
}

// ════════════════════════════════════════════════════════════
//  에뮬레이션 시작/일시정지/전체화면
// ════════════════════════════════════════════════════════════
void MainWindow::startEmu() {
    qDebug("[emu] startEmu() gameLoaded=%d npPlaying=%d",
           gState.gameLoaded ? 1 : 0, gNetplay().playing() ? 1 : 0);
    m_npStates.clear();
    m_npInputHistory.clear();
    m_frameDelay     = 0.0;
    m_pendingResimTo = -1;
    gState.frameCount    = 0;
    gState.gameLoadFrame = 0;  // 치트 딜레이 기준점
    gState.fastForward   = false;
    gState.isPaused      = false;

    // ── 입력 상태 완전 초기화 ────────────────────────────────
    // 이전 게임 잔류 키 입력 방지:
    //   1) GamepadManager 누산기 초기화 (Linux: m_buttonBits/stickBits/dpadBits 클리어
    //      + fd 보류 이벤트 드레인) → m_jsBits 잔류로 rawKeys 재오염 차단
    //   2) kbHeld 를 비워야 applyBits 가 해당 인덱스를 건너뛰지 않음
    //   3) rawKeys/keys/p2Keys 도 0 으로 리셋 → 첫 프레임 오입력 차단
    if (m_gamepad) m_gamepad->clearState();
    gState.kbHeld.clear();
    gState.rawKeys.fill(0);
    gState.keys.fill(0);
    gState.p2Keys.fill(0);

    // Canvas 옵션 적용
    if (m_canvas) {
        m_canvas->setScaleMode(gSettings.videoScaleMode);
        m_canvas->setSmooth(gSettings.videoSmooth);
        m_canvas->setCrtMode(gSettings.videoCrtMode, gSettings.videoCrtIntensity);
        m_canvas->setFlashGuard(gSettings.videoFlashGuard,
                                gSettings.videoFlashStrength / 100.0f);
        if (!gSettings.videoShaderPath.isEmpty())
            m_canvas->setShaderPath(gSettings.videoShaderPath);
    }

    // ── TATE 자동 감지: 코어 회전값 적용 ───────────────────────
    // SET_ROTATION으로 회전이 보고된 경우 auto(-1) 상태에서 자동 반영
    // m_canvas->rotation() == -1이면 gState.videoRotation을 그대로 사용
    // 수동 설정(0/1/3)이면 유지
    QTimer::singleShot(50, this, [this]{
        // 게임 첫 프레임 이후 videoRotation이 확정됨
        if (m_canvas && m_canvas->rotation() == -1) {
            // auto 상태: 버튼 라벨만 갱신 (실제 적용은 updateVertices에서 gState 참조)
            applyTate(-1);
        }
        if (gState.videoRotation != 0)
            qDebug().noquote() << QString("[video] 세로형 자동회전 %1도").arg(gState.videoRotation * 90);
    });

    m_frameAccum = 0.0;
    m_aflClock.start();
    m_timer->start(1);

    QTimer::singleShot(100, this, [this]{
        enterGameScreen();
    });
    // 코어가 SET_VARIABLES 로 옵션(DIP·네이티브 치트)을 알려 주는 첫 프레임 뒤에
    //   네이티브 치트 유무를 판정한다. DIP/치트 화면은 메뉴를 열 때 직접 읽는다.
    QTimer::singleShot(300, this, [this]{
        refreshNativeCheatFlag();
        if (m_menu) m_menu->refreshOpen();
    });

    // 코어가 알려준 버튼 의미를 남기고, 그 값으로 기본 매핑을 확정한다.
    //   ★ 이게 유일한 정답이다. 롬 이름 목록·소스 추정은 여기서 덮어써진다.
    QTimer::singleShot(320, this, [this]{
        if (gState.inputDesc.isEmpty()) return;
        QStringList parts;
        QList<int> ids = gState.inputDesc.keys();
        std::sort(ids.begin(), ids.end());
        for (int id : ids) parts << QString("%1=%2").arg(id).arg(gState.inputDesc.value(id));
        qDebug().noquote() << "[core] 버튼 정의: " + parts.join(", ");
        applyCoreButtonDefs();
    });
    qDebug().noquote() << QString("[emu] 시작 %1 FPS").arg(gState.coreFps, 0, 'f', 2);
}

void MainWindow::launchGame() {
    if (!m_core || m_selectedGame.isEmpty()) { log("게임을 먼저 선택하세요"); return; }

    // 마지막 플레이 게임 기억 → 다음 실행 때 이 게임이 선택된 상태로 복원
    if (gSettings.lastGame != m_selectedGame) {
        gSettings.lastGame = m_selectedGame;
        gSettings.save();
    }

    // 일시정지 상태 → 같은 게임이면 재개, 다른 게임이면 새로 로드
    if (gState.isPaused && m_loadedGame == m_selectedGame) {
        gState.isPaused = false;
        if (m_audio && m_audio->isReady()) m_audio->flush();  // 링버퍼 재초기화
        m_aflClock.start();
        m_timer->start(1);
        enterGameScreen();
        log("▶ 재개");
        return;
    }

    // 게임 로드 중이거나 다른 게임이 일시정지 상태 → 기존 게임 완전 정리
    if (gState.gameLoaded || gState.isPaused) {
        m_timer->stop();
        gState.isPaused = false;
        m_core->unloadGame();
        m_loadedGame.clear();
    }

    if (loadRomInternal()) {
        m_loadedGame = m_selectedGame;  // 로드된 게임 이름 기록
        // 코어의 실제 샘플레이트로 오디오 재초기화 (끊김 방지)
        int sr = static_cast<int>(gState.coreSampleRate > 8000 ? gState.coreSampleRate : 44100);
        qDebug().noquote() << QString("[audio] %1 Hz, %2 ms").arg(sr).arg(gSettings.audioBufferMs);
        m_audio->init(sr, gSettings.audioBufferMs);
        applyResolvedSoundMode();   // 재초기화로 리셋된 모드 다시 적용
        applyBezel();               // 이 게임의 베젤 (bezels/<롬>.png)
        startEmu();
    } else {
        log("✖ ROM 로드 실패: " + m_selectedGame);
    }
}

void MainWindow::toggleSwapPlayers() {
    gState.swapPlayers = !gState.swapPlayers;

    // ── 게임 화면 오버레이 업데이트 ──────────────────────────
    if (m_playerOverlay && m_canvas) {
        m_overlayTimer->stop();

        if (gState.swapPlayers) {
            // 2P 모드: 초록 배지 — 계속 표시
            m_playerOverlay->setStyleSheet(
                "QLabel{"
                "  background:rgba(0,60,0,200);"
                "  color:#44ff88;"
                "  font-family:'Courier New';font-size:13px;font-weight:bold;"
                "  padding:5px 12px;border:1px solid #00cc44;border-radius:5px;"
                "}");
            m_playerOverlay->setText("◉  2P MODE");
        } else {
            // 1P 복귀: 파란 배지 — 1.5초 후 자동 숨김
            m_playerOverlay->setStyleSheet(
                "QLabel{"
                "  background:rgba(0,20,80,200);"
                "  color:#66aaff;"
                "  font-family:'Courier New';font-size:13px;font-weight:bold;"
                "  padding:5px 12px;border:1px solid #4477cc;border-radius:5px;"
                "}");
            m_playerOverlay->setText("◎  1P MODE");
            m_overlayTimer->start();
        }

        m_playerOverlay->adjustSize();
        m_playerOverlay->move(
            m_canvasW->width()  - m_playerOverlay->width()  - 12,
            12);
        m_playerOverlay->show();
        m_playerOverlay->raise();
    }

    log(gState.swapPlayers
        ? "⇄ 2P 포트로 전환 (F10 — 1P 복귀)"
        : "⇄ 1P 포트로 복귀");
}

// ════════════════════════════════════════════════════════════
//  TATE 모드 (세로형 게임 화면 회전)
// ════════════════════════════════════════════════════════════
void MainWindow::toggleTate() {
    if (!m_canvas) return;
    int cur = m_canvas->rotation();  // 현재 적용값 (-1=auto, 0, 1, 3)

    // 순환: auto(-1) → 90°CCW(1) → 90°CW(3) → off(0) → auto(-1)
    int next;
    if      (cur == -1) next = 1;
    else if (cur ==  1) next = 3;
    else if (cur ==  3) next = 0;
    else                next = -1;

    applyTate(next);
}

void MainWindow::applyTate(int rot) {
    if (!m_canvas) return;
    m_canvas->setRotation(rot);


    QString rotName;
    switch (rot) {
    case  1: rotName = "90°CCW"; break;
    case  3: rotName = "90°CW";  break;
    case  0: rotName = "OFF";    break;
    default: rotName = QString("AUTO(%1°)").arg(gState.videoRotation * 90); break;
    }
    log("⟳ TATE: " + rotName);
}

void MainWindow::togglePause() {
    if (!gState.gameLoaded) return;
    gState.isPaused = !gState.isPaused;
    if (gState.isPaused) {
        m_timer->stop();
        leaveGameScreen();
        log("⏸ 일시정지");
    } else {
        // 재개: 링버퍼·PID·리샘플러 초기화 (일시정지 중 링버퍼가 비어
        //        DRC 재수렴에 ~25초 걸리는 문제 방지)
        if (m_audio && m_audio->isReady()) m_audio->flush();
        m_frameAccum = 0.0;
        m_aflClock.start();
        m_timer->start(1);
        enterGameScreen();
        log("▶ 재개");
    }
}

// 지금 전체화면인가. Windows 는 창의 실제 상태를 본다. 스팀덱(gamescope)은 창이 처음부터
//   전체화면으로 떠 있어도 Qt 가 그렇다고 알려 주지 않는 일이 있어, 우리가 기억한 상태도 함께 본다.
bool MainWindow::fullscreenNow() const {
#ifdef _WIN32
    return isFullScreen();
#else
    return isFullScreen() || m_isFullscreen;
#endif
}

void MainWindow::toggleFullscreen() {
    if (!fullscreenNow()) {
        m_windowedSize = size();
        m_isFullscreen = true;
        showFullScreen();
    } else {
        m_isFullscreen = false;
        showNormal();
        // 창 크기가 화면보다 크면 오른쪽·아래가 잘린다 (스팀덱 1280x800 에서 그랬다)
        QSize sz = m_windowedSize.isValid() ? m_windowedSize : QSize(1360, 840);
        if (screen()) sz = sz.boundedTo(screen()->availableGeometry().size());
        resize(sz);
    }
}

void MainWindow::toggleFastForward(bool on) {
    gState.fastForward = on;
}

// ════════════════════════════════════════════════════════════
//  넷플레이 입력 합성 헬퍼
//  호스트: lb=P1(로컬), rb=P2(원격)
//  클라이언트: lb=P2(로컬), rb=P1(원격)
// ════════════════════════════════════════════════════════════
void MainWindow::npApplyInput(uint16_t lb, uint16_t rb) {
    if (gNetplay().isHost()) {
        for (int i = 0; i < 16; ++i) {
            gState.keys[i]   = (lb >> i) & 1;
            gState.p2Keys[i] = (rb >> i) & 1;
        }
    } else {
        for (int i = 0; i < 16; ++i) {
            gState.keys[i]   = (rb >> i) & 1;
            gState.p2Keys[i] = (lb >> i) & 1;
        }
    }
}

// ════════════════════════════════════════════════════════════
//  에뮬 루프 (Phase 3: AFL + Rollback)
// ════════════════════════════════════════════════════════════
void MainWindow::onEmuTimer() {
    // ── 재진입 방지 ──────────────────────────────────────────
    // FBNeo DLL이 retro_run() 내부에서 DirectSound/WinMM API를 호출하면
    // Windows 메시지 펌프가 돌아 Qt 이벤트 루프가 재진입될 수 있음.
    // 재진입 시 m_core 상태 충돌 → 크래시. 플래그로 완전 차단.
    if (m_emuTimerBusy) return;
    m_emuTimerBusy = true;
    auto _busyGuard = qScopeGuard([this]{ m_emuTimerBusy = false; });

    if (!gState.gameLoaded || !m_core || gState.isPaused) return;

    // 넷플레이 첫 프레임 진입 진단 (frame=0일 때만 1회)
    if (gNetplay().playing() && gState.frameCount == 0) {
        qDebug("[NP] FIRST FRAME entered — playing=true frameCount=0 core=%p", (void*)m_core);
    }

    // ── 서비스 모드 자동 해제 (5초 = 300프레임) ───────────────────
    if (gState.serviceMode) {
        if (++gState.serviceModeFrames >= 300) {
            gState.serviceMode       = false;
            gState.serviceModeFrames = 0;
            log("🔒 서비스 모드 해제 (타임아웃)");
        }
    }

    // ── AFL 타이밍 게이트 + No-Wait Frame Pacing ────────────────
    // m_frameDelay: 넷플레이 시 프레임 차이를 기반으로 ±1ms씩 자동 조절
    //   양수(슬로우다운) → 로컬이 리모트보다 앞설 때
    //   음수(스피드업)   → 로컬이 리모트보다 뒤처질 때
    {
        double fps     = (gState.coreFps > 0) ? gState.coreFps : 60.0;
        double baseMs  = 1000.0 / fps;
        double targetMs = std::clamp(baseMs + m_frameDelay,
                                     baseMs * 0.70, baseMs * 1.30);

        double elapsedMs = m_aflClock.nsecsElapsed() / 1.0e6;
        m_aflClock.start();
        m_frameAccum += elapsedMs;

        if (m_frameAccum > targetMs * 4.0) m_frameAccum = targetMs;
        if (m_frameAccum < targetMs) return;
        m_frameAccum -= targetMs;
    }

    // ════════════════════════════════════════════════════════════
    //  Rollback Netcode  (개선된 구현)
    //
    //  핵심 수정:
    //  ① 롤백 후 예측값을 확정값으로 갱신 → 반복 롤백 루프 차단
    //  ② 프레임 스톨 → MAX_AHEAD 이상 앞서면 1틱 대기 (롤백창 초과 방지)
    //  ③ 스냅샷을 run() 직전에 저장 (롤백 복원 기준점 정확)
    // ════════════════════════════════════════════════════════════
    // Playing 상태일 때만 넷플레이 롤백 루프 실행
    // (Lobby/Loading/Ready 상태에서는 싱글플레이어 경로 사용)
    if (gNetplay().playing()) {

        // ── ① 호스트 스냅샷 큐 적용 ─────────────────────────────
        // stateReceived 는 소켓 시그널 핸들러 안이므로 m_core 호출 금지.
        // 데이터를 큐에 저장해 두고, 여기서(onEmuTimer) 안전하게 적용한다.
        if (m_pendingSyncSf >= 0) {
            int sf       = m_pendingSyncSf;
            int savedCur = m_pendingSyncCur;
            QByteArray syncData = std::move(m_pendingSyncData);
            m_pendingSyncSf  = -1;
            m_pendingSyncCur = -1;

            size_t expectedSz = m_core->serializeSize();
            bool sizeOk = (!syncData.isEmpty() && expectedSz > 0
                           && static_cast<size_t>(syncData.size()) == expectedSz);
            if (!sizeOk) {
                // 크기 불일치는 깊은 문제(롬/컨텍스트 차이)이거나 손상 → 이 상태만 건너뜀
                static int s_szCount = 0;
                if ((++s_szCount % 30) == 1)
                    qDebug("[SYNC] reject SIZE #%d recv=%d expected=%zu",
                           s_szCount, (int)syncData.size(), expectedSz);
            } else {
                // ★ 호스트 상태는 항상 적용(권위적). 격차(gap)로 적용 *방법*만 결정.
                //   gap > 0 : 클라가 호스트 송신시점보다 앞서있음(정상 — 지연 때문)
                //   gap ≤ 0 : 클라가 뒤처짐
                int gap = savedCur - sf;

                static int s_applyCount = 0;
                if ((++s_applyCount % 30) == 1)
                    qDebug("[SYNC] APPLY #%d sf=%d cur=%d gap=%d sz=%d",
                           s_applyCount, sf, savedCur, gap, (int)syncData.size());

                // Time Drift 조정 (항상 실행 — 격차를 0 근처로 수렴시켜
                //   하드스냅을 거의 안 쓰게 함. 이게 죽음의 소용돌이 방지의 핵심)
                {
                    double fps    = (gState.coreFps > 0) ? gState.coreFps : 60.0;
                    double baseMs = 1000.0 / fps;
                    double adj = std::clamp((gap * baseMs) / 30.0,
                                           -3.0 * baseMs, 3.0 * baseMs);
                    m_frameDelay = std::clamp(m_frameDelay + adj, -5.0, 8.0);
                }

                m_core->unserialize(syncData.constData(),
                                    static_cast<size_t>(syncData.size()));
                gState.frameCount = sf;
                gNetplay().confirmFramesUpTo(static_cast<uint32_t>(sf));

                // 재동기 완료 → 플래그/체크섬 맵 리셋 (다음 desync 감지 준비)
                m_resyncPending = false;
                m_localChecksums.clear();
                m_remoteChecksums.clear();
                m_lastChecksumFrame = 0;

                // 동기화 이전 스냅샷/입력은 호스트 state 와 호환 불가 → 전부 삭제
                m_npStates.clear();
                m_npInputHistory.clear();

                // 격차에 따라 따라잡기 방법 결정.
                //   RESIM_BUDGET 이내 양수 격차 → 부드럽게 재시뮬(틱당 8프레임 분산)
                //   그 외(너무 큼/음수) → 하드 스냅(시각 점프 감수, 영구 desync 보다 나음)
                const int RESIM_BUDGET = 90;   // 1.5초
                if (gap > 0 && gap <= RESIM_BUDGET) {
                    gState.netplayResim = true;
                    m_pendingResimTo    = savedCur;
                } else {
                    gState.netplayResim = false;
                    m_pendingResimTo    = -1;
                    static int s_hardCount = 0;
                    if ((++s_hardCount % 10) == 1)
                        qDebug("[SYNC] HARD-SNAP #%d sf=%d cur=%d gap=%d",
                               s_hardCount, sf, savedCur, gap);
                }
            }
        }

        int cur = gState.frameCount;

        // ── ② 청크 재시뮬 계속 처리 (pendingResimTo 분산 처리) ──
        // 재시뮬 중에는 일반 프레임 진행 없이 catch-up 우선
        if (m_pendingResimTo >= 0 && cur < m_pendingResimTo) {
            int chunkEnd = std::min(cur + MAX_RESIM_PER_TICK, m_pendingResimTo);
            for (int rf = cur; rf < chunkEnd; ++rf) {
                NpInputState& is = m_npInputHistory[rf];
                uint16_t lb = is.local;
                uint16_t rb = static_cast<uint16_t>(
                    gNetplay().getRemoteInput(static_cast<uint32_t>(rf)));
                is.remote = rb;
                gNetplay().recordPrediction(static_cast<uint32_t>(rf), rb);
                npApplyInput(lb, rb);
                m_core->run();
                gState.frameCount = rf + 1;
                size_t sz = m_core->serializeSize();
                if (sz > 0) {
                    QByteArray buf(static_cast<int>(sz), Qt::Uninitialized);
                    if (m_core->serialize(buf.data(), sz))
                        m_npStates[rf + 1] = buf;
                }
            }
            if (gState.frameCount >= m_pendingResimTo) {
                gState.netplayResim = false;
                m_pendingResimTo = -1;
                // 히스토리 정리 (pendingResimTo 완료 후)
                int sf = cur;   // catch-up 완료 기준점
                int cutoff = sf - 2;
                for (auto it = m_npStates.begin(); it != m_npStates.end(); )
                    it = (it.key() < cutoff) ? m_npStates.erase(it) : ++it;
                while (!m_npInputHistory.empty() &&
                       m_npInputHistory.begin()->first < cutoff)
                    m_npInputHistory.erase(m_npInputHistory.begin());
            }
            return;  // 이번 틱은 catch-up 전용
        }

        // 재갱신 (cur 은 pending 완료 후 변경됐을 수 있음)
        cur = gState.frameCount;

        // ── ② No-Wait 프레임 페이싱 ────────────────────────────
        // diff가 클수록 빠르게 보정: 소diff ±1ms, 중diff ±2ms, 대diff ±3ms
        {
            uint32_t remoteF = gNetplay().remoteMaxFrame();
            if (remoteF > 0) {
                int diff = cur - static_cast<int>(remoteF);
                if      (diff >  8) m_frameDelay = std::min(m_frameDelay + 3.0,  8.0);
                else if (diff >  2) m_frameDelay = std::min(m_frameDelay + 1.0,  8.0);
                else if (diff < -8) m_frameDelay = std::max(m_frameDelay - 3.0, -5.0);
                else if (diff < -2) m_frameDelay = std::max(m_frameDelay - 1.0, -5.0);
                else                m_frameDelay *= 0.90;  // 오차 범위: 자연 수렴
            }
        }

        // ── ③ 롤백 처리 (입력 예측 불일치 수정) ───────────────────
        // 수신된 원격 입력과 예측값이 다른 가장 오래된 프레임으로 롤백
        int rollbackTo = gNetplay().getRollbackFrame(cur);
        if (rollbackTo >= 0 && rollbackTo < cur
                && m_npStates.contains(rollbackTo)) {
            // 크기 검증 — 불일치 시 unserialize 크래시 방지
            size_t expectedSz = m_core->serializeSize();
            const QByteArray& rbState = m_npStates[rollbackTo];
            if (expectedSz == 0
                    || static_cast<size_t>(rbState.size()) != expectedSz) {
                log(QString("⚠ 롤백 크기 불일치 — 건너뜀 (frame=%1 size=%2 expected=%3)")
                    .arg(rollbackTo).arg(rbState.size()).arg(expectedSz));
                m_npStates.clear();   // 불량 상태 전체 제거
            } else {
                gState.netplayResim = true;
                m_core->unserialize(rbState.constData(), rbState.size());
                gState.frameCount = rollbackTo;

                for (int rf = rollbackTo; rf < cur; ++rf) {
                    NpInputState& is = m_npInputHistory[rf];
                    uint16_t lb = is.local;
                    uint16_t rb = static_cast<uint16_t>(
                        gNetplay().getRemoteInput(static_cast<uint32_t>(rf)));
                    is.remote = rb;
                    // 재시뮬 확정값으로 예측 덮어씀 → 무한 롤백 루프 차단
                    gNetplay().recordPrediction(static_cast<uint32_t>(rf), rb);
                    npApplyInput(lb, rb);
                    m_core->run();
                    gState.frameCount = rf + 1;
                }
                gState.netplayResim = false;
            }  // else (크기 정상)
        }  // if (rollbackTo...)

        // ── ④ 현재 프레임 스냅샷 저장 (run() 직전) ──────────────
        // m_npStates[cur] = "cur 실행 직전" 상태 → 롤백 기준점
        // ★ 순수 GGPO: 풀스테이트 상시 전송 안 함 (이전의 sendState 제거).
        //   대신 확정 프레임에서 체크섬(8바이트)만 교환해 desync 를 감지한다.
        {
            size_t sz = m_core->serializeSize();
            if (sz > 0) {
                QByteArray buf(static_cast<int>(sz), Qt::Uninitialized);
                if (m_core->serialize(buf.data(), sz)) {
                    m_npStates[cur] = buf;

                    // ── 체크섬 교환 (desync 감지) ──────────────────
                    // 확정 프레임(양쪽 입력 final, 더 이상 롤백 안 됨)에서만 계산.
                    //   confirmed = min(cur, remoteMaxFrame)
                    uint32_t remoteF   = gNetplay().remoteMaxFrame();
                    uint32_t confirmed = std::min<uint32_t>(
                                            static_cast<uint32_t>(cur), remoteF);
                    if (confirmed >= NetplayManager::CHECKSUM_INTERVAL
                            && (confirmed % NetplayManager::CHECKSUM_INTERVAL) == 0
                            && confirmed > m_lastChecksumFrame
                            && m_npStates.contains(static_cast<int>(confirmed))) {
                        const QByteArray& cs = m_npStates[static_cast<int>(confirmed)];
                        uint32_t crc = npChecksum(cs);
                        m_localChecksums[confirmed] = crc;
                        gNetplay().sendChecksum(confirmed, crc);
                        m_lastChecksumFrame = confirmed;
                        checkDesync(confirmed);
                        // 오래된 체크섬 정리 (최근 것만 유지)
                        while (m_localChecksums.size() > 16)
                            m_localChecksums.erase(m_localChecksums.begin());
                    }
                }
            }
        }

        // ── ⑤ 입력 수집 · 전송 · 히스토리 저장 ──────────────────
        uint16_t rawBits = 0;
        for (int i = 0; i < 16; ++i)
            if (gState.rawKeys[i]) rawBits |= (1 << i);

        // ── 입력 지연 큐 (Fightcade-style delay-based netcode) ──
        // rawBits를 (cur + delay) 프레임에 예약, cur 프레임은 큐에서 꺼냄
        // delay=0이면 즉시 전송 (롤백 전용)
        uint16_t localBits = 0;
        {
            const int delay = qMax(0, gSettings.netplayInputDelay);
            if (delay > 0) {
                m_npDelayQueue[static_cast<uint32_t>(cur + delay)] = rawBits;
                localBits = m_npDelayQueue.value(static_cast<uint32_t>(cur), 0);
                m_npDelayQueue.remove(static_cast<uint32_t>(cur));
            } else {
                localBits = rawBits;
            }
        }

        gNetplay().sendInput(static_cast<uint32_t>(cur), localBits);

        // 입력 히스토리: local + remote 동시 보관 (재시뮬 시 사용)
        NpInputState& his = m_npInputHistory[cur];
        his.local  = localBits;
        uint16_t remoteBits = static_cast<uint16_t>(
            gNetplay().getRemoteInput(static_cast<uint32_t>(cur)));
        his.remote = remoteBits;
        gNetplay().recordPrediction(static_cast<uint32_t>(cur), remoteBits);

        // ── ⑥ 프레임 실행 ──────────────────────────────────────
        npApplyInput(localBits, remoteBits);
        m_core->run();
        gState.frameCount++;

        // ── ⑦ 오래된 버퍼 정리 ─────────────────────────────────
        gNetplay().confirmFramesUpTo(static_cast<uint32_t>(cur));
        int cutoff = cur - NetplayManager::MAX_ROLLBACK - 2;
        for (auto it = m_npStates.begin(); it != m_npStates.end(); )
            it = (it.key() < cutoff) ? m_npStates.erase(it) : ++it;
        while (!m_npInputHistory.empty() &&
               m_npInputHistory.begin()->first < cutoff)
            m_npInputHistory.erase(m_npInputHistory.begin());

    } else {
        // ── 싱글 플레이어 ─────────────────────────────────
        // 터보 처리: 활성 버튼을 turboPeriod 주기로 ON/OFF
        gState.turboFrame++;
        for (int i = 0; i < 16; ++i) {
            gState.keys[i] = gState.rawKeys[i];
            if (gState.turboBtns.value(i, false) && gState.rawKeys[i]) {
                // 눌린 상태일 때만 터보 적용 (뗀 상태는 0 유지)
                int phase = (gState.turboFrame / gState.turboPeriod) % 2;
                gState.keys[i] = (phase == 0) ? 1 : 0;
            }
        }

        // ── 로컬 2P~4P: 두 번째 이후 패드를 각 플레이어에 배정 ──────
        //   패드 1개 = 플레이어 1명. 연결 순서대로 2P, 3P, 4P.
        //   1P 는 위에서 키보드+첫 패드 합산으로 이미 만들어졌다.
        //   ※ 넷플레이 중에는 이 분기 자체를 타지 않으므로 영향 없음.
        if (m_gamepad) {
            auto spread = [](uint16_t bits, std::array<int,16>& dst) {
                for (int i = 0; i < 16; ++i) dst[i] = (bits >> i) & 1;
            };
            spread(m_gamepad->playerBits(2), gState.p2Keys);
            spread(m_gamepad->playerBits(3), gState.p3Keys);
            spread(m_gamepad->playerBits(4), gState.p4Keys);
        }

        // ── 서비스(TEST) 입력: 전용 핫키 펄스로만 전달 ───────────────
        // FBNeo 기판 서비스/테스트 입력 = RETRO_DEVICE_ID_JOYPAD_L2 (index 12).
        //   · 평소에는 항상 0 으로 막는다 → 게임패드 LT(L2)나 우발 입력으로
        //     서비스 메뉴에 들어가지 않는다.
        //   · 전용 "service" 핫키를 누르면 m_serviceHoldFrames 만큼 1 을 넣어
        //     기종 무관하게 테스트 입력을 직접 전송한다(마메식 전용 키).
        //   START 홀드와 완전히 분리 → START 오래 누르는 게임에서 오작동 없음.
        if (m_serviceHoldFrames > 0) {
            gState.keys[12] = 1;
            --m_serviceHoldFrames;
        } else {
            gState.keys[12] = 0;
        }

        // ── START 홀드 캡 제거 ───────────────────────────────────────────
        // NeoGeo 서비스 메뉴는 MVS BIOS 내부 로직으로 START 홀드를 감지함.
        // 프론트엔드 키 캡으로는 "보스선택 커맨드(30초 START 홀드)"와
        // "서비스 메뉴 진입(START 홀드)"을 구분할 수 없어 근본 해결 불가.
        // → NeoGeo 게임은 Machine Settings에서 Mode를 MVS→AES로 변경하면
        //   가정용 BIOS 사용으로 오퍼레이터 테스트 메뉴 자체가 사라짐.
        // → CPS 등 다른 시스템은 위의 L2(index 12) 차단으로 처리됨.
        m_startHoldFrames = 0;  // 사용 안 함 (변수 유지, 향후 확장용)

        int runs = gState.fastForward ? 3 : 1;
        for (int i = 0; i < runs; ++i) m_core->run();
        gState.frameCount++;
        pushFrameHistory();
    }

    // ── 치트 매 프레임 적용 ────────────────────────────────
    // 수동 치트 엔진(RAM 직접 쓰기)은 코어 네이티브 치트가 없을 때만 사용.
    //   네이티브 치트가 등록된 게임에서는 코어가 CheatEnable 로 적용하므로,
    //   여기서 또 쓰면 이중 적용/충돌이 된다.
    if (m_cheat && !m_nativeCheatsActive)
        m_cheat->applyFrame(m_core, gState.frameCount, gState.gameLoadFrame);

    // ── 오디오 처리 ────────────────────────────────────────
    if (m_audio && m_audio->isReady())
        m_audio->processDrc(0);

    // ── 녹화: 프레임 + 오디오 전송 (VideoRecorder — libav*) ─
    if (gState.isRecording && m_videoRecorder && m_videoRecorder->isOpen()
        && gState.videoWidth > 0) {
        // 비디오 프레임 (VideoRecorder 는 open() 시 width/height/pixFmt 를 기억함)
        m_videoRecorder->addVideoFrame(
            gState.videoBuffer.constData(),
            static_cast<int>(gState.videoPitch));

        // 오디오
        if (!gState.audioRecBuf.isEmpty()) {
            int samples = gState.audioRecBuf.size() / (2 * sizeof(int16_t));
            m_videoRecorder->addAudioSamples(
                reinterpret_cast<const int16_t*>(gState.audioRecBuf.constData()),
                samples);
            gState.audioRecBuf.clear();
        }
    }

    // ── 렌더링 ─────────────────────────────────────────────
    if (m_stack->currentIndex() == 1 && m_canvas)
        m_canvasW->update();
}

// ════════════════════════════════════════════════════════════
//  즐겨찾기
// ════════════════════════════════════════════════════════════
bool MainWindow::isFavorite(const QString& romName) const {
    return gSettings.favorites.contains(romName.toLower());
}

void MainWindow::toggleFavorite(const QString& romName) {
    QString lc = romName.toLower();
    if (gSettings.favorites.contains(lc)) {
        gSettings.favorites.removeAll(lc);
        log("☆ 즐겨찾기 제거: " + romName);
    } else {
        gSettings.favorites.append(lc);
        log("★ 즐겨찾기 추가: " + romName + QString("  (총 %1개)").arg(gSettings.favorites.size()));
    }
    gSettings.save();

    filterRoms();      // 목록 위치는 셸이 유지한다 (선택 게임 기준)
    log(QString("즐겨찾기 저장 완료. 현재 %1개").arg(gSettings.favorites.size()));
}

// ════════════════════════════════════════════════════════════
//  세이브스테이트 / 스크린샷
// ════════════════════════════════════════════════════════════
void MainWindow::saveState(int slot) {
    if (!gState.gameLoaded || !m_core) { log("게임이 실행 중이 아님"); return; }
    size_t sz = m_core->serializeSize();
    if (sz == 0) { log("세이브스테이트 미지원 코어"); return; }
    QByteArray buf(static_cast<int>(sz), Qt::Uninitialized);
    if (!m_core->serialize(buf.data(), sz)) { log("세이브 직렬화 실패"); return; }
    QDir().mkpath(gSettings.savePath);
    QString path = gSettings.savePath + "/" + m_selectedGame
                 + QString("_s%1.sav").arg(slot);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        log("세이브 파일 쓰기 실패: " + path); return;
    }
    f.write(buf);
    log(QString("💾 슬롯 %1 저장 — %2 bytes").arg(slot).arg(sz));
}

void MainWindow::loadState(int slot) {
    if (!gState.gameLoaded || !m_core) { log("게임이 실행 중이 아님"); return; }
    QString path = gSettings.savePath + "/" + m_selectedGame
                 + QString("_s%1.sav").arg(slot);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        log(QString("📂 슬롯 %1 없음").arg(slot)); return;
    }
    QByteArray buf = f.readAll();
    if (!m_core->unserialize(buf.constData(), buf.size()))
        log("로드 역직렬화 실패");
    else
        log(QString("📂 슬롯 %1 로드 완료").arg(slot));
}

// ── 프레임 기록 / FRAME LAB ─────────────────────────────────
//   게임이 도는 동안 최근 프레임을 쌓아 둔다. FRAME LAB 에서 뒤로 한 프레임씩 볼 때 쓴다.
static constexpr int kFrameHistory = 120;                 // 약 2초

void MainWindow::pushFrameHistory() {
    if (m_histGame != m_loadedGame) { m_frameHist.clear(); m_histGame = m_loadedGame; }
    const QImage img = currentFrameImage();
    if (img.isNull()) return;
    m_frameHist.append(img);
    while (m_frameHist.size() > kFrameHistory) m_frameHist.removeFirst();
}

// 게임이 멈춘 상태에서 코어를 정확히 한 프레임만 실행한다 (입력은 모두 뗀 상태로).
QImage MainWindow::stepOneFrame() {
    if (!gState.gameLoaded || !m_core || gNetplay().playing()) return QImage();
    gState.keys.fill(0);  gState.p2Keys.fill(0);  gState.p3Keys.fill(0);  gState.p4Keys.fill(0);
    m_core->run();
    gState.frameCount++;
    if (m_cheat && !m_nativeCheatsActive)
        m_cheat->applyFrame(m_core, gState.frameCount, gState.gameLoadFrame);
    const QImage img = currentFrameImage();
    if (!img.isNull()) {
        m_frameHist.append(img);
        while (m_frameHist.size() > kFrameHistory) m_frameHist.removeFirst();
    }
    return img;
}

void MainWindow::openFrameLab() {
    if (!gState.gameLoaded || m_frameHist.isEmpty() || m_histGame != m_loadedGame) {
        log(isEn() ? "FRAME LAB: run a game first (then press Tab to open the menu)"
                   : "프레임 랩: 먼저 게임을 실행한 뒤 Tab 으로 메뉴를 여세요");
        return;
    }
    if (gNetplay().playing()) {
        log(isEn() ? "FRAME LAB: not available during netplay"
                   : "프레임 랩: 넷플레이 중에는 쓸 수 없습니다");
        return;
    }
    m_lab->open(QVector<QImage>(m_frameHist.begin(), m_frameHist.end()),
                [this] { return stepOneFrame(); },
                [this](const QImage& img, int rel) {
                    QDir().mkpath(gSettings.screenshotPath);
                    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
                    const QString path = gSettings.screenshotPath + "/" + m_loadedGame + "_" + ts
                                         + "_f" + (rel >= 0 ? "+" : "") + QString::number(rel) + ".png";
                    if (!img.save(path)) return QString();
                    log("📷 " + path);
                    return path;
                },
                !isEn());
    m_stack->setCurrentIndex(4);
    m_lab->setFocus();
}

// 지금 코어가 내보낸 마지막 화면 (32비트로 통일). 프레임이 없으면 널 이미지.
QImage MainWindow::currentFrameImage() const {
    if (!gState.gameLoaded || gState.videoWidth == 0) return QImage();
    const int w = int(gState.videoWidth), h = int(gState.videoHeight), pitch = int(gState.videoPitch);
    const bool x8888 = gState.pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888;
    const QImage view(reinterpret_cast<const uchar*>(gState.videoBuffer.constData()), w, h, pitch,
                      x8888 ? QImage::Format_RGB32 : QImage::Format_RGB16);
    return x8888 ? view.copy() : view.convertToFormat(QImage::Format_RGB32);
}

void MainWindow::takeScreenshot() {
    const QImage img = currentFrameImage();
    if (img.isNull()) { log("스크린샷: 프레임 없음"); return; }
    QDir().mkpath(gSettings.screenshotPath);
    QString ts   = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString path = gSettings.screenshotPath + "/" + m_selectedGame + "_" + ts + ".png";
    if (img.save(path)) log("📷 " + path);
    else                log("📷 스크린샷 저장 실패");
}

// ── 프리뷰 이미지 저장 ───────────────────────────────────────
void MainWindow::savePreviewShot() {
    const QImage img = currentFrameImage();
    if (img.isNull()) {
        log("프리뷰 저장: 프레임 없음 (게임 실행 중이 아님)"); return;
    }
    if (m_selectedGame.isEmpty()) {
        log("프리뷰 저장: 선택된 게임 없음"); return;
    }

    QDir().mkpath(gSettings.previewPath);

    // 기존 프리뷰 이미지(확장자 무관)를 모두 지워 중복/유령 파일이 남지 않게 한다.
    // (파일명은 롬 이름만 → previews/{rom}.png 하나만 유지)
    for (const QString ext : {"png","jpg","jpeg","bmp","gif"}) {
        QString old = gSettings.previewPath + "/" + m_selectedGame + "." + ext;
        if (QFile::exists(old)) QFile::remove(old);
    }

    QString path = gSettings.previewPath + "/" + m_selectedGame + ".png";
    if (img.save(path)) {
        log("🖼 프리뷰 저장: " + path);
        // 저장 즉시 프리뷰 패널에 반영 (재선택 없이 바로 표시)
        loadPreview(m_selectedGame);
    } else {
        log("🖼 프리뷰 저장 실패: " + path);
    }
}

// ── 프리뷰 영상 녹화 토글 ────────────────────────────────────
void MainWindow::togglePreviewRecord() {
    if (gState.isRecording) {
        // stopRecording() 호출 전에 경로 저장 (stop 내부에서 clear 됨)
        QString finalPath   = gState.lastRecordPath;
        QString romName     = m_selectedGame;

        stopRecording();  // VideoRecorder::close() → 파일 즉시 완료

        if (!finalPath.isEmpty() && !romName.isEmpty()) {
            QString previewDest = gSettings.previewPath + "/" + romName + ".mp4";
            // VideoRecorder 는 동기 flush 이므로 딜레이 없이 바로 복사 가능
            QDir().mkpath(QFileInfo(previewDest).absolutePath());

            // 기존 프리뷰 영상(확장자 무관)을 모두 지워 중복이 남지 않게 한다.
            for (const QString ext : {"mp4","avi","mkv","webm","mov"}) {
                QString old = gSettings.previewPath + "/" + romName + "." + ext;
                if (QFile::exists(old)) QFile::remove(old);
            }

            if (QFile::copy(finalPath, previewDest)) {
                log("🎬 프리뷰 영상 저장: " + previewDest);
                // 저장 즉시 프리뷰 패널에 반영 (이미지→3초 후 영상 자동재생)
                loadPreview(m_selectedGame);
            } else {
                log("🎬 프리뷰 영상 복사 실패 — 원본: " + finalPath);
            }
        }
    } else {
        // 녹화 시작
        startRecording();
        log("🎬 프리뷰 녹화 시작 — 완료 후 다시 버튼을 누르면 previews/ 에 저장됩니다");
    }
}

// ════════════════════════════════════════════════════════════
//  녹화 (Phase 8)
// ════════════════════════════════════════════════════════════
void MainWindow::toggleRecording() {
    if (gState.isRecording) stopRecording();
    else                    startRecording();
}

void MainWindow::startRecording() {
    if (!gState.gameLoaded) { log("녹화: 게임 실행 중이 아닙니다"); return; }
    if (gState.isRecording)  return;

    if (gState.videoWidth == 0 || gState.videoHeight == 0) {
        log("녹화: 비디오 해상도를 알 수 없습니다 (프레임 없음)");
        return;
    }

    QDir().mkpath(gSettings.recordPath);
    QString ts      = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outPath = gSettings.recordPath + "/" + m_selectedGame + "_" + ts + ".mp4";

    double fps = gState.coreFps > 0.0 ? gState.coreFps : 60.0;
    VideoPixelFormat vpf = (gState.pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888)
                           ? VPF_XRGB8888 : VPF_RGB565;

    m_videoRecorder = new VideoRecorder();
    if (!m_videoRecorder->open(outPath,
                               static_cast<int>(gState.videoWidth),
                               static_cast<int>(gState.videoHeight),
                               fps,
                               gSettings.audioSampleRate,
                               2,   // stereo
                               vpf))
    {
        log("🔴 녹화 시작 실패: " + m_videoRecorder->lastError());
        delete m_videoRecorder;
        m_videoRecorder = nullptr;
        return;
    }

    gState.isRecording    = true;
    gState.lastRecordPath = outPath;
    gState.audioRecBuf.clear();

    if (m_canvas) m_canvas->setRecording(true);
    log("🔴 녹화 시작 → " + outPath);
}

void MainWindow::stopRecording() {
    if (!gState.isRecording) return;
    gState.isRecording = false;

    if (m_canvas) m_canvas->setRecording(false);

    QString dest = gState.lastRecordPath;
    gState.lastRecordPath.clear();

    if (m_videoRecorder) {
        // close() 는 동기 완료 (WMF: Finalize(), FFmpeg: flush+trailer)
        m_videoRecorder->close();
        delete m_videoRecorder;
        m_videoRecorder = nullptr;
        log("■ 녹화 완료: " + dest);
    }
}

// ════════════════════════════════════════════════════════════
//  마우스 클릭음 — WAV(PCM) 직접 재생
//  · QSoundEffect 는 두어 번 재생하면 소리가 끊기는 문제가 있어 쓰지 않는다.
//    (백엔드 스트림이 재시작되지 않아 이후 play() 가 무음이 됨)
//  · 리소스에서 PCM 을 한 번 읽어 두고, 클릭마다 sink 를 재시작한다.
// ════════════════════════════════════════════════════════════
void MainWindow::loadClickSound() {
    QFile f(":/assets/clik.wav");
    if (!f.open(QIODevice::ReadOnly)) { qWarning("클릭음 리소스 없음"); return; }
    const QByteArray wav = f.readAll();
    f.close();
    if (wav.size() < 44 || !wav.startsWith("RIFF") || wav.mid(8, 4) != "WAVE") {
        qWarning("클릭음: WAV 형식 아님"); return;
    }

    // 청크를 순회해 fmt/data 를 찾는다 (LIST 등 부가 청크가 있어도 안전)
    auto u16 = [&](int o){ return quint16(quint8(wav[o])) | (quint16(quint8(wav[o+1])) << 8); };
    auto u32 = [&](int o){ return quint32(quint8(wav[o]))        | (quint32(quint8(wav[o+1])) << 8)
                                | (quint32(quint8(wav[o+2])) << 16) | (quint32(quint8(wav[o+3])) << 24); };

    int rate = 0, ch = 0, bits = 0;
    QByteArray pcm;
    for (int pos = 12; pos + 8 <= wav.size(); ) {
        const QByteArray id = wav.mid(pos, 4);
        const int sz = int(u32(pos + 4));
        const int body = pos + 8;
        if (sz < 0 || body + sz > wav.size()) break;

        if (id == "fmt " && sz >= 16) {
            ch   = u16(body + 2);
            rate = int(u32(body + 4));
            bits = u16(body + 14);
        } else if (id == "data") {
            pcm = wav.mid(body, sz);
        }
        pos = body + sz + (sz & 1);        // 청크는 짝수 정렬
    }
    if (pcm.isEmpty() || rate <= 0 || ch <= 0 || bits != 16) {
        qWarning("클릭음: 지원하지 않는 WAV (16bit PCM 필요)"); return;
    }

    QAudioFormat fmt;
    fmt.setSampleRate(rate);
    fmt.setChannelCount(ch);
    fmt.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) { qWarning("클릭음: 오디오 출력 장치 없음"); return; }

    // ★ 오디오 장치를 미리 열어두지 않는다.
    //   상시 스트림을 유지하면 PulseAudio 가 write 를 거부해
    //   ("pa_stream_write error: Invalid argument") 40ms 마다 오류가 쏟아지고,
    //   그 상태에서 프리뷰 영상이 오디오를 열면 프로그램이 멈췄다.
    //   → 클릭할 때만 짧게 열고 재생이 끝나면 닫는다. 장치를 붙잡지 않으므로
    //     프리뷰 영상/에뮬 오디오와 충돌하지 않는다.
    m_sfxFmt       = fmt;
    m_sfxPcm       = pcm;
    m_sfxBytesPerMs = double(rate) * ch * 2 / 1000.0;   // 재생 길이 계산용
    qDebug("클릭음 준비: %d Hz, %dch, %d bytes", rate, ch, pcm.size());
}

void MainWindow::playClickSound() {
    if (m_sfxPcm.isEmpty()) return;

    // 연타 제한 — 소리가 겹쳐 뭉치는 것과 장치 재열기 부담을 막는다
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_sfxLastPlay < 90) return;
    m_sfxLastPlay = now;

    // 이전 재생 정리 (장치를 붙잡고 있지 않도록 확실히 닫는다)
    if (m_sfxSink) { m_sfxSink->stop(); m_sfxSink->deleteLater(); m_sfxSink = nullptr; }
    if (m_sfxBuf)  { m_sfxBuf->close();  m_sfxBuf->deleteLater();  m_sfxBuf  = nullptr; }

    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) return;

    m_sfxBuf = new QBuffer(&m_sfxPcm, this);
    if (!m_sfxBuf->open(QIODevice::ReadOnly)) return;

    m_sfxSink = new QAudioSink(dev, m_sfxFmt, this);
    m_sfxSink->setVolume(0.35);
    m_sfxSink->start(m_sfxBuf);      // pull 모드 — 버퍼에서 읽어간다

    // ★ IdleState 를 보고 정지시키면 안 된다.
    //   QAudioSink 는 start 직후 데이터를 읽기 전에 잠깐 Idle 로 보고하는데,
    //   그때 stop() 하면 한 번도 울리지 않는다(실측: 3회 중 0회 재생).
    //   → 재생 길이만큼 기다린 뒤 정지해 장치를 놓아준다.
    const int clipMs = m_sfxBytesPerMs > 0
                       ? int(m_sfxPcm.size() / m_sfxBytesPerMs) + 150 : 400;
    QPointer<QAudioSink> guard(m_sfxSink);
    QTimer::singleShot(clipMs, this, [guard]{ if (guard) guard->stop(); });
}

// ════════════════════════════════════════════════════════════
//  장치별 패드 프로필 적용
//   패드마다 버튼 번호 체계가 달라(8BitDo 16버튼 / Xbox 11버튼) 매핑을
//   장치 이름별로 저장해 두고, 연결될 때마다 해당 프로필을 적용한다.
//   저장된 프로필이 없으면 기본 매핑으로 시작한다.
// ════════════════════════════════════════════════════════════
// 실제로 적용된 패드 매핑을 로그에 남긴다.
//   기본값이 저장된 프로필에 덮여도 겉으로는 알 수 없어서, 무엇이 쓰이는지
//   눈으로 확인할 수 있게 한다. (배치 문제를 두 번 놓친 뒤 추가)
void MainWindow::logPadMappingSummary() {
    if (!m_gamepad) return;
    int pad = -1;
    for (int i = 0; i < 4; ++i)
        if (m_gamepad->padPresent(i)) { pad = i; break; }
    if (pad < 0) return;

    const QString name = m_gamepad->padName(pad);
    const QString src  = m_padMapSource[pad].isEmpty() ? QString("?")
                                                       : m_padMapSource[pad];
    const QHash<int,int> m = m_gamepad->padMapping(pad);

    // 패드 버튼 → libretro 인덱스 (사람이 읽을 수 있게)
    struct { int bit; const char* nm; } btns[] = {
#ifdef _WIN32
        {0x4000,"X"}, {0x8000,"Y"}, {0x0200,"R1"},
        {0x1000,"A"}, {0x2000,"B"}, {0x20000,"R2"}, {0x0100,"L1"},
#else
        {1<<2,"X"}, {1<<3,"Y"}, {1<<5,"R1"},
        {1<<0,"A"}, {1<<1,"B"}, {1<<17,"R2"}, {1<<4,"L1"},
#endif
    };
    QStringList parts;
    for (const auto& b : btns)
        parts << QString("%1=%2").arg(b.nm)
                 .arg(m.contains(b.bit) ? QString::number(m.value(b.bit))
                                        : QString("-"));
    // 진단용 — 화면 로그가 아니라 crash_log 에만 남긴다
    qDebug().noquote() << QString("[pad] %1 출처=%2 : %3")
                          .arg(name, src, parts.join(" "));
}

// ── 패드 매핑: 해석은 PadMapping.cpp 한 곳에서만 한다 ────────
QString MainWindow::padScopeKeyDev(const QString& device) const {
    return ::padScopeKeyDev(device,
        m_gamepad ? m_gamepad->padLayout() : PadLayout::Standard);
}

// 코어가 보낸 버튼 정의로 6버튼 기본 매핑을 확정한다.
//   사용자가 원하는 배치 그대로 채운다:
//        X   Y   R1        LP  MP  HP
//        A   B   R2        LK  MK  HK
void MainWindow::applyCoreButtonDefs() {
    if (!m_gamepad) return;
    const CoreSixButtons cb = resolveSixButtons(gState.inputDesc, padLayoutOf(m_loadedGame) == PadLayout::SixButton);
    m_coreBtns = cb;

    if (!cb.valid) {
        // 6버튼 게임이 아니다 → 일반 배치로 확정 (롬 목록 추정이 틀렸어도 교정된다)
        if (m_gamepad->padLayout() != PadLayout::Standard) {
            m_gamepad->setPadLayout(PadLayout::Standard);
            qDebug().noquote() << "[pad] 배치=일반 (코어 정의 기준)";
        }
        applyPadProfiles();
        reloadKeymap();
        refreshControlsUi();
        logPadMappingSummary();
        return;
    }

    if (m_gamepad->padLayout() != PadLayout::SixButton) {
        m_gamepad->setPadLayout(PadLayout::SixButton);
        qDebug().noquote() << "[pad] 배치=6버튼 (코어 정의 기준)";
    }
    qDebug().noquote() << QString("[pad] 6버튼 LP=%1 MP=%2 HP=%3 / LK=%4 MK=%5 HK=%6")
                          .arg(cb.lp).arg(cb.mp).arg(cb.hp).arg(cb.lk).arg(cb.mk).arg(cb.hk);

    applyPadProfiles();      // 저장된 사용자 설정이 있으면 그게 여전히 우선
    reloadKeymap();
    refreshControlsUi();
    logPadMappingSummary();
}

// 배치 기본값 — 코어가 알려준 인덱스가 있으면 그것을 쓴다.
//   하드코딩된 표는 게임 실행 전(메뉴)에만 쓰이는 추정치다.
QHash<int,int> MainWindow::layoutDefaultMap() const {
    return GamepadManager::makeDefaultMapping(
        m_gamepad ? m_gamepad->padLayout() : PadLayout::Standard);
}

QHash<int,int> MainWindow::resolvePadMap(const QString& device,
                                         const QString& rom,
                                         QString* sourceOut) const {
    const PadLayout lay = m_gamepad ? m_gamepad->padLayout() : PadLayout::Standard;
    PadMapSource src = PadMapSource::Default;
    // 우선순위: 이 게임(자동 저장) > 이 기종(직접 저장) > 이 장치의 배치별 표 > 배치 기본값.
    //   어느 것이 적용 중인지는 컨트롤 화면의 "적용 중" 줄에 나온다.
    PadMap m = ::resolvePadMap(gSettings.padMaps, device, rom, platScopeKey(rom), lay,
                               layoutDefaultMap(), &src);
    if (sourceOut) *sourceOut = padMapSourceLabel(src, isEn());
    return m;
}

// 현재 대상 게임 (실행 중이면 그 게임, 아니면 목록에서 고른 게임)
QString MainWindow::currentPadRom() const {
    return m_loadedGame.isEmpty() ? m_selectedGame : m_loadedGame;
}

// 범위("game"/"plat"/"all")의 저장 키: "game:<롬>" / "plat:<기종>" / "all". 게임이 없으면 "".
QString MainWindow::bezelKeyFor(const QString& scope) const {
    const QString rom = m_loadedGame.isEmpty() ? m_selectedGame : m_loadedGame;
    if (scope == "game") return rom.isEmpty() ? QString() : ("game:" + rom);
    if (scope == "plat") {
        const QString plat = gamePlatform(rom);
        return plat.isEmpty() ? QString() : ("plat:" + plat);
    }
    return QStringLiteral("all");
}

QString MainWindow::bezelScopeLabel(const QString& scope) const {
    const QString rom  = m_loadedGame.isEmpty() ? m_selectedGame : m_loadedGame;
    const QString plat = gamePlatform(rom);
    const bool    en   = isEn();
    if (scope == "game")
        return rom.isEmpty() ? (en ? "THIS GAME" : "이 게임")
                             : (en ? QString("THIS GAME (%1)").arg(rom) : QString("이 게임 (%1)").arg(rom));
    if (scope == "plat")
        return plat.isEmpty() ? (en ? "THIS PLATFORM" : "이 기종")
                              : (en ? QString("THIS PLATFORM (%1)").arg(plat)
                                    : QString("이 기종 (%1 전체)").arg(plat));
    return en ? "ALL GAMES" : "모든 게임";
}

// 지금 베젤이 어떻게 적용되고 있는지 (VIDEO 화면의 안내 줄)
QString MainWindow::bezelInfoText() const {
    const QString rom  = m_loadedGame.isEmpty() ? m_selectedGame : m_loadedGame;
    const QString plat = gamePlatform(rom);
    const bool    en   = isEn();
    if (!gSettings.bezelEnabled) return en ? "Bezel is off." : "베젤이 꺼져 있습니다.";
    QString why;
    const QString path = gSettings.bezelPathFor(rom, plat, &why);
    return path.isEmpty()
        ? (en ? "No bezel found for this game (put a PNG in bezels/)."
              : "이 게임에 쓸 베젤이 없습니다 (bezels/ 에 PNG 를 넣으세요).")
        : (en ? QString("Now: %1  (%2)").arg(QFileInfo(path).fileName(), why)
              : QString("현재 적용: %1  (%2)").arg(QFileInfo(path).fileName(), why));
}

void MainWindow::assignBezel(const QString& key, const QString& value) {
    gSettings.bezelAssign[key] = value;
    gSettings.bezelEnabled = true;
    gSettings.save();
    applyBezel();
}

void MainWindow::clearBezelAssign(const QString& key) {
    if (gSettings.bezelAssign.remove(key) > 0) {
        gSettings.save();
        log(QString(isEn() ? "Bezel assignment cleared: %1" : "베젤 배정 해제: %1").arg(key));
    }
    applyBezel();
}

// 셰이더 파일을 걸고 저장한다. 컴파일/링크에 실패하면 설정을 되돌리고 안내한다.
bool MainWindow::applyShaderFile(const QString& p) {
    gSettings.videoShaderPath = p;
    const bool ok = m_canvas ? m_canvas->setShaderPath(p) : true;
    if (ok) {
        log("✔ 셰이더 로드: " + QFileInfo(p).fileName());
    } else {
        gSettings.videoShaderPath.clear();
        QMessageBox::warning(this, isEn() ? "Shader error" : "셰이더 오류",
            (isEn() ? "Shader compile/link failed.\n\nFile: "
                    : "셰이더 컴파일/링크에 실패했습니다.\n\n파일: ") + QFileInfo(p).fileName() +
            (isEn() ? "\n\nSee the log panel for details."
                    : "\n\n아래 로그 패널에서 오류 내용을 확인하세요."));
    }
    gSettings.save();
    return ok;
}

void MainWindow::clearShaderFile() {
    gSettings.videoShaderPath.clear();
    if (m_canvas) m_canvas->setShaderPath({});
    gSettings.save();
    log("셰이더 해제");
}

// 터보 설정은 셸에서 바꾸는 즉시 저장한다 (예전엔 APPLY 버튼에서만 저장했다).
//   게임이 정해져 있으면 그 게임 범위에 자동 저장한다 (다른 게임의 터보는 그대로).
void MainWindow::saveTurboSettings() {
    const QString rom = currentPadRom();
    if (rom.isEmpty()) {
        gSettings.turboPeriod = gState.turboPeriod;
        QStringList turboList;
        for (auto it = gState.turboBtns.begin(); it != gState.turboBtns.end(); ++it)
            if (it.value()) turboList.append(QString::number(it.key()));
        gSettings.turboButtons = turboList.join(',');
    } else {
        gSettings.turboScoped[gameScopeKey(rom)] = turboToString();
    }
    gSettings.save();
}

// 지금 켜 둔 터보를 "0,8,1|6" 형태로 (켠 버튼 인덱스 | 주기)
QString MainWindow::turboToString() const {
    QStringList l;
    for (auto it = gState.turboBtns.begin(); it != gState.turboBtns.end(); ++it)
        if (it.value()) l.append(QString::number(it.key()));
    l.sort();
    return l.join(',') + QLatin1Char('|') + QString::number(gState.turboPeriod);
}

// 게임 > 기종 > 전역 순으로 터보 설정을 골라 적용한다.
void MainWindow::applyTurboFor(const QString& rom) {
    QString v;
    if (!rom.isEmpty()) {
        v = gSettings.turboScoped.value(gameScopeKey(rom));
        if (v.isEmpty()) v = gSettings.turboScoped.value(platScopeKey(rom));
    }
    if (v.isEmpty())
        v = gSettings.turboButtons + QLatin1Char('|') + QString::number(gSettings.turboPeriod);
    const QStringList parts = v.split(QLatin1Char('|'));
    gState.turboBtns.clear();
    for (const QString& s : parts.value(0).split(',', Qt::SkipEmptyParts)) {
        bool ok = false; const int idx = s.trimmed().toInt(&ok);
        if (ok && idx >= 0 && idx < 16) gState.turboBtns[idx] = true;
    }
    bool ok = false; const int per = parts.value(1).toInt(&ok);
    gState.turboPeriod = (ok && per >= 1 && per <= 30) ? per : gSettings.turboPeriod;
}

// 현재 게임에 맞는 베젤을 캔버스에 올린다 (꺼져 있거나 파일이 없으면 해제).
void MainWindow::applyBezel() {
    if (!m_canvas) return;
    if (m_menu) m_menu->refreshOpen();      // VIDEO 화면의 안내 줄

    if (!gSettings.bezelEnabled) { m_canvas->setBezelImage(QImage()); return; }

    const QString rom  = m_loadedGame.isEmpty() ? m_selectedGame : m_loadedGame;
    QString why;
    const QString path = gSettings.bezelPathFor(rom, gamePlatform(rom), &why);
    if (path.isEmpty()) {
        m_canvas->setBezelImage(QImage());
        return;
    }
    QImage img(path);
    if (img.isNull()) {
        m_canvas->setBezelImage(QImage());
        log("⚠ " + QString(isEn() ? "Bezel load failed: " : "베젤 로드 실패: ")
            + QFileInfo(path).fileName());
        return;
    }
    m_canvas->setBezelImage(img);
    log(QString("🖼 %1: %2 (%3)")
        .arg(isEn() ? "Bezel" : "베젤", QFileInfo(path).fileName(), why));
}

void MainWindow::applyPadProfiles() {
    if (!m_gamepad) return;
    for (int i = 0; i < 4; ++i) {
        if (!m_gamepad->padPresent(i)) continue;
        const QString name = m_gamepad->padName(i);

        // ★ 프로필이 없어도 "기본 매핑의 사본"을 각 패드에 넣어 둔다.
        //   빈 값으로 두면 공용 매핑을 폴백으로 쓰게 되어, 다른 패드를
        //   건드렸을 때 같이 영향을 받는다. 사본을 주면 완전히 독립된다.
        QString src;
        const PadMap stored = resolvePadMap(name, currentPadRom(), &src);
        m_padUiMap[i]      = stored;                     // 화면에 보여줄 표
        m_padMapSource[i]  = src;
        // 게임에는 이 게임의 실제 인덱스로 바꿔서 넘긴다
        m_gamepad->setPadMapping(i, materializePadMap(stored, m_coreBtns));

        // 배정: 저장값이 있으면 그것.
        //   저장값이 없을 때는 첫 패드만 1P 로 두고 나머지는 "사용 안 함".
        //   ★ 스팀 인풋은 같은 컨트롤러를 가상 장치로 하나 더 만들어 내므로,
        //     자동으로 2P·3P 까지 배정하면 패드 하나가 두 플레이어를 동시에
        //     조종하게 된다. 추가 패드는 사용자가 직접 켜도록 한다.
        if (gSettings.padAssign.contains(name))
            m_gamepad->setPadPlayer(i, gSettings.padAssign.value(name));
        else
            m_gamepad->setPadPlayer(i, (i == 0) ? 1 : 0);
    }
}

// ════════════════════════════════════════════════════════════
//  화면보호기 — 무조작 5분 후 프리뷰 영상을 무작위 전체화면 재생
//   같은 화면이 계속 떠 있어 생기는 번인을 줄이기 위한 기능.
//   아무 입력(키·마우스·패드)이 들어오면 즉시 빠져나온다.
// ════════════════════════════════════════════════════════════
void MainWindow::resetIdleTimer() {
    m_ssIdleTicks = 0;
    if (m_ssActive) stopScreensaver();   // 재생 중이면 즉시 해제
}

void MainWindow::startScreensaver() {
    if (m_ssActive || !m_stack || !m_ssPage) return;
    if (m_stack->currentIndex() != 0) return;
    if (gState.gameLoaded || gState.isPaused || m_captureActive) return;

    // 재생할 영상이 하나도 없으면 켜지 않는다
    QDir dir(gSettings.previewPath);
    const QStringList vids = dir.entryList(
        {"*.mp4", "*.avi", "*.mkv", "*.webm", "*.mov"}, QDir::Files);
    if (vids.isEmpty()) return;

    m_ssActive = true;
    if (m_previewVidTimer) m_previewVidTimer->stop();
#if HAVE_FFMPEG
    if (m_previewVideo) m_previewVideo->stop();     // 프리뷰 재생과 충돌 방지
#endif
    if (m_mediaPlayer) m_mediaPlayer->stop();

    if (m_ssLabel) m_ssLabel->setPixmap(QPixmap());
    m_stack->setCurrentIndex(2);
    // 번인 방지가 목적이므로 창모드였다면 전체화면으로 띄운다 (해제 시 원복)
    m_ssWasWindowed = !isFullScreen();
    if (m_ssWasWindowed) showFullScreen();
    log("💤 화면보호기 시작 — 아무 키나 누르면 해제됩니다");
    playRandomScreensaverVideo();
}

void MainWindow::stopScreensaver() {
    if (!m_ssActive) return;
    m_ssActive = false;
#if HAVE_FFMPEG
    if (m_previewVideo) m_previewVideo->stop();
#endif
    if (m_mediaPlayer) m_mediaPlayer->stop();
    if (m_ssLabel) { m_ssLabel->setPixmap(QPixmap()); m_ssLabel->show(); }
    if (m_stack)  m_stack->setCurrentIndex(0);
    if (m_ssWasWindowed) { m_ssWasWindowed = false; showNormal(); }
    log("💤 화면보호기 해제");

    // 방금 나온 영상의 게임으로 목록 커서를 옮긴다 (previews/<롬>.mp4 → 롬 이름)
    revealGame(QFileInfo(m_ssLastFile).completeBaseName());

    // 선택된 게임의 프리뷰를 다시 보여준다
    if (!m_selectedGame.isEmpty()) {
        m_previewVideoDone = true;      // 복귀 직후 영상이 또 뜨지 않게
        loadPreview(m_selectedGame);
    }
    m_ssIdleTicks = 0;
}

// 게임 목록에서 그 롬을 골라 보이는 자리로 스크롤한다.
//   필터나 검색어 때문에 목록에 없으면 필터를 풀어서라도 보여 준다.
void MainWindow::revealGame(const QString& rom) {
    if (rom.isEmpty() || !m_shell) return;
    bool known = false;
    for (const auto& pr : m_allRoms) if (pr.second == rom) { known = true; break; }
    if (!known) return;

    bool inList = false;
    for (const ListRow& r : m_rows) if (r.rom == rom) { inList = true; break; }
    if (!inList) {
        m_glFilter = 0;
        m_hwFilter.clear();
        m_searchText.clear();
        m_shell->setSearchText(QString());
        filterRoms();            // 목록을 다시 만든다 (선택은 아래에서)
    }
    selectGame(rom);
    syncShellList();             // 셸의 커서·스크롤을 선택한 게임으로
}

void MainWindow::playRandomScreensaverVideo() {
    if (!m_ssActive) return;
    QDir dir(gSettings.previewPath);
    QStringList vids = dir.entryList(
        {"*.mp4", "*.avi", "*.mkv", "*.webm", "*.mov"}, QDir::Files);
    if (vids.isEmpty()) { stopScreensaver(); return; }

    // 바로 직전 영상은 빼고 고른다 (한 편만 있으면 그대로 반복)
    if (vids.size() > 1 && !m_ssLastFile.isEmpty()) vids.removeAll(m_ssLastFile);
    const QString pick = vids.at(QRandomGenerator::global()->bounded(vids.size()));
    m_ssLastFile = pick;
    const QString path = dir.filePath(pick);

#if HAVE_FFMPEG
    if (m_previewVideo) {
        m_previewVideo->setVolume(gSettings.audioVolume);
        if (!m_previewVideo->open(path)) { stopScreensaver(); return; }
    }
#else
    if (m_mediaPlayer) {
        m_mediaPlayer->setSource(QUrl::fromLocalFile(path));
        m_mediaPlayer->setLoops(1);
        m_mediaPlayer->play();
    }
#endif
    qDebug().noquote() << QString("[saver] %1").arg(pick);
}

void MainWindow::retranslateUi() {
    if (m_menu) m_menu->retranslate();     // 카테고리·버튼·열려 있는 메뉴를 새 언어로
}

void MainWindow::toggleLanguage() {
    gSettings.uiLanguage = isEn() ? "ko" : "en";
    retranslateUi();
    gSettings.save();
    log(isEn() ? "Language: English" : "언어: 한국어");
}

// ════════════════════════════════════════════════════════════
//  설정 적용 / 갱신
// ════════════════════════════════════════════════════════════
// ── 셰이더 파라미터 편집 ─────────────────────────────────────
//   RetroArch 의 "셰이더 파라미터" 메뉴에 해당한다. 값은 프리셋 파일 이름으로
//   묶어 config.json 에 남기고, 다음에 같은 셰이더를 걸면 자동으로 복원된다.
void MainWindow::openShaderParams() {
    if (!m_canvas) return;
    const QString key = m_canvas->shaderKey();
    if (key.isEmpty() || m_canvas->shaderParameters().isEmpty()) {
        QMessageBox::information(this,
            isEn() ? "Shader parameters" : "셰이더 파라미터",
            isEn() ? "Load a .slang / .slangp shader first.\n"
                     "(Legacy .glsl shaders have no parameters.)"
                   : "먼저 .slang / .slangp 셰이더를 불러오세요.\n"
                     "(구형 .glsl 셰이더에는 파라미터가 없습니다.)");
        return;
    }

    ShaderParamDialog dlg(m_canvas, key, isEn(), this);
    connect(&dlg, &ShaderParamDialog::parameterChanged, this,
            [this, key](const QString& name, float value) {
                gSettings.shaderParams[key][name] = value;
            });
    connect(&dlg, &ShaderParamDialog::resetRequested, this,
            [this, key] { gSettings.shaderParams.remove(key); });
    dlg.exec();
    gSettings.save();
    log(QString("셰이더 파라미터 저장: %1").arg(key));
}

// 저장된 화면·음량 설정을 지금 화면과 오디오에 반영한다.
//   옛 APPLY 와 셸의 설정 초기화가 같은 코드를 쓴다.
void MainWindow::applyLiveSettings() {
    if (m_canvas) {
        m_canvas->setScaleMode(gSettings.videoScaleMode);
        m_canvas->setSmooth(gSettings.videoSmooth);
        m_canvas->setCrtMode(gSettings.videoCrtMode, gSettings.videoCrtIntensity);
        m_canvas->setFlashGuard(gSettings.videoFlashGuard,
                                gSettings.videoFlashStrength / 100.0f);
    }
    if (m_audio) m_audio->setVolume(gSettings.audioVolume / 100.0);
}

// 코어가 네이티브 치트를 등록했으면 수동 치트 엔진(RAM 직접 쓰기)을 멈춘다.
//   둘 다 돌리면 같은 값을 두 번 써서 충돌한다. 메인 루프가 이 플래그를 본다.
void MainWindow::refreshNativeCheatFlag() {
    m_nativeCheatsActive = nativecheats::any();
}

// ROM/프리뷰 폴더가 바뀐 뒤에 부른다.
//   코어의 저장·시스템(BIOS 탐색) 폴더를 맞추고 게임 목록을 다시 읽는다.
//   (scanRoms 가 끝에서 filterRoms 까지 하므로 따로 부를 필요가 없다)
void MainWindow::applyPathSettings() {
    if (m_core) {
        m_core->setSaveDir(gSettings.savePath);
        // ROM 경로 변경 시 system dir 도 갱신 (BIOS 파일 탐색 경로)
        m_core->setSystemDir(gSettings.romPath.isEmpty() ? AppSettings::baseDir()
                                                         : gSettings.romPath);
    }
    scanRoms();
    syncShellPreview();      // 프리뷰 폴더가 바뀌었으면 그림도 다시 찾는다
}

// ════════════════════════════════════════════════════════════
//  게임 화면 전환 헬퍼
// ════════════════════════════════════════════════════════════

// 게임 화면 진입: 프리뷰 완전 정지 후 캔버스 표시
// 빈 자리 위젯을 게임 캔버스로 바꿔 끼운다 (처음 한 번만). 스택 번호는 그대로 1번이다.
void MainWindow::attachCanvas() {
    if (!m_canvasHolder || !m_canvas) return;
    const bool soft = gSettings.videoRenderer == QLatin1String("software");
    qDebug(soft ? "[gfx] 게임 화면(소프트웨어)을 창에 연결합니다"
                : "[gfx] 게임 캔버스를 창에 연결합니다 (이 시점부터 GPU 합성 사용)");
    m_stack->insertWidget(1, m_canvasW);
    m_stack->removeWidget(m_canvasHolder);
    m_canvasHolder->deleteLater();
    m_canvasHolder = nullptr;
#ifdef _WIN32
    //   창 캡처로 실제 화면을 확인하는 방식이라 Windows 에서만 쓴다 (Wayland/gamescope 는 캡처가 막혀 오판한다)
    if (!soft) startCompositingWatch();     // OpenGL 합성이 안 되는 PC 를 자동으로 알아챈다
#endif
}

// ── 화면 합성 점검 ───────────────────────────────────────────
//   일부 PC(구형 내장 그래픽 등)는 OpenGL 게임 화면을 붙이는 순간 창 전체가 검게 나온다.
//   게임이 밝은 장면을 내보내는데도 실제 화면(창 캡처)이 계속 검으면 소프트웨어 렌더러로 바꿔 다시 시작한다.
static double lumaOf(const QImage& src) {
    if (src.isNull()) return 0.0;
    const QImage s = src.scaled(24, 24, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    for (int y = 0; y < s.height(); ++y) {
        const QRgb* l = reinterpret_cast<const QRgb*>(s.constScanLine(y));
        for (int x = 0; x < s.width(); ++x)
            sum += 0.299 * qRed(l[x]) + 0.587 * qGreen(l[x]) + 0.114 * qBlue(l[x]);
    }
    return sum / (s.width() * s.height() * 255.0);
}

void MainWindow::startCompositingWatch() {
    if (!m_gfxWatch) {
        m_gfxWatch = new QTimer(this);
        m_gfxWatch->setInterval(1500);
        connect(m_gfxWatch, &QTimer::timeout, this, [this] { checkCompositing(); });
    }
    m_gfxBad = 0;
    m_gfxChecks = 0;
    m_gfxWatch->start();
}

void MainWindow::checkCompositing() {
    if (++m_gfxChecks > 12) { m_gfxWatch->stop(); return; }            // 약 18초만 지켜본다
    if (!m_stack || m_stack->currentIndex() != 1 || !gState.gameLoaded || gState.isPaused) return;
    if (isMinimized() || !isVisible() || !windowHandle() || !windowHandle()->screen()) return;

    const double game = lumaOf(currentFrameImage());
    if (game < 0.08) return;                                            // 게임 장면 자체가 어두우면 판단하지 않는다
    const QImage shot = windowHandle()->screen()->grabWindow(winId()).toImage();
    double screen = lumaOf(shot);
    if (qEnvironmentVariableIsSet("FBNRX_TEST_FALLBACK")) screen = 0.0;   // 진단·시험용
    qDebug("[gfx] 합성 점검: 게임 %.3f / 화면 %.3f", game, screen);
    if (screen > 0.02) { m_gfxWatch->stop(); return; }                  // 화면에 제대로 나온다 → 정상
    if (++m_gfxBad < 3) return;                                          // 연속 3번 검을 때만

    m_gfxWatch->stop();
    gSettings.videoRenderer = QStringLiteral("software");
    gSettings.save();
    log("⚠ 게임 화면이 그려지지 않는 그래픽 환경으로 보여 소프트웨어 렌더러로 전환합니다");
    QMessageBox::information(this, QStringLiteral("FBNeoRageX"),
        isEn() ? QStringLiteral("The game screen is not being displayed on this PC's graphics setup.\n"
                                "Switching to the software renderer and restarting.\n"
                                "(Shaders / CRT are unavailable; change it back in VIDEO OPTIONS > RENDERER.)")
               : QStringLiteral("이 PC의 그래픽 환경에서 게임 화면이 표시되지 않아\n"
                                "소프트웨어 렌더러로 전환하고 다시 시작합니다.\n"
                                "(셰이더·CRT 는 쓸 수 없습니다. VIDEO OPTIONS > RENDERER 에서 되돌릴 수 있습니다.)"));
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
    close();
}

void MainWindow::enterGameScreen() {
    // 프리뷰 타이머 + 영상 + 소리 완전 정지
    if (m_previewVidTimer) m_previewVidTimer->stop();
#if HAVE_FFMPEG
    if (m_previewVideo) m_previewVideo->stop();   // 자체 디코더도 정지
#endif
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
        m_mediaPlayer->setSource(QUrl());  // 소스 해제 → 재생 불가 상태
    }

    attachCanvas();
    m_stack->setCurrentIndex(1);
    if (m_canvasW) m_canvasW->setFocus();

    // ── 입력 상태 초기화 (leaveGameScreen 과 대칭) ────────────────
    // GUI → 게임 재개 시, GUI 를 조작하던 키/패드 상태가 그대로 남아
    // 게임에 "눌린 채"로 들어가는 문제를 막는다.
    //   특히 applyBits() 는 게임 로드 중 kbHeld 에 있는 인덱스를 건너뛰므로,
    //   릴리즈를 놓쳐 kbHeld 에 남은 항목이 있으면 패드 폴링이 그 키를
    //   영원히 0 으로 되돌리지 못해 고착된다 (탭,탭 하면 풀리던 증상의 원인).
    //   Linux(스팀덱)는 조이스틱이 이벤트 기반 누산기라 더 쉽게 재현된다.
    if (m_gamepad) m_gamepad->clearState();   // Linux: fd 보류 이벤트 드레인 포함
    gState.kbHeld.clear();
    gState.rawKeys.fill(0);
    gState.keys.fill(0);
    gState.p2Keys.fill(0);

    // 게임 화면 진입 시 커서 자동 숨김 타이머 시작
    resetCursorTimer();
}

// GUI 복귀: 현재 선택된 롬의 프리뷰 재시작
void MainWindow::leaveGameScreen() {
    // 마우스로 대체하던 트리거 입력을 반드시 푼다 (눌린 채로 남지 않게)
    if (m_gamepad) m_gamepad->setMouseTriggerBits(false, false);

    // GUI로 돌아오면 커서 타이머 중지 + 커서 복원
    if (m_cursorTimer) m_cursorTimer->stop();
    if (m_cursorHidden) {
        setCursor(m_customCursor);  // 커서 복원 (widget-level)
        m_cursorHidden = false;
    }

    // ── 입력 상태 초기화 ────────────────────────────────────
    // 게임 화면 → GUI 복귀 시 눌린 채로 남아있는 키/버튼 초기화
    // GamepadManager 누산기도 함께 초기화해야 다음 폴링에서 rawKeys 재오염 방지
    if (m_gamepad) m_gamepad->clearState();
    gState.kbHeld.clear();
    gState.rawKeys.fill(0);
    gState.keys.fill(0);
    gState.p2Keys.fill(0);

    // 게임 종료 시 스왑 상태 리셋
    if (gState.swapPlayers) {
        gState.swapPlayers = false;
    }
    if (m_overlayTimer) m_overlayTimer->stop();
    if (m_playerOverlay) m_playerOverlay->hide();

    // 게임 종료 시 TATE/회전 상태 리셋 (다음 게임은 auto부터)
    gState.videoRotation = 0;
    if (m_canvas) m_canvas->setRotation(-1);  // auto

    m_stack->setCurrentIndex(0);
    filterRoms();
    // 선택된 게임이 있으면 프리뷰 재로드
    if (!m_selectedGame.isEmpty())
        loadPreview(m_selectedGame);

    // 포커스를 셸에 돌려줘야 방향키가 화면의 목록을 움직인다.
    //   (예전에는 숨겨진 목록에 포커스를 줘서, 게임에서 돌아온 뒤 키보드가 먹통이었다)
    if (m_shell) m_shell->setFocus();
}

// ── 마우스 커서 자동 숨김 ─────────────────────────────────────
void MainWindow::resetCursorTimer() {
    // 게임 화면에서는 포인터가 필요 없다. 스팀덱은 플레이 중 터치패드를 건드리는 일이 잦아
    //   포인터가 떠다니면 방해되므로, 움직임이 있어도 다시 보이게 하지 않고 계속 숨긴다.
    if (!m_stack || m_stack->currentIndex() != 1) return;
    hideCursor();
}

void MainWindow::hideCursor() {
    // 게임 화면 중이고 아직 숨기지 않았을 때만
    if (!m_stack || m_stack->currentIndex() != 1) return;
    if (!m_cursorHidden) {
        setCursor(m_blankCursor);   // widget-level: Wayland 동기 통신 없음
        m_cursorHidden = true;
    }
}

// ════════════════════════════════════════════════════════════
//  앱 전역 이벤트 필터 — 탭키 GUI↔게임 전환
// ════════════════════════════════════════════════════════════
bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    // ── 캔버스 리사이즈 → 오버레이 재배치 ────────────────────
    if (obj == m_canvasW && ev->type() == QEvent::Resize && m_playerOverlay) {
        auto reposition = [this]{
            if (!m_playerOverlay->isVisible()) return;
            m_playerOverlay->adjustSize();
            m_playerOverlay->move(
                m_canvasW->width()  - m_playerOverlay->width()  - 12,
                12);
        };
        reposition();
    }

    // ── 마우스 좌클릭 → 클릭음 재생 (원본 NeoRageX 느낌) ──────
    //   ※ 좌클릭에만 반응한다. 우클릭(컨텍스트 메뉴)·휠·더블클릭은 제외.
    //     더블클릭 시 소리가 두 번 겹치지 않도록 Press 만 사용.
    if (ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        // ★ qApp 필터는 같은 클릭을 위젯용/윈도우용으로 두 번 본다.
        //   그대로 두면 클릭 한 번에 소리가 두 번 났다.
        //   → 위젯 이벤트만 받고, 같은 타임스탬프는 한 번만 처리한다.
        if (me->button() == Qt::LeftButton && obj && obj->isWidgetType()
            && me->timestamp() != m_lastClickTs) {
            m_lastClickTs = me->timestamp();
            playClickSound();
        }
    }

    // ── 게임 실행 중에는 마우스 클릭/휠을 무시한다 ───────────────
    //   스팀덱은 L2/R2 트리거에 마우스 우/좌클릭이 할당돼 있어(스팀 입력),
    //   트리거를 게임 버튼으로 쓰면 클릭이 같이 들어와 게임 화면에 간섭한다.
    //   ★ 커서 이동은 막지 않는다 — 커서 자동숨김 동작을 그대로 두기 위해서.
    //   탭/ESC 로 GUI 로 돌아오면 아래 조건이 풀려 마우스가 다시 정상 동작한다.
    if (m_stack && m_stack->currentIndex() == 1
        && gState.gameLoaded && !gState.isPaused) {
        switch (ev->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick: {
            // ★ 버리지 않고 트리거 입력으로 넘긴다.
            //   스팀 입력에서 L2/R2 에 마우스를 걸어 두면 트리거가 조이스틱으로
            //   보고되지 않아 게임 버튼으로 쓸 수 없다. 클릭을 그대로 L2/R2 로
            //   바꿔 주면 마우스 설정은 그대로 두고도 인게임에서 트리거가 산다.
            if (auto* me = static_cast<QMouseEvent*>(ev)) {
                const Qt::MouseButtons b = me->buttons();
                if (m_gamepad)
                    m_gamepad->setMouseTriggerBits(b & Qt::LeftButton,
                                                   b & Qt::RightButton);
            }
            return true;    // UI 로는 전달하지 않는다 (클릭음도 없음)
        }
        case QEvent::Wheel:
            return true;
        default:
            break;
        }
    } else if (m_gamepad) {
        // 게임 화면을 벗어나면 대체 입력을 반드시 푼다 (눌린 채로 남지 않게)
        m_gamepad->setMouseTriggerBits(false, false);
    }

    // ── 아무 입력이나 들어오면 화면보호기 대기시간 초기화/해제 ──
    //   재생 중이라면 그 입력은 "해제"에만 쓰고 원래 동작으로는 넘기지 않는다.
    //   (ESC 로 프로그램이 꺼지거나 클릭으로 게임이 실행되면 안 되므로)
    switch (ev->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        const bool wasSaver = m_ssActive;
        m_ssIdleTicks = 0;
        if (wasSaver) {
            // 마우스 이동만으로는 끄지 않는다 (책상 진동 등으로 바로 꺼지는 것 방지)
            if (ev->type() != QEvent::MouseMove) stopScreensaver();
            return true;   // 이벤트 소비
        }
        break;
    }
    default:
        break;
    }

    // ── 마우스 이동 / 클릭 → 커서 타이머 리셋 ─────────────────
    switch (ev->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        if (m_stack && m_stack->currentIndex() == 1)
            resetCursorTimer();
        break;
    default:
        break;
    }

    // ── 탭키: GUI↔게임 전환 ────────────────────────────────────
    if (ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (!ke->isAutoRepeat() && ke->key() == Qt::Key_Tab) {
            // 게임이 로드되어 있고 일시정지 상태(GUI 표시 중)일 때만 가로챔
            if (gState.gameLoaded && gState.isPaused && m_stack && m_stack->currentIndex() != 4) {
                togglePause();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

// ════════════════════════════════════════════════════════════
//  키 이벤트
// ════════════════════════════════════════════════════════════
void MainWindow::keyPressEvent(QKeyEvent* e) {
    // 화면보호기 중에는 어떤 키든 "해제"로만 쓰고 원래 기능은 실행하지 않는다.
    // (ESC 로 프로그램이 꺼지거나, 엔터로 게임이 실행되면 안 되므로)
    if (m_ssActive) { stopScreensaver(); e->accept(); return; }

    int k    = e->key();
    bool alt = (e->modifiers() & Qt::AltModifier);

    // ── GUI 모드: 방향키/Enter → 셸 조작 ────────────────────────
    //   포커스가 셸에 있으면 셸이 키를 직접 처리한다. 다른 위젯에 가 있어도
    //   같은 동작이 나가도록 여기서 같은 navigate() 로 보낸다.
    //   (예전에는 숨겨진 목록을 움직여서 화면과 어긋났다)
    if (m_stack && m_stack->currentIndex() == 0 && m_shell) {
        using Nav = NeoRageXShell::Nav;
        bool isNav = true;
        Nav nav = Nav::Up;
        switch (k) {
        case Qt::Key_Up:    nav = Nav::Up;    break;
        case Qt::Key_Down:  nav = Nav::Down;  break;
        case Qt::Key_Left:  nav = Nav::Left;  break;
        case Qt::Key_Right: nav = Nav::Right; break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // Alt+Enter 는 전체화면. 누르고 있는 동안 연달아 실행되면 안 된다.
            if (alt || e->isAutoRepeat()) isNav = false;
            nav = Nav::Accept;
            break;
        default: isNav = false; break;
        }
        if (isNav) { m_shell->navigate(nav); return; }
    }
    // ────────────────────────────────────────────────────────────

    if (e->isAutoRepeat()) { QMainWindow::keyPressEvent(e); return; }
    bool shift = (e->modifiers() & Qt::ShiftModifier);
    const int mods = e->modifiers();

    // ── 설정 가능한 핫키 (action 별로 hotkeyMatch) ──────────────
    //   기본값은 buildDefaultKeymap 옆 hotkeyDefs() 와 동일 → 기존 동작 유지.
    //   사용자가 컨트롤 옵션에서 재배정하면 그 키로 동작.

    // 게임 ↔ 메뉴 전환 (어느 상태에서나)
    if (hotkeyMatch("pause", k, mods)) { togglePause(); return; }

    // 게임 종료 (게임 로드 중일 때만)
    if (hotkeyMatch("exit", k, mods) && gState.gameLoaded) {
        if (gNetplay().playing()) {
            gNetplay().sendGameOver();
            log("■ 게임 종료 — 넷플레이 (상대 통지, Lobby 복귀)");
            cleanupNetplay();
            return;
        }
        m_timer->stop();
        gState.isPaused = false;
        if (m_core) m_core->unloadGame();
        m_loadedGame.clear();
        leaveGameScreen();
        log("■ 게임 종료");
        return;
    }

    // ESC — 게임 중이 아니면 프로그램 종료
    //   (게임 중에는 위에서 "게임 종료"로 먼저 처리된다)
    if (hotkeyMatch("exit", k, mods) && !gState.gameLoaded && !gState.isPaused) {
        close();
        return;
    }

    // 전체화면
    if (hotkeyMatch("fullscreen", k, mods)) { toggleFullscreen(); return; }

    // 서비스(TEST) 입력 — 전용 키 한 번 = 서비스 입력 1회 전송 (마메식)
    //   START 홀드/게임패드 L2 와 무관하게 이 키로만 서비스 입력이 나간다.
    //   한 번 누르면 ~12프레임 assert → 코어가 테스트 버튼 눌림으로 인식.
    //   메뉴 진입/이동 시 필요하면 다시 눌러 반복 전송.
    if (hotkeyMatch("service", k, mods)) {
        if (gState.gameLoaded && !gState.isPaused) {
            m_serviceHoldFrames = 12;
            log("🔧 서비스(TEST) 입력 전송");
        }
        return;
    }

    // ── 세이브스테이트 슬롯 (F1~F8 로드 / Shift+F1~F8 저장) ──────
    //   안전을 위해 고정·예약 (재배정 불가). 핫키보다 먼저 체크.
    static const int fKeys[] = {
        Qt::Key_F1, Qt::Key_F2, Qt::Key_F3, Qt::Key_F4,
        Qt::Key_F5, Qt::Key_F6, Qt::Key_F7, Qt::Key_F8
    };
    for (int i = 0; i < 8; ++i) {
        if (k == fKeys[i]) {
            if (shift) saveState(i + 1);
            else       loadState(i + 1);
            return;
        }
    }

    // 프리뷰 녹화 / 일반 녹화 (모디파이어 포함이 먼저 매칭되도록 순서 주의)
    if (hotkeyMatch("preview_rec",  k, mods)) { togglePreviewRecord(); return; }
    if (hotkeyMatch("record",       k, mods)) { toggleRecording();     return; }
    // 1P↔2P 스왑
    if (hotkeyMatch("swap",         k, mods)) { toggleSwapPlayers();   return; }
    // 패스트포워드
    if (hotkeyMatch("fast_forward", k, mods)) { toggleFastForward(!gState.fastForward); return; }
    // 프리뷰 이미지 저장 / 스크린샷
    if (hotkeyMatch("preview_shot", k, mods)) { savePreviewShot();     return; }
    if (hotkeyMatch("screenshot",   k, mods)) { takeScreenshot();      return; }

    applyKeyPress(k);
    QMainWindow::keyPressEvent(e);
}

void MainWindow::keyReleaseEvent(QKeyEvent* e) {
    if (!e->isAutoRepeat()) applyKeyRelease(e->key());
    QMainWindow::keyReleaseEvent(e);
}

// 저장된 값이 "의미"(6버튼 격투)면 이 게임의 실제 인덱스로 바꾼다.
int MainWindow::keyActionIndex(int stored) const {
    if (!padIsSem(stored)) return stored;
    if (!m_coreBtns.valid) return -1;
    switch (padSemSlot(stored)) {
    case SEM_LP: return m_coreBtns.lp;
    case SEM_MP: return m_coreBtns.mp;
    case SEM_HP: return m_coreBtns.hp;
    case SEM_LK: return m_coreBtns.lk;
    case SEM_MK: return m_coreBtns.mk;
    case SEM_HK: return m_coreBtns.hk;
    default:     return -1;
    }
}

void MainWindow::applyKeyPress(int qtKey) {
    auto it = m_keymap.find(qtKey);
    if (it == m_keymap.end()) return;
    int idx = keyActionIndex(it.value());
    if (idx >= 0 && idx < 16) {
        gState.rawKeys[idx] = 1;
        gState.kbHeld.insert(idx);
    }
}

void MainWindow::applyKeyRelease(int qtKey) {
    auto it = m_keymap.find(qtKey);
    if (it == m_keymap.end()) return;
    int idx = keyActionIndex(it.value());
    if (idx >= 0 && idx < 16) {
        gState.rawKeys[idx] = 0;
        gState.kbHeld.remove(idx);
    }
}

// ════════════════════════════════════════════════════════════
//  기종(플랫폼) 분류기 — ROM 이름 프리픽스 기반
//  컨트롤/머신 "기종별 전역" 설정의 그룹 키로 사용.
//  gamelist.xml 에 하드웨어 필드가 없어 프리픽스로 추정한다.
//  분류가 틀리면 사용자가 "게임별" 저장으로 우회 가능.
// ════════════════════════════════════════════════════════════
QString MainWindow::gamePlatform(const QString& rom) {
    const QString lc = rom.toLower();
    auto starts = [&](std::initializer_list<const char*> pfx) {
        for (const char* p : pfx) if (lc.startsWith(p)) return true;
        return false;
    };
    if (starts({"kof","mslug","garou","samsho","rbff","fatfury","aof","wh",
                "nam1975","lbowling","blazstar","lastsold","neo","magdrop",
                "pbobble","neobombe","turfmast","lastblad","rotd","ssideki",
                "twinspri","ironclad"," kabuki","matrim","svc"," kizuna",
                "shocktro","ragnagrd","breakers","galaxyfg","wjammers"}))
        return "neogeo";
    if (starts({"sf","ssf","sfa","sfz","xmvsf","msh","mvsc","mvc","avsp","vsav",
                "knights","ffight","ghouls","strider","1941","1944","19xx",
                "progear","gigawing","mmatrix","cybots","ddtod","ddsom",
                "dino","punisher","slammast","wof","kod","mercs","willow",
                "unsquad","dynwar","cawing","forgottn","varth","captcomm",
                "pnickj","qad","nwarr","sgemf","jojo","redearth","vhunt",
                "vsavo","cps","hsf2","dstlk","batcir","armwar","ringdest","tk2",
                "3wonders","mtwins","chikij","nemo","msword","cworld2","spf2",
                "pang3","megaman","rockmanj","gulunpa","daimakai","sfiii"}))
        return "cps";
    if (starts({"rtype","hharry","dkgen","poundfor","airduel","gallop",
                "cosmccop","kengo","matchit","xmultipl","dbreed","loht",
                "imgfight","nspirit","mrheli","bchopper","gunforce","bmaster",
                "lethalth","thndblst","uccops","mysticri","gunhohki","majtitl",
                "hook","ppan","rtypeleo","inthunt","kaiteids","leaguemn",
                "ssoldier","psoldier","dsoccr","gunforc2","geostorm","nbbatman",
                "hcube","spelunk","kungfum","ldrun","kidniki","vigilant"}))
        return "irem";
    if (starts({"ddonpach","donpachi","esprade","guwange","dfeveron","uopoko",
                "ddpdoj","espgal","mushi","ketsui","pinkswts","deathsml",
                "ibara","ddp"}))
        return "cave";
    if (starts({"batsugun","dogyuun","hellfire","truxton","tatsujin","zerowing",
                "outzone","snowbros","fixeight","vfive","grindstm","kingdmgp",
                "kbash","pipibibs","whoopee","tekipaki","ghox","dharma",
                "rallybik","demonwld","vimana","teki"}))
        return "toaplan";
    if (starts({"gunbird","strikers","s1945","sengoku","samuraia","btlkroad",
                "sengokmj","tengai","gachiko","loverboy","daraku","hotgmck"}))
        return "psikyo";
    return "arcade";   // 기본 (그 외 모든 게임 공통)
}

// ════════════════════════════════════════════════════════════
//  핫키 정의 — action 이름 / 표시 라벨 / 기본 키 / 기본 모디파이어
//  modifiers 비트: 1=Shift, 2=Ctrl, 4=Alt
//  인코딩:  enc = (key & 0x0FFFFFFF) | (mods << 28)
//  (Qt::Key 는 25비트 이내라 상위 비트가 비어있음)
//  ※ 세이브스테이트 슬롯(F1~F8 / Shift+F1~F8)은 안전을 위해 고정 — 표에 없음
// ════════════════════════════════════════════════════════════
const MainWindow::HotkeyDef* MainWindow::hotkeyDefs(int* count) {
    static const HotkeyDef defs[] = {
        {"pause",        "게임 ↔ 메뉴 전환",   "Game / Menu Toggle", Qt::Key_Tab,        0},
        {"exit",         "게임 종료",          "Exit Game",          Qt::Key_Escape,     0},
        {"fullscreen",   "전체화면",           "Fullscreen",         Qt::Key_Return,     4}, // Alt+Enter
        {"service",      "서비스(TEST) 입력",  "Service (TEST)",     Qt::Key_QuoteLeft,  0}, // ` — 전용 키
        {"record",       "녹화",               "Record",             Qt::Key_F9,         0},
        {"preview_rec",  "프리뷰 영상 녹화",   "Record Preview",     Qt::Key_F9,         2}, // Ctrl+F9
        {"swap",         "1P ↔ 2P 스왑",       "Swap 1P / 2P",       Qt::Key_F10,        0},
        {"fast_forward", "패스트포워드",       "Fast Forward",       Qt::Key_F11,        0},
        {"screenshot",   "스크린샷",           "Screenshot",         Qt::Key_F12,        0},
        {"preview_shot", "프리뷰 이미지 저장", "Save Preview Image", Qt::Key_F12,        2}, // Ctrl+F12
    };
    if (count) *count = static_cast<int>(sizeof(defs) / sizeof(defs[0]));
    return defs;
}

int MainWindow::hotkeyEncode(int key, int mods) {
    return (key & 0x0FFFFFFF) | ((mods & 0x7) << 28);
}
void MainWindow::hotkeyDecode(int enc, int& key, int& mods) {
    key  = enc & 0x0FFFFFFF;
    mods = (enc >> 28) & 0x7;
}

// action 의 현재 핫키 인코딩 (사용자 설정 > 기본값)
// 핫키는 고정이다. 예전에 저장된 사용자 핫키가 남아 있어도 무시한다.
//   (변경 UI 를 없앴으므로, 옛 저장값이 살아 있으면 안내와 실제가 달라진다)
int MainWindow::hotkeyOf(const QString& action) {
    int n = 0; const HotkeyDef* d = hotkeyDefs(&n);
    for (int i = 0; i < n; ++i)
        if (action == d[i].action) return hotkeyEncode(d[i].key, d[i].mods);
    return 0;
}

// 현재 이벤트(key+mods)가 action 핫키와 일치하는가
bool MainWindow::hotkeyMatch(const QString& action, int key, int qtMods) {
    int enc = hotkeyOf(action);
    int hk, hm; hotkeyDecode(enc, hk, hm);
    if (hk == 0) return false;
    int curMods = ((qtMods & Qt::ShiftModifier)   ? 1 : 0)
                | ((qtMods & Qt::ControlModifier) ? 2 : 0)
                | ((qtMods & Qt::AltModifier)     ? 4 : 0);
    return key == hk && curMods == hm;
}

QHash<int, int> MainWindow::buildDefaultKeymap(bool six) {
    QHash<int, int> k = {
        {Qt::Key_Return,  3},   // START
        {Qt::Key_1,       3},
        {Qt::Key_Space,   2},   // SELECT / COIN
        {Qt::Key_2,       2},
        {Qt::Key_Up,      4},   // UP
        {Qt::Key_Down,    5},   // DOWN
        {Qt::Key_Left,    6},   // LEFT
        {Qt::Key_Right,   7},   // RIGHT
    };
    if (six) {
        // 6버튼 격투: 위 줄은 손, 아래 줄은 발 (아케이드 패널과 같은 모양)
        //     A  S  D  =  약손 중손 강손
        //     Z  X  C  =  약발 중발 강발
        k[Qt::Key_A] = padSemId(SEM_LP);
        k[Qt::Key_S] = padSemId(SEM_MP);
        k[Qt::Key_D] = padSemId(SEM_HP);
        k[Qt::Key_Z] = padSemId(SEM_LK);
        k[Qt::Key_X] = padSemId(SEM_MK);
        k[Qt::Key_C] = padSemId(SEM_HK);
    } else {
        // 그 밖의 게임: A B C D 가 A S D F 순서다 (네오지오 A B C D, CPS 2~3버튼 A B C).
        //   그 옆의 Z, X 는 동시 입력 매크로(CD, AB)다.
        k[Qt::Key_A] = 0;      // A
        k[Qt::Key_S] = 8;      // B
        k[Qt::Key_D] = 1;      // C
        k[Qt::Key_F] = 9;      // D
        k[Qt::Key_Z] = 10;     // CD
        k[Qt::Key_X] = 11;     // AB
    }
    return k;
}

// 지금 배치(6버튼 격투인가)에 맞는 키보드 표를 다시 고른다.
//   게임별 > 기종별 > (배치별) 저장값 > 배치별 기본값 순이다.
void MainWindow::reloadKeymap() {
    const bool six = m_gamepad && m_gamepad->padLayout() == PadLayout::SixButton;
    m_keymap = resolveCtrlMap(gSettings.kbScoped,
                              six ? gSettings.keyboardMappingSix : gSettings.keyboardMapping,
                              buildDefaultKeymap(six), m_ctrlScopeRom);
}

// ── 컨트롤 매핑 해석: 게임별 > 기종별 > 전역 > 기본 ──────────
QHash<int,int> MainWindow::resolveCtrlMap(
        const QHash<QString,QHash<int,int>>& scoped,
        const QHash<int,int>& global,
        const QHash<int,int>& dflt,
        const QString& rom)
{
    if (!rom.isEmpty()) {
        QString gk = "game:" + rom;
        if (scoped.contains(gk) && !scoped[gk].isEmpty()) return scoped[gk];
        QString pk = platScopeKey(rom);
        if (scoped.contains(pk) && !scoped[pk].isEmpty()) return scoped[pk];
    }
    if (!global.isEmpty()) return global;
    return dflt;
}

// 게임 로드 시 — 해당 게임에 맞는 컨트롤을 해석해 적용
void MainWindow::resolveAndApplyControls(const QString& rom) {
    m_ctrlScopeRom = rom;

    // ── 아케이드 버튼 배치 확정 ──────────────────────────────
    //   FBNeo 는 6버튼 격투게임에 다른 retropad 인덱스를 쓴다. 그래서
    //   "기본 매핑"의 내용이 게임에 따라 달라져야 한다. 저장해 둔 사용자
    //   매핑(게임별/기종별/전역/장치별)은 아래에서 그대로 우선 적용되므로,
    //   여기서 바뀌는 것은 기본값뿐이다.
    if (m_gamepad) {
        const PadLayout lay = padLayoutOf(rom);
        const bool changed = (lay != m_gamepad->padLayout());
        m_gamepad->setPadLayout(lay);
        m_gamepad->resetDefaultMapping();   // 공용 폴백 표도 배치에 맞춤
        // ★ 배치가 그대로여도 매번 다시 적용한다. 예전에는 "바뀔 때만" 적용해서
        //   패드가 아직 인식되기 전에 한 번 지나가면 영영 반영되지 않았다.
        applyPadProfiles();
        if (changed)
            qDebug().noquote() << QString("[pad] 배치 %1")
                                  .arg(padLayoutLabel(lay, false));
        logPadMappingSummary();   // 실제로 적용된 표를 로그에 남긴다
    }

    // 키보드 (배치가 6버튼 격투인지에 따라 기본 배정이 다르다)
    reloadKeymap();
    // 아케이드 스틱(WinMM). 게임패드는 위 applyPadProfiles() 가 이미 처리했다.
    //   저장된 표가 없으면 이전 게임의 표가 남지 않도록 기본값으로 되돌린다.
    if (m_gamepad) {
        QHash<int,int> wm = resolveCtrlMap(gSettings.wmScoped,
                                gSettings.winmmMapping,  {}, rom);
        if (!wm.isEmpty()) m_gamepad->setWinMMMapping(wm);
        else               m_gamepad->resetDefaultWinMM();
    }
    applyTurboFor(rom);
    // 테이블이 보이면 갱신
    refreshControlsUi();
}

// ── 컨트롤 저장 범위 ─────────────────────────────────────────
//   · 게임별  : 매핑·터보를 바꾸면 그 게임 범위에 자동 저장한다 (다른 게임은 그대로).
//   · 기종별  : 컨트롤 화면의 "이 기종 전체에 저장" 을 눌렀을 때. 기종은 NEOGEO / CPS / 그 밖(OTHER),
//               6버튼 격투와 일반 배치는 따로 저장한다 (같은 CPS 라도 버튼 의미가 다르므로).
//   · 적용 순서: 게임별 > 기종별 > 전역·장치별 > 기본값.

// 기종+배치 저장 키 ("plat:neogeo#std", "plat:cps#6btn", "plat:other#std")
QString MainWindow::platScopeKey(const QString& rom) const {
    if (rom.isEmpty()) return QString();
    QString p = gamePlatform(rom);
    if (p != QLatin1String("neogeo") && p != QLatin1String("cps")) p = QStringLiteral("other");
    return QStringLiteral("plat:") + p
         + (padLayoutOf(rom) == PadLayout::SixButton ? QStringLiteral("#6btn") : QStringLiteral("#std"));
}

QString MainWindow::platformSaveLabel() const {
    const QString p = gamePlatform(currentPadRom());
    if (p == QLatin1String("neogeo")) return QStringLiteral("NEOGEO");
    if (p == QLatin1String("cps"))    return QStringLiteral("CPS");
    return QStringLiteral("OTHER");
}

void MainWindow::autoSaveKeyboard() {
    const QString rom = currentPadRom();
    if (rom.isEmpty()) (sixLayoutNow() ? gSettings.keyboardMappingSix : gSettings.keyboardMapping) = m_keymap;
    else               gSettings.kbScoped[gameScopeKey(rom)] = m_keymap;
    gSettings.save();
}

void MainWindow::autoSavePad(int idx) {
    if (!m_gamepad || idx < 0 || idx >= 4) return;
    const QString name = m_gamepad->padName(idx);
    const QString rom  = currentPadRom();
    QString key = padScopeKeyGame(rom, name, m_gamepad->padLayout());
    if (key.isEmpty()) key = padScopeKeyDev(name);
    if (key.isEmpty()) return;
    gSettings.padMaps[key] = m_padUiMap[idx];
    m_padMapSource[idx] = padMapSourceLabel(PadMapSource::Game, isEn());
    gSettings.save();
}

void MainWindow::autoSaveStick() {
    if (!m_gamepad) return;
    const QString rom = currentPadRom();
    if (rom.isEmpty()) gSettings.winmmMapping = m_gamepad->getWinMMMapping();
    else               gSettings.wmScoped[gameScopeKey(rom)] = m_gamepad->getWinMMMapping();
    gSettings.save();
}

// 지금 키보드·패드·스틱 표와 터보를 이 기종 전체(이 배치)에 저장한다.
void MainWindow::saveControlsForPlatform() {
    const QString rom = currentPadRom();
    const QString pk  = platScopeKey(rom);
    if (pk.isEmpty()) {
        log(isEn() ? "⚠ Pick a game first" : "⚠ 먼저 게임을 고르세요");
        return;
    }
    gSettings.kbScoped[pk] = m_keymap;
    if (m_gamepad) {
        for (int i = 0; i < 4; ++i) {
            if (!m_gamepad->padPresent(i) || m_padUiMap[i].isEmpty()) continue;
            const QString key = padScopeKeyPlat(pk, m_gamepad->padName(i));
            if (!key.isEmpty()) gSettings.padMaps[key] = m_padUiMap[i];
        }
        gSettings.wmScoped[pk] = m_gamepad->getWinMMMapping();
    }
    gSettings.turboScoped[pk] = turboToString();
    gSettings.save();
    log(isEn() ? QString("🎮 Controls saved for all %1 games").arg(platformSaveLabel())
               : QString("🎮 컨트롤을 %1 기종 전체에 저장했습니다").arg(platformSaveLabel()));
    applyPadProfiles();
    refreshControlsUi();
}

// 이 게임에 따로 저장된 컨트롤·터보를 지우고, 기종별 → 기본값 순으로 다시 적용한다.
void MainWindow::forgetGameControls() {
    const QString rom = currentPadRom();
    if (rom.isEmpty()) return;
    const QString gk = gameScopeKey(rom);
    gSettings.kbScoped.remove(gk);
    gSettings.xiScoped.remove(gk);
    gSettings.wmScoped.remove(gk);
    gSettings.turboScoped.remove(gk);
    const QString prefix = gk + QLatin1Char('|');
    for (auto it = gSettings.padMaps.begin(); it != gSettings.padMaps.end(); )
        it = it.key().startsWith(prefix) ? gSettings.padMaps.erase(it) : ++it;
    gSettings.save();
    log(isEn() ? "🎮 This game's saved controls were cleared"
               : "🎮 이 게임에 저장된 컨트롤을 지웠습니다");
    resolveAndApplyControls(rom);
}

// 저장된 패드 매핑을 모두 지우고 기본값으로 되돌린다.
void MainWindow::clearPadScope(const QString& scope) {
    Q_UNUSED(scope);
    const int n = int(gSettings.padMaps.size());
    gSettings.padMaps.clear();
    gSettings.save();
    log(QString(isEn() ? "🎮 Pad mappings cleared (%1) - back to defaults"
                       : "🎮 패드 매핑 %1개 삭제 → 기본값으로").arg(n));
    applyPadProfiles();
    refreshControlsUi();
}

// ════════════════════════════════════════════════════════════
//  넷플레이 슬롯
// ════════════════════════════════════════════════════════════
void MainWindow::onNetConnected(bool isHost) {
    qDebug("[NP-conn] onNetConnected isHost=%d", isHost ? 1 : 0);
    log(QString("🌐 연결됨 — %1").arg(isHost ? "HOST(P1)" : "CLIENT(P2)"));
    m_net.status   = isHost ? "● HOSTING — 게임을 선택하세요" : "● CONNECTED — 호스트 대기 중";
    m_net.canStart = isHost;
    m_net.canDisc  = true;
    m_net.canHost  = false;
    m_net.canJoin  = false;
    refreshNetUi();

    // 토큰 방식: 룸 코드는 HOST GAME 클릭 시점에 이미 생성·표시됨.
    // 연결 성립 시점에 재생성하지 않음 (코드 안정성 유지).
    Q_UNUSED(isHost);
}

// ════════════════════════════════════════════════════════════
//  릴레이 홀펀칭 (Cloudflare Worker 기반)
// ════════════════════════════════════════════════════════════

// 공유 QNetworkAccessManager — 앱 생명주기 동안 1개만 유지 (TLS race 방지)
QNetworkAccessManager* MainWindow::relayNam() {
    if (!m_relayNam) m_relayNam = new QNetworkAccessManager(this);
    return m_relayNam;
}

// 내 IP:Port 를 릴레이에 등록. 피어 정보가 있으면 즉시 반환.
// 로그 메시지에서 릴레이 서버 주소를 지운다. Qt 의 errorString() 등은 전체 URL
// (계정 ID 포함)을 담을 수 있어, 그대로 로그에 찍으면 화면/스크린샷에 노출된다.
// 윈도우·스팀덱 공통으로 육안 노출을 막기 위해 URL 과 호스트를 "***" 로 치환.
QString MainWindow::redactRelayUrl(const QString& s) {
    QString out = s;
    QString base = gSettings.netplayRelayUrl;
    if (!base.isEmpty()) {
        if (base.endsWith('/')) base.chop(1);
        out.replace(base, "***");
        const QString host = QUrl(base).host();   // 호스트만 담긴 경우도 가림
        if (!host.isEmpty()) out.replace(host, "***");
    }
    return out;
}

void MainWindow::relayRegister(const QString& code, const QString& role,
                                const QString& ip,  int port)
{
    log(QString("[DIAG] relayRegister 진입 role=%1 ip=%2:%3").arg(role).arg(ip).arg(port));
    QString base = gSettings.netplayRelayUrl;
    if (base.isEmpty()) { log("[DIAG] relayRegister: base 비어있음 → return"); return; }
    if (base.endsWith('/')) base.chop(1);
    log("[DIAG] relayRegister: QNAM 생성 직전");

    QJsonObject body;
    body["code"] = code;
    body["role"] = role;
    body["ip"]   = ip;
    body["port"] = port;

    QUrl url(base + "/room");
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto* reply = relayNam()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    log("[DIAG] relayRegister: POST 요청 전송 완료 (응답 대기)");
    connect(reply, &QNetworkReply::finished, this, [this, reply, role](){
        log("[DIAG] relayRegister 응답 핸들러 진입");
        reply->deleteLater();   // 공유 nam 은 파괴하지 않음
        if (reply->error() != QNetworkReply::NoError) {
            log("릴레이 등록 실패: " + redactRelayUrl(reply->errorString()));
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        // ★ Qt6 함정 회피: doc.object() 임시객체를 명명 변수로 살려두고
        //   peer 는 QJsonValue(값 복사)로 받는다. auto 로 받으면
        //   QJsonValueConstRef(임시 참조) → 댕글링 → toObject() 크래시.
        QJsonObject root = doc.object();
        QJsonValue  peer = root.value("peer");
        log("[DIAG] relayRegister 응답 파싱 완료");
        if (peer.isObject()) {
            QJsonObject po   = peer.toObject();
            QString peerIp   = po.value("ip").toString();
            int     peerPort = po.value("port").toInt();
            if (!peerIp.isEmpty() && peerPort > 0) {
                if (m_relayPeerHandled) {
                    log("[DIAG] relayRegister: 피어 이미 처리됨 → 중복 무시");
                    return;
                }
                m_relayPeerHandled = true;
                log(QString("릴레이: 피어 발견 %1:%2 → 즉시 홀펀칭").arg(peerIp).arg(peerPort));
                // 즉시 5회 프로브 (양쪽 동시 송신 → NAT 매핑 동시 생성)
                for (int i = 0; i < 5; ++i)
                    gNetplay().sendProbeTo(peerIp, peerPort);
                // 클라이언트: 호스트 주소 확정 → HELLO 핸드셰이크 시작
                if (role == "client" && !gNetplay().isHost()) {
                    gNetplay().clientStartHandshake(peerIp, peerPort);
                }
            }
        } else {
            log("릴레이 등록 완료 — 상대 대기 중...");
        }
    });
}

// 피어가 나타날 때까지 1초 간격으로 폴링 (최대 60초)
// ※ active() 대신 playing() 사용 — hostListen/clientConnect 후 active()는 즉시 true가 되므로
void MainWindow::relayPollPeer(const QString& code, const QString& myRole, int tries)
{
    // 새 폴링 시퀀스 시작(tries==0)이면 세대를 올려 이전 루프를 무효화한다.
    if (tries == 0) { ++m_relayPollGen; log(QString("[DIAG] relayPollPeer 진입 myRole=%1").arg(myRole)); }
    const int gen = m_relayPollGen;    // 이 루프가 속한 세대

    if (tries >= 60) { log("릴레이 폴링 타임아웃"); return; }
    if (gNetplay().playing()) return;  // 게임 중이면 중단

    QString base = gSettings.netplayRelayUrl;
    if (base.isEmpty()) return;
    if (base.endsWith('/')) base.chop(1);

    // 내가 host → peer는 client, 내가 client → peer는 host
    QString peerRole = (myRole == "host") ? "client" : "host";
    QString urlStr   = base + "/room/" + code + "/" + peerRole;

    QUrl  url(urlStr);
    QNetworkRequest req;
    req.setUrl(url);
    auto* reply = relayNam()->get(req);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, code, myRole, tries, gen](){
        reply->deleteLater();   // 공유 nam 은 파괴하지 않음

        if (gen != m_relayPollGen) return; // 디스커넥트/새 연결로 무효화된 루프 → 종료
        if (gNetplay().playing()) return;  // 게임 중이면 중단

        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc  = QJsonDocument::fromJson(reply->readAll());
            QJsonObject   root = doc.object();   // 임시객체 댕글링 방지
            if (root.value("found").toBool()) {
                QString peerIp   = root.value("ip").toString();
                int     peerPort = root.value("port").toInt();
                if (!peerIp.isEmpty() && peerPort > 0) {
                    if (m_relayPeerHandled) {
                        log("[DIAG] relayPollPeer: 피어 이미 처리됨 → 중복 무시");
                        return;
                    }
                    m_relayPeerHandled = true;
                    log(QString("릴레이: 피어 발견 %1:%2 → 홀펀칭 시작").arg(peerIp).arg(peerPort));
                    qDebug("[relay] peer found %s:%d → probing",
                           peerIp.toUtf8().constData(), peerPort);

                    // 즉시 3회 프로브 (양쪽 동시 송신 → NAT 매핑 동시 생성)
                    for (int i = 0; i < 3; ++i)
                        gNetplay().sendProbeTo(peerIp, peerPort);

                    // 클라이언트: 호스트 주소 확정 → HELLO 핸드셰이크 시작
                    if (myRole == "client" && !gNetplay().isHost()) {
                        gNetplay().clientStartHandshake(peerIp, peerPort);
                    }

                    qDebug("[relay] 3 probes sent — starting probeTimer");

                    // 이후 500ms 간격으로 10초 동안 계속 프로브 (NAT 매핑 유지)
                    auto* probeTimer = new QTimer(this);
                    probeTimer->setInterval(500);
                    int* probeCount = new int(0);
                    connect(probeTimer, &QTimer::timeout, this, [this, peerIp, peerPort, probeTimer, probeCount](){
                        if (gNetplay().active() || ++(*probeCount) >= 20) {
                            probeTimer->stop();
                            probeTimer->deleteLater();
                            delete probeCount;
                            return;
                        }
                        gNetplay().sendProbeTo(peerIp, peerPort);
                    });
                    probeTimer->start();
                    qDebug("[relay] probeTimer started — relay lambda done");
                    return;  // 폴링 종료 (피어 찾음)
                }
            }
        }

        // 아직 피어 없음 → 1초 후 재시도
        if (m_relayPollTimer) m_relayPollTimer->deleteLater();
        m_relayPollTimer = new QTimer(this);
        m_relayPollTimer->setSingleShot(true);
        connect(m_relayPollTimer, &QTimer::timeout, this, [this, code, myRole, tries, gen](){
            if (gen != m_relayPollGen) return;  // 무효화된 루프 → 재시도 안 함
            relayPollPeer(code, myRole, tries + 1);
        });
        m_relayPollTimer->start(1000);
    });
}

void MainWindow::onNetDisconnected() {
    log("🌐 연결 끊김");
    m_relayPeerHandled = false;   // 다음 연결을 위해 피어 처리 플래그 리셋
    cleanupNetplay();
    m_net.status   = "● DISCONNECTED";
    m_net.canStart = false;
    m_net.canDisc  = false;
    m_net.canHost  = true;
    m_net.canJoin  = true;
    refreshNetUi();
}

void MainWindow::onNetError(const QString& msg) {
    log("🌐 오류: " + msg);
    m_net.status = "● ERROR: " + msg;
    refreshNetUi();
}

// 상태 변화 → UI 업데이트
void MainWindow::onNetStateChanged(NetplayManager::State s) {
    qDebug("[NP-state] onNetStateChanged s=%d", (int)s);
    const QString rtt = QString("RTT: %1 ms").arg(gNetplay().rttMs());
    switch (s) {
    case NetplayManager::State::Lobby:
        m_net.status = gNetplay().isHost()
            ? "● HOSTING — 게임을 선택하세요" : "● CONNECTED — 호스트 대기 중";
        m_net.canStart = gNetplay().isHost();
        m_net.rtt = rtt;                       // Lobby 복귀 시 갱신
        break;
    case NetplayManager::State::Loading:
        m_net.status = "● 로딩 중...";
        m_net.canStart = false;
        break;
    case NetplayManager::State::Ready:
        m_net.status = "● 준비 완료 — 상대 대기 중...";
        m_net.rtt = rtt;
        break;
    case NetplayManager::State::Playing:
        m_net.status = QString("● 게임 중  |  RTT: %1 ms  |  Delay: %2f")
            .arg(gNetplay().rttMs()).arg(gNetplay().inputDelay());
        m_net.rtt.clear();
        break;
    default: return;
    }
    refreshNetUi();
}

// 조인: 호스트가 선택한 게임을 자동 로드 → 로딩 완료 시 READY 전송
void MainWindow::onNetLoadGame(const QString& romName, int inputDelay) {
    qDebug("[NP-conn] onNetLoadGame rom=%s delay=%d",
           romName.toUtf8().constData(), inputDelay);

    // ★ 중복 LOAD_GAME 무시: 호스트가 신뢰성 위해 5회+재전송하므로 같은 메시지가
    //   여러 번 옴. 이미 같은 ROM 을 로드 완료했으면 재로드(각 ~600ms) 하지 말고
    //   READY 만 재전송한다. (이전엔 매번 재로드 → 수 초 지연 → 시작 늦음)
    if (m_npSelfLoaded && gState.gameLoaded && m_selectedGame == romName) {
        gNetplay().sendReady();   // 호스트가 내 READY 를 놓쳤을 수 있으니 재전송
        return;
    }

    // 플래그 초기화
    m_npSelfLoaded = false;
    m_npPeerReady  = false;
    m_npStarted    = false;

    // 호스트가 지정한 입력 지연 반영 + 딜레이 큐 초기화
    gSettings.netplayInputDelay = inputDelay;
    m_npDelayQueue.clear();
    m_netDelay = inputDelay;

    log(QString("🌐 게임 수신: %1  (딜레이 %2f)").arg(romName).arg(inputDelay));
    m_selectedGame = romName;
    if (loadRomInternal()) {
        m_npSelfLoaded = true;
        log("🌐 로딩 완료 → READY 전송");
        gNetplay().sendReady();
        // 상대(호스트) READY 수신 전까지 300ms마다 재전송
        m_npReadyRetry->start();
    } else {
        log("🌐 ROM 없음 — 같은 ROM 파일을 ROM 폴더에 넣어주세요");
    }
}

// 양쪽: 상대방 READY 수신
void MainWindow::onNetReady() {
    qDebug("[NP-conn] onNetReady host=%d selfLoaded=%d",
           gNetplay().isHost() ? 1 : 0, m_npSelfLoaded ? 1 : 0);
    log("🌐 상대 READY");
    m_npPeerReady = true;

    if (gNetplay().isHost()) {
        // 호스트: 내가 이미 로딩 완료했으면 START 전송
        if (m_npSelfLoaded) {
            m_npReadyRetry->stop();
            gNetplay().sendStart();   // 조인에게 MSG_START 전송 + 상태→Playing
            onNetStart();             // 호스트 자신도 즉시 시작
        }
        // 아직 로딩 중이면 netplayStartGame() 마지막에서 m_npPeerReady 플래그 확인
    }
    // 조인: MSG_START 수신 시 onNetStart() 자동 호출 — 여기선 아무것도 안 함
}

// 양쪽: START 수신 → 프레임 0부터 동시 시작
void MainWindow::onNetStart() {
    // 이중 시작 방지: MSG_START 중복 수신 or processEvents 재진입 시 크래시 차단
    // (호스트가 신뢰성 위해 START 를 여러 번 보내므로 중복은 정상 — 로그 안 함)
    if (m_npStarted) return;
    m_npStarted = true;
    qDebug("[NP] onNetStart() begin — gameLoaded=%d core=%p",
           gState.gameLoaded ? 1 : 0, (void*)m_core);
    log(QString("🌐 START — Frame 0 동시 시작 (딜레이 %1f)").arg(gSettings.netplayInputDelay));
    // READY 재전송 타이머 중지
    if (m_npReadyRetry) m_npReadyRetry->stop();
    gState.frameCount  = 0;
    m_npStates.clear();
    m_npInputHistory.clear();
    m_npDelayQueue.clear();
    m_frameDelay     = 0.0;
    m_pendingSyncSf  = -1;
    m_pendingSyncCur = -1;
    m_pendingSyncData.clear();
    startEmu();
}

// 양쪽: 게임 종료 → Lobby 복귀 (소켓 유지)
void MainWindow::onNetGameOver() {
    log("🌐 GAME OVER — Lobby 복귀");
    cleanupNetplay();
}

// ════════════════════════════════════════════════════════════
//  GGPO desync 감지 (체크섬 비교 + 재동기)
// ════════════════════════════════════════════════════════════

// 상태 CRC32 (FNV-1a 32bit — 빠르고 결정론적, 충돌 무시 가능 수준)
uint32_t MainWindow::npChecksum(const QByteArray& data) {
    uint32_t h = 2166136261u;               // FNV offset basis
    const unsigned char* p =
        reinterpret_cast<const unsigned char*>(data.constData());
    const int n = data.size();
    for (int i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;                      // FNV prime
    }
    return h;
}

// 상대 체크섬 수신 → 저장 후 같은 프레임 비교
void MainWindow::onNetChecksum(quint32 frame, quint32 crc) {
    if (gNetplay().isHost()) {
        // 호스트도 체크섬을 받지만, 재동기는 클라가 요청하므로
        // 호스트는 비교만 (로그용). 저장만 해둠.
    }
    m_remoteChecksums[frame] = crc;
    checkDesync(frame);
    while (m_remoteChecksums.size() > 16)
        m_remoteChecksums.erase(m_remoteChecksums.begin());
}

// 같은 프레임의 로컬/원격 CRC 비교 → 불일치면 desync
void MainWindow::checkDesync(uint32_t frame) {
    auto itL = m_localChecksums.find(frame);
    auto itR = m_remoteChecksums.find(frame);
    if (itL == m_localChecksums.end() || itR == m_remoteChecksums.end())
        return;   // 아직 양쪽 다 안 모임

    if (itL->second == itR->second) {
        // 일치 — 결정론 정상 (가끔 로그)
        static int s_okCount = 0;
        if ((++s_okCount % 20) == 1)
            qDebug("[GGPO] sync OK #%d frame=%u crc=%08x", s_okCount, frame, itL->second);
        return;
    }

    // 불일치 = desync
    qDebug("[GGPO] DESYNC frame=%u local=%08x remote=%08x",
           frame, itL->second, itR->second);
    log(QString("⚠ desync 감지 (frame %1) → 재동기").arg(frame));

    // 클라이언트: 호스트에 풀스테이트 재동기 요청 (중복 방지)
    if (!gNetplay().isHost() && !m_resyncPending) {
        m_resyncPending = true;
        gNetplay().sendResyncReq(frame);
    }
}

// 호스트: 클라의 재동기 요청 받음 → 현재 풀스테이트 1회 전송
void MainWindow::onNetResyncReq(quint32 frame) {
    Q_UNUSED(frame);
    if (!gNetplay().isHost() || !m_core || !gState.gameLoaded) return;
    size_t sz = m_core->serializeSize();
    if (sz == 0) return;
    QByteArray buf(static_cast<int>(sz), Qt::Uninitialized);
    if (m_core->serialize(buf.data(), sz)) {
        log(QString("🔄 재동기 요청 수신 → 풀스테이트 전송 (frame %1)")
            .arg(gState.frameCount));
        gNetplay().sendState(static_cast<uint32_t>(gState.frameCount), buf);
    }
}

// ── 호스트: 게임 선택 후 START 버튼 ─────────────────────────
void MainWindow::netplayStartGame() {
    qDebug("[NP-conn] netplayStartGame rom=%s",
           m_selectedGame.toUtf8().constData());
    if (m_selectedGame.isEmpty()) { log("게임을 먼저 선택하세요"); return; }
    if (!gNetplay().active() || !gNetplay().isHost()) return;
    if (gNetplay().netState() != NetplayManager::State::Lobby) {
        log("🌐 이미 게임 진행 중"); return;
    }

    // 플래그 초기화
    m_npSelfLoaded = false;
    m_npPeerReady  = false;
    m_npStarted    = false;

    int delay = m_netDelay;
    gSettings.netplayInputDelay = delay;
    m_npDelayQueue.clear();
    log(QString("🌐 게임 선택 동기화 → %1  (딜레이 %2f)").arg(m_selectedGame).arg(delay));

    // 조인에게 게임 이름 + 입력 지연 전송 (조인은 자동 로드 → sendReady)
    gNetplay().sendLoadGame(m_selectedGame, delay);

    // 호스트도 동시에 로드
    if (!loadRomInternal()) { log("🌐 ROM 로드 실패"); return; }
    m_npSelfLoaded = true;
    log("🌐 호스트 로딩 완료 → READY 전송");
    gNetplay().sendReady();

    // 상대 READY 수신 전까지 300ms 마다 READY 재전송
    m_npReadyRetry->start();

    // 이미 onNetReady()가 먼저 실행된 경우 (빠른 join) → 즉시 시작
    if (m_npPeerReady) {
        m_npReadyRetry->stop();
        gNetplay().sendStart();
        onNetStart();
    }
}

// ── CleanupNetplay: 소켓 유지 + 게임 상태만 초기화 ──────────
// 게임 종료, 다른 게임 선택, GAME OVER 수신 시 호출
void MainWindow::cleanupNetplay() {
    // 에뮬 루프 정지
    if (m_timer) m_timer->stop();
    if (m_core && m_core->gameLoaded()) m_core->unloadGame();
    gState.gameLoaded  = false;
    gState.isPaused    = false;
    gState.frameCount  = 0;
    gState.netplayResim = false;

    // 롤백 버퍼 초기화 (소켓은 건드리지 않음)
    m_npStates.clear();
    m_npInputHistory.clear();
    m_npDelayQueue.clear();
    m_frameDelay = 0.0;
    m_pendingResimTo = -1;   // ← 재시뮬 상태 리셋 (다음 게임 속도 이상 방지)
    m_npSelfLoaded   = false;
    m_npPeerReady    = false;
    m_npStarted      = false;
    m_pendingSyncSf  = -1;
    m_pendingSyncCur = -1;
    m_pendingSyncData.clear();
    // GGPO 체크섬 상태 리셋
    m_localChecksums.clear();
    m_remoteChecksums.clear();
    m_lastChecksumFrame = 0;
    m_resyncPending     = false;
    // AFL 타이밍 누산기 리셋 (다음 게임이 잔류 누산으로 빨라지는 것 방지)
    m_frameAccum = 0.0;
    if (m_npReadyRetry) m_npReadyRetry->stop();

    // ── 릴레이 폴링 완전 중단 ────────────────────────────────
    //   세대를 올려 in-flight 응답·예약된 재시도 람다를 모두 무효화하고,
    //   타이머와 피어 처리 플래그를 리셋한다. 이게 없으면 디스커넥트 후에도
    //   옛 폴링 루프가 살아남아 재호스트/재조인이 먹지 않았다(스팀덱에서 관찰).
    ++m_relayPollGen;
    if (m_relayPollTimer) { m_relayPollTimer->stop(); m_relayPollTimer->deleteLater(); m_relayPollTimer = nullptr; }
    m_relayPeerHandled = false;

    gNetplay().resetGameState();
    gNetplay().cleanupGame();

    // 게임 화면 → GUI 복귀
    if (m_stack && m_stack->currentIndex() == 1)
        leaveGameScreen();

    log("🌐 게임 정리 완료 — Lobby 대기 중");
}

void MainWindow::log(const QString& msg) {
    qDebug() << msg;
    if (m_shell) m_shell->log(msg);      // EVENTS 박스에 남긴다
}
// ════════════════════════════════════════════════════════════
//  닫기
// ════════════════════════════════════════════════════════════
void MainWindow::closeEvent(QCloseEvent* e) {
    m_timer->stop();
    if (gState.isRecording) stopRecording();  // 녹화 중이면 안전 종료
    if (m_mediaPlayer) m_mediaPlayer->stop();
    if (m_gamepad) m_gamepad->stop();
    gNetplay().shutdown();
    if (m_core) {
        if (m_core->gameLoaded()) m_core->unloadGame();
        m_core->unload();
    }
    if (m_audio) m_audio->shutdown();
    gSettings.save();
    e->accept();
}
