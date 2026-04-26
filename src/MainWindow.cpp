// MainWindow.cpp — 메인 윈도우 (Phase 3 완전 구현)

#include "MainWindow.h"
#include "AppSettings.h"
#include "EmulatorState.h"
#include "GameNamesDb.h"

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
#include <QAudioOutput>
#include <QVideoFrame>
#include <QMediaFormat>
#include <QDialog>
#include <QKeySequence>
#include <QHeaderView>
#include <QTableWidget>
#include <QMessageBox>

// ── KEY CAPTURE DIALOG (non-Q_OBJECT) ──────────────────────────
class KeyCaptureDialog : public QDialog {
public:
    int capturedKey = 0;
    explicit KeyCaptureDialog(const QString& action, QWidget* parent)
        : QDialog(parent) {
        setWindowTitle("Key Remap");
        setFixedSize(300, 100);
        setStyleSheet("QDialog{background:#000820;border:1px solid #334488;}");
        auto* lbl = new QLabel(
            QString("<center><b style='color:#aaccff;font-family:Courier New;font-size:13px;'>"
                    "[ %1 ]</b><br>"
                    "<span style='color:#668899;font-family:Courier New;font-size:10px;'>"
                    "Press any key... (Esc = cancel)</span></center>").arg(action),
            this);
        lbl->setTextFormat(Qt::RichText);
        lbl->setAlignment(Qt::AlignCenter);
        auto* lay = new QVBoxLayout(this);
        lay->addWidget(lbl);
        setModal(true);
    }
protected:
    void keyPressEvent(QKeyEvent* e) override {
        int k = e->key();
        if (k == Qt::Key_Escape) { reject(); return; }
        capturedKey = k;
        accept();
    }
};

// ── GAMEPAD BUTTON CAPTURE DIALOG ──────────────────────────────
// 게임패드/아케이드스틱의 버튼을 실제로 눌러서 캡처하는 다이얼로그
// Q_OBJECT 없이 QTimer 람다로 폴링 (KeyCaptureDialog와 동일한 패턴)
class GamepadCaptureDialog : public QDialog {
public:
    int capturedBtn = -1;  // XInput: bitmask / WinMM: 0-based index / -1=취소

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
        int init = gpad->pollRawForCapture(winmm);
        int prevState = (init >= 0) ? init : 0;

        // ~60Hz 폴링으로 새 버튼 입력 감지
        auto* timer = new QTimer(this);
        timer->setInterval(16);
        connect(timer, &QTimer::timeout, this, [=, &prevState]() mutable {
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

// ── 공통 스타일 ───────────────────────────────────────────
QString MainWindow::btnStyle(bool accent) {
    if (accent)
        return "QPushButton{background:#002266;color:#66aaff;border:2px solid #0055ff;"
               "padding:7px 10px;font-family:'Courier New';font-size:11px;}"
               "QPushButton:hover{background:#003399;color:#ffffff;}"
               "QPushButton:pressed{background:#0044cc;}"
               "QPushButton:disabled{background:#111122;color:#334466;border-color:#223355;}";
    return "QPushButton{background:#000055;color:#aaccff;border:2px solid #4466ff;"
           "padding:7px 10px;font-family:'Courier New';font-size:11px;}"
           "QPushButton:hover{background:#0000aa;color:#ffffff;}"
           "QPushButton:pressed{background:#0000dd;}"
           "QPushButton:disabled{background:#111122;color:#446688;border-color:#223355;}";
}
QString MainWindow::editStyle() {
    return "QLineEdit,QSpinBox,QComboBox{"
           "background:#000820;color:#aaccff;border:1px solid #334488;"
           "padding:4px;font-family:'Courier New';font-size:11px;}"
           "QLineEdit:focus,QSpinBox:focus,QComboBox:focus{border-color:#6688ff;}"
           "QComboBox::drop-down{border:none;}"
           "QComboBox QAbstractItemView{background:#000820;color:#aaccff;"
           "selection-background-color:#001166;}";
}
QString MainWindow::labelStyle() {
    return "QLabel{color:#6688aa;font-family:'Courier New';font-size:10px;}";
}
QString MainWindow::groupStyle() {
    return "QGroupBox{color:#4466aa;border:1px solid #223366;"
           "border-radius:2px;margin-top:12px;padding:4px;"
           "font-family:'Courier New';font-size:10px;}"
           "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;}";
}

// ── 생성자 ─────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("FBNEORAGEX Core Edition 1.8");
    m_windowedSize = QSize(1360, 840);
    resize(m_windowedSize);
    setMinimumSize(900, 640);

    // 아이콘 — EXE 내장 리소스(icon.png)를 모든 크기로 사용
    // 탐색기 아이콘은 app.rc + icon.ico 가 담당 (winres 링크 단계)
    // 창 제목표시줄 아이콘은 QIcon(픽스맵)
    {
        QIcon appIcon;
        // Qt 리소스에서 로드 (빌드 시 EXE에 내장)
        QPixmap pm(":/assets/icon.png");
        if (!pm.isNull()) {
            // 여러 크기 추가 → 제목표시줄/작업표시줄 모두 선명하게 표시
            for (int sz : {16, 24, 32, 48, 64, 128, 256})
                appIcon.addPixmap(pm.scaled(sz, sz,
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            // fallback: 외부 파일
            QString icoPath = QCoreApplication::applicationDirPath() + "/assets/icon.ico";
            if (QFile::exists(icoPath))
                appIcon = QIcon(icoPath);
        }
        if (!appIcon.isNull())
            setWindowIcon(appIcon);
    }

    // 키맵 — 저장된 매핑이 있으면 복원, 없으면 기본값
    m_keymap = buildDefaultKeymap();
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
        refreshCheatList();
    });
    connect(m_cheat, &CheatManager::cheatsCleared, this, [this]{
        refreshCheatList();
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
        // 게임 화면(스택 인덱스 1)이면 네비 비활성화
        if (!m_stack || m_stack->currentIndex() != 0) {
            m_navDir = 0; m_navRepeatMs = 0; return;
        }
        if (!m_gameList) return;

        static constexpr int REPEAT_INIT = 380; // 첫 반복 딜레이 (ms)
        static constexpr int REPEAT_RATE =  90; // 반복 간격 (ms)
        static constexpr int TICK        =  16; // 타이머 주기

        int up     = gState.rawKeys[4]; // libretro UP
        int down   = gState.rawKeys[5]; // libretro DOWN
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

        if (moved) {
            int cnt = m_gameList->count();
            if (cnt > 0) {
                int row    = m_gameList->currentRow();
                if (row < 0) row = (m_navDir < 0) ? cnt - 1 : 0;
                else         row = std::clamp(row + m_navDir, 0, cnt - 1);
                m_gameList->setCurrentRow(row);
                m_gameList->scrollToItem(m_gameList->item(row),
                                         QAbstractItemView::EnsureVisible);
            }
        }

        // A버튼 (rawKeys[8] = libretro A) — 엣지 감지로 게임 실행
        bool aDown = (gState.rawKeys[8] != 0);
        if (aDown && !m_navAWasDown && !m_selectedGame.isEmpty()) {
            launchGame();
        }
        m_navAWasDown = aDown;

        // ── LEFT / RIGHT D-패드 → 페이지 업/다운 ─────────────────
        bool lDown = (gState.rawKeys[6] != 0);  // libretro LEFT
        bool rDown = (gState.rawKeys[7] != 0);  // libretro RIGHT
        int  cnt   = m_gameList->count();

        auto pageMove = [&](int dir) {
            if (cnt <= 0) return;
            int rowH     = m_gameList->sizeHintForRow(0);
            int pageSize = (rowH > 0)
                ? std::max(1, m_gameList->viewport()->height() / rowH)
                : 10;
            int cur2    = std::max(0, m_gameList->currentRow());
            int newRow  = std::clamp(cur2 + dir * pageSize, 0, cnt - 1);
            m_gameList->setCurrentRow(newRow);
            m_gameList->scrollToItem(m_gameList->item(newRow),
                                     QAbstractItemView::PositionAtTop);
        };

        if (lDown && !m_navLWasDown) pageMove(-1);  // Page Up
        if (rDown && !m_navRWasDown) pageMove( 1);  // Page Down
        m_navLWasDown = lDown;
        m_navRWasDown = rDown;
    });
    m_uiNavTimer->start();

    connect(m_core, &LibretroCore::logMessage, this, &MainWindow::log);

    // 넷플레이 시그널
    connect(&gNetplay(), &NetplayManager::connected,       this, &MainWindow::onNetConnected);
    connect(&gNetplay(), &NetplayManager::disconnected,    this, &MainWindow::onNetDisconnected);
    connect(&gNetplay(), &NetplayManager::error,           this, &MainWindow::onNetError);
    connect(&gNetplay(), &NetplayManager::stateChanged,    this, &MainWindow::onNetStateChanged);
    connect(&gNetplay(), &NetplayManager::loadGameReceived,this, &MainWindow::onNetLoadGame);
    connect(&gNetplay(), &NetplayManager::readyReceived,   this, &MainWindow::onNetReady);
    connect(&gNetplay(), &NetplayManager::startReceived,   this, &MainWindow::onNetStart);
    connect(&gNetplay(), &NetplayManager::gameOverReceived,this, &MainWindow::onNetGameOver);

    // 하드 싱크 수신 (클라이언트 측): 호스트가 보낸 스냅샷을 대기열에 저장
    // 클라이언트는 해당 프레임에 도달했을 때 onEmuTimer 안에서 적용
    // ── 즉각 롤백 + Re-simulation Catch-up ─────────────────────
    // stateReceived 가 발화되는 순간(Qt 이벤트 루프, 메인스레드) 즉시 실행
    // → onEmuTimer 다음 틱까지 기다리지 않으므로 화면 갈림 0
    connect(&gNetplay(), &NetplayManager::stateReceived,
            this, [this](quint32 frame, QByteArray data) {
        // 호스트는 수신 무시, 코어 미로드 상태도 무시
        if (gNetplay().isHost() || !m_core || !gState.gameLoaded) return;

        int sf  = static_cast<int>(frame);
        int cur = gState.frameCount;

        // 너무 오래된 싱크는 폐기 (이미 MAX_ROLLBACK 창 밖)
        if (sf < cur - NetplayManager::MAX_ROLLBACK) return;

        // ── Time Drift: m_frameDelay ±1ms 방향 설정 ────────────
        // 로컬이 호스트보다 앞서면 슬로우다운, 뒤처지면 스피드업
        // (60프레임에 걸쳐 분산 흡수 → 속도 점프 없음)
        {
            double fps    = (gState.coreFps > 0) ? gState.coreFps : 60.0;
            double baseMs = 1000.0 / fps;
            int    drift  = cur - sf;               // 양수=로컬 앞섬
            m_frameDelay  = std::clamp(
                m_frameDelay + (drift * baseMs) / 60.0,
                -5.0, 8.0);
        }

        // ── Step 1: 호스트 스냅샷으로 sf 시점 복원 ─────────────
        int savedCur = cur;
        m_core->unserialize(data.constData(), static_cast<size_t>(data.size()));
        gState.frameCount = sf;
        gNetplay().confirmFramesUpTo(static_cast<uint32_t>(sf));

        // ── Step 2: Re-simulation Catch-up (sf → savedCur) ─────
        // m_npInputHistory 에 저장된 로컬 입력으로 즉시 현재 프레임까지 재시뮬
        // → 화면이 뒤로 점프하지 않고 투명하게 복구
        gState.netplayResim = true;
        for (int rf = sf; rf < savedCur; ++rf) {
            NpInputState& is = m_npInputHistory[rf];
            uint16_t lb = is.local;
            // 확정 원격 입력 우선, 없으면 hold-last 예측
            uint16_t rb = static_cast<uint16_t>(
                gNetplay().getRemoteInput(static_cast<uint32_t>(rf)));
            is.remote = rb;  // 히스토리 갱신
            gNetplay().recordPrediction(static_cast<uint32_t>(rf), rb);

            npApplyInput(lb, rb);
            m_core->run();
            gState.frameCount = rf + 1;

            // 재시뮬된 프레임의 스냅샷 저장 → 이후 롤백 기준점 최신화
            size_t sz = m_core->serializeSize();
            if (sz > 0) {
                QByteArray buf(static_cast<int>(sz), Qt::Uninitialized);
                if (m_core->serialize(buf.data(), sz))
                    m_npStates[rf + 1] = buf;
            }
        }
        gState.netplayResim = false;

        // ── Step 3: 오래된 히스토리 정리 ────────────────────────
        int cutoff = sf - 2;
        for (auto it = m_npStates.begin(); it != m_npStates.end(); )
            it = (it.key() < cutoff) ? m_npStates.erase(it) : ++it;
        while (!m_npInputHistory.empty() &&
               m_npInputHistory.begin()->first < cutoff)
            m_npInputHistory.erase(m_npInputHistory.begin());

        // cur 은 savedCur 유지 → gState.frameCount 동기화
        gState.frameCount = savedCur;
    });

    // 에뮬 타이머 (1ms → AFL 로 조절)
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onEmuTimer);

    // 커스텀 마우스 커서 적용
    {
        QPixmap curPx(":/assets/mousepoint.png");
        if (!curPx.isNull())
            QApplication::setOverrideCursor(QCursor(curPx, 0, 0));
    }

    // UI 빌드
    buildUi();

    // 코어 설정
    QString base = QCoreApplication::applicationDirPath();
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
    m_audio->init(gSettings.audioSampleRate, gSettings.audioBufferMs);

    // 터보 설정 복원
    gState.turboPeriod = gSettings.turboPeriod;
    for (const QString& s : gSettings.turboButtons.split(',', Qt::SkipEmptyParts)) {
        bool ok; int idx = s.trimmed().toInt(&ok);
        if (ok && idx >= 0 && idx < 16) gState.turboBtns[idx] = true;
    }

    // 게임패드 매핑 복원
    if (!gSettings.xinputMapping.isEmpty())
        m_gamepad->setXInputMapping(gSettings.xinputMapping);
    if (!gSettings.winmmMapping.isEmpty())
        m_gamepad->setWinMMMapping(gSettings.winmmMapping);

    // 게임패드 시작
    m_gamepad->start();

    // 앱 전역 이벤트 필터 (탭키 전환 등)
    qApp->installEventFilter(this);

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

    // 패널 애니메이션
    if (m_gamelistPanel) m_gamelistPanel->startAnim(0);
    if (m_optionsPanel)  m_optionsPanel->startAnim(80);
    if (m_previewPanel)  m_previewPanel->startAnim(160);
    if (m_eventsPanel)   m_eventsPanel->startAnim(240);
}

MainWindow::~MainWindow() {}

// ════════════════════════════════════════════════════════════
//  buildUi — 최상위 레이아웃: 탭 위젯 + 게임 캔버스
// ════════════════════════════════════════════════════════════
void MainWindow::buildUi() {
    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    // ── GUI 화면 (index 0) ──────────────────────────────────
    m_guiWidget = new QWidget;
    m_guiWidget->setObjectName("guiRoot");
    m_guiWidget->setStyleSheet(
        "QWidget#guiRoot{"
        "border-image:url(:/assets/background.png) 0 0 0 0 stretch stretch;}"
        "QWidget{background:transparent;}");

    QVBoxLayout* guiV = new QVBoxLayout(m_guiWidget);
    guiV->setContentsMargins(0, 0, 0, 0);
    guiV->setSpacing(0);

    // 탭 없이 단일 메인 위젯
    m_mainTab = new QWidget;
    buildMainTab();
    guiV->addWidget(m_mainTab);

    m_stack->addWidget(m_guiWidget);

    // ── 게임 캔버스 (index 1) ───────────────────────────────
    m_canvas = new GameCanvas;
    m_canvas->setMouseTracking(true);   // 버튼 안 눌러도 마우스 이동 감지
    connect(m_canvas, &GameCanvas::glLogMessage, this, &MainWindow::log);
    m_stack->addWidget(m_canvas);
}

// ════════════════════════════════════════════════════════════
//  buildMainTab — 게임 목록 + 프리뷰 + 버튼 + 로그
// ════════════════════════════════════════════════════════════
void MainWindow::buildMainTab() {
    QVBoxLayout* vRoot = new QVBoxLayout(m_mainTab);
    vRoot->setContentsMargins(6, 6, 6, 6);
    vRoot->setSpacing(4);

    // ════════════════════════════════════════════════
    //  상단: GAMELIST(좌) + OPTIONS 패널(우)
    // ════════════════════════════════════════════════
    QHBoxLayout* hTop = new QHBoxLayout;
    hTop->setSpacing(6);

    // ── 좌: GAMELIST ─────────────────────────────────
    m_gamelistPanel = new BorderPanel("GAMELIST");

    // 즐겨찾기 필터 버튼
    QHBoxLayout* filterH = new QHBoxLayout;
    filterH->setSpacing(3);
    auto makeFilterBtn = [&](const QString& label) {
        QPushButton* b = new QPushButton(label);
        b->setCheckable(true);
        b->setStyleSheet(
            "QPushButton{background:#000033;color:#6688bb;border:1px solid #224488;"
            "padding:3px 10px;font-family:'Courier New';font-size:10px;font-weight:bold;}"
            "QPushButton:checked{background:#001166;color:#aaddff;border-color:#4488ff;}"
            "QPushButton:hover{background:#00004d;color:#99ccff;}");
        return b;
    };
    auto* btnAll  = makeFilterBtn("ALL");
    auto* btnFav  = makeFilterBtn("★ FAV");
    auto* btnStar = makeFilterBtn("☆");
    btnAll->setChecked(true);
    filterH->addWidget(btnAll); filterH->addWidget(btnFav); filterH->addWidget(btnStar);
    filterH->addStretch();
    m_gamelistPanel->innerLayout()->addLayout(filterH);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("Search...");
    m_searchEdit->setStyleSheet(editStyle());
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::filterRoms);
    m_gamelistPanel->innerLayout()->addWidget(m_searchEdit);

