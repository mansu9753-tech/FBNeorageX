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
#include <QAudioOutput>
#include <QVideoFrame>
#include <QMediaFormat>
#include <QDialog>
#include <QKeySequence>
#include <QHeaderView>
#include <QTableWidget>
#include <QMessageBox>
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
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("FBNEORAGEX Core Edition 1.9");
    m_windowedSize = QSize(1360, 840);
    resize(m_windowedSize);
    setMinimumSize(900, 640);

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

        // ── LEFT / RIGHT D-패드 → 페이지 업/다운 (길게 누르면 반복) ──
#ifdef Q_OS_LINUX
        bool lDown = (dp >> 6) & 1;  // LR_LT = bit 6
        bool rDown = (dp >> 7) & 1;  // LR_RT = bit 7
#else
        bool lDown = (gState.rawKeys[6] != 0);  // libretro LEFT
        bool rDown = (gState.rawKeys[7] != 0);  // libretro RIGHT
#endif
        int  cnt   = m_gameList->count();

        auto pageMove = [&](int dir) {
            if (cnt <= 0) return;
            int rowH     = m_gameList->sizeHintForRow(0);
            int pageSize = (rowH > 0)
                ? std::max(1, m_gameList->viewport()->height() / rowH)
                : 10;
            int cur2   = std::max(0, m_gameList->currentRow());
            int newRow = std::clamp(cur2 + dir * pageSize, 0, cnt - 1);
            m_gameList->setCurrentRow(newRow);
            m_gameList->scrollToItem(m_gameList->item(newRow),
                                     QAbstractItemView::PositionAtTop);
        };

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
        if (hMoved) pageMove(m_navHDir);
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
    // 앱 시작 시 게임리스트 포커스 (D패드/방향키 즉시 동작)
    if (m_gameList) m_gameList->setFocus();
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

    // ── 1P↔2P 게임 화면 오버레이 ─────────────────────────────
    m_playerOverlay = new QLabel(m_canvas);
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

    m_canvas->installEventFilter(this);
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
    hTop->setSpacing(0);

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
    // ★ QListWidget 완전 투명 처리 — BorderPanel 의 내부 알파만 색조 담당
    //   1) viewport autoFillBackground=false
    //   2) viewport stylesheet background:transparent (명시적 지정)
    //   3) QListWidget frame background:transparent
    //   4) ::item background:transparent (palette Base fall-through 차단)
    //   → 패널 안 어떤 paint 경로로도 색을 칠하지 않음.
    //     PREVIEW(QLabel) / EVENTS(QTextEdit) 와 동일한 가시성 보장.
    m_gameList->viewport()->setAutoFillBackground(false);
    m_gameList->viewport()->setStyleSheet("background:transparent;");
    m_gameList->setStyleSheet(
        "QListWidget{background:transparent;border:none;color:#99ccee;"
        "font-family:'Courier New';font-size:12px;outline:none;}"
        "QListWidget::item{padding:4px 8px;background:transparent;}"
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
    // 좌측 패널: 오른쪽 코너는 OPTIONS 와 맞닿으므로 직각
    m_gamelistPanel->setRoundedCorners(BorderPanel::CornerTL | BorderPanel::CornerBL);
    hTop->addWidget(m_gamelistPanel, 3);

    // ── 우: OPTIONS (전체폭 스택, 클릭→상세→BACK 방식) ──────
    m_optionsPanel = new BorderPanel("OPTIONS");

    // ── 전체폭 콘텐츠 스택 ───────────────────────────────────
    m_optionsStack = new QStackedWidget;
    // palette 자동 채움 해제 — BorderPanel 의 반투명 내부가 비치도록
    m_optionsStack->setAutoFillBackground(false);
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
        // BgWidget 의 배경 이미지가 비치도록 autoFillBackground 명시 해제
        // (stylesheet 의 background:transparent 만으로는 Qt6 에서 viewport 자동
        //  채움이 그대로 동작하는 경우가 있어 명시적 false 설정 필수)
        homePage->setAutoFillBackground(false);
        // guiRoot의 border-image가 비쳐 보이도록 transparent 유지
        // (border-image 중복 설정 제거 → guiRoot 하나로 통일)
        homePage->setStyleSheet(
            "QWidget{background:transparent;}"
            "QPushButton{background:rgba(0,4,20,180);color:#00aaff;"
            "border:none;border-top:1px solid transparent;border-bottom:1px solid transparent;"
            "font-family:'Courier New';font-size:13px;font-weight:bold;"
            "text-align:center;padding:8px 20px;letter-spacing:2px;}"
            "QPushButton:hover{color:#00eeff;background:rgba(0,60,180,160);"
            "border-top:1px solid #0044aa;border-bottom:1px solid #0088ff;}"
            "QPushButton:pressed{color:#ffffff;background:rgba(0,40,140,200);}");
        homePage->setObjectName("optHome");

        QVBoxLayout* homeV = new QVBoxLayout(homePage);
        homeV->setContentsMargins(0, 0, 0, 0);
        homeV->setSpacing(0);

        // 상단 타이틀 레이블 (가운데 정렬)
        QLabel* titleLbl = new QLabel("OPTIONS");
        titleLbl->setAlignment(Qt::AlignCenter);
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
    // 우측 패널: 왼쪽 코너는 GAMELIST 와 맞닿으므로 직각
    m_optionsPanel->setRoundedCorners(BorderPanel::CornerTR | BorderPanel::CornerBR);
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

    // 1P↔2P 스왑 버튼 (토글, F10)
    m_swapBtn = new QPushButton("⇄  1P");
    m_swapBtn->setCheckable(true);
    m_swapBtn->setFixedHeight(36);
    m_swapBtn->setToolTip("1P / 2P 포트 전환 (F10)  — 싱글 연습용");
    m_swapBtn->setStyleSheet(
        "QPushButton{background:#000033;color:#6688bb;border:2px solid #224488;"
        "font-family:'Courier New';font-size:10px;font-weight:bold;}"
        "QPushButton:checked{background:#003300;color:#44ff88;border-color:#00cc44;}"
        "QPushButton:hover{background:#00004d;color:#99ccff;}"
        "QPushButton:pressed{background:#001166;}");
    connect(m_swapBtn, &QPushButton::clicked, this, [this]{ toggleSwapPlayers(); });
    btnBar->addWidget(m_swapBtn);

    // TATE 버튼 (세로형 슈팅게임 화면 회전, F8)
    m_tateBtn = new QPushButton("⟳  TATE");
    m_tateBtn->setCheckable(false);
    m_tateBtn->setFixedHeight(36);
    m_tateBtn->setToolTip(
        "세로형 화면 회전 (F8)\n"
        "AUTO → 90°CCW → 90°CW → OFF → AUTO\n"
        "세로형 슈팅게임: 1942, DonPachi, Raiden 등");
    m_tateBtn->setStyleSheet(
        "QPushButton{background:#000033;color:#6688bb;border:2px solid #224488;"
        "font-family:'Courier New';font-size:10px;font-weight:bold;}"
        "QPushButton:hover{background:#00004d;color:#99ccff;}"
        "QPushButton:pressed{background:#001166;}");
    connect(m_tateBtn, &QPushButton::clicked, this, [this]{ toggleTate(); });
    btnBar->addWidget(m_tateBtn);

    makeBarBtn("⛶  FULLSCREEN",false, [this]{ toggleFullscreen(); });
    makeBarBtn("✖  EXIT",       false, [this]{ close(); });
    vRoot->addLayout(btnBar);

    // ════════════════════════════════════════════════
    //  하단: PREVIEW(좌) + EVENTS(우)
    // ════════════════════════════════════════════════
    QHBoxLayout* hBot = new QHBoxLayout; hBot->setSpacing(0);

    m_previewPanel = new BorderPanel("PREVIEW");
    m_previewLabel = new QLabel("NO PREVIEW");
    m_previewLabel->setAlignment(Qt::AlignCenter);
    // 완전 투명 — BorderPanel 알파만 색조 담당 (다른 패널과 동일)
    m_previewLabel->setAutoFillBackground(false);
    m_previewLabel->setStyleSheet("color:#335577;background:transparent;"
                                  "font-family:'Courier New';font-size:10px;");
    m_previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoWidget  = new QVideoWidget;
    // QVideoWidget 은 영상 재생 중엔 영상이 모든 픽셀을 덮으므로
    // 배경색은 영상 미재생 시(placeholder 대체 직전)에만 영향. 검정 유지.
    m_videoWidget->setStyleSheet("background:#000008;");
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_previewStack = new QStackedWidget;
    // QStackedWidget 도 palette 색 자동 채움이 활성화되어 있어
    // BorderPanel 의 반투명 내부 오버레이를 가려버린다. 명시적 해제.
    m_previewStack->setAutoFillBackground(false);
    m_previewStack->setStyleSheet("QStackedWidget{background:transparent;}");
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
    // 좌측 패널: 오른쪽 코너는 EVENTS 와 맞닿으므로 직각
    m_previewPanel->setRoundedCorners(BorderPanel::CornerTL | BorderPanel::CornerBL);
    hBot->addWidget(m_previewPanel, 3);

    m_eventsPanel = new BorderPanel("EVENTS");
    m_logEdit = new QTextEdit;
    m_logEdit->setReadOnly(true);
    // QTextEdit 완전 투명 — BorderPanel 알파만 색조 담당 (m_gameList 와 동일)
    m_logEdit->viewport()->setAutoFillBackground(false);
    m_logEdit->viewport()->setStyleSheet("background:transparent;");
    m_logEdit->setStyleSheet(
        "QTextEdit{background:transparent;border:none;color:#99ccee;"
        "font-family:'Courier New';font-size:10px;}"
        "QScrollBar:vertical{background:#000022;width:8px;}"
        "QScrollBar::handle:vertical{background:#224466;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");
    m_eventsPanel->innerLayout()->addWidget(m_logEdit);
    // 우측 패널: 왼쪽 코너는 PREVIEW 와 맞닿으므로 직각
    m_eventsPanel->setRoundedCorners(BorderPanel::CornerTR | BorderPanel::CornerBR);
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

    // ── 컨트롤 저장 범위 (전역/기종별/게임별) ───────────────────
    {
        const QString grpStyleCtrl =
            "QGroupBox{color:#4488cc;border:1px solid #223366;border-radius:2px;"
            "margin-top:14px;padding:6px;font-family:'Courier New';font-size:10px;}"
            "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 6px;}";
        QGroupBox* scopeGroup = new QGroupBox("저장 범위 (현재 매핑을 어디에 저장할지)");
        scopeGroup->setStyleSheet(grpStyleCtrl);
        QVBoxLayout* sgV = new QVBoxLayout(scopeGroup);

        QLabel* scopeHint = new QLabel(
            "전역=모든 게임 / 기종별=같은 기종 게임 공통 / 게임별=이 게임 전용\n"
            "적용 우선순위:  게임별 > 기종별 > 전역 > 기본");
        scopeHint->setStyleSheet("color:#446688;font-family:'Courier New';font-size:9px;");
        sgV->addWidget(scopeHint);

        QHBoxLayout* sh = new QHBoxLayout; sh->setSpacing(6);
        auto* saveGlobalBtn = new QPushButton("전역 저장");
        auto* savePlatBtn   = new QPushButton("기종별 저장");
        auto* saveGameBtn   = new QPushButton("게임별 저장");
        for (auto* b : {saveGlobalBtn, savePlatBtn, saveGameBtn}) {
            b->setStyleSheet(btnStyle(true)); b->setFixedHeight(26);
        }
        connect(saveGlobalBtn, &QPushButton::clicked, this, [this]{ saveControlsToScope("global"); });
        connect(savePlatBtn,   &QPushButton::clicked, this, [this]{ saveControlsToScope("plat");   });
        connect(saveGameBtn,   &QPushButton::clicked, this, [this]{ saveControlsToScope("game");   });
        sh->addWidget(saveGlobalBtn); sh->addWidget(savePlatBtn); sh->addWidget(saveGameBtn);
        sgV->addLayout(sh);
        v->addWidget(scopeGroup);
    }

    // ── 핫키 설정 ────────────────────────────────────────────
    {
        const QString grpStyleCtrl =
            "QGroupBox{color:#4488cc;border:1px solid #223366;border-radius:2px;"
            "margin-top:14px;padding:6px;font-family:'Courier New';font-size:10px;}"
            "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 6px;}";
        QGroupBox* hkGroup = new QGroupBox("핫키 설정");
        hkGroup->setStyleSheet(grpStyleCtrl);
        QVBoxLayout* hkV = new QVBoxLayout(hkGroup);

        QLabel* hkHint = new QLabel(
            "[REMAP] 클릭 후 원하는 키(조합)를 누르세요. F1~F8(세이브스테이트)은 고정.\n"
            "잘못되면 [기본값 복원]으로 언제든 되돌릴 수 있습니다.");
        hkHint->setStyleSheet("color:#446688;font-family:'Courier New';font-size:9px;");
        hkV->addWidget(hkHint);

        m_hotkeyTable = new QTableWidget;
        m_hotkeyTable->setColumnCount(3);
        m_hotkeyTable->setHorizontalHeaderLabels({"기능", "현재 키", ""});
        m_hotkeyTable->setStyleSheet(tblStyle);
        m_hotkeyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_hotkeyTable->setSelectionMode(QAbstractItemView::NoSelection);
        m_hotkeyTable->verticalHeader()->setVisible(false);
        m_hotkeyTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_hotkeyTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        m_hotkeyTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        m_hotkeyTable->setColumnWidth(1, 110);
        m_hotkeyTable->setColumnWidth(2, 68);
        m_hotkeyTable->verticalHeader()->setDefaultSectionSize(26);
        m_hotkeyTable->setFixedHeight(26 * 11 + 28);   // 행 수에 맞춤
        hkV->addWidget(m_hotkeyTable);
        rebuildHotkeyTable();

        QHBoxLayout* hkBtns = new QHBoxLayout; hkBtns->setSpacing(6);
        auto* hkResetBtn = new QPushButton("기본값 복원");
        auto* hkSaveBtn  = new QPushButton("저장");
        hkResetBtn->setStyleSheet(btnStyle(false)); hkResetBtn->setFixedHeight(26);
        hkSaveBtn->setStyleSheet(btnStyle(true));   hkSaveBtn->setFixedHeight(26);
        connect(hkResetBtn, &QPushButton::clicked, this, [this]{
            gSettings.hotkeyMap.clear();   // 비우면 기본값 사용
            gSettings.save();
            rebuildHotkeyTable();
            log("핫키 기본값으로 복원됨");
        });
        connect(hkSaveBtn, &QPushButton::clicked, this, [this]{
            gSettings.save();
            log("핫키 설정 저장됨");
        });
        hkBtns->addStretch();
        hkBtns->addWidget(hkResetBtn);
        hkBtns->addWidget(hkSaveBtn);
        hkV->addLayout(hkBtns);
        v->addWidget(hkGroup);
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

// ── 핫키 테이블 갱신 ─────────────────────────────────────────
void MainWindow::rebuildHotkeyTable() {
    if (!m_hotkeyTable) return;
    const QString remapStyle =
        "QPushButton{background:#001133;color:#6688aa;border:1px solid #334488;"
        "padding:2px;font-family:'Courier New';font-size:9px;}"
        "QPushButton:hover{background:#002255;color:#aaccff;}";

    int n = 0; const HotkeyDef* defs = hotkeyDefs(&n);
    m_hotkeyTable->setRowCount(n);
    m_hotkeyTable->blockSignals(true);

    for (int row = 0; row < n; ++row) {
        const QString action = defs[row].action;

        auto* actItem = new QTableWidgetItem(defs[row].label);
        actItem->setForeground(QColor("#99ccee"));
        m_hotkeyTable->setItem(row, 0, actItem);

        auto* keyItem = new QTableWidgetItem(hotkeyText(hotkeyOf(action)));
        keyItem->setTextAlignment(Qt::AlignCenter);
        keyItem->setForeground(QColor("#ffcc44"));
        keyItem->setFont(QFont("Courier New", 10, QFont::Bold));
        m_hotkeyTable->setItem(row, 1, keyItem);

        QPushButton* remapBtn = new QPushButton("REMAP");
        remapBtn->setStyleSheet(remapStyle);
        connect(remapBtn, &QPushButton::clicked, this, [this, action]{
            int n2 = 0; const HotkeyDef* d = hotkeyDefs(&n2);
            QString label = action;
            for (int i = 0; i < n2; ++i)
                if (action == d[i].action) { label = d[i].label; break; }

            KeyCaptureDialog dlg(label, this, /*withMods=*/true);
            if (dlg.exec() == QDialog::Accepted && dlg.capturedKey != 0) {
                gSettings.hotkeyMap[action] =
                    hotkeyEncode(dlg.capturedKey, dlg.capturedMods);
                gSettings.save();
                rebuildHotkeyTable();
                log(QString("핫키 재설정: %1 → %2")
                    .arg(label, hotkeyText(hotkeyOf(action))));
            }
        });
        m_hotkeyTable->setCellWidget(row, 2, remapBtn);
    }
    m_hotkeyTable->blockSignals(false);
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

    // ── 플래시 감소 (눈 보호) ────────────────────────────────
    m_flashGuardCheck = new QCheckBox("플래시 감소 (눈 보호)");
    m_flashGuardCheck->setStyleSheet(ckStyle);
    m_flashGuardCheck->setToolTip(
        "카운터 번쩍임·총구 화염 등 화면이 갑자기 밝아지는 순간을 감지해\n"
        "그 프레임을 어둡게 처리합니다. 눈부심·눈 피로를 줄여줍니다.");
    vidForm->addRow(makeLabel(""), m_flashGuardCheck);

    QHBoxLayout* flashH = new QHBoxLayout;
    m_flashSlider = new QSlider(Qt::Horizontal);
    m_flashSlider->setRange(0, 100);
    m_flashSlider->setValue(gSettings.videoFlashStrength);
    m_flashSlider->setStyleSheet(slStyle);
    QLabel* flashValLbl = new QLabel(QString("%1%").arg(gSettings.videoFlashStrength));
    flashValLbl->setStyleSheet(labelStyle()); flashValLbl->setFixedWidth(36);
    connect(m_flashSlider, &QSlider::valueChanged, this, [flashValLbl](int v){
        flashValLbl->setText(QString("%1%").arg(v));
    });
    // 실시간 반영: 슬라이더/체크 변경 즉시 캔버스에 적용
    connect(m_flashSlider, &QSlider::valueChanged, this, [this](int v){
        if (m_canvas) m_canvas->setFlashGuard(
            m_flashGuardCheck && m_flashGuardCheck->isChecked(), v / 100.0f);
    });
    connect(m_flashGuardCheck, &QCheckBox::toggled, this, [this](bool on){
        if (m_canvas) m_canvas->setFlashGuard(
            on, (m_flashSlider ? m_flashSlider->value() : 80) / 100.0f);
    });
    flashH->addWidget(m_flashSlider); flashH->addWidget(flashValLbl);
    vidForm->addRow(makeLabel("플래시 강도"), flashH);

    m_vsyncCheck = new QCheckBox("VSync"); m_vsyncCheck->setStyleSheet(ckStyle);
#ifdef Q_OS_LINUX
    // Linux(GameScope): swapInterval은 항상 0 고정 → VSync 옵션 비활성화
    m_vsyncCheck->setEnabled(false);
    m_vsyncCheck->setToolTip("Steam Deck(GameScope)에서는 컴포지터가 VSync를 처리합니다.\n이 설정은 Linux에서 비활성화됩니다.");
#endif
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

    auto groupStyle = []{
        return QString(
            "QGroupBox{color:#4488cc;border:1px solid #223366;"
            "border-radius:2px;margin-top:14px;padding:6px;"
            "font-family:'Courier New';font-size:10px;}"
            "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 6px;}");
    };
    auto hintStyle = []{ return QString("color:#446688;font-family:'Courier New';font-size:9px;"); };

    // ── 스크린샷 ──────────────────────────────────────────
    QGroupBox* shotGroup = new QGroupBox("SCREENSHOT");
    shotGroup->setStyleSheet(groupStyle());
    QVBoxLayout* sgV = new QVBoxLayout(shotGroup); sgV->setSpacing(6);
    QLabel* shotHint = new QLabel("F12 — screenshots/{rom}_{timestamp}.png 저장");
    shotHint->setStyleSheet(hintStyle()); shotHint->setWordWrap(true);
    sgV->addWidget(shotHint);
    QPushButton* shotBtn = new QPushButton("📷  TAKE SCREENSHOT  (F12)");
    shotBtn->setStyleSheet(btnStyle(false)); shotBtn->setFixedHeight(34);
    connect(shotBtn, &QPushButton::clicked, this, &MainWindow::takeScreenshot);
    sgV->addWidget(shotBtn);

    QLabel* prevShotHint = new QLabel("Ctrl+F12 — 현재 프레임을 previews/{rom}.png 로 저장 (기존 덮어씌움)");
    prevShotHint->setStyleSheet(hintStyle()); prevShotHint->setWordWrap(true);
    sgV->addWidget(prevShotHint);
    QPushButton* prevShotBtn = new QPushButton("🖼  SAVE AS PREVIEW IMAGE  (Ctrl+F12)");
    prevShotBtn->setStyleSheet(btnStyle(true)); prevShotBtn->setFixedHeight(34);
    connect(prevShotBtn, &QPushButton::clicked, this, &MainWindow::savePreviewShot);
    sgV->addWidget(prevShotBtn);
    v->addWidget(shotGroup);

    // ── 녹화 ─────────────────────────────────────────────
    QGroupBox* recGroup = new QGroupBox("VIDEO RECORD");
    recGroup->setStyleSheet(groupStyle());
    QVBoxLayout* rgV = new QVBoxLayout(recGroup); rgV->setSpacing(6);
    QLabel* recHint = new QLabel("F9 — recordings/{rom}_{timestamp}.mp4 저장");
    recHint->setStyleSheet(hintStyle()); recHint->setWordWrap(true);
    rgV->addWidget(recHint);
    QPushButton* recBtn = new QPushButton("⏺  START / STOP RECORDING  (F9)");
    recBtn->setStyleSheet(btnStyle(false)); recBtn->setFixedHeight(34);
    connect(recBtn, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    rgV->addWidget(recBtn);

    QLabel* prevRecHint = new QLabel("Ctrl+F9 — 녹화 시작 → 다시 누르면 previews/{rom}.mp4 로 저장 (기존 덮어씌움)");
    prevRecHint->setStyleSheet(hintStyle()); prevRecHint->setWordWrap(true);
    rgV->addWidget(prevRecHint);
    QPushButton* prevRecBtn = new QPushButton("🎬  RECORD PREVIEW VIDEO  (Ctrl+F9)");
    prevRecBtn->setStyleSheet(btnStyle(true)); prevRecBtn->setFixedHeight(34);
    connect(prevRecBtn, &QPushButton::clicked, this, &MainWindow::togglePreviewRecord);
    rgV->addWidget(prevRecBtn);
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

// ── 페이지 7: NETPLAY (MULTIPLAYER) — Fightcade Style ───────
void MainWindow::buildNetplayPage(QWidget* page) {
    QVBoxLayout* vRoot = new QVBoxLayout(page);
    vRoot->setContentsMargins(16, 10, 16, 10);
    vRoot->setSpacing(6);

    auto mkLbl = [](const QString& t, bool bold = false, const QString& col = "") {
        QLabel* l = new QLabel(t);
        QString c = col.isEmpty() ? (bold ? "#aaccff" : "#6688aa") : col;
        l->setStyleSheet(QString("color:%1;font-family:'Courier New';font-size:%2px;%3")
                         .arg(c).arg(bold ? 12 : 10).arg(bold ? "font-weight:bold;" : ""));
        return l;
    };
    auto mkLine = [&]{
        QFrame* f = new QFrame; f->setFrameShape(QFrame::HLine);
        f->setStyleSheet("color:#1a2a4a;"); vRoot->addWidget(f);
    };

    // ── 상태 · IP 헤더 ──────────────────────────────────────
    m_npStatusLabel = new QLabel("● OFFLINE");
    m_npStatusLabel->setStyleSheet(
        "color:#cc4444;font-family:'Courier New';font-size:11px;font-weight:bold;");
    m_npStatusLabel->setAlignment(Qt::AlignCenter);
    vRoot->addWidget(m_npStatusLabel);

    // RTT 레이블 (연결 후 표시)
    m_npRttLabel = new QLabel("");
    m_npRttLabel->setStyleSheet(
        "color:#aaaaff;font-family:'Courier New';font-size:9px;");
    m_npRttLabel->setAlignment(Qt::AlignCenter);
    vRoot->addWidget(m_npRttLabel);

    mkLine();

    // ── 공개 IP 표시 ────────────────────────────────────────
    {
        QHBoxLayout* h = new QHBoxLayout; h->setSpacing(6);
        h->addWidget(mkLbl("YOUR IP :"));
        m_npPublicIpLabel = new QLabel("조회 중...");
        m_npPublicIpLabel->setStyleSheet(
            "color:#44ffaa;font-family:'Courier New';font-size:11px;font-weight:bold;");
        m_npPublicIpLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        h->addWidget(m_npPublicIpLabel);
        h->addStretch();

        // 로컬 IP도 숨겨두기 (localIp() 조회용 — localIpLabel은 더 이상 UI에 표시 안 함)
        m_npLocalIpLabel = new QLabel(gNetplay().localIp());
        m_npLocalIpLabel->hide();
        vRoot->addLayout(h);
    }

    // 공개 IP 비동기 조회
    {
        auto* nam = new QNetworkAccessManager(this);
        connect(nam, &QNetworkAccessManager::finished, this,
                [this, nam](QNetworkReply* reply){
            if (reply->error() == QNetworkReply::NoError) {
                m_publicIp = reply->readAll().trimmed();
                if (m_npPublicIpLabel) m_npPublicIpLabel->setText(m_publicIp);
            } else {
                m_publicIp = gNetplay().localIp();
                if (m_npPublicIpLabel) m_npPublicIpLabel->setText(m_publicIp + " (local)");
            }
            reply->deleteLater();
            nam->deleteLater();

            // 토큰 방식: 코드 갱신 불필요 (IP가 코드에 포함되지 않음).
            // ipify 결과는 STUN 실패 시 폴백용으로만 사용.
        });
        nam->get(QNetworkRequest(QUrl("https://api.ipify.org")));
    }

    mkLine();

    // ── HOST 섹션 ────────────────────────────────────────────
    vRoot->addWidget(mkLbl("— HOST —", true));

    // Port + Input Delay + HOST GAME
    {
        QHBoxLayout* h = new QHBoxLayout; h->setSpacing(6);
        h->addWidget(mkLbl("Port:"));
        m_npPortSpin = new QSpinBox; m_npPortSpin->setStyleSheet(editStyle());
        m_npPortSpin->setRange(1024, 65535);
        m_npPortSpin->setValue(gSettings.netplayPort);
        m_npPortSpin->setFixedWidth(80); h->addWidget(m_npPortSpin);

        h->addWidget(mkLbl("Delay:"));
        m_npDelaySpinBox = new QSpinBox; m_npDelaySpinBox->setStyleSheet(editStyle());
        m_npDelaySpinBox->setRange(0, 8);
        m_npDelaySpinBox->setValue(gSettings.netplayInputDelay);
        m_npDelaySpinBox->setSuffix("f");
        m_npDelaySpinBox->setFixedWidth(60); h->addWidget(m_npDelaySpinBox);
        h->addWidget(mkLbl("(권장: 해외 2~4f)"));

        m_npHostBtn = new QPushButton("📡  HOST GAME");
        m_npHostBtn->setStyleSheet(btnStyle(true));
        connect(m_npHostBtn, &QPushButton::clicked, this, [this]{
            // 중복 클릭 방지 (룸코드 갱신되어 매칭 깨지는 문제 차단)
            // 재시도하려면 DISCONNECT 후 다시 HOST GAME.
            m_npHostBtn->setEnabled(false);
            if (m_npConnectBtn) m_npConnectBtn->setEnabled(false);
            if (m_npDisconnBtn) m_npDisconnBtn->setEnabled(true);
            // 버튼 비활성화 시 포커스가 Relay URL 로 튀는 것 방지
            if (m_npDisconnBtn) m_npDisconnBtn->setFocus(Qt::OtherFocusReason);

            int port  = m_npPortSpin->value();
            int delay = m_npDelaySpinBox->value();
            gSettings.netplayPort       = port;
            gSettings.netplayInputDelay = delay;
            gSettings.save();

            // 1. UDP 소켓 바인드 (호스트 대기)
            gNetplay().hostListen(port);
            m_relayPeerHandled = false;   // 새 연결 — 피어 처리 플래그 리셋

            // 2. 토큰 룸 코드 생성 (워커가 토큰→IP:Port 매핑 관리)
            QString code = generateRoomCode();
            if (m_npRoomCodeLabel) m_npRoomCodeLabel->setText(code);
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
                if (m_npStatusLabel)
                    m_npStatusLabel->setText(QString("● 대기 중 (UPnP %1)").arg(port));
            });
            connect(m_upnp, &UPnpMapper::failed, this, [this](const QString& reason){
                log("· UPnP 미지원: " + reason.split('\n').first()
                    + "  (릴레이 홀펀칭으로 진행)");
            });
            m_upnp->map(port, gNetplay().localIp());
        });
        h->addWidget(m_npHostBtn); h->addStretch();
        vRoot->addLayout(h);
    }

    // 룸 코드 표시 (HOST GAME 클릭 후 onNetConnected에서 갱신)
    {
        QHBoxLayout* h = new QHBoxLayout; h->setSpacing(6);
        h->addWidget(mkLbl("Room Code:"));
        m_npRoomCodeLabel = new QLabel("—");
        m_npRoomCodeLabel->setStyleSheet(
            "color:#ffdd44;font-family:'Courier New';font-size:13px;font-weight:bold;"
            "letter-spacing:2px;");
        m_npRoomCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        h->addWidget(m_npRoomCodeLabel);

        QPushButton* copyBtn = new QPushButton("⎘ COPY");
        copyBtn->setStyleSheet(
            "QPushButton{background:#001133;color:#aaccff;border:1px solid #334488;"
            "padding:3px 8px;font-family:'Courier New';font-size:10px;}"
            "QPushButton:hover{background:#002255;}");
        connect(copyBtn, &QPushButton::clicked, this, [this]{
            if (m_npRoomCodeLabel && m_npRoomCodeLabel->text() != "—")
                QApplication::clipboard()->setText(m_npRoomCodeLabel->text());
        });
        h->addWidget(copyBtn); h->addStretch();
        vRoot->addLayout(h);
    }

    mkLine();

    // ── JOIN 섹션 ────────────────────────────────────────────
    vRoot->addWidget(mkLbl("— JOIN —", true));

    // 룸 코드 입력
    {
        QHBoxLayout* h = new QHBoxLayout; h->setSpacing(6);
        h->addWidget(mkLbl("Room Code:"));
        m_npRoomCodeEdit = new QLineEdit;
        m_npRoomCodeEdit->setStyleSheet(editStyle());
        m_npRoomCodeEdit->setPlaceholderText("XXXXXX");
        m_npRoomCodeEdit->setMaxLength(kTokenLen);
        // 입력 마스크 제거 — 6자 영숫자만 (대문자 자동 변환은 toUpper)
        m_npRoomCodeEdit->setFixedWidth(90); h->addWidget(m_npRoomCodeEdit);

        // 직접 IP 입력 (룸 코드 없을 때 대안)
        h->addWidget(mkLbl("or IP:"));
        m_npIpEdit = new QLineEdit("127.0.0.1");
        m_npIpEdit->setStyleSheet(editStyle());
        m_npIpEdit->setFixedWidth(120); h->addWidget(m_npIpEdit);

        h->addStretch();
        vRoot->addLayout(h);
    }

    // CONNECT 버튼
    {
        QHBoxLayout* h = new QHBoxLayout; h->setSpacing(6);
        m_npConnectBtn = new QPushButton("🔌  JOIN GAME");
        m_npConnectBtn->setStyleSheet(btnStyle(true));
        connect(m_npConnectBtn, &QPushButton::clicked, this, [this]{
            // 중복 클릭 방지 (재시도는 DISCONNECT 후 다시 JOIN GAME)
            m_npConnectBtn->setEnabled(false);
            if (m_npHostBtn)    m_npHostBtn->setEnabled(false);
            if (m_npDisconnBtn) m_npDisconnBtn->setEnabled(true);
            // 버튼 비활성화 시 Qt 가 포커스를 다음 위젯(Relay URL)으로 옮기는 것 방지
            if (m_npDisconnBtn) m_npDisconnBtn->setFocus(Qt::OtherFocusReason);

            log("[JOIN] 버튼 클릭 — 핸들러 진입");   // ← 진단: 핸들러 진입 확인

            QString rawCode = m_npRoomCodeEdit ? m_npRoomCodeEdit->text().trimmed() : QString();
            QString code    = rawCode.toUpper();
            log(QString("[JOIN] 입력 룸코드='%1'").arg(code));

            // ── 직접 IP 입력 모드 (룸 코드 비어있음) ──
            // 동일 LAN 테스트 / 포트포워딩 직결 환경용. 릴레이/STUN 미사용.
            if (code.isEmpty()) {
                QString ip = m_npIpEdit->text().trimmed();
                int port   = gSettings.netplayPort;
                log(QString("[JOIN] 직접 연결 모드 → %1:%2").arg(ip).arg(port));
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
            log("[JOIN] clientPrepare() 호출 직전");
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
                    log("[DIAG] JOIN extAddr 람다 — singleShot 스케줄 직전");
                    // ★ 네트워크 호출을 다음 이벤트 루프 틱으로 지연 (재진입 차단)
                    QTimer::singleShot(0, this, [this, code, extIp, extPort]{
                        log("[DIAG] JOIN 지연 람다 실행 — relayRegister 호출 직전");
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
        });
        h->addWidget(m_npConnectBtn); h->addStretch();
        vRoot->addLayout(h);
    }

    mkLine();

    // ── 게임 제어 버튼 ─────────────────────────────────────
    {
        QHBoxLayout* h = new QHBoxLayout; h->setSpacing(8);
        m_npStartBtn = new QPushButton("▶  START GAME (HOST)");
        m_npStartBtn->setStyleSheet(btnStyle(true));
        m_npStartBtn->setEnabled(false);
        connect(m_npStartBtn, &QPushButton::clicked, this, &MainWindow::netplayStartGame);
        h->addWidget(m_npStartBtn);

        m_npDisconnBtn = new QPushButton("✖  DISCONNECT");
        m_npDisconnBtn->setStyleSheet(btnStyle(false));
        m_npDisconnBtn->setEnabled(false);
        connect(m_npDisconnBtn, &QPushButton::clicked, this, [this]{
            log("✖ DISCONNECT — 연결 완전 해제 중...");
            // 게임 중이면 상대에게도 종료 통지
            if (gNetplay().playing()) gNetplay().sendGameOver();
            cleanupNetplay();
            gNetplay().shutdown();          // 소켓 완전 종료 → disconnected 신호
            if (m_npRoomCodeLabel) m_npRoomCodeLabel->setText("—");
            if (m_npRttLabel)      m_npRttLabel->setText("");
            // 버튼 상태 복구 (HOST/JOIN 재시도 가능하게)
            if (m_npHostBtn)    m_npHostBtn->setEnabled(true);
            if (m_npConnectBtn) m_npConnectBtn->setEnabled(true);
            if (m_npStartBtn)   m_npStartBtn->setEnabled(false);
            m_npDisconnBtn->setEnabled(false);
            log("✖ 연결 해제됨");
        });
        h->addWidget(m_npDisconnBtn); h->addStretch();
        vRoot->addLayout(h);
    }

    vRoot->addStretch();

    // ── 릴레이 서버 (URL 숨김 — 프라이버시) ────────────────────
    // URL 에 계정 ID 가 포함되므로 화면에 그대로 노출하지 않는다.
    // Password 에코 모드로 점(●) 표시 → 어깨너머/스크린샷 노출 방지.
    // (값은 그대로 동작·저장되며, 본인이 클릭해 편집은 가능)
    {
        QHBoxLayout* h = new QHBoxLayout; h->setSpacing(6);
        h->addWidget(mkLbl("Relay:"));
        m_npRelayUrlEdit = new QLineEdit(gSettings.netplayRelayUrl);
        m_npRelayUrlEdit->setEchoMode(QLineEdit::Password);   // ● 로 표시
        m_npRelayUrlEdit->setPlaceholderText("(내장 릴레이 서버 사용 중)");
        m_npRelayUrlEdit->setStyleSheet(editStyle());
        connect(m_npRelayUrlEdit, &QLineEdit::editingFinished, this, [this]{
            gSettings.netplayRelayUrl = m_npRelayUrlEdit->text().trimmed();
            gSettings.save();
        });
        // 연결 상태만 간단히 표시 (URL 미노출)
        QLabel* relayState = new QLabel(
            gSettings.netplayRelayUrl.isEmpty() ? "● 미설정" : "● 내장 서버 사용 중");
        relayState->setStyleSheet("color:#44aa66;font-family:'Courier New';font-size:9px;");
        h->addWidget(relayState);
        h->addWidget(m_npRelayUrlEdit, 1);
        vRoot->addLayout(h);
    }

    // ── 사용 안내 ────────────────────────────────────────────
    QLabel* hint = new QLabel(
        "[ HOST ]  Delay 설정 → HOST GAME → 6자 룸 코드 공유 → 게임 선택 → START GAME\n"
        "[ JOIN ]  6자 룸 코드 입력 → JOIN GAME  (또는 직접 IP 입력)\n"
        "Delay: 국내 0~1f / 아시아 2~3f / 해외 4~6f  |  롤백: 최대 30f\n"
        "게임 중 ESC = 양쪽 게임 종료(Lobby 복귀) / DISCONNECT = 연결 완전 해제");
    hint->setStyleSheet("color:#2a3a5a;font-family:'Courier New';font-size:9px;");
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

    // ── 저장 범위 선택 (게임별 / 기종별) ──────────────────────
    {
        QHBoxLayout* sh = new QHBoxLayout; sh->setSpacing(6);
        sh->addWidget(makeLabel("저장 범위:"));
        QComboBox* scopeCombo = new QComboBox;
        scopeCombo->setStyleSheet(editStyle());
        scopeCombo->addItem("게임별 (이 게임 전용)", "game");
        scopeCombo->addItem(QString("기종별 (%1 공통)")
                            .arg(gamePlatform(m_loadedGame.isEmpty()
                                              ? m_selectedGame : m_loadedGame)), "plat");
        int si = scopeCombo->findData(m_machineScope);
        if (si >= 0) scopeCombo->setCurrentIndex(si);
        connect(scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, scopeCombo](int){
            m_machineScope = scopeCombo->currentData().toString();
            log("머신세팅 저장 범위: " + m_machineScope);
        });
        sh->addWidget(scopeCombo, 1);
        v->addLayout(sh);
    }

    // 안내
    auto* hint = new QLabel("변경 사항은 즉시 적용·저장됩니다. 일부 설정은 리셋 후 반영됩니다.\n"
                            "적용 우선순위:  게임별 > 기종별 > 코어 기본");
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
                [this, key, options](int i) {
            if (i >= 0 && i < options.size()) {
                gState.variables[key] = options[i];
                gState.variablesUpdated.store(true);
                // 머신 세팅 즉시 저장 — 선택된 범위(게임별/기종별)에
                if (m_machineScope == "plat") {
                    QString plat = gamePlatform(m_loadedGame);
                    gSettings.machineVarsByPlatform[plat][key] = options[i];
                } else {
                    gSettings.machineVars[m_loadedGame][key] = options[i];
                }
                gSettings.save();
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

    // 필터 후 선택 복원: 이전에 선택된 게임 항목 유지, 없으면 첫 번째 행 선택
    bool selRestored = false;
    if (!m_selectedGame.isEmpty()) {
        for (int i = 0; i < m_gameList->count(); ++i) {
            if (m_gameList->item(i)->data(Qt::UserRole).toString() == m_selectedGame) {
                m_gameList->setCurrentRow(i);
                m_gameList->scrollToItem(m_gameList->item(i), QAbstractItemView::EnsureVisible);
                selRestored = true;
                break;
            }
        }
    }
    if (!selRestored && m_gameList->count() > 0)
        m_gameList->setCurrentRow(0);
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
            log("치트 없음 (" + m_selectedGame + ".ini)");
        else
            log(QString("치트 %1개 로드 (%2.ini)").arg(m_cheat->count()).arg(m_selectedGame));
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
            log(QString("⟳ 세로형 게임 감지 — 자동 회전 %1° 적용").arg(gState.videoRotation * 90));
    });

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
        log(QString("🔊 오디오: %1 Hz, %2 ms").arg(sr).arg(gSettings.audioBufferMs));
        m_audio->init(sr, gSettings.audioBufferMs);
        startEmu();
    } else {
        log("✖ ROM 로드 실패: " + m_selectedGame);
    }
}

void MainWindow::toggleSwapPlayers() {
    gState.swapPlayers = !gState.swapPlayers;

    if (m_swapBtn) {
        m_swapBtn->setChecked(gState.swapPlayers);
        m_swapBtn->setText(gState.swapPlayers ? "⇄  2P" : "⇄  1P");
    }

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
            m_canvas->width()  - m_playerOverlay->width()  - 12,
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

    // 버튼 라벨 + 색상 업데이트
    if (m_tateBtn) {
        QString lbl;
        QString activeStyle =
            "QPushButton{background:#002200;color:#44ff88;border:2px solid #00cc44;"
            "font-family:'Courier New';font-size:10px;font-weight:bold;}"
            "QPushButton:hover{background:#003300;}";
        QString inactiveStyle =
            "QPushButton{background:#000033;color:#6688bb;border:2px solid #224488;"
            "font-family:'Courier New';font-size:10px;font-weight:bold;}"
            "QPushButton:hover{background:#00004d;}";

        switch (rot) {
        case  1: lbl = "⟳  90°CCW"; m_tateBtn->setStyleSheet(activeStyle);   break;
        case  3: lbl = "⟲  90°CW";  m_tateBtn->setStyleSheet(activeStyle);   break;
        case  0: lbl = "⟳  OFF";    m_tateBtn->setStyleSheet(inactiveStyle);  break;
        default: // -1 = auto
            {
                int autoRot = gState.videoRotation;
                if (autoRot == 0)
                    lbl = "⟳  TATE";     // 코어가 회전 안 함 → 비활성
                else
                    lbl = "⟳  AUTO";     // 코어가 회전 지정 → 활성
                m_tateBtn->setStyleSheet(autoRot != 0 ? activeStyle : inactiveStyle);
            }
            break;
        }
        m_tateBtn->setText(lbl);
    }

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

    // ── 게임패드 메뉴 진입: SELECT+START 2초 홀드 ─────────────────
    // · Start 단독은 게임으로 그대로 전달 (KOF 보스선택 커맨드 정상 작동)
    // · SELECT+START 동시 홀드 → 2초 후 메인 GUI 복귀
    {
        bool combo = (gState.rawKeys[2] != 0) && (gState.rawKeys[3] != 0);
        if (combo) {
            if (++m_menuHoldCount >= 120) {
                m_menuHoldCount = 0;
                togglePause();
                return;
            }
        } else {
            m_menuHoldCount = 0;
        }
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

        // ── L2(서비스 버튼) 차단 ─────────────────────────────────────
        // FBNeo 기판 서비스 메뉴 진입 트리거: RETRO_DEVICE_ID_JOYPAD_L2 (index 12)
        // Xbox LT 트리거 → 0x10000 → index 12 로 매핑됨
        // serviceMode=false(기본) 상태에서는 코어에 0 전달 → 서비스 메뉴 차단
        // serviceMode=true(` 키 토글 후 5초간)에서만 L2 허용 → 의도적 진입 가능
        if (!gState.serviceMode)
            gState.keys[12] = 0;

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
    }

    // ── 치트 매 프레임 적용 ────────────────────────────────
    if (m_cheat) m_cheat->applyFrame(m_core, gState.frameCount, gState.gameLoadFrame);

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

// ── 프리뷰 이미지 저장 ───────────────────────────────────────
void MainWindow::savePreviewShot() {
    if (!gState.gameLoaded || gState.videoWidth == 0) {
        log("프리뷰 저장: 프레임 없음 (게임 실행 중이 아님)"); return;
    }
    if (m_selectedGame.isEmpty()) {
        log("프리뷰 저장: 선택된 게임 없음"); return;
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

    QDir().mkpath(gSettings.previewPath);
    QString path = gSettings.previewPath + "/" + m_selectedGame + ".png";
    if (img.save(path))
        log("🖼 프리뷰 저장: " + path);
    else
        log("🖼 프리뷰 저장 실패: " + path);
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
            QFile::remove(previewDest);
            if (QFile::copy(finalPath, previewDest))
                log("🎬 프리뷰 영상 저장: " + previewDest);
            else
                log("🎬 프리뷰 영상 복사 실패 — 원본: " + finalPath);
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
    if (m_flashGuardCheck)    gSettings.videoFlashGuard    = m_flashGuardCheck->isChecked();
    if (m_flashSlider)        gSettings.videoFlashStrength = m_flashSlider->value();

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
        m_canvas->setFlashGuard(gSettings.videoFlashGuard,
                                gSettings.videoFlashStrength / 100.0f);
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
    if (m_flashGuardCheck)    m_flashGuardCheck->setChecked(gSettings.videoFlashGuard);
    if (m_flashSlider)        m_flashSlider->setValue(gSettings.videoFlashStrength);

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
        if (m_swapBtn) { m_swapBtn->setChecked(false); m_swapBtn->setText("⇄  1P"); }
    }
    if (m_overlayTimer) m_overlayTimer->stop();
    if (m_playerOverlay) m_playerOverlay->hide();

    // 게임 종료 시 TATE/회전 상태 리셋 (다음 게임은 auto부터)
    gState.videoRotation = 0;
    if (m_canvas) m_canvas->setRotation(-1);  // auto
    if (m_tateBtn) {
        m_tateBtn->setText("⟳  TATE");
        m_tateBtn->setStyleSheet(
            "QPushButton{background:#000033;color:#6688bb;border:2px solid #224488;"
            "font-family:'Courier New';font-size:10px;font-weight:bold;}"
            "QPushButton:hover{background:#00004d;color:#99ccff;}"
            "QPushButton:pressed{background:#001166;}");
    }

    m_stack->setCurrentIndex(0);
    filterRoms(m_searchEdit ? m_searchEdit->text() : QString());
    // 선택된 게임이 있으면 프리뷰 재로드
    if (!m_selectedGame.isEmpty())
        loadPreview(m_selectedGame);

    // 게임리스트 포커스 복원: D패드/방향키 즉시 동작하도록
    if (m_gameList) {
        m_gameList->setFocus();
        if (m_gameList->currentRow() < 0 && m_gameList->count() > 0)
            m_gameList->setCurrentRow(0);
    }
}

// ── 마우스 커서 자동 숨김 ─────────────────────────────────────
void MainWindow::resetCursorTimer() {
    // 게임 화면 중에만 유효
    if (!m_stack || m_stack->currentIndex() != 1) return;

    // 숨겨져 있으면 즉시 복원
    if (m_cursorHidden) {
        setCursor(m_customCursor);  // widget-level: Wayland 동기 통신 없음
        m_cursorHidden = false;
    }

    // 타이머 재시작 (3초 후 hideCursor 호출)
    if (m_cursorTimer) m_cursorTimer->start();
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
    if (obj == m_canvas && ev->type() == QEvent::Resize && m_playerOverlay) {
        auto reposition = [this]{
            if (!m_playerOverlay->isVisible()) return;
            m_playerOverlay->adjustSize();
            m_playerOverlay->move(
                m_canvas->width()  - m_playerOverlay->width()  - 12,
                12);
        };
        reposition();
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
    int k    = e->key();
    bool alt = (e->modifiers() & Qt::AltModifier);

    // ── GUI 모드 전용: 방향키/엔터 → 게임리스트 전용 처리 ──────────
    // 게임 화면(스택 1)이 아닌 경우만 적용
    // → 방향키가 다른 위젯(버튼, 스크롤바 등)으로 포커스 이동하는 것을 차단
    if (m_stack && m_stack->currentIndex() == 0 && m_gameList) {

        // 상하 방향키: 게임리스트 한 칸 이동 (auto-repeat 포함)
        if (k == Qt::Key_Up || k == Qt::Key_Down) {
            int cnt = m_gameList->count();
            if (cnt > 0) {
                int row = m_gameList->currentRow();
                if (row < 0) row = (k == Qt::Key_Down) ? 0 : cnt - 1;
                else         row = std::clamp(row + (k == Qt::Key_Down ? 1 : -1), 0, cnt - 1);
                m_gameList->setCurrentRow(row);
                m_gameList->scrollToItem(m_gameList->item(row),
                                         QAbstractItemView::EnsureVisible);
            }
            return;  // 소비 — Qt 포커스 이동 차단
        }

        // 좌우 방향키: 페이지 이동 (auto-repeat 포함)
        if (k == Qt::Key_Left || k == Qt::Key_Right) {
            int cnt = m_gameList->count();
            if (cnt > 0) {
                int rowH     = m_gameList->sizeHintForRow(0);
                int pageSize = (rowH > 0)
                    ? std::max(1, m_gameList->viewport()->height() / rowH) : 10;
                int dir = (k == Qt::Key_Left) ? -1 : 1;
                int row = std::clamp(
                    std::max(0, m_gameList->currentRow()) + dir * pageSize,
                    0, cnt - 1);
                m_gameList->setCurrentRow(row);
                m_gameList->scrollToItem(m_gameList->item(row),
                                         QAbstractItemView::PositionAtTop);
            }
            return;  // 소비
        }

        // Enter / Return → 선택 게임 실행 (Alt+Enter는 전체화면이므로 제외, auto-repeat 제외)
        if (!e->isAutoRepeat() && !alt
            && (k == Qt::Key_Return || k == Qt::Key_Enter)
            && !m_selectedGame.isEmpty()) {
            launchGame();
            return;
        }
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

    // 전체화면
    if (hotkeyMatch("fullscreen", k, mods)) { toggleFullscreen(); return; }

    // 서비스 모드 (게임 중 + 일시정지 아닐 때)
    if (hotkeyMatch("service", k, mods)) {
        if (gState.gameLoaded && !gState.isPaused) {
            gState.serviceMode       = !gState.serviceMode;
            gState.serviceModeFrames = 0;
            log(gState.serviceMode ? "🔓 서비스 모드 활성 (5초간)" : "🔒 서비스 모드 해제");
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
                "vsavo","cps"}))
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
        {"pause",        "게임 ↔ 메뉴 전환",      Qt::Key_Tab,        0},
        {"exit",         "게임 종료",             Qt::Key_Escape,     0},
        {"fullscreen",   "전체화면",              Qt::Key_Return,     4}, // Alt+Enter
        {"service",      "서비스 모드",           Qt::Key_QuoteLeft,  0}, // `
        {"record",       "녹화",                  Qt::Key_F9,         0},
        {"preview_rec",  "프리뷰 영상 녹화",      Qt::Key_F9,         2}, // Ctrl+F9
        {"swap",         "1P ↔ 2P 스왑",          Qt::Key_F10,        0},
        {"fast_forward", "패스트포워드",          Qt::Key_F11,        0},
        {"screenshot",   "스크린샷",              Qt::Key_F12,        0},
        {"preview_shot", "프리뷰 이미지 저장",    Qt::Key_F12,        2}, // Ctrl+F12
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

// 핫키 인코딩 → 사람이 읽는 문자열 ("Ctrl+F9", "Tab", "`" 등)
QString MainWindow::hotkeyText(int enc) {
    int key, mods; hotkeyDecode(enc, key, mods);
    if (key == 0) return "—";
    QString s;
    if (mods & 2) s += "Ctrl+";
    if (mods & 1) s += "Shift+";
    if (mods & 4) s += "Alt+";
    QString kn = QKeySequence(key).toString(QKeySequence::NativeText);
    if (kn.isEmpty()) kn = QString("0x%1").arg(key, 0, 16);
    return s + kn;
}

// action 의 현재 핫키 인코딩 (사용자 설정 > 기본값)
int MainWindow::hotkeyOf(const QString& action) {
    if (gSettings.hotkeyMap.contains(action))
        return gSettings.hotkeyMap.value(action);
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
        QString pk = "plat:" + gamePlatform(rom);
        if (scoped.contains(pk) && !scoped[pk].isEmpty()) return scoped[pk];
    }
    if (!global.isEmpty()) return global;
    return dflt;
}

// 게임 로드 시 — 해당 게임에 맞는 컨트롤을 해석해 적용
void MainWindow::resolveAndApplyControls(const QString& rom) {
    m_ctrlScopeRom = rom;

    // 키보드
    m_keymap = resolveCtrlMap(gSettings.kbScoped, gSettings.keyboardMapping,
                              buildDefaultKeymap(), rom);
    // 게임패드
    if (m_gamepad) {
        QHash<int,int> xi = resolveCtrlMap(gSettings.xiScoped,
                                gSettings.xinputMapping, {}, rom);
        QHash<int,int> wm = resolveCtrlMap(gSettings.wmScoped,
                                gSettings.winmmMapping,  {}, rom);
        if (!xi.isEmpty()) m_gamepad->setXInputMapping(xi);
        if (!wm.isEmpty()) m_gamepad->setWinMMMapping(wm);
    }
    // 테이블이 보이면 갱신
    refreshControlsTable();
    refreshPadTable();
    refreshWinMMTable();
}

// 현재 테이블(m_keymap 등) 상태를 지정 스코프로 저장
//   scope: "global"(전역) / "plat"(기종별) / "game"(게임별)
void MainWindow::saveControlsToScope(const QString& scope) {
    const QString rom = m_ctrlScopeRom.isEmpty() ? m_selectedGame : m_ctrlScopeRom;

    QHash<int,int> xi = m_gamepad ? m_gamepad->getXInputMapping() : QHash<int,int>();
    QHash<int,int> wm = m_gamepad ? m_gamepad->getWinMMMapping()  : QHash<int,int>();

    if (scope == "global") {
        gSettings.keyboardMapping = m_keymap;
        gSettings.xinputMapping   = xi;
        gSettings.winmmMapping    = wm;
        log("🎮 컨트롤 → 전역 저장");
    } else if (scope == "plat") {
        if (rom.isEmpty()) { log("⚠ 기종 저장: 게임을 먼저 선택하세요"); return; }
        QString pk = "plat:" + gamePlatform(rom);
        gSettings.kbScoped[pk] = m_keymap;
        gSettings.xiScoped[pk] = xi;
        gSettings.wmScoped[pk] = wm;
        log(QString("🎮 컨트롤 → 기종별 저장 (%1)").arg(gamePlatform(rom)));
    } else { // game
        if (rom.isEmpty()) { log("⚠ 게임별 저장: 게임을 먼저 선택하세요"); return; }
        QString gk = "game:" + rom;
        gSettings.kbScoped[gk] = m_keymap;
        gSettings.xiScoped[gk] = xi;
        gSettings.wmScoped[gk] = wm;
        log(QString("🎮 컨트롤 → 게임별 저장 (%1)").arg(rom));
    }
    gSettings.save();
}

// ════════════════════════════════════════════════════════════
//  넷플레이 슬롯
// ════════════════════════════════════════════════════════════
void MainWindow::onNetConnected(bool isHost) {
    qDebug("[NP-conn] onNetConnected isHost=%d", isHost ? 1 : 0);
    log(QString("🌐 연결됨 — %1").arg(isHost ? "HOST(P1)" : "CLIENT(P2)"));
    if (m_npStatusLabel) {
        m_npStatusLabel->setText(
            isHost ? "● HOSTING — 게임을 선택하세요" : "● CONNECTED — 호스트 대기 중");
        m_npStatusLabel->setStyleSheet(
            "color:#44cc44;font-family:'Courier New';font-size:11px;font-weight:bold;");
    }
    if (m_npStartBtn)   m_npStartBtn->setEnabled(isHost);
    if (m_npDisconnBtn) m_npDisconnBtn->setEnabled(true);
    if (m_npHostBtn)    m_npHostBtn->setEnabled(false);
    if (m_npConnectBtn) m_npConnectBtn->setEnabled(false);

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
            log("릴레이 등록 실패: " + reply->errorString());
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
    if (tries == 0) log(QString("[DIAG] relayPollPeer 진입 myRole=%1").arg(myRole));
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
            [this, reply, code, myRole, tries](){
        reply->deleteLater();   // 공유 nam 은 파괴하지 않음

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
        connect(m_relayPollTimer, &QTimer::timeout, this, [this, code, myRole, tries](){
            relayPollPeer(code, myRole, tries + 1);
        });
        m_relayPollTimer->start(1000);
    });
}

void MainWindow::onNetDisconnected() {
    log("🌐 연결 끊김");
    m_relayPeerHandled = false;   // 다음 연결을 위해 피어 처리 플래그 리셋
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
    qDebug("[NP-state] onNetStateChanged s=%d label=%p", (int)s, (void*)m_npStatusLabel);
    if (!m_npStatusLabel) return;
    const char* style =
        "color:#44cc44;font-family:'Courier New';font-size:11px;font-weight:bold;";
    switch (s) {
    case NetplayManager::State::Lobby:
        m_npStatusLabel->setText(gNetplay().isHost()
            ? "● HOSTING — 게임을 선택하세요" : "● CONNECTED — 호스트 대기 중");
        m_npStatusLabel->setStyleSheet(style);
        if (m_npStartBtn) m_npStartBtn->setEnabled(gNetplay().isHost());
        // RTT 갱신 (Lobby 복귀 시)
        if (m_npRttLabel)
            m_npRttLabel->setText(QString("RTT: %1 ms").arg(gNetplay().rttMs()));
        break;
    case NetplayManager::State::Loading:
        m_npStatusLabel->setText("● 로딩 중...");
        m_npStatusLabel->setStyleSheet(style);
        if (m_npStartBtn) m_npStartBtn->setEnabled(false);
        break;
    case NetplayManager::State::Ready:
        m_npStatusLabel->setText("● 준비 완료 — 상대 대기 중...");
        m_npStatusLabel->setStyleSheet(style);
        if (m_npRttLabel)
            m_npRttLabel->setText(QString("RTT: %1 ms").arg(gNetplay().rttMs()));
        break;
    case NetplayManager::State::Playing:
        m_npStatusLabel->setText(QString("● 게임 중  |  RTT: %1 ms  |  Delay: %2f")
            .arg(gNetplay().rttMs()).arg(gNetplay().inputDelay()));
        m_npStatusLabel->setStyleSheet(style);
        if (m_npRttLabel) m_npRttLabel->setText("");
        break;
    default: break;
    }
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
    if (m_npDelaySpinBox) m_npDelaySpinBox->setValue(inputDelay);

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

    int delay = m_npDelaySpinBox ? m_npDelaySpinBox->value() : gSettings.netplayInputDelay;
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