    m_gameList = new QListWidget;
    m_gameList->setStyleSheet(
        "QListWidget{background:rgba(0,0,8,220);border:none;color:#99ccee;"
        "font-family:'Courier New';font-size:12px;outline:none;}"
        "QListWidget::item{padding:4px 8px;}"
        "QListWidget::item:selected{background:#001166;color:#ffffff;"
        "border-left:3px solid #0088ff;font-weight:bold;}"
        "QListWidget::item:hover{background:#000833;}"
        "QScrollBar:vertical{background:#001133;width:14px;border:none;margin:2px 2px 2px 0;}"
        "QScrollBar::handle:vertical{background:#3366aa;border-radius:5px;min-height:30px;}"
        "QScrollBar::handle:vertical:hover{background:#5588cc;}"
        "QScrollBar::handle:vertical:pressed{background:#77aaee;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:none;}");
    m_gameList->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(btnAll,  &QPushButton::clicked, this, [this, btnAll, btnFav, btnStar]{
        m_glFilter = 0;
        btnAll->setChecked(true); btnFav->setChecked(false); btnStar->setChecked(false);
        log(QString("필터: ALL (총 %1개)").arg(m_allRoms.size()));
        filterRoms(m_searchEdit->text());
    });
    connect(btnFav,  &QPushButton::clicked, this, [this, btnAll, btnFav, btnStar]{
        m_glFilter = 1;
        btnFav->setChecked(true); btnAll->setChecked(false); btnStar->setChecked(false);
        int cnt = 0;
        for (const auto& [d, r] : m_allRoms) if (isFavorite(r)) ++cnt;
        log(QString("필터: ★FAV (%1개)").arg(cnt));
        filterRoms(m_searchEdit->text());
    });
    connect(btnStar, &QPushButton::clicked, this, [this, btnAll, btnFav, btnStar]{
        m_glFilter = 2;
        btnStar->setChecked(true); btnAll->setChecked(false); btnFav->setChecked(false);
        int cnt = 0;
        for (const auto& [d, r] : m_allRoms) if (!isFavorite(r)) ++cnt;
        log(QString("필터: ☆ (%1개)").arg(cnt));
        filterRoms(m_searchEdit->text());
    });

    connect(m_gameList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos){
        QListWidgetItem* item = m_gameList->itemAt(pos);
        if (!item) return;
        QString rom = item->data(Qt::UserRole).toString();
        bool fav = isFavorite(rom);
        QMenu menu(this);
        menu.setStyleSheet("QMenu{background:#000820;color:#aaccff;border:1px solid #334488;"
                           "font-family:'Courier New';font-size:10px;}"
                           "QMenu::item:selected{background:#001166;}");
        QAction* launchAct = menu.addAction("▶  실행");
        QAction* favAct    = menu.addAction(fav ? "☆  즐겨찾기 제거" : "★  즐겨찾기 추가");
        menu.addSeparator();
        QAction* cheatAct  = menu.addAction("CHEATS");
        QAction* sel = menu.exec(m_gameList->viewport()->mapToGlobal(pos));
        if      (sel == launchAct) { selectGame(rom); launchGame(); }
        else if (sel == favAct)    { toggleFavorite(rom); }
        else if (sel == cheatAct)  { selectGame(rom); if (m_optionsStack) m_optionsStack->setCurrentIndex(7); }
    });
    connect(m_gameList, &QListWidget::itemSelectionChanged, this, [this]{
        auto items = m_gameList->selectedItems();
        if (!items.isEmpty()) selectGame(items.first()->data(Qt::UserRole).toString());
    });
    connect(m_gameList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item){
        if (item) { selectGame(item->data(Qt::UserRole).toString()); launchGame(); }
    });
    m_gamelistPanel->innerLayout()->addWidget(m_gameList, 1);
    hTop->addWidget(m_gamelistPanel, 3);

    // ── 우: OPTIONS (전체폭 스택, 클릭→상세→BACK 방식) ──────
    m_optionsPanel = new BorderPanel("OPTIONS");

    // ── 전체폭 콘텐츠 스택 ───────────────────────────────────
    m_optionsStack = new QStackedWidget;
    m_optionsStack->setStyleSheet(
        "QStackedWidget{background:transparent;}"
        "QScrollArea{background:#000410;border:none;}"
        "QGroupBox{color:#4488cc;border:1px solid #223366;border-radius:2px;"
        "margin-top:14px;padding:6px;font-family:'Courier New';font-size:10px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 6px;}"
        "QScrollBar:vertical{background:#000022;width:8px;border:none;}"
        "QScrollBar::handle:vertical{background:#224466;border-radius:4px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");

    // ════════════════════════════════════════════════════════
    //  페이지 0: 홈 메뉴 (background.png 배경 + 메뉴 버튼)
    // ════════════════════════════════════════════════════════
    {
        QWidget* homePage = new QWidget;
        // guiRoot의 border-image가 비쳐 보이도록 transparent 유지
        // (border-image 중복 설정 제거 → guiRoot 하나로 통일)
        homePage->setStyleSheet(
            "QWidget{background:transparent;}"
            "QPushButton{background:rgba(0,4,20,180);color:#00aaff;"
            "border:none;border-left:3px solid transparent;"
            "font-family:'Courier New';font-size:13px;font-weight:bold;"
            "text-align:left;padding:8px 20px;letter-spacing:2px;}"
            "QPushButton:hover{color:#00eeff;background:rgba(0,60,180,160);"
            "border-left:3px solid #0088ff;}"
            "QPushButton:pressed{color:#ffffff;background:rgba(0,40,140,200);}");
        homePage->setObjectName("optHome");

        QVBoxLayout* homeV = new QVBoxLayout(homePage);
        homeV->setContentsMargins(0, 0, 0, 0);
        homeV->setSpacing(0);

        // 상단 타이틀 레이블
        QLabel* titleLbl = new QLabel("OPTIONS");
        titleLbl->setStyleSheet(
            "QLabel{color:#0066cc;font-family:'Courier New';font-size:10px;"
            "font-weight:bold;letter-spacing:4px;background:rgba(0,0,10,160);"
            "padding:6px 14px;border-bottom:1px solid #001a44;}");
        homeV->addWidget(titleLbl);

        homeV->addStretch(1);

        // 메뉴 버튼
        static const struct { const char* label; int page; } menuItems[] = {
            {"CONTROLS",         1},
            {"DIRECTORIES",      2},
            {"VIDEO OPTIONS",    3},
            {"AUDIO OPTIONS",    4},
            {"MACHINE SETTINGS", 5},
            {"SHOTS FACTORY",    6},
            {"CHEATS",           7},
            {"MULTIPLAYER",      8},
        };
        for (const auto& mi : menuItems) {
            QPushButton* b = new QPushButton(
                QString("▸  %1").arg(mi.label));
            b->setFlat(true);
            b->setFixedHeight(38);
            int pageIdx = mi.page;
            connect(b, &QPushButton::clicked, this, [this, pageIdx]{
                m_optionsStack->setCurrentIndex(pageIdx);
            });
            homeV->addWidget(b);
        }

        homeV->addStretch(1);

        // 구분선
        auto* div = new QFrame; div->setFrameShape(QFrame::HLine);
        div->setStyleSheet("background:rgba(0,80,160,120);border:none;max-height:1px;");
        homeV->addWidget(div);

        // 하단: 슬롯 + SAVE/LOAD/SHOT/REC/FF
        QWidget* bottomCtrl = new QWidget;
        bottomCtrl->setStyleSheet("background:rgba(0,2,14,200);");
        QVBoxLayout* bottomV = new QVBoxLayout(bottomCtrl);
        bottomV->setContentsMargins(12, 6, 12, 6);
        bottomV->setSpacing(4);

        QHBoxLayout* slotH = new QHBoxLayout; slotH->setSpacing(6);
        QLabel* slotLbl = new QLabel("SLOT: 1");
        slotLbl->setStyleSheet("color:#446688;font-family:'Courier New';font-size:9px;");
        QComboBox* slotBox = new QComboBox;
        slotBox->setStyleSheet(editStyle()); slotBox->setFixedWidth(54);
        for (int i = 1; i <= 8; ++i) slotBox->addItem(QString::number(i));
        connect(slotBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, slotLbl](int idx){
                    m_stateSlot = idx+1;
                    slotLbl->setText(QString("SLOT: %1").arg(m_stateSlot));
                });
        slotH->addWidget(slotLbl); slotH->addWidget(slotBox); slotH->addStretch();
        bottomV->addLayout(slotH);

        QHBoxLayout* ctrlH2 = new QHBoxLayout; ctrlH2->setSpacing(3);
        auto makeSmallBtn = [&](const QString& t, auto fn) {
            auto* b = new QPushButton(t); b->setStyleSheet(btnStyle()); b->setFixedHeight(26);
            connect(b, &QPushButton::clicked, this, fn); ctrlH2->addWidget(b);
        };
        makeSmallBtn("SAVE", [this]{ saveState(m_stateSlot); });
        makeSmallBtn("LOAD", [this]{ loadState(m_stateSlot); });
        makeSmallBtn("SHOT", [this]{ takeScreenshot(); });
        makeSmallBtn("REC",  [this]{ toggleRecording(); });
        makeSmallBtn("FF",   [this]{ gState.fastForward = !gState.fastForward; });
        bottomV->addLayout(ctrlH2);
        homeV->addWidget(bottomCtrl);

        m_optionsStack->addWidget(homePage);  // index 0
    }

    // ════════════════════════════════════════════════════════
    //  페이지 1-8: 각 콘텐츠 페이지 (BACK 버튼 헤더 포함)
    // ════════════════════════════════════════════════════════
    static const char* kPageTitles[] = {
        "CONTROLS",
        "DIRECTORIES",
        "VIDEO OPTIONS",
        "AUDIO OPTIONS",
        "MACHINE SETTINGS",
        "SHOTS FACTORY",
        "CHEATS",
        "MULTIPLAYER",
    };

    auto makeContentPage = [&](int titleIdx, auto buildFn) {
        QWidget* wrapper = new QWidget;
        wrapper->setStyleSheet("QWidget{background:#000410;}"
                               "QScrollArea{background:#000410;border:none;}");
        QVBoxLayout* wv = new QVBoxLayout(wrapper);
        wv->setContentsMargins(0, 0, 0, 0);
        wv->setSpacing(0);

        // ── BACK 헤더 바 ─────────────────────────────────────
        QWidget* header = new QWidget;
        header->setFixedHeight(38);
        header->setStyleSheet("QWidget{background:#000820;border-bottom:1px solid #223366;}");
        QHBoxLayout* hh = new QHBoxLayout(header);
        hh->setContentsMargins(8, 0, 12, 0);
        hh->setSpacing(8);

        QPushButton* backBtn = new QPushButton("◀  BACK");
        backBtn->setStyleSheet(btnStyle(false));
        backBtn->setFixedHeight(28);
        backBtn->setFixedWidth(90);
        connect(backBtn, &QPushButton::clicked, this, [this]{
            m_optionsStack->setCurrentIndex(0);
        });

        QLabel* pageTitleLbl = new QLabel(kPageTitles[titleIdx]);
        pageTitleLbl->setStyleSheet(
            "QLabel{color:#aaccff;font-family:'Courier New';"
            "font-size:12px;font-weight:bold;letter-spacing:2px;background:transparent;}");

        hh->addWidget(backBtn);
        hh->addSpacing(10);
        hh->addWidget(pageTitleLbl, 1);
        wv->addWidget(header);

        // ── 콘텐츠 영역 ──────────────────────────────────────
        QWidget* content = new QWidget;
        content->setStyleSheet("QWidget{background:#000410;}");
        buildFn(content);
        wv->addWidget(content, 1);

        m_optionsStack->addWidget(wrapper);
    };

    makeContentPage(0, [&](QWidget* p){ buildControlsPage(p);    }); // 1
    makeContentPage(1, [&](QWidget* p){ buildDirectoriesPage(p); }); // 2
    makeContentPage(2, [&](QWidget* p){ buildVideoPage(p);       }); // 3
    makeContentPage(3, [&](QWidget* p){ buildAudioPage(p);       }); // 4
    makeContentPage(4, [&](QWidget* p){ buildMachinePage(p);     }); // 5
    makeContentPage(5, [&](QWidget* p){ buildShotsPage(p);       }); // 6
    makeContentPage(6, [&](QWidget* p){ buildCheatsPage(p);      }); // 7
    makeContentPage(7, [&](QWidget* p){ buildNetplayPage(p);     }); // 8

    m_optionsStack->setCurrentIndex(0);
    m_optionsPanel->innerLayout()->addWidget(m_optionsStack);
    hTop->addWidget(m_optionsPanel, 5);

    vRoot->addLayout(hTop, 6);

    // ════════════════════════════════════════════════
    //  버튼 바
    // ════════════════════════════════════════════════
    QHBoxLayout* btnBar = new QHBoxLayout; btnBar->setSpacing(4);
    auto makeBarBtn = [&](const QString& t, bool accent, auto fn) {
        auto* b = new QPushButton(t); b->setStyleSheet(btnStyle(accent)); b->setFixedHeight(36);
        connect(b, &QPushButton::clicked, this, fn); btnBar->addWidget(b);
    };
    makeBarBtn("▶  LAUNCH / RESUME", true,  [this]{ launchGame(); });
    makeBarBtn("■  STOP GAME",       false, [this]{
        if (!gState.gameLoaded && !gState.isPaused) return;
        m_timer->stop();
        gState.isPaused = false;
        if (m_core) m_core->unloadGame();
        m_loadedGame.clear();
        leaveGameScreen();
        log("■ 게임 종료");
    });
    makeBarBtn("⏮  RESET",     false, [this]{ if (m_core) m_core->reset(); });
    makeBarBtn("⛶  FULLSCREEN",false, [this]{ toggleFullscreen(); });
    makeBarBtn("✖  EXIT",       false, [this]{ close(); });
    vRoot->addLayout(btnBar);

    // ════════════════════════════════════════════════
    //  하단: PREVIEW(좌) + EVENTS(우)
    // ════════════════════════════════════════════════
    QHBoxLayout* hBot = new QHBoxLayout; hBot->setSpacing(6);

    m_previewPanel = new BorderPanel("PREVIEW");
    m_previewLabel = new QLabel("NO PREVIEW");
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("color:#335577;background:#000008;font-family:'Courier New';font-size:10px;");
    m_previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoWidget  = new QVideoWidget;
    m_videoWidget->setStyleSheet("background:#000008;");
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_previewStack = new QStackedWidget;
    m_previewStack->addWidget(m_previewLabel);
    m_previewStack->addWidget(m_videoWidget);
    m_previewStack->setCurrentIndex(0);
    m_previewPanel->innerLayout()->addWidget(m_previewStack);
    m_mediaPlayer = new QMediaPlayer(this);
    m_mediaPlayer->setVideoOutput(m_videoWidget);
    { auto* ao = new QAudioOutput(this); m_mediaPlayer->setAudioOutput(ao); }
    m_previewVidTimer = new QTimer(this);
    m_previewVidTimer->setSingleShot(true);
    m_previewVidTimer->setInterval(3000);
    connect(m_previewVidTimer, &QTimer::timeout, this, [this]{ loadPreviewVideo(m_selectedGame); });
    hBot->addWidget(m_previewPanel, 3);

    m_eventsPanel = new BorderPanel("EVENTS");
    m_logEdit = new QTextEdit;
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet(
        "QTextEdit{background:rgba(0,0,8,200);border:none;color:#99ccee;"
        "font-family:'Courier New';font-size:10px;}"
        "QScrollBar:vertical{background:#000022;width:8px;}"
        "QScrollBar::handle:vertical{background:#224466;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");
    m_eventsPanel->innerLayout()->addWidget(m_logEdit);
    hBot->addWidget(m_eventsPanel, 5);

    vRoot->addLayout(hBot, 3);
}

// ════════════════════════════════════════════════════════════
//  OPTIONS 패널 페이지 빌더들
// ════════════════════════════════════════════════════════════

// ── 공통 액션 정의 (3개 테이블 공유) ─────────────────────────
static const struct { int id; const char* name; } kCtrlActions[] = {
    { 4, "UP"            },
    { 5, "DOWN"          },
    { 6, "LEFT"          },
    { 7, "RIGHT"         },
    { 1, "BTN A  (Y)"    },
    { 0, "BTN B  (B)"    },
    { 8, "BTN C  (A)"    },
    { 9, "BTN D  (X)"    },
    {10, "BTN E  (L)"    },
    {11, "BTN F  (R)"    },
    { 3, "START"         },
    { 2, "SELECT / COIN" },
};
static const int kCtrlActionCount = (int)(sizeof(kCtrlActions)/sizeof(kCtrlActions[0]));

// XInput 버튼 bitmask → 표시 이름
static QString xinputBtnName(int bitmask) {
    switch (bitmask) {
    case 0x0001:  return "D-Pad Up";
    case 0x0002:  return "D-Pad Down";
    case 0x0004:  return "D-Pad Left";
    case 0x0008:  return "D-Pad Right";
    case 0x0010:  return "Start";
    case 0x0020:  return "Back / Select";
    case 0x0040:  return "L3  (LS Click)";
    case 0x0080:  return "R3  (RS Click)";
    case 0x0100:  return "LB  (L1)";
    case 0x0200:  return "RB  (R1)";
    case 0x1000:  return "A";
    case 0x2000:  return "B";
    case 0x4000:  return "X";
    case 0x8000:  return "Y";
    case 0x10000: return "LT  (L2)";
    case 0x20000: return "RT  (R2)";
    default:      return QString("Btn 0x%1").arg(bitmask, 0, 16);
    }
}

// ── 페이지 0: CONTROLS (키 매핑) ─────────────────────────────
void MainWindow::buildControlsPage(QWidget* page) {
    QVBoxLayout* v = new QVBoxLayout(page);
    v->setContentsMargins(10, 8, 10, 8);
    v->setSpacing(6);

    // 헤더
    QLabel* title = new QLabel("KEY BINDINGS — PLAYER 1");
    title->setStyleSheet("color:#aaccff;font-family:'Courier New';"
                         "font-size:11px;font-weight:bold;border-bottom:1px solid #223366;padding-bottom:4px;");
    v->addWidget(title);

    QLabel* hint = new QLabel("[REMAP] 버튼 클릭 후 키보드 키 또는 게임패드 버튼을 눌러 재설정 / Esc = 취소");
    hint->setStyleSheet("color:#446688;font-family:'Courier New';font-size:9px;");
    v->addWidget(hint);

    // ── 공통 테이블 스타일 ──────────────────────────────────
    const QString tblStyle =
        "QTableWidget{background:#000410;border:1px solid #223366;"
        "color:#aaccff;font-family:'Courier New';font-size:10px;gridline-color:#112233;outline:none;}"
        "QHeaderView::section{background:#001133;color:#6688aa;border:none;"
        "border-bottom:1px solid #223366;padding:3px;"
        "font-family:'Courier New';font-size:9px;font-weight:bold;}"
        "QTableWidget::item{padding:3px 6px;border:none;}"
        "QTableWidget::item:selected{background:#001a66;color:#ffffff;}"
        "QScrollBar:vertical{background:#000022;width:8px;border:none;}"
        "QScrollBar::handle:vertical{background:#224466;border-radius:3px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}";
    const QString remapBtnStyle =
        "QPushButton{background:#001133;color:#6688aa;border:1px solid #334488;"
        "padding:2px;font-family:'Courier New';font-size:9px;}"
        "QPushButton:hover{background:#002255;color:#aaccff;}";

    auto makeTable = [&](QTableWidget*& tbl, const QString& col1) {
        tbl = new QTableWidget;
        tbl->setColumnCount(3);
        tbl->setHorizontalHeaderLabels({"ACTION", col1, ""});
        tbl->setStyleSheet(tblStyle);
        tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
        tbl->setSelectionMode(QAbstractItemView::SingleSelection);
        tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tbl->verticalHeader()->setVisible(false);
        tbl->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        tbl->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        tbl->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        tbl->setColumnWidth(1, 110);
        tbl->setColumnWidth(2, 68);
        tbl->verticalHeader()->setDefaultSectionSize(26);
    };

    // ── 탭 위젯 ─────────────────────────────────────────────
    QTabWidget* tabs = new QTabWidget;
    tabs->setStyleSheet(
        "QTabWidget::pane{background:#000410;border:1px solid #223366;border-top:none;}"
        "QTabBar::tab{background:#000820;color:#446688;border:1px solid #223366;"
        "padding:5px 14px;font-family:'Courier New';font-size:9px;"
        "border-bottom:none;margin-right:2px;}"
        "QTabBar::tab:selected{background:#001133;color:#aaccff;"
        "border-bottom:1px solid #001133;}"
        "QTabBar::tab:hover{background:#001133;color:#8899cc;}");

    // ── Tab 0: KEYBOARD ──────────────────────────────────────
    {
        QWidget* kbPage = new QWidget; kbPage->setStyleSheet("background:#000410;");
        QVBoxLayout* kbV = new QVBoxLayout(kbPage);
        kbV->setContentsMargins(0, 0, 0, 0); kbV->setSpacing(0);
        makeTable(m_ctrlTable, "KEY");
        kbV->addWidget(m_ctrlTable);
        refreshControlsTable();
        tabs->addTab(kbPage, "KEYBOARD");
    }

    // ── Tab 1: GAMEPAD (XInput) ──────────────────────────────
    {
        QWidget* padPage = new QWidget; padPage->setStyleSheet("background:#000410;");
        QVBoxLayout* padV = new QVBoxLayout(padPage);
        padV->setContentsMargins(0, 0, 0, 0); padV->setSpacing(0);
        makeTable(m_padTable, "BUTTON");
        padV->addWidget(m_padTable);
        refreshPadTable();
        tabs->addTab(padPage, "GAMEPAD  (XInput)");
    }

    // ── Tab 2: ARCADE STICK (WinMM) ─────────────────────────
    {
        QWidget* arcPage = new QWidget; arcPage->setStyleSheet("background:#000410;");
        QVBoxLayout* arcV = new QVBoxLayout(arcPage);
        arcV->setContentsMargins(0, 0, 0, 0); arcV->setSpacing(0);
        makeTable(m_winmmTable, "BUTTON");
        arcV->addWidget(m_winmmTable);
        refreshWinMMTable();
        tabs->addTab(arcPage, "ARCADE STICK  (DirectInput)");
    }

    v->addWidget(tabs, 1);

    // ── 게임패드 입력 모드 선택 ─────────────────────────────
    QHBoxLayout* padRow = new QHBoxLayout;
    QLabel* padLbl = new QLabel("GAMEPAD MODE:");
    padLbl->setStyleSheet(labelStyle());
    padRow->addWidget(padLbl);
    QComboBox* padCombo = new QComboBox;
    padCombo->setStyleSheet(editStyle());
    padCombo->addItem("Auto  (XInput → DirectInput)", "auto");
    padCombo->addItem("XInput  (Xbox / 표준 게임패드)", "xinput");
    padCombo->addItem("DirectInput / WinMM  (아케이드 스틱)", "winmm");
    {
        int idx = padCombo->findData(gSettings.inputMode);
        if (idx >= 0) padCombo->setCurrentIndex(idx);
    }
    connect(padCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, padCombo](int) {
            gSettings.inputMode = padCombo->currentData().toString();
            gSettings.save();
            log("게임패드 모드: " + gSettings.inputMode);
        });
    padRow->addWidget(padCombo, 1);
    v->addLayout(padRow);

    // ── TURBO BUTTONS 그룹 ───────────────────────────────────
    {
        const QString grpStyleCtrl =
            "QGroupBox{color:#4488cc;border:1px solid #223366;"
            "border-radius:2px;margin-top:14px;padding:6px;"
            "font-family:'Courier New';font-size:10px;}"
            "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 6px;}";
        const QString ckStyleCtrl =
            "QCheckBox{color:#aaccff;font-family:'Courier New';font-size:10px;}"
            "QCheckBox::indicator{width:14px;height:14px;}";

        QGroupBox* turboGroup = new QGroupBox("TURBO BUTTONS");
        turboGroup->setStyleSheet(grpStyleCtrl);
        QVBoxLayout* turboV = new QVBoxLayout(turboGroup);
        turboV->setSpacing(4);

        static const struct { int idx; const char* name; } turboMap[] = {
            {0,"B(Z)"},{8,"A(X)"},{1,"Y(A)"},{9,"X(S)"},{10,"L(D)"},{11,"R(C)"}
        };
        QHBoxLayout* turboH1 = new QHBoxLayout;
        QHBoxLayout* turboH2 = new QHBoxLayout;
        for (int k = 0; k < 6; ++k) {
            auto* cb = new QCheckBox(turboMap[k].name);
            cb->setStyleSheet(ckStyleCtrl);
            int idx = turboMap[k].idx;
            cb->setChecked(gState.turboBtns.value(idx, false));
            connect(cb, &QCheckBox::toggled, this, [idx](bool on){ gState.turboBtns[idx] = on; });
            (k < 3 ? turboH1 : turboH2)->addWidget(cb);
        }
        turboH1->addStretch(); turboH2->addStretch();
        turboV->addLayout(turboH1); turboV->addLayout(turboH2);

        QHBoxLayout* periodH = new QHBoxLayout; periodH->setSpacing(6);
        QLabel* periodLbl = new QLabel("주기(프레임):"); periodLbl->setStyleSheet(labelStyle());
        QSpinBox* periodSpin = new QSpinBox;
        periodSpin->setStyleSheet(editStyle());
        periodSpin->setRange(1, 30); periodSpin->setValue(gSettings.turboPeriod);
        periodSpin->setFixedWidth(70);
        connect(periodSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [](int v){ gState.turboPeriod = v; });
        periodH->addWidget(periodLbl); periodH->addWidget(periodSpin); periodH->addStretch();
        turboV->addLayout(periodH);
        v->addWidget(turboGroup);
    }

    // ── 하단 리셋 버튼 ───────────────────────────────────────
    QPushButton* resetKbBtn = new QPushButton("RESET KEYBOARD");
    resetKbBtn->setStyleSheet(btnStyle(false)); resetKbBtn->setFixedHeight(26);
    connect(resetKbBtn, &QPushButton::clicked, this, [this]{
        m_keymap = buildDefaultKeymap();
        gSettings.keyboardMapping.clear();
        gSettings.save(); refreshControlsTable();
        log("키보드 매핑 기본값으로 초기화됨");
    });

    QPushButton* resetPadBtn = new QPushButton("RESET GAMEPAD");
    resetPadBtn->setStyleSheet(btnStyle(false)); resetPadBtn->setFixedHeight(26);
    connect(resetPadBtn, &QPushButton::clicked, this, [this]{
        m_gamepad->resetDefaultMapping();
        gSettings.xinputMapping.clear();
        gSettings.save(); refreshPadTable();
        log("게임패드(XInput) 매핑 기본값으로 초기화됨");
    });

    QPushButton* resetArcBtn = new QPushButton("RESET ARCADE");
    resetArcBtn->setStyleSheet(btnStyle(false)); resetArcBtn->setFixedHeight(26);
    connect(resetArcBtn, &QPushButton::clicked, this, [this]{
        m_gamepad->resetDefaultWinMM();
        gSettings.winmmMapping.clear();
        gSettings.save(); refreshWinMMTable();
        log("아케이드 스틱(WinMM) 매핑 기본값으로 초기화됨");
    });

    QHBoxLayout* bh = new QHBoxLayout;
    bh->addStretch();
    bh->addWidget(resetKbBtn);
    bh->addWidget(resetPadBtn);
    bh->addWidget(resetArcBtn);
    v->addLayout(bh);
}

// 컨트롤 테이블 UI 갱신
void MainWindow::refreshControlsTable() {
    if (!m_ctrlTable) return;

    m_ctrlTable->setRowCount(kCtrlActionCount);
    m_ctrlTable->blockSignals(true);

    for (int row = 0; row < kCtrlActionCount; ++row) {
        int libId = kCtrlActions[row].id;

        // 현재 이 libId에 매핑된 Qt 키 역방향 조회
        int curKey = 0;
        for (auto it = m_keymap.constBegin(); it != m_keymap.constEnd(); ++it)
            if (it.value() == libId) { curKey = it.key(); break; }

        QString keyStr = curKey ? QKeySequence(curKey).toString() : "---";

        // Col 0: Action
        auto* actItem = new QTableWidgetItem(kCtrlActions[row].name);
        actItem->setData(Qt::UserRole, libId);
        actItem->setForeground(QColor("#99ccee"));
        m_ctrlTable->setItem(row, 0, actItem);

        // Col 1: Key
        auto* keyItem = new QTableWidgetItem(keyStr);
        keyItem->setTextAlignment(Qt::AlignCenter);
        keyItem->setForeground(QColor("#44ffaa"));
        keyItem->setFont(QFont("Courier New", 10, QFont::Bold));
        m_ctrlTable->setItem(row, 1, keyItem);

        // Col 2: REMAP 버튼
        QPushButton* remapBtn = new QPushButton("REMAP");
        remapBtn->setStyleSheet(
            "QPushButton{background:#001133;color:#6688aa;border:1px solid #334488;"
            "padding:2px;font-family:'Courier New';font-size:9px;}"
            "QPushButton:hover{background:#002255;color:#aaccff;}");
        connect(remapBtn, &QPushButton::clicked, this, [this, row]{
            if (!m_ctrlTable) return;
            auto* actItm = m_ctrlTable->item(row, 0);
            if (!actItm) return;
            int libId2  = actItm->data(Qt::UserRole).toInt();
            QString act = actItm->text();

            // 기존 매핑 키 제거
            for (auto it = m_keymap.begin(); it != m_keymap.end(); ) {
                if (it.value() == libId2) it = m_keymap.erase(it);
                else ++it;
            }

            // 키 캡처 다이얼로그
            KeyCaptureDialog dlg(act, this);
            if (dlg.exec() == QDialog::Accepted && dlg.capturedKey != 0) {
                // 충돌 제거
                m_keymap.remove(dlg.capturedKey);
                m_keymap.insert(dlg.capturedKey, libId2);
                // 설정 저장
                gSettings.keyboardMapping = m_keymap;
                gSettings.save();
                refreshControlsTable();
                log(QString("키 재설정: %1 → %2")
                    .arg(act)
                    .arg(QKeySequence(dlg.capturedKey).toString()));
            } else {
                refreshControlsTable(); // 복원
            }
        });
        m_ctrlTable->setCellWidget(row, 2, remapBtn);
    }
    m_ctrlTable->blockSignals(false);
}

// 게임패드(XInput) 테이블 갱신
void MainWindow::refreshPadTable() {
    if (!m_padTable) return;

    const QString remapStyle =
        "QPushButton{background:#001133;color:#6688aa;border:1px solid #334488;"
        "padding:2px;font-family:'Courier New';font-size:9px;}"
        "QPushButton:hover{background:#002255;color:#aaccff;}";

    QHash<int,int> mapping = m_gamepad->getXInputMapping();

    m_padTable->setRowCount(kCtrlActionCount);
    m_padTable->blockSignals(true);

    for (int row = 0; row < kCtrlActionCount; ++row) {
        int libId = kCtrlActions[row].id;

        // 역방향 조회: libId에 매핑된 버튼 bitmask
        int curBtn = -1;
        for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it)
            if (it.value() == libId) { curBtn = it.key(); break; }

        QString btnStr = (curBtn >= 0) ? xinputBtnName(curBtn) : "---";

        auto* actItem = new QTableWidgetItem(kCtrlActions[row].name);
        actItem->setData(Qt::UserRole, libId);
        actItem->setForeground(QColor("#99ccee"));
        m_padTable->setItem(row, 0, actItem);

        auto* btnItem = new QTableWidgetItem(btnStr);
        btnItem->setTextAlignment(Qt::AlignCenter);
        btnItem->setForeground(QColor("#ffcc44"));
        btnItem->setFont(QFont("Courier New", 10, QFont::Bold));
        m_padTable->setItem(row, 1, btnItem);

        QPushButton* remapBtn = new QPushButton("REMAP");
        remapBtn->setStyleSheet(remapStyle);
        connect(remapBtn, &QPushButton::clicked, this, [this, row] {
            if (!m_padTable) return;
            auto* actItm = m_padTable->item(row, 0);
            if (!actItm) return;
            int libId2  = actItm->data(Qt::UserRole).toInt();
            QString act = actItm->text();

            QHash<int,int> map2 = m_gamepad->getXInputMapping();
            // 기존 매핑 제거
            for (auto it = map2.begin(); it != map2.end(); )
                it.value() == libId2 ? it = map2.erase(it) : ++it;

            GamepadCaptureDialog dlg(act, m_gamepad, false, this);
            if (dlg.exec() == QDialog::Accepted && dlg.capturedBtn >= 0) {
                map2.remove(dlg.capturedBtn);  // 충돌 제거
                map2.insert(dlg.capturedBtn, libId2);
                m_gamepad->setXInputMapping(map2);
                gSettings.xinputMapping = map2;
                gSettings.save();
                refreshPadTable();
                log(QString("게임패드 재설정: %1 → %2")
                    .arg(act, xinputBtnName(dlg.capturedBtn)));
            } else {
                refreshPadTable();
            }
        });
        m_padTable->setCellWidget(row, 2, remapBtn);
    }
    m_padTable->blockSignals(false);
}

// 아케이드 스틱(WinMM) 테이블 갱신
void MainWindow::refreshWinMMTable() {
    if (!m_winmmTable) return;

    const QString remapStyle =
        "QPushButton{background:#001133;color:#6688aa;border:1px solid #334488;"
        "padding:2px;font-family:'Courier New';font-size:9px;}"
        "QPushButton:hover{background:#002255;color:#aaccff;}";

    QHash<int,int> mapping = m_gamepad->getWinMMMapping();

    m_winmmTable->setRowCount(kCtrlActionCount);
    m_winmmTable->blockSignals(true);

    for (int row = 0; row < kCtrlActionCount; ++row) {
        int libId = kCtrlActions[row].id;

        // 역방향 조회: libId에 매핑된 버튼 0-based 인덱스
        int curBtn = -1;
        for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it)
            if (it.value() == libId) { curBtn = it.key(); break; }

        QString btnStr = (curBtn >= 0)
                         ? QString("Button %1").arg(curBtn + 1)  // 1-based 표시
                         : "---";

        auto* actItem = new QTableWidgetItem(kCtrlActions[row].name);
        actItem->setData(Qt::UserRole, libId);
        actItem->setForeground(QColor("#99ccee"));
        m_winmmTable->setItem(row, 0, actItem);

        auto* btnItem = new QTableWidgetItem(btnStr);
        btnItem->setTextAlignment(Qt::AlignCenter);
        btnItem->setForeground(QColor("#ff9944"));
        btnItem->setFont(QFont("Courier New", 10, QFont::Bold));
        m_winmmTable->setItem(row, 1, btnItem);

        QPushButton* remapBtn = new QPushButton("REMAP");
        remapBtn->setStyleSheet(remapStyle);
        connect(remapBtn, &QPushButton::clicked, this, [this, row] {
            if (!m_winmmTable) return;
            auto* actItm = m_winmmTable->item(row, 0);
            if (!actItm) return;
            int libId2  = actItm->data(Qt::UserRole).toInt();
            QString act = actItm->text();

            QHash<int,int> map2 = m_gamepad->getWinMMMapping();
            // 기존 매핑 제거
            for (auto it = map2.begin(); it != map2.end(); )
                it.value() == libId2 ? it = map2.erase(it) : ++it;

            GamepadCaptureDialog dlg(act, m_gamepad, true, this);
            if (dlg.exec() == QDialog::Accepted && dlg.capturedBtn >= 0) {
                map2.remove(dlg.capturedBtn);  // 충돌 제거
                map2.insert(dlg.capturedBtn, libId2);
                m_gamepad->setWinMMMapping(map2);
                gSettings.winmmMapping = map2;
                gSettings.save();
                refreshWinMMTable();
                log(QString("아케이드 스틱 재설정: %1 → Button %2")
                    .arg(act).arg(dlg.capturedBtn + 1));
            } else {
                refreshWinMMTable();
            }
        });
        m_winmmTable->setCellWidget(row, 2, remapBtn);
    }
    m_winmmTable->blockSignals(false);
}

// ── 페이지 1: DIRECTORIES (경로 설정) ────────────────────────
void MainWindow::buildDirectoriesPage(QWidget* page) {
    // 스크롤 래퍼
    QVBoxLayout* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea{background:#000410;border:none;}");
    auto* inner = new QWidget; inner->setStyleSheet("background:#000410;");
    QVBoxLayout* v = new QVBoxLayout(inner);
    v->setContentsMargins(12, 10, 12, 10); v->setSpacing(8);

    auto makeLabel = [](const QString& t) {
        QLabel* l = new QLabel(t);
        l->setStyleSheet("color:#6688aa;font-family:'Courier New';font-size:10px;");
        return l;
    };
    auto browseBtn = [](QLineEdit* edit, QWidget* par) {
        QPushButton* b = new QPushButton("...");
        b->setFixedWidth(32);
        b->setStyleSheet("QPushButton{background:#001133;color:#6688aa;border:1px solid #334488;"
                         "padding:3px;font-size:10px;}"
                         "QPushButton:hover{background:#002255;color:#aaccff;}");
        QObject::connect(b, &QPushButton::clicked, par, [edit]{
            QString d = QFileDialog::getExistingDirectory(nullptr, "폴더 선택", edit->text());
            if (!d.isEmpty()) edit->setText(d);
        });
        return b;
    };
    auto makePathRow = [&](QFormLayout* fl, const QString& lbl, QLineEdit*& out) {
        out = new QLineEdit; out->setStyleSheet(editStyle());
        QHBoxLayout* h = new QHBoxLayout; h->setSpacing(4);
        h->addWidget(out); h->addWidget(browseBtn(out, page));
        fl->addRow(makeLabel(lbl), h);
    };

    QGroupBox* pathGroup = new QGroupBox("PATHS");
    pathGroup->setStyleSheet("QGroupBox{color:#4488cc;border:1px solid #223366;"
        "border-radius:2px;margin-top:14px;padding:6px;"
        "font-family:'Courier New';font-size:10px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 6px;}");
    QFormLayout* pathForm = new QFormLayout(pathGroup);
    pathForm->setLabelAlignment(Qt::AlignRight); pathForm->setSpacing(6);
    // ROM Path / Preview Path 만 사용자 설정 가능
    // 나머지(saves/screenshots/cheats/records)는 프로그램 위치 기준 자동 설정
    makePathRow(pathForm, "ROM Path",     m_romPathEdit);
    makePathRow(pathForm, "Preview Path", m_previewPathEdit);
    v->addWidget(pathGroup);

    // 자동 경로 안내
    QString base = QCoreApplication::applicationDirPath();
    QLabel* autoNote = new QLabel(
        QString("<span style='color:#446688;font-family:Courier New;font-size:9px;'>"
                "자동 경로 (변경 불가):<br>"
                "  Save      → %1/saves/<br>"
                "  Screenshot→ %1/screenshots/<br>"
                "  Cheat     → %1/cheats/<br>"
                "  Record    → %1/recordings/<br>"
                "  Shader    → %1/shaders/</span>").arg(base));
    autoNote->setTextFormat(Qt::RichText);
    autoNote->setWordWrap(true);
    v->addWidget(autoNote);

    // APPLY 버튼
    QPushButton* applyBtn = new QPushButton("APPLY & SAVE");
    applyBtn->setStyleSheet(btnStyle(true)); applyBtn->setFixedHeight(30);
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applySettings);
    QHBoxLayout* bh = new QHBoxLayout; bh->addStretch(); bh->addWidget(applyBtn);
    v->addLayout(bh);
    v->addStretch();

    scroll->setWidget(inner);
    root->addWidget(scroll);
    refreshSettingsUi();
}

// ── 페이지 2: VIDEO OPTIONS ──────────────────────────────────
void MainWindow::buildVideoPage(QWidget* page) {
    QVBoxLayout* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea{background:#000410;border:none;}");
    auto* inner = new QWidget; inner->setStyleSheet("background:#000410;");
    QVBoxLayout* v = new QVBoxLayout(inner);
    v->setContentsMargins(12, 10, 12, 10); v->setSpacing(8);

    auto makeLabel = [](const QString& t) {
        QLabel* l = new QLabel(t);
        l->setStyleSheet("color:#6688aa;font-family:'Courier New';font-size:10px;");
        return l;
    };
    const QString ckStyle = "QCheckBox{color:#aaccff;font-family:'Courier New';font-size:10px;}"
                            "QCheckBox::indicator{width:14px;height:14px;}";
    const QString slStyle = "QSlider::groove:horizontal{background:#001133;height:4px;}"
                            "QSlider::handle:horizontal{background:#4466ff;width:12px;height:12px;margin:-4px 0;border-radius:6px;}"
                            "QSlider::sub-page:horizontal{background:#2244aa;}";

    QGroupBox* vidGroup = new QGroupBox("VIDEO");
    vidGroup->setStyleSheet("QGroupBox{color:#4488cc;border:1px solid #223366;"
        "border-radius:2px;margin-top:14px;padding:6px;"
        "font-family:'Courier New';font-size:10px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 6px;}");
    QFormLayout* vidForm = new QFormLayout(vidGroup);
    vidForm->setLabelAlignment(Qt::AlignRight); vidForm->setSpacing(6);

    m_scaleCombo = new QComboBox; m_scaleCombo->setStyleSheet(editStyle());
    m_scaleCombo->addItems({"Fill", "Fit", "1:1"});
    vidForm->addRow(makeLabel("Scale Mode"), m_scaleCombo);

    m_smoothCheck = new QCheckBox("Smooth Filter"); m_smoothCheck->setStyleSheet(ckStyle);
    vidForm->addRow(makeLabel(""), m_smoothCheck);

    m_crtCheck = new QCheckBox("CRT Scanline Effect"); m_crtCheck->setStyleSheet(ckStyle);
    vidForm->addRow(makeLabel(""), m_crtCheck);

    QHBoxLayout* crtH = new QHBoxLayout;
    m_crtSlider = new QSlider(Qt::Horizontal);
    m_crtSlider->setRange(0, 100); m_crtSlider->setValue(40);
    m_crtSlider->setStyleSheet(slStyle);
    QLabel* crtValLbl = new QLabel("40%");
    crtValLbl->setStyleSheet(labelStyle()); crtValLbl->setFixedWidth(36);
    connect(m_crtSlider, &QSlider::valueChanged, this, [crtValLbl](int v){
        crtValLbl->setText(QString("%1%").arg(v));
    });
    crtH->addWidget(m_crtSlider); crtH->addWidget(crtValLbl);
    vidForm->addRow(makeLabel("CRT Intensity"), crtH);

    m_vsyncCheck = new QCheckBox("VSync"); m_vsyncCheck->setStyleSheet(ckStyle);
    vidForm->addRow(makeLabel(""), m_vsyncCheck);

    // GLSL 셰이더
    QHBoxLayout* shaderH = new QHBoxLayout; shaderH->setSpacing(4);
    QLineEdit* shaderEdit = new QLineEdit;
    shaderEdit->setStyleSheet(editStyle());
    shaderEdit->setPlaceholderText("(기본 CRT 셰이더)");
    shaderEdit->setReadOnly(true);
    if (!gSettings.videoShaderPath.isEmpty()) shaderEdit->setText(gSettings.videoShaderPath);
    QPushButton* shaderLoadBtn = new QPushButton("📂");
    shaderLoadBtn->setFixedWidth(32);
    shaderLoadBtn->setStyleSheet("QPushButton{background:#001133;color:#6688aa;border:1px solid #334488;padding:3px;}"
                                 "QPushButton:hover{background:#002255;color:#aaccff;}");
    QPushButton* shaderClearBtn = new QPushButton("✖");
    shaderClearBtn->setFixedWidth(28); shaderClearBtn->setStyleSheet(shaderLoadBtn->styleSheet());
    connect(shaderLoadBtn, &QPushButton::clicked, this, [this, shaderEdit]{
        QString p = QFileDialog::getOpenFileName(this, "GLSL 셰이더 선택",
            gSettings.videoShaderPath.isEmpty() ? QCoreApplication::applicationDirPath() : gSettings.videoShaderPath,
            "GLSL Shader (*.glsl *.vert *.frag);;All Files (*)");
        if (!p.isEmpty()) {
            shaderEdit->setText(p);
            gSettings.videoShaderPath = p;
            bool ok = m_canvas ? m_canvas->setShaderPath(p) : true;
            if (ok) {
                log("✔ 셰이더 로드: " + QFileInfo(p).fileName());
            } else {
                // 컴파일/링크 실패 — 에러를 눈에 띄게 표시
                shaderEdit->setText("(로드 실패 — 로그 확인)");
                gSettings.videoShaderPath.clear();
                QMessageBox::warning(this, "셰이더 오류",
                    "셰이더 컴파일/링크에 실패했습니다.\n\n파일: " + QFileInfo(p).fileName() +
                    "\n\n아래 로그 패널에서 오류 내용을 확인하세요.");
            }
            gSettings.save();
        }
    });
    connect(shaderClearBtn, &QPushButton::clicked, this, [this, shaderEdit]{
        shaderEdit->clear(); gSettings.videoShaderPath.clear();
        if (m_canvas) m_canvas->setShaderPath({}); gSettings.save();
        log("셰이더 해제");
    });
    shaderH->addWidget(shaderEdit, 1); shaderH->addWidget(shaderLoadBtn); shaderH->addWidget(shaderClearBtn);
    vidForm->addRow(makeLabel("GLSL Shader"), shaderH);

    m_frameskipSpin = new QSpinBox; m_frameskipSpin->setStyleSheet(editStyle());
    m_frameskipSpin->setRange(-1, 5); m_frameskipSpin->setSpecialValueText("AUTO(-1)");
    vidForm->addRow(makeLabel("Frameskip"), m_frameskipSpin);
    v->addWidget(vidGroup);

    QPushButton* applyBtn = new QPushButton("✔  APPLY & SAVE");
    applyBtn->setStyleSheet(btnStyle(true)); applyBtn->setFixedHeight(30);
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applySettings);
    QHBoxLayout* bh = new QHBoxLayout; bh->addStretch(); bh->addWidget(applyBtn);
    v->addLayout(bh); v->addStretch();

    scroll->setWidget(inner); root->addWidget(scroll);
}

// ── 페이지 3: AUDIO OPTIONS ──────────────────────────────────
void MainWindow::buildAudioPage(QWidget* page) {
    QVBoxLayout* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea{background:#000410;border:none;}");
    auto* inner = new QWidget; inner->setStyleSheet("background:#000410;");
    QVBoxLayout* v = new QVBoxLayout(inner);
    v->setContentsMargins(12, 10, 12, 10); v->setSpacing(8);

    const QString grpStyle = "QGroupBox{color:#4488cc;border:1px solid #223366;"
        "border-radius:2px;margin-top:14px;padding:6px;"
        "font-family:'Courier New';font-size:10px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 6px;}";
    auto makeLabel = [](const QString& t) {
        QLabel* l = new QLabel(t);
        l->setStyleSheet("color:#6688aa;font-family:'Courier New';font-size:10px;");
        return l;
    };
    const QString slStyle = "QSlider::groove:horizontal{background:#001133;height:4px;}"
                            "QSlider::handle:horizontal{background:#4466ff;width:12px;height:12px;margin:-4px 0;border-radius:6px;}"
                            "QSlider::sub-page:horizontal{background:#2244aa;}";

    // AUDIO 그룹
    QGroupBox* audGroup = new QGroupBox("AUDIO"); audGroup->setStyleSheet(grpStyle);
    QFormLayout* audForm = new QFormLayout(audGroup);
    audForm->setLabelAlignment(Qt::AlignRight); audForm->setSpacing(6);

    QHBoxLayout* volH = new QHBoxLayout;
    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100); m_volumeSlider->setValue(100);
    m_volumeSlider->setStyleSheet(slStyle);
    m_volumeLabel = new QLabel("100%");
    m_volumeLabel->setStyleSheet(labelStyle()); m_volumeLabel->setFixedWidth(36);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v){
        m_volumeLabel->setText(QString("%1%").arg(v));
        if (m_audio) m_audio->setVolume(v / 100.0);
    });
    volH->addWidget(m_volumeSlider); volH->addWidget(m_volumeLabel);
    audForm->addRow(makeLabel("Volume"), volH);

    m_sampleRateCombo = new QComboBox; m_sampleRateCombo->setStyleSheet(editStyle());
    m_sampleRateCombo->addItems({"22050", "44100", "48000"});
    m_sampleRateCombo->setCurrentText("48000");
    audForm->addRow(makeLabel("Sample Rate"), m_sampleRateCombo);

    m_bufferMsSpin = new QSpinBox; m_bufferMsSpin->setStyleSheet(editStyle());
    m_bufferMsSpin->setRange(16, 512); m_bufferMsSpin->setSingleStep(16);
    m_bufferMsSpin->setSuffix(" ms");
    audForm->addRow(makeLabel("Buffer"), m_bufferMsSpin);
    v->addWidget(audGroup);

    // APPLY / RESET 버튼
    QHBoxLayout* btnH = new QHBoxLayout;
    QPushButton* resetBtn = new QPushButton("↺  RESET DEFAULT"); resetBtn->setStyleSheet(btnStyle(false));
    QPushButton* applyBtn = new QPushButton("✔  APPLY & SAVE");  applyBtn->setStyleSheet(btnStyle(true));
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applySettings);
    connect(resetBtn, &QPushButton::clicked, this, [this]{
        QString base = QCoreApplication::applicationDirPath();
        gSettings.romPath = base+"/roms"; gSettings.previewPath = base+"/previews";
        gSettings.screenshotPath = base+"/screenshots"; gSettings.savePath = base+"/saves";
        gSettings.audioVolume = 100; gSettings.audioSampleRate = 48000;
        gSettings.audioBufferMs = 80;  gSettings.videoScaleMode = "Fit";
        gSettings.videoSmooth = false; gSettings.videoCrtMode = false;
        gSettings.videoCrtIntensity = 0.4; gSettings.videoVsync = true;
        gSettings.videoFrameskip = 0; gSettings.region = "USA";
        gSettings.netplayPort = 7845;
        refreshSettingsUi(); log("설정이 기본값으로 초기화됨");
    });
    btnH->addStretch(); btnH->addWidget(resetBtn); btnH->addWidget(applyBtn);
    v->addLayout(btnH); v->addStretch();

    scroll->setWidget(inner); root->addWidget(scroll);
    refreshSettingsUi();
}

// ── 페이지 4: MACHINE SETTINGS (DIP 스위치) ──────────────────
void MainWindow::buildMachinePage(QWidget* page) {
    QVBoxLayout* vRoot = new QVBoxLayout(page);
    vRoot->setContentsMargins(0, 0, 0, 0);
    vRoot->setSpacing(0);

    // ── Region 선택 (상단 고정) ──────────────────────────────
    auto* regionBar = new QWidget;
    regionBar->setStyleSheet("background:#000820;border-bottom:1px solid #223366;");
    regionBar->setFixedHeight(38);
    auto* rbH = new QHBoxLayout(regionBar);
    rbH->setContentsMargins(14, 4, 14, 4); rbH->setSpacing(10);
    auto* rbLabel = new QLabel("Region");
    rbLabel->setStyleSheet("color:#6688aa;font-family:'Courier New';font-size:10px;");
    m_regionCombo = new QComboBox; m_regionCombo->setStyleSheet(editStyle());
    m_regionCombo->addItems({"USA", "JPN", "EUR", "ASIA"});
    auto* rbApply = new QPushButton("APPLY"); rbApply->setStyleSheet(btnStyle(true));
    rbApply->setFixedWidth(70);
    connect(rbApply, &QPushButton::clicked, this, [this]{
        applySettings();
        log("Region: " + gSettings.region);
    });
    rbH->addWidget(rbLabel);
    rbH->addWidget(m_regionCombo, 1);
    rbH->addWidget(rbApply);
    vRoot->addWidget(regionBar);

    m_machineScroll = new QScrollArea;
    m_machineScroll->setWidgetResizable(true);
    m_machineScroll->setStyleSheet(
        "QScrollArea{background:#000410;border:none;}"
        "QScrollBar:vertical{background:#000022;width:8px;border:none;}"
        "QScrollBar::handle:vertical{background:#224466;min-height:20px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");

    m_machineContent = new QWidget;
    m_machineContent->setStyleSheet("background:#000410;");
    auto* ph = new QVBoxLayout(m_machineContent);
    ph->setContentsMargins(16, 12, 16, 12);
    auto* lbl = new QLabel("게임을 실행하면 DIP 스위치가 표시됩니다");
    lbl->setStyleSheet("color:#446688;font-family:'Courier New';font-size:10px;");
    ph->addWidget(lbl); ph->addStretch();

    m_machineScroll->setWidget(m_machineContent);
    vRoot->addWidget(m_machineScroll);
}

// ── 페이지 5: SHOTS FACTORY ──────────────────────────────────
void MainWindow::buildShotsPage(QWidget* page) {
    QVBoxLayout* v = new QVBoxLayout(page);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(12);

    QLabel* title = new QLabel("SHOTS FACTORY");
    title->setStyleSheet("color:#aaccff;font-family:'Courier New';"
                         "font-size:13px;font-weight:bold;letter-spacing:2px;");
    v->addWidget(title);

    auto* line = new QFrame; line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color:#223366;"); v->addWidget(line);

    // 스크린샷
    QGroupBox* shotGroup = new QGroupBox("SCREENSHOT");
    shotGroup->setStyleSheet("QGroupBox{color:#4488cc;border:1px solid #223366;"
        "border-radius:2px;margin-top:14px;padding:6px;"
        "font-family:'Courier New';font-size:10px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 6px;}");
    QVBoxLayout* sgV = new QVBoxLayout(shotGroup); sgV->setSpacing(6);
    QLabel* shotHint = new QLabel("게임 중 F8 또는 아래 버튼으로 스크린샷 촬영");
    shotHint->setStyleSheet("color:#446688;font-family:'Courier New';font-size:9px;");
    shotHint->setWordWrap(true);
    sgV->addWidget(shotHint);
    QPushButton* shotBtn = new QPushButton("📷  TAKE SCREENSHOT");
    shotBtn->setStyleSheet(btnStyle(true)); shotBtn->setFixedHeight(34);
    connect(shotBtn, &QPushButton::clicked, this, &MainWindow::takeScreenshot);
    sgV->addWidget(shotBtn);
    v->addWidget(shotGroup);

    // 녹화
    QGroupBox* recGroup = new QGroupBox("VIDEO RECORD");
    recGroup->setStyleSheet(shotGroup->styleSheet());
    QVBoxLayout* rgV = new QVBoxLayout(recGroup); rgV->setSpacing(6);
    QLabel* recHint = new QLabel("게임 중 F9 또는 아래 버튼으로 MP4 녹화 시작/정지\n"
                                 "파일: Record Path/{romname}_{timestamp}.mp4");
    recHint->setStyleSheet("color:#446688;font-family:'Courier New';font-size:9px;");
    recHint->setWordWrap(true);
    rgV->addWidget(recHint);
    QPushButton* recBtn = new QPushButton("⏺  START / STOP RECORDING");
    recBtn->setStyleSheet(btnStyle(false)); recBtn->setFixedHeight(34);
    connect(recBtn, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    rgV->addWidget(recBtn);
    v->addWidget(recGroup);

    v->addStretch();
}

// ── 페이지 6: CHEATS ─────────────────────────────────────────
void MainWindow::buildCheatsPage(QWidget* page) {
    QVBoxLayout* vRoot = new QVBoxLayout(page);
    vRoot->setContentsMargins(12, 10, 12, 10);
    vRoot->setSpacing(8);

    // ── 상단 버튼 행 ────────────────────────────────────────────
    QHBoxLayout* topH = new QHBoxLayout; topH->setSpacing(6);
    QPushButton* loadBtn = new QPushButton("📂  INI 로드");
    loadBtn->setStyleSheet(btnStyle(true));
    connect(loadBtn, &QPushButton::clicked, this, [this]{
        QString path = QFileDialog::getOpenFileName(
            this, "치트 INI 선택", gSettings.cheatPath, "Cheat INI (*.ini);;All Files (*)");
        if (!path.isEmpty() && m_cheat) { m_cheat->loadIni(path); refreshCheatList(); }
    });
    QPushButton* clearBtn = new QPushButton("✖  전체 해제");
    clearBtn->setStyleSheet(btnStyle(false));
    connect(clearBtn, &QPushButton::clicked, this, [this]{
        if (m_cheat) { m_cheat->clearAll(); refreshCheatList(); }
    });
    QPushButton* applyAllBtn = new QPushButton("✔  전체 활성화");
    applyAllBtn->setStyleSheet(btnStyle(false));
    connect(applyAllBtn, &QPushButton::clicked, this, [this]{
        if (!m_cheat) return;
        for (int i = 0; i < m_cheat->count(); ++i) m_cheat->setActive(i, true);
        refreshCheatList();
        log(QString("치트 %1개 전체 활성화").arg(m_cheat->count()));
    });
    topH->addWidget(loadBtn); topH->addWidget(clearBtn); topH->addWidget(applyAllBtn); topH->addStretch();
    vRoot->addLayout(topH);

    // ── 상태 레이블 ─────────────────────────────────────────────
    m_cheatStatusLabel = new QLabel("게임을 실행하면 치트가 자동 로드됩니다");
    m_cheatStatusLabel->setStyleSheet("color:#446688;font-family:'Courier New';font-size:10px;");
    vRoot->addWidget(m_cheatStatusLabel);

    // ── 스크롤 영역 + 행 컨테이너 ─────────────────────────────
    m_cheatScroll = new QScrollArea;
    m_cheatScroll->setWidgetResizable(true);
    m_cheatScroll->setStyleSheet(
        "QScrollArea{background:rgba(0,0,8,210);border:1px solid #223366;}"
        "QScrollBar:vertical{background:#000022;width:8px;border:none;}"
        "QScrollBar::handle:vertical{background:#224466;border-radius:4px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");

    m_cheatRows = new QWidget;
    m_cheatRows->setStyleSheet("background:transparent;");
    QVBoxLayout* rowsLayout = new QVBoxLayout(m_cheatRows);
    rowsLayout->setContentsMargins(4, 4, 4, 4);
    rowsLayout->setSpacing(3);
    rowsLayout->addStretch();   // placeholder, refreshCheatList() 에서 채움

    m_cheatScroll->setWidget(m_cheatRows);
    vRoot->addWidget(m_cheatScroll, 1);

    // ── 하단 힌트 ───────────────────────────────────────────────
    QLabel* hint = new QLabel("게임 선택 시 cheats/{rom}.ini 자동 로드 | 포맷: N \"Label\", 0, ADDR, VAL");
    hint->setStyleSheet("color:#335566;font-family:'Courier New';font-size:9px;");
    hint->setWordWrap(true);
    vRoot->addWidget(hint);
}

// ── 페이지 7: NETPLAY (MULTIPLAYER) ─────────────────────────
void MainWindow::buildNetplayPage(QWidget* page) {
    QVBoxLayout* vRoot = new QVBoxLayout(page);
    vRoot->setContentsMargins(20, 14, 20, 14);
    vRoot->setSpacing(10);

    auto makeLabel = [](const QString& t, bool bold = false) {
        QLabel* l = new QLabel(t);
        l->setStyleSheet(QString("color:%1;font-family:'Courier New';font-size:%2px;")
                         .arg(bold ? "#aaccff" : "#6688aa").arg(bold ? 12 : 10));
        return l;
    };
    auto makeLine = [&]{
        QFrame* f = new QFrame; f->setFrameShape(QFrame::HLine);
        f->setStyleSheet("color:#223366;"); vRoot->addWidget(f);
    };

    // 로컬 IP
    QHBoxLayout* ipH = new QHBoxLayout;
    ipH->addWidget(makeLabel("LOCAL IP :"));
    m_npLocalIpLabel = new QLabel(gNetplay().localIp());
    m_npLocalIpLabel->setStyleSheet(
        "color:#44ffaa;font-family:'Courier New';font-size:12px;font-weight:bold;");
    m_npLocalIpLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ipH->addWidget(m_npLocalIpLabel); ipH->addStretch();
    vRoot->addLayout(ipH);

    m_npStatusLabel = new QLabel("● DISCONNECTED");
    m_npStatusLabel->setStyleSheet("color:#cc4444;font-family:'Courier New';font-size:11px;");
    m_npStatusLabel->setAlignment(Qt::AlignCenter);
    vRoot->addWidget(m_npStatusLabel);
    makeLine();

    // HOST
    vRoot->addWidget(makeLabel("— HOST —", true));
    QHBoxLayout* hostH = new QHBoxLayout; hostH->setSpacing(8);
    hostH->addWidget(makeLabel("Port:"));
    m_npPortSpin = new QSpinBox; m_npPortSpin->setStyleSheet(editStyle());
    m_npPortSpin->setRange(1024, 65535); m_npPortSpin->setValue(gSettings.netplayPort);
    m_npPortSpin->setFixedWidth(90); hostH->addWidget(m_npPortSpin);
    m_npHostBtn = new QPushButton("📡  HOST GAME"); m_npHostBtn->setStyleSheet(btnStyle(true));
    connect(m_npHostBtn, &QPushButton::clicked, this, [this]{
        gNetplay().hostListen(m_npPortSpin->value());
        log(QString("포트 %1 대기 중...").arg(m_npPortSpin->value()));
    });
    hostH->addWidget(m_npHostBtn); hostH->addStretch();
    vRoot->addLayout(hostH);
    makeLine();

    // JOIN
    vRoot->addWidget(makeLabel("— JOIN —", true));
    QHBoxLayout* joinH = new QHBoxLayout; joinH->setSpacing(8);
    joinH->addWidget(makeLabel("IP:"));
    m_npIpEdit = new QLineEdit("127.0.0.1"); m_npIpEdit->setStyleSheet(editStyle());
    m_npIpEdit->setFixedWidth(160); joinH->addWidget(m_npIpEdit);
    joinH->addWidget(makeLabel("Port:"));
    QSpinBox* joinPort = new QSpinBox; joinPort->setStyleSheet(editStyle());
    joinPort->setRange(1024, 65535); joinPort->setValue(gSettings.netplayPort);
    joinPort->setFixedWidth(90); joinH->addWidget(joinPort);
    m_npConnectBtn = new QPushButton("🔌  CONNECT"); m_npConnectBtn->setStyleSheet(btnStyle(true));
    connect(m_npConnectBtn, &QPushButton::clicked, this, [this, joinPort]{
        gNetplay().clientConnect(m_npIpEdit->text(), joinPort->value());
        log(QString("%1:%2 연결 중...").arg(m_npIpEdit->text()).arg(joinPort->value()));
    });
    joinH->addWidget(m_npConnectBtn); joinH->addStretch();
    vRoot->addLayout(joinH);
    makeLine();

    QHBoxLayout* ctrlH = new QHBoxLayout; ctrlH->setSpacing(8);
    m_npStartBtn = new QPushButton("▶  START GAME (HOST)");
    m_npStartBtn->setStyleSheet(btnStyle(true)); m_npStartBtn->setEnabled(false);
    connect(m_npStartBtn, &QPushButton::clicked, this, &MainWindow::netplayStartGame);
    ctrlH->addWidget(m_npStartBtn);
    m_npDisconnBtn = new QPushButton("✖  DISCONNECT");
    m_npDisconnBtn->setStyleSheet(btnStyle(false)); m_npDisconnBtn->setEnabled(false);
    connect(m_npDisconnBtn, &QPushButton::clicked, this, [this]{
        cleanupNetplay();       // 게임 중이면 정리
        gNetplay().shutdown();  // 소켓까지 완전 종료
        log("연결 해제됨");
    });
    ctrlH->addWidget(m_npDisconnBtn); ctrlH->addStretch();
    vRoot->addLayout(ctrlH);

    vRoot->addStretch();
    QLabel* hint = new QLabel("1. HOST: 포트 설정 후 HOST GAME → 게임 선택 → START GAME\n"
                              "2. JOIN: IP/포트 입력 후 CONNECT\n"
                              "최대 롤백 8프레임 / 예측 기반 0ms 레이턴시");
    hint->setStyleSheet("color:#335566;font-family:'Courier New';font-size:9px;");
    hint->setWordWrap(true);
    vRoot->addWidget(hint);
}

// ── DIP 스위치 재빌드 (게임 로드 후 300ms 후 호출) ────────────
void MainWindow::rebuildMachineSettings() {
    if (!m_machineContent) return;

    // 기존 레이아웃/위젯 모두 제거
    if (QLayout* old = m_machineContent->layout()) {
        QLayoutItem* item;
        while ((item = old->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        delete old;
    }

    QVBoxLayout* v = new QVBoxLayout(m_machineContent);
    v->setContentsMargins(16, 12, 16, 12);
    v->setSpacing(10);

    auto makeLabel = [](const QString& t, bool bold = false) {
        QLabel* l = new QLabel(t);
        l->setStyleSheet(QString("color:%1;font-family:'Courier New';font-size:%2px;")
                         .arg(bold ? "#aaccff" : "#6688aa").arg(bold ? 11 : 10));
        l->setWordWrap(true);
        return l;
    };

    if (gState.variableOptions.isEmpty()) {
        v->addWidget(makeLabel("게임을 실행하면 DIP 스위치가 표시됩니다"));
        v->addStretch();
        return;
    }

    v->addWidget(makeLabel("— DIP SWITCHES —", true));

    // 안내
    auto* hint = new QLabel("변경 사항은 즉시 적용됩니다. 일부 설정은 리셋 후 반영됩니다.");
    hint->setStyleSheet("color:#334455;font-family:'Courier New';font-size:9px;");
    hint->setWordWrap(true);
    v->addWidget(hint);

    // 구분선
    auto* line = new QFrame; line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color:#223366;"); v->addWidget(line);

    // 변수별 QFormLayout
    QFormLayout* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setSpacing(8);
    form->setContentsMargins(0, 4, 0, 4);

    QList<QString> keys = gState.variableOptions.keys();
    std::sort(keys.begin(), keys.end());

    for (const QString& key : keys) {
        const QStringList& options = gState.variableOptions.value(key);
        if (options.isEmpty()) continue;

        QString desc = gState.variableDescriptions.value(key, key);
        QString cur  = gState.variables.value(key, options.first());

        QLabel* lbl = makeLabel(desc);
        lbl->setFixedWidth(260);
        lbl->setToolTip(key);  // 내부 키 이름은 툴팁으로

        QComboBox* combo = new QComboBox;
        combo->setStyleSheet(editStyle());
        combo->setMinimumWidth(160);
        for (const QString& opt : options) combo->addItem(opt);
        int idx = options.indexOf(cur);
        if (idx >= 0) combo->setCurrentIndex(idx);

        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [key, options](int i) {
            if (i >= 0 && i < options.size()) {
                gState.variables[key] = options[i];
                gState.variablesUpdated.store(true);
            }
        });

        form->addRow(lbl, combo);
    }

    v->addLayout(form);
    v->addStretch();

    log(QString("🖥 DIP 스위치 %1개 로드됨").arg(keys.size()));
}

void MainWindow::refreshCheatList() {
    if (!m_cheatRows) return;

    // ── 기존 행 모두 제거 ───────────────────────────────────────
    QLayout* layout = m_cheatRows->layout();
    QLayoutItem* child;
    while ((child = layout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    if (!m_cheat || m_cheat->count() == 0) {
        if (m_cheatStatusLabel) {
            // 게임이 실행 중이 아니면 → 실행 후 확인 안내
            if (!gState.gameLoaded && m_loadedGame.isEmpty())
                m_cheatStatusLabel->setText("게임을 실행하면 치트가 자동 로드됩니다");
            else
                m_cheatStatusLabel->setText("치트 없음 (" + m_selectedGame + ".ini)");
        }
        // 빈 stretch 복원
        qobject_cast<QVBoxLayout*>(layout)->addStretch();
        return;
    }

    if (m_cheatStatusLabel)
        m_cheatStatusLabel->setText(
            QString("✔ %1개 치트 로드됨 — %2")
            .arg(m_cheat->count())
            .arg(QFileInfo(m_cheat->loadedPath()).fileName()));

    // ── 치트별 행 생성 ──────────────────────────────────────────
    QVBoxLayout* vl = qobject_cast<QVBoxLayout*>(layout);
    for (int i = 0; i < m_cheat->count(); ++i) {
        const CheatEntry& e = m_cheat->entries().at(i);

        // 행 컨테이너
        QWidget* row = new QWidget;
        row->setStyleSheet(
            e.active
            ? "background:rgba(0,30,10,200);border:1px solid #226644;border-radius:4px;"
            : "background:rgba(0,0,8,180);border:1px solid #1a2a3a;border-radius:4px;");
        QHBoxLayout* hl = new QHBoxLayout(row);
        hl->setContentsMargins(8, 5, 8, 5);
        hl->setSpacing(8);

        // 활성 상태 표시 (● / ○)
        QLabel* dot = new QLabel(e.active ? "●" : "○");
        dot->setFixedWidth(14);
        dot->setStyleSheet(
            e.active
            ? "color:#44ff88;font-size:12px;background:transparent;border:none;"
            : "color:#334455;font-size:12px;background:transparent;border:none;");
        dot->setAlignment(Qt::AlignCenter);
        hl->addWidget(dot);

        // 텍스트 영역: description(메인) + label(서브, "Enabled"이 아닌 경우만)
        QWidget* textCol = new QWidget;
        textCol->setStyleSheet("background:transparent;border:none;");
        QVBoxLayout* textVl = new QVBoxLayout(textCol);
        textVl->setContentsMargins(0, 0, 0, 0);
        textVl->setSpacing(1);

        // 메인 텍스트: cheat "..." 그룹 이름 (description)
        QString mainText = e.description.isEmpty() ? e.label : e.description;
        QLabel* descLbl = new QLabel(mainText);
        descLbl->setStyleSheet(
            e.active
            ? "color:#aaffcc;font-family:'Courier New';font-size:11px;font-weight:bold;"
              "background:transparent;border:none;"
            : "color:#8899aa;font-family:'Courier New';font-size:11px;font-weight:bold;"
              "background:transparent;border:none;");
        descLbl->setWordWrap(true);
        textVl->addWidget(descLbl);

        // 서브 텍스트: 옵션 레이블 ("Enabled" 제외, 의미 있는 옵션명만)
        bool showLabel = !e.label.isEmpty()
                      && !e.description.isEmpty()
                      && !e.label.contains("enabled", Qt::CaseInsensitive);
        if (showLabel) {
            QLabel* optLbl = new QLabel("▸ " + e.label);
            optLbl->setStyleSheet(
                e.active
                ? "color:#55cc88;font-family:'Courier New';font-size:9px;"
                  "background:transparent;border:none;"
                : "color:#445566;font-family:'Courier New';font-size:9px;"
                  "background:transparent;border:none;");
            optLbl->setWordWrap(true);
            textVl->addWidget(optLbl);
        }

        textCol->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        hl->addWidget(textCol, 1);

        // 패치 수 표시
        QLabel* patchCount = new QLabel(
            QString("[%1p]").arg(e.patches.size()));
        patchCount->setStyleSheet(
            "color:#334455;font-family:'Courier New';font-size:9px;background:transparent;border:none;");
        patchCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hl->addWidget(patchCount);

        // [ON] 버튼
        QPushButton* onBtn = new QPushButton("ON");
        onBtn->setFixedSize(42, 22);
        onBtn->setEnabled(!e.active);
        onBtn->setStyleSheet(
            e.active
            ? "QPushButton{background:#113322;color:#336644;border:1px solid #224433;"
              "font-family:'Courier New';font-size:10px;border-radius:3px;}"
            : "QPushButton{background:#114422;color:#44cc88;border:1px solid #33aa66;"
              "font-family:'Courier New';font-size:10px;border-radius:3px;}"
              "QPushButton:hover{background:#1a6633;}");
        // log에 description 우선 표시
        QString cheatName = e.description.isEmpty() ? e.label : e.description;
        if (showLabel) cheatName += " ▸ " + e.label;
        connect(onBtn, &QPushButton::clicked, this, [this, i, cheatName]{
            if (m_cheat && i < m_cheat->count()) {
                m_cheat->setActive(i, true);
                log("치트 ON: " + cheatName);
                refreshCheatList();
            }
        });
        hl->addWidget(onBtn);

        // [OFF] 버튼
        QPushButton* offBtn = new QPushButton("OFF");
        offBtn->setFixedSize(42, 22);
        offBtn->setEnabled(e.active);
        offBtn->setStyleSheet(
            !e.active
            ? "QPushButton{background:#221122;color:#443355;border:1px solid #332244;"
              "font-family:'Courier New';font-size:10px;border-radius:3px;}"
            : "QPushButton{background:#331122;color:#cc4466;border:1px solid #aa3355;"
              "font-family:'Courier New';font-size:10px;border-radius:3px;}"
              "QPushButton:hover{background:#551122;}");
        connect(offBtn, &QPushButton::clicked, this, [this, i, cheatName]{
            if (m_cheat && i < m_cheat->count()) {
                m_cheat->setActive(i, false);
                log("치트 OFF: " + cheatName);
                refreshCheatList();
            }
        });
        hl->addWidget(offBtn);

        vl->addWidget(row);
    }
    vl->addStretch();
}

// ════════════════════════════════════════════════════════════
//  ROM 관리
// ════════════════════════════════════════════════════════════
void MainWindow::scanRoms() {
    if (!m_gameList) return;
    m_gameList->clear();
    m_allRoms.clear();

    QDir dir(gSettings.romPath);
    if (!dir.exists()) { log("⚠ ROM 폴더 없음: " + gSettings.romPath); return; }

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

    filterRoms(m_searchEdit ? m_searchEdit->text() : QString());
    log(QString("ROM %1개 검색됨 (%2)").arg(m_allRoms.size()).arg(gSettings.romPath));
}

void MainWindow::filterRoms(const QString& text) {
    if (!m_gameList) return;
    m_gameList->clear();
    QString filter = text.trimmed().toLower();
    int shown = 0;
    for (const auto& [disp, rom] : m_allRoms) {
        bool fav = isFavorite(rom);

        // ── 탭 필터 ──────────────────────────────────────
        if (m_glFilter == 1 && !fav) continue;  // ★ FAV: 즐겨찾기만
        if (m_glFilter == 2 &&  fav) continue;  // ☆: 미즐겨찾기만

        // ── 검색어 필터 ───────────────────────────────────
        if (!filter.isEmpty()
            && !disp.toLower().contains(filter)
            && !rom.toLower().contains(filter)) continue;

        bool running = (rom == m_selectedGame);

        QString label = (fav ? "★ " : "  ") + disp;
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, rom);
        item->setData(Qt::UserRole + 1, fav);

        if (running) {
            item->setForeground(QColor("#44ffaa"));
            item->setFont(QFont("Courier New", 12, QFont::Bold));
        } else if (fav) {
            item->setForeground(QColor("#ffdd44"));
        }
        m_gameList->addItem(item);
        ++shown;
    }
    // 로그: FAV/☆ 필터 적용 시 결과 확인용
    if (m_glFilter != 0)
        log(QString("[필터=%1] %2개 표시").arg(m_glFilter).arg(shown));
}

void MainWindow::selectGame(const QString& romName) {
    if (m_selectedGame == romName) return;
    m_selectedGame = romName;

    // 게임명 DB 표시명
    QString displayName = getGameDisplayName(romName);
    log(QString("선택: %1 (%2)").arg(displayName).arg(romName));

    loadPreview(romName);
}

void MainWindow::loadPreview(const QString& romName) {
    // 이전 영상 정지 + 이미지 모드로 복귀
    if (m_mediaPlayer)     m_mediaPlayer->stop();
    if (m_previewVidTimer) m_previewVidTimer->stop();
    if (m_previewStack)    m_previewStack->setCurrentIndex(0);

    if (!m_previewLabel) return;
    for (const QString ext : {"png","jpg","jpeg","bmp","gif"}) {
        QString path = gSettings.previewPath + "/" + romName + "." + ext;
        if (QFile::exists(path)) {
            QPixmap px(path);
            if (!px.isNull()) {
                m_previewLabel->setPixmap(
                    px.scaled(m_previewLabel->size(),
                              Qt::KeepAspectRatio, Qt::SmoothTransformation));
                // 3초 후 영상 자동재생 시작
                if (m_previewVidTimer) m_previewVidTimer->start();
                return;
            }
        }
    }
    m_previewLabel->setPixmap(QPixmap());
    m_previewLabel->setText("NO PREVIEW\n" + romName.toUpper());
    m_previewLabel->setStyleSheet(
        "color:#335577;background:#000008;font-family:'Courier New';font-size:9px;");
    // 이미지는 없어도 영상이 있을 수 있음 → 타이머 시작
    if (m_previewVidTimer) m_previewVidTimer->start();
}

void MainWindow::loadPreviewVideo(const QString& romName) {
    if (!m_mediaPlayer || romName.isEmpty()) return;
    for (const QString ext : {"mp4","avi","mkv","webm","mov"}) {
        QString path = gSettings.previewPath + "/" + romName + "." + ext;
        if (QFile::exists(path)) {
            m_mediaPlayer->setSource(QUrl::fromLocalFile(path));
            m_mediaPlayer->setLoops(QMediaPlayer::Infinite);
            m_mediaPlayer->play();
            if (m_previewStack) m_previewStack->setCurrentIndex(1); // 영상 모드
            log("▶ 프리뷰 영상: " + romName);
            return;
        }
    }
    // 영상 없음 → 이미지 모드 유지
}

bool MainWindow::loadRomInternal() {
    if (!m_core || m_selectedGame.isEmpty()) return false;

    // 로딩 커서 적용
    {
        QPixmap loadPx(":/assets/loading.png");
        if (!loadPx.isNull())
            QApplication::setOverrideCursor(QCursor(loadPx, 0, 0));
        else
            QApplication::setOverrideCursor(Qt::WaitCursor);
    }
    QApplication::processEvents();

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
    bool ok = m_core->loadGame(romPath);

    // 치트 로드 (게임 실행 시점에만 — 선택 시 로드하면 목록 이동마다 파일I/O 발생)
    if (ok && m_cheat) {
        m_cheat->autoLoad(m_selectedGame, gSettings.cheatPath);
        if (m_cheat->count() == 0)
            log("치트 없음 (" + m_selectedGame + ".ini)");
        else
            log(QString("치트 %1개 로드 (%2.ini)").arg(m_cheat->count()).arg(m_selectedGame));
    }

    // 로딩 커서 → 커스텀 마우스 커서로 복원
    QApplication::restoreOverrideCursor();
    {
        QPixmap curPx(":/assets/mousepoint.png");
        if (!curPx.isNull())
            QApplication::setOverrideCursor(QCursor(curPx, 0, 0));
    }

    return ok;
}

// ════════════════════════════════════════════════════════════
//  에뮬레이션 시작/일시정지/전체화면
// ════════════════════════════════════════════════════════════
void MainWindow::startEmu() {
    m_npStates.clear();
    m_npInputHistory.clear();
    m_frameDelay = 0.0;
    gState.frameCount    = 0;
    gState.gameLoadFrame = 0;  // 치트 딜레이 기준점
    gState.fastForward   = false;
    gState.isPaused      = false;

    // Canvas 옵션 적용
    if (m_canvas) {
        m_canvas->setScaleMode(gSettings.videoScaleMode);
        m_canvas->setSmooth(gSettings.videoSmooth);
        m_canvas->setCrtMode(gSettings.videoCrtMode, gSettings.videoCrtIntensity);
        if (!gSettings.videoShaderPath.isEmpty())
            m_canvas->setShaderPath(gSettings.videoShaderPath);
    }

    m_frameAccum = 0.0;
    m_aflClock.start();
    m_timer->start(1);

    QTimer::singleShot(100, this, [this]{
        enterGameScreen();
    });
    // 게임 첫 프레임 실행 후 DIP 스위치 탭 재빌드 (코어가 SET_VARIABLES 전달한 뒤)
    QTimer::singleShot(300, this, &MainWindow::rebuildMachineSettings);
    log(QString("▶ 에뮬 시작 (%1 FPS)").arg(gState.coreFps, 0, 'f', 2));
}

void MainWindow::launchGame() {
    if (!m_core || m_selectedGame.isEmpty()) { log("게임을 먼저 선택하세요"); return; }

    // 일시정지 상태 → 같은 게임이면 재개, 다른 게임이면 새로 로드
    if (gState.isPaused && m_loadedGame == m_selectedGame) {
        gState.isPaused = false;
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
        log(QString("🔊 오디오: %1 Hz, %2 ms").arg(sr).arg(gSettings.audioBufferMs));
        m_audio->init(sr, gSettings.audioBufferMs);
        startEmu();
    } else {
        log("✖ ROM 로드 실패: " + m_selectedGame);
    }
}

void MainWindow::togglePause() {
    if (!gState.gameLoaded) return;
    gState.isPaused = !gState.isPaused;
    if (gState.isPaused) {
        m_timer->stop();
        leaveGameScreen();
        log("⏸ 일시정지");
    } else {
        m_frameAccum = 0.0;
        m_aflClock.start();
        m_timer->start(1);
        enterGameScreen();
        log("▶ 재개");
    }
}

void MainWindow::toggleFullscreen() {
    m_isFullscreen = !m_isFullscreen;
    if (m_isFullscreen) {
        m_windowedSize = size();
        showFullScreen();
    } else {
        showNormal();
        resize(m_windowedSize);
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
    if (!gState.gameLoaded || !m_core || gState.isPaused) return;

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
        int cur = gState.frameCount;

        // ── ① No-Wait 프레임 페이싱 ────────────────────────────
        // 프레임 스톨(return) 대신 m_frameDelay 를 ±1ms 조절해 부드럽게 동기화
        // diff > 0: 로컬이 앞섬 → 슬로우다운 / diff < 0: 뒤처짐 → 스피드업
        {
            uint32_t remoteF = gNetplay().remoteMaxFrame();
            if (remoteF > 0) {
                int diff = cur - static_cast<int>(remoteF);
                if      (diff >  2) m_frameDelay = std::min(m_frameDelay + 1.0,  8.0);
                else if (diff < -2) m_frameDelay = std::max(m_frameDelay - 1.0, -5.0);
                else                m_frameDelay *= 0.90;  // 오차 범위: 자연 수렴
            }
        }

        // ── ② 롤백 처리 (입력 예측 불일치 수정) ───────────────────
        // 수신된 원격 입력과 예측값이 다른 가장 오래된 프레임으로 롤백
        int rollbackTo = gNetplay().getRollbackFrame(cur);
        if (rollbackTo >= 0 && rollbackTo < cur
                && m_npStates.contains(rollbackTo)) {
            gState.netplayResim = true;
            m_core->unserialize(m_npStates[rollbackTo].constData(),
                                m_npStates[rollbackTo].size());
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
        }

        // ── ③ 현재 프레임 스냅샷 저장 (run() 직전) ──────────────
        // m_npStates[cur] = "cur 실행 직전" 상태 → 롤백 기준점
        {
            size_t sz = m_core->serializeSize();
            if (sz > 0) {
                QByteArray buf(static_cast<int>(sz), Qt::Uninitialized);
                if (m_core->serialize(buf.data(), sz)) {
                    m_npStates[cur] = buf;

                    // 호스트: SYNC_INTERVAL마다 스냅샷을 클라이언트로 전송
                    // 클라이언트는 stateReceived에서 즉각 롤백+재시뮬 실행
                    if (gNetplay().isHost()
                            && cur >= NetplayManager::SYNC_INTERVAL
                            && (cur % NetplayManager::SYNC_INTERVAL) == 0) {
                        gNetplay().sendState(static_cast<uint32_t>(cur), buf);
                    }
                }
            }
        }

        // ── ④ 입력 수집 · 전송 · 히스토리 저장 ──────────────────
        uint16_t localBits = 0;
        for (int i = 0; i < 16; ++i)
            if (gState.rawKeys[i]) localBits |= (1 << i);

        gNetplay().sendInput(static_cast<uint32_t>(cur), localBits);

        // 입력 히스토리: local + remote 동시 보관 (재시뮬 시 사용)
        NpInputState& his = m_npInputHistory[cur];
        his.local  = localBits;
        uint16_t remoteBits = static_cast<uint16_t>(
            gNetplay().getRemoteInput(static_cast<uint32_t>(cur)));
        his.remote = remoteBits;
        gNetplay().recordPrediction(static_cast<uint32_t>(cur), remoteBits);

        // ── ⑤ 프레임 실행 ──────────────────────────────────────
        npApplyInput(localBits, remoteBits);
        m_core->run();
        gState.frameCount++;

        // ── ⑥ 오래된 버퍼 정리 ─────────────────────────────────
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

        int runs = gState.fastForward ? 3 : 1;
        for (int i = 0; i < runs; ++i) m_core->run();
        gState.frameCount++;
    }

    // ── 치트 매 프레임 적용 ────────────────────────────────
    if (m_cheat) m_cheat->applyFrame(m_core, gState.frameCount, gState.gameLoadFrame);

    // ── 오디오 처리 ────────────────────────────────────────
    if (m_audio && m_audio->isReady())
        m_audio->processDrc(0);

    // ── 녹화: 프레임 + 오디오 전송 ────────────────────────
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (gState.isRecording && m_videoInput && gState.videoWidth > 0) {
        int w = static_cast<int>(gState.videoWidth);
        int h = static_cast<int>(gState.videoHeight);
        QImage img;
        if (gState.pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
            img = QImage(
                reinterpret_cast<const uchar*>(gState.videoBuffer.constData()),
                w, h, static_cast<int>(gState.videoPitch),
                QImage::Format_RGB32).copy();
        } else {
            img = QImage(
                reinterpret_cast<const uchar*>(gState.videoBuffer.constData()),
                w, h, static_cast<int>(gState.videoPitch),
                QImage::Format_RGB16)
                .convertedTo(QImage::Format_RGBX8888);
        }
        if (!img.isNull())
            m_videoInput->sendVideoFrame(QVideoFrame(img));

        // 오디오
        if (m_audioInput && !gState.audioRecBuf.isEmpty()) {
            QAudioFormat afmt;
            afmt.setSampleRate(gSettings.audioSampleRate);
            afmt.setChannelCount(2);
            afmt.setSampleFormat(QAudioFormat::Int16);
            m_audioInput->sendAudioBuffer(QAudioBuffer(gState.audioRecBuf, afmt));
            gState.audioRecBuf.clear();
        }
    }
#endif

    // ── 렌더링 ─────────────────────────────────────────────
    if (m_stack->currentIndex() == 1 && m_canvas)
        m_canvas->update();
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
    scanRoms();  // 목록 재정렬 (즐겨찾기 상위 정렬)
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

void MainWindow::takeScreenshot() {
    if (!gState.gameLoaded || gState.videoWidth == 0) {
        log("스크린샷: 프레임 없음"); return;
    }
    int w = static_cast<int>(gState.videoWidth);
    int h = static_cast<int>(gState.videoHeight);
    QImage img;
    if (gState.pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
        img = QImage(
            reinterpret_cast<const uchar*>(gState.videoBuffer.constData()),
            w, h, static_cast<int>(gState.videoPitch),
            QImage::Format_RGB32).copy();
    } else {
        img = QImage(
            reinterpret_cast<const uchar*>(gState.videoBuffer.constData()),
            w, h, static_cast<int>(gState.videoPitch),
            QImage::Format_RGB16).copy();
        img = img.convertToFormat(QImage::Format_RGB32);
    }
    QDir().mkpath(gSettings.screenshotPath);
    QString ts   = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString path = gSettings.screenshotPath + "/" + m_selectedGame + "_" + ts + ".png";
    if (img.save(path)) log("📷 " + path);
    else                log("📷 스크린샷 저장 실패");
}

// ════════════════════════════════════════════════════════════
//  녹화 (Phase 8)
// ════════════════════════════════════════════════════════════
void MainWindow::toggleRecording() {
    if (gState.isRecording) stopRecording();
    else                    startRecording();
}

void MainWindow::startRecording() {
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    log("녹화는 Qt 6.8 이상에서 지원됩니다");
    return;
#else
    if (!gState.gameLoaded) { log("녹화: 게임 실행 중이 아닙니다"); return; }
    if (gState.isRecording)  return;

    QDir().mkpath(gSettings.recordPath);
    QString ts      = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outPath = gSettings.recordPath + "/" + m_selectedGame + "_" + ts + ".mp4";

    // ── 입력 소스 생성 ─────────────────────────────────────
    m_videoInput = new QVideoFrameInput(this);
    m_audioInput = new QAudioBufferInput(this);

    // ── 캡처 세션 ──────────────────────────────────────────
    m_captureSession = new QMediaCaptureSession(this);
    m_captureSession->setVideoFrameInput(m_videoInput);
    m_captureSession->setAudioBufferInput(m_audioInput);

    // ── 레코더 ─────────────────────────────────────────────
    m_recorder = new QMediaRecorder(this);
    m_captureSession->setRecorder(m_recorder);

    QMediaFormat fmt;
    fmt.setFileFormat(QMediaFormat::MPEG4);
    fmt.setVideoCodec(QMediaFormat::VideoCodec::H264);
    fmt.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    m_recorder->setMediaFormat(fmt);
    m_recorder->setOutputLocation(QUrl::fromLocalFile(outPath));
    m_recorder->setVideoFrameRate(gState.coreFps > 0 ? gState.coreFps : 60.0);

    connect(m_recorder, &QMediaRecorder::errorOccurred, this,
        [this](QMediaRecorder::Error, const QString& msg){
            log("🔴 녹화 오류: " + msg);
            stopRecording();
        });

    m_recorder->record();
    gState.isRecording = true;
    gState.audioRecBuf.clear();

    if (m_canvas) m_canvas->setRecording(true);
    log("🔴 녹화 시작: " + outPath);
#endif
}

void MainWindow::stopRecording() {
    if (!gState.isRecording) return;
    gState.isRecording = false;

    if (m_canvas) m_canvas->setRecording(false);

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (m_recorder) {
        m_recorder->stop();
        m_recorder->deleteLater();
        m_recorder = nullptr;
    }
    if (m_captureSession) {
        m_captureSession->deleteLater();
        m_captureSession = nullptr;
    }
    m_videoInput = nullptr;  // captureSession이 소유 → deleteLater로 정리됨
    m_audioInput = nullptr;
#endif

    log("■ 녹화 완료");
}

// ════════════════════════════════════════════════════════════
//  설정 적용 / 갱신
// ════════════════════════════════════════════════════════════
void MainWindow::applySettings() {
    if (m_romPathEdit)     gSettings.romPath     = m_romPathEdit->text();
    if (m_previewPathEdit) gSettings.previewPath = m_previewPathEdit->text();

    // 나머지 경로는 프로그램 위치 기준 자동 고정
    {
        QString base = QCoreApplication::applicationDirPath();
        gSettings.screenshotPath = base + "/screenshots";
        gSettings.savePath       = base + "/saves";
        gSettings.cheatPath      = base + "/cheats";
        gSettings.recordPath     = base + "/recordings";
        // videoShaderPath는 사용자가 VIDEO 탭에서 선택
    }

    if (m_scaleCombo)         gSettings.videoScaleMode   = m_scaleCombo->currentText();
    if (m_smoothCheck)        gSettings.videoSmooth      = m_smoothCheck->isChecked();
    if (m_crtCheck)           gSettings.videoCrtMode     = m_crtCheck->isChecked();
    if (m_crtSlider)          gSettings.videoCrtIntensity = m_crtSlider->value() / 100.0;
    if (m_vsyncCheck)         gSettings.videoVsync       = m_vsyncCheck->isChecked();
    if (m_frameskipSpin)      gSettings.videoFrameskip   = m_frameskipSpin->value();

    if (m_volumeSlider)       gSettings.audioVolume      = m_volumeSlider->value();
    if (m_sampleRateCombo)    gSettings.audioSampleRate  = m_sampleRateCombo->currentText().toInt();
    if (m_bufferMsSpin)       gSettings.audioBufferMs    = m_bufferMsSpin->value();
    if (m_regionCombo)        gSettings.region           = m_regionCombo->currentText();

    // 터보 설정 저장
    gSettings.turboPeriod = gState.turboPeriod;
    QStringList turboList;
    for (auto it = gState.turboBtns.begin(); it != gState.turboBtns.end(); ++it)
        if (it.value()) turboList.append(QString::number(it.key()));
    gSettings.turboButtons = turboList.join(',');

    // 즉시 적용
    if (m_canvas) {
        m_canvas->setScaleMode(gSettings.videoScaleMode);
        m_canvas->setSmooth(gSettings.videoSmooth);
        m_canvas->setCrtMode(gSettings.videoCrtMode, gSettings.videoCrtIntensity);
    }
    if (m_audio) {
        m_audio->setVolume(gSettings.audioVolume / 100.0);
    }
    if (m_core) {
        m_core->setSaveDir(gSettings.savePath);
        // ROM 경로 변경 시 system dir도 갱신 (BIOS 파일 탐색 경로)
        QString base = QCoreApplication::applicationDirPath();
        m_core->setSystemDir(gSettings.romPath.isEmpty() ? base : gSettings.romPath);
    }

    // 경로 변경 시 ROM 재스캔
    filterRoms(m_searchEdit ? m_searchEdit->text() : QString());
    scanRoms();

    gSettings.save();
    log("⚙ 설정 저장 완료");
}

void MainWindow::refreshSettingsUi() {
    if (m_romPathEdit)        m_romPathEdit->setText(gSettings.romPath);
    if (m_previewPathEdit)    m_previewPathEdit->setText(gSettings.previewPath);
    if (m_screenshotPathEdit) m_screenshotPathEdit->setText(gSettings.screenshotPath);
    if (m_savePathEdit)       m_savePathEdit->setText(gSettings.savePath);
    if (m_recordPathEdit)     m_recordPathEdit->setText(gSettings.recordPath);

    if (m_scaleCombo)         m_scaleCombo->setCurrentText(gSettings.videoScaleMode);
    if (m_smoothCheck)        m_smoothCheck->setChecked(gSettings.videoSmooth);
    if (m_crtCheck)           m_crtCheck->setChecked(gSettings.videoCrtMode);
    if (m_crtSlider)          m_crtSlider->setValue(
                                  static_cast<int>(gSettings.videoCrtIntensity * 100));
    if (m_vsyncCheck)         m_vsyncCheck->setChecked(gSettings.videoVsync);
    if (m_frameskipSpin)      m_frameskipSpin->setValue(gSettings.videoFrameskip);

    if (m_volumeSlider)       m_volumeSlider->setValue(gSettings.audioVolume);
    if (m_sampleRateCombo)    m_sampleRateCombo->setCurrentText(
                                  QString::number(gSettings.audioSampleRate));
    if (m_bufferMsSpin)       m_bufferMsSpin->setValue(gSettings.audioBufferMs);
    if (m_regionCombo)        m_regionCombo->setCurrentText(gSettings.region);
}

// ════════════════════════════════════════════════════════════
//  게임 화면 전환 헬퍼
// ════════════════════════════════════════════════════════════

// 게임 화면 진입: 프리뷰 완전 정지 후 캔버스 표시
void MainWindow::enterGameScreen() {
    // 프리뷰 타이머 + 영상 + 소리 완전 정지
    if (m_previewVidTimer) m_previewVidTimer->stop();
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
        m_mediaPlayer->setSource(QUrl());  // 소스 해제 → 재생 불가 상태
    }
    if (m_previewStack) m_previewStack->setCurrentIndex(0);  // 이미지 모드

    m_stack->setCurrentIndex(1);
    if (m_canvas) m_canvas->setFocus();

    // 게임 화면 진입 시 커서 자동 숨김 타이머 시작
    resetCursorTimer();
}

// GUI 복귀: 현재 선택된 롬의 프리뷰 재시작
void MainWindow::leaveGameScreen() {
    // GUI로 돌아오면 커서 타이머 중지 + 커서 복원
    if (m_cursorTimer) m_cursorTimer->stop();
    if (m_cursorHidden) {
        QApplication::restoreOverrideCursor();
        m_cursorHidden = false;
    }

    m_stack->setCurrentIndex(0);
    filterRoms(m_searchEdit ? m_searchEdit->text() : QString());
    // 선택된 게임이 있으면 프리뷰 재로드
    if (!m_selectedGame.isEmpty())
        loadPreview(m_selectedGame);
}

// ── 마우스 커서 자동 숨김 ─────────────────────────────────────
void MainWindow::resetCursorTimer() {
    // 게임 화면 중에만 유효
    if (!m_stack || m_stack->currentIndex() != 1) return;

    // 숨겨져 있으면 즉시 복원
    if (m_cursorHidden) {
        QApplication::restoreOverrideCursor();
        m_cursorHidden = false;
    }

    // 타이머 재시작 (3초 후 hideCursor 호출)
    if (m_cursorTimer) m_cursorTimer->start();
}

void MainWindow::hideCursor() {
    // 게임 화면 중이고 아직 숨기지 않았을 때만
    if (!m_stack || m_stack->currentIndex() != 1) return;
    if (!m_cursorHidden) {
        QApplication::setOverrideCursor(Qt::BlankCursor);
        m_cursorHidden = true;
    }
}

// ════════════════════════════════════════════════════════════
//  앱 전역 이벤트 필터 — 탭키 GUI↔게임 전환
// ════════════════════════════════════════════════════════════
bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
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
            if (gState.gameLoaded && gState.isPaused) {
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
    if (e->isAutoRepeat()) { QMainWindow::keyPressEvent(e); return; }
    int k = e->key();
    bool shift = (e->modifiers() & Qt::ShiftModifier);

    // Tab: 일시정지 토글
    if (k == Qt::Key_Tab)  { togglePause(); return; }

    // ESC: 게임 종료 → GUI로 복귀
    if (k == Qt::Key_Escape && gState.gameLoaded) {
        m_timer->stop();
        gState.isPaused = false;
        if (m_core) m_core->unloadGame();
        m_loadedGame.clear();
        leaveGameScreen();
        log("■ ESC — 게임 종료");
        return;
    }

    // Alt+Enter: 전체화면
    if ((k == Qt::Key_Return || k == Qt::Key_Enter)
        && (e->modifiers() & Qt::AltModifier)) { toggleFullscreen(); return; }

    // F1~F8: 슬롯 로드 / Shift+F1~F8: 슬롯 저장
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

    // F9: 녹화 토글
    if (k == Qt::Key_F9)  { toggleRecording(); return; }

    // F12: 스크린샷
    if (k == Qt::Key_F12) { takeScreenshot(); return; }

    // F11: 패스트포워드 토글
    if (k == Qt::Key_F11) { toggleFastForward(!gState.fastForward); return; }

    applyKeyPress(k);
    QMainWindow::keyPressEvent(e);
}

void MainWindow::keyReleaseEvent(QKeyEvent* e) {
    if (!e->isAutoRepeat()) applyKeyRelease(e->key());
    QMainWindow::keyReleaseEvent(e);
}

void MainWindow::applyKeyPress(int qtKey) {
    auto it = m_keymap.find(qtKey);
    if (it == m_keymap.end()) return;
    int idx = it.value();
    if (idx >= 0 && idx < 16) {
        gState.rawKeys[idx] = 1;
        gState.kbHeld.insert(idx);
    }
}

void MainWindow::applyKeyRelease(int qtKey) {
    auto it = m_keymap.find(qtKey);
    if (it == m_keymap.end()) return;
    int idx = it.value();
    if (idx >= 0 && idx < 16) {
        gState.rawKeys[idx] = 0;
        gState.kbHeld.remove(idx);
    }
}

QHash<int, int> MainWindow::buildDefaultKeymap() {
    return {
        {Qt::Key_Z,       0},   // B     (JOYPAD_B)
        {Qt::Key_A,       1},   // Y     (JOYPAD_Y)
        {Qt::Key_Return,  3},   // START (JOYPAD_START)
        {Qt::Key_Space,   2},   // SELECT(JOYPAD_SELECT)
        {Qt::Key_Up,      4},   // UP
        {Qt::Key_Down,    5},   // DOWN
        {Qt::Key_Left,    6},   // LEFT
        {Qt::Key_Right,   7},   // RIGHT
        {Qt::Key_X,       8},   // A     (JOYPAD_A)
        {Qt::Key_S,       9},   // X     (JOYPAD_X)
        {Qt::Key_D,      10},   // L     (JOYPAD_L)
        {Qt::Key_C,      11},   // R     (JOYPAD_R)
    };
}

// ════════════════════════════════════════════════════════════
//  넷플레이 슬롯
// ════════════════════════════════════════════════════════════
void MainWindow::onNetConnected(bool isHost) {
    log(QString("🌐 연결됨 — %1").arg(isHost ? "HOST(P1)" : "CLIENT(P2)"));
    if (m_npStatusLabel)
        m_npStatusLabel->setText(
            isHost ? "● HOSTING — 대기 중" : "● CONNECTED — 호스트 대기 중");
    m_npStatusLabel->setStyleSheet(
        "color:#44cc44;font-family:'Courier New';font-size:11px;");
    if (m_npStartBtn)  m_npStartBtn->setEnabled(isHost);
    if (m_npDisconnBtn) m_npDisconnBtn->setEnabled(true);
    if (m_npHostBtn)   m_npHostBtn->setEnabled(false);
    if (m_npConnectBtn) m_npConnectBtn->setEnabled(false);
}

void MainWindow::onNetDisconnected() {
    log("🌐 연결 끊김");
    cleanupNetplay();
    if (m_npStatusLabel) {
        m_npStatusLabel->setText("● DISCONNECTED");
        m_npStatusLabel->setStyleSheet(
            "color:#cc4444;font-family:'Courier New';font-size:11px;");
    }
    if (m_npStartBtn)   m_npStartBtn->setEnabled(false);
    if (m_npDisconnBtn) m_npDisconnBtn->setEnabled(false);
    if (m_npHostBtn)    m_npHostBtn->setEnabled(true);
    if (m_npConnectBtn) m_npConnectBtn->setEnabled(true);
}

void MainWindow::onNetError(const QString& msg) {
    log("🌐 오류: " + msg);
    if (m_npStatusLabel) {
        m_npStatusLabel->setText("● ERROR: " + msg);
        m_npStatusLabel->setStyleSheet(
            "color:#ff4444;font-family:'Courier New';font-size:11px;");
    }
}

// 상태 변화 → UI 업데이트
void MainWindow::onNetStateChanged(NetplayManager::State s) {
    if (!m_npStatusLabel) return;
    const char* style = "color:#44cc44;font-family:'Courier New';font-size:11px;";
    switch (s) {
    case NetplayManager::State::Lobby:
        m_npStatusLabel->setText(gNetplay().isHost()
            ? "● HOSTING — 게임을 선택하세요" : "● CONNECTED — 호스트 대기 중");
        m_npStatusLabel->setStyleSheet(style);
        if (m_npStartBtn) m_npStartBtn->setEnabled(gNetplay().isHost());
        break;
    case NetplayManager::State::Loading:
        m_npStatusLabel->setText("● 로딩 중...");
        m_npStatusLabel->setStyleSheet(style);
        if (m_npStartBtn) m_npStartBtn->setEnabled(false);
        break;
    case NetplayManager::State::Ready:
        m_npStatusLabel->setText("● 준비 완료 — 상대 대기 중...");
        m_npStatusLabel->setStyleSheet(style);
        break;
    case NetplayManager::State::Playing:
        m_npStatusLabel->setText("● 게임 중");
        m_npStatusLabel->setStyleSheet(style);
        break;
    default: break;
    }
}

// 조인: 호스트가 선택한 게임을 자동 로드 → 로딩 완료 시 READY 전송
void MainWindow::onNetLoadGame(const QString& romName) {
    // 플래그 초기화
    m_npSelfLoaded = false;
    m_npPeerReady  = false;

    log("🌐 게임 수신: " + romName);
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
    log("🌐 START — Frame 0 동시 시작");
    // READY 재전송 타이머 중지
    if (m_npReadyRetry) m_npReadyRetry->stop();
    gState.frameCount = 0;
    m_npStates.clear();
    m_npInputHistory.clear();
    m_frameDelay = 0.0;
    startEmu();
}

// 양쪽: 게임 종료 → Lobby 복귀 (소켓 유지)
void MainWindow::onNetGameOver() {
    log("🌐 GAME OVER — Lobby 복귀");
    cleanupNetplay();
}

// ── 호스트: 게임 선택 후 START 버튼 ─────────────────────────
void MainWindow::netplayStartGame() {
    if (m_selectedGame.isEmpty()) { log("게임을 먼저 선택하세요"); return; }
    if (!gNetplay().active() || !gNetplay().isHost()) return;
    if (gNetplay().netState() != NetplayManager::State::Lobby) {
        log("🌐 이미 게임 진행 중"); return;
    }

    // 플래그 초기화
    m_npSelfLoaded = false;
    m_npPeerReady  = false;

    log("🌐 게임 선택 동기화 → " + m_selectedGame);

    // 조인에게 게임 이름 전송 (조인은 자동 로드 → sendReady)
    gNetplay().sendLoadGame(m_selectedGame);

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
    m_frameDelay = 0.0;
    m_npSelfLoaded = false;
    m_npPeerReady  = false;
    if (m_npReadyRetry) m_npReadyRetry->stop();
    gNetplay().resetGameState();
    gNetplay().cleanupGame();

    // 게임 화면 → GUI 복귀
    if (m_stack && m_stack->currentIndex() == 1)
        leaveGameScreen();

    log("🌐 게임 정리 완료 — Lobby 대기 중");
}

// ════════════════════════════════════════════════════════════
//  로그
// ════════════════════════════════════════════════════════════
void MainWindow::log(const QString& msg) {
    qDebug() << msg;
    if (!m_logEdit) return;

    m_logEdit->append(msg);

    // 최대 150블록(줄) 유지 — 초과 시 맨 앞 50줄 삭제
    QTextDocument* doc = m_logEdit->document();
    if (doc->blockCount() > 150) {
        QTextCursor c(doc);
        c.movePosition(QTextCursor::Start);
        c.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, 50);
        c.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    }

    // 스크롤 자동 아래로
    QScrollBar* sb = m_logEdit->verticalScrollBar();
    sb->setValue(sb->maximum());
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
