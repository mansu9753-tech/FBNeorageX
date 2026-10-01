// main.cpp — FBNeoRageX C++ 진입점

#include <QApplication>
#include <QSurfaceFormat>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScreen>
#include <QWindow>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <cstdio>
#include <cwchar>   // _wfopen (Windows 한글 경로 지원)

#ifdef _WIN32
#include <windows.h>   // SetUnhandledExceptionFilter, CaptureStackBackTrace
#endif

#include "MainWindow.h"
#include "AppSettings.h"

// ── 크래시 진단용 영속 로그 핸들러 ──────────────────────────────
// qDebug()/qWarning() 출력을 crash_log.txt 에 즉시 flush
// 앱이 native crash로 즉사해도 직전까지의 로그가 파일에 남음
//
// ★ QFile(QObject 파생)을 전역 정적으로 선언하면 main() 실행 전에
//   QObject 생성자가 QThread::currentThread()를 호출 → QApplication 미초기화
//   상태에서 Windows TLS 접근 → Access Violation → 프로그램 즉사.
//   해결: 포인터로 선언 후 QApplication 생성 이후에만 할당.
static FILE* g_logFp = nullptr;   // C FILE* 사용 — Qt 없이 안전

static void persistentMsgHandler(QtMsgType type,
                                  const QMessageLogContext& /*ctx*/,
                                  const QString& msg)
{
    if (g_logFp) {
        const char* lvl = (type == QtDebugMsg)    ? "D" :
                          (type == QtWarningMsg)   ? "W" :
                          (type == QtCriticalMsg)  ? "C" : "F";
        QByteArray ts = QDateTime::currentDateTime()
                            .toString("hh:mm:ss.zzz").toUtf8();
        QByteArray mb = msg.toUtf8();
        fprintf(g_logFp, "%s [%s] %s\n", ts.constData(), lvl, mb.constData());
        fflush(g_logFp);  // ← 크래시 직전까지 파일에 남도록 즉시 flush
    }
    // 콘솔/디버거 출력도 유지
    fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
}

#ifdef _WIN32
// ── 네이티브 크래시 핸들러 ──────────────────────────────────────
// 처리되지 않은 예외(Access Violation 등) 발생 시, 예외 코드 + 폴트 주소 +
// 스택 백트레이스(모듈 베이스 기준 오프셋)를 crash_log.txt 에 기록.
// 오프셋 → addr2line 으로 정확한 소스 라인 역추적 가능.
static LONG WINAPI nativeCrashHandler(EXCEPTION_POINTERS* ep)
{
    if (g_logFp && ep && ep->ExceptionRecord) {
        HMODULE hMod = GetModuleHandleW(nullptr);
        const auto base = reinterpret_cast<uintptr_t>(hMod);
        const auto* rec = ep->ExceptionRecord;

        fprintf(g_logFp, "\n========== NATIVE CRASH ==========\n");
        fprintf(g_logFp, "Exception code : 0x%08lX\n",
                static_cast<unsigned long>(rec->ExceptionCode));
        const auto faultAddr = reinterpret_cast<uintptr_t>(rec->ExceptionAddress);
        fprintf(g_logFp, "Fault address  : 0x%llX  (module+0x%llX)\n",
                static_cast<unsigned long long>(faultAddr),
                static_cast<unsigned long long>(faultAddr - base));
        // AV 인 경우 접근 주소
        if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            rec->NumberParameters >= 2) {
            fprintf(g_logFp, "AV %s addr   : 0x%llX\n",
                    rec->ExceptionInformation[0] ? "write" : "read",
                    static_cast<unsigned long long>(rec->ExceptionInformation[1]));
        }
        fprintf(g_logFp, "Module base    : 0x%llX\n",
                static_cast<unsigned long long>(base));

        void* frames[40];
        USHORT n = CaptureStackBackTrace(0, 40, frames, nullptr);
        fprintf(g_logFp, "Backtrace (%u frames, module+offset):\n", n);
        for (USHORT i = 0; i < n; ++i) {
            const auto fa = reinterpret_cast<uintptr_t>(frames[i]);
            fprintf(g_logFp, "  [%02u] 0x%llX  (module+0x%llX)\n",
                    i, static_cast<unsigned long long>(fa),
                    static_cast<unsigned long long>(fa - base));
        }
        fprintf(g_logFp, "==================================\n");
        fflush(g_logFp);
    }
    return EXCEPTION_EXECUTE_HANDLER;   // 프로세스 종료
}
#endif

int main(int argc, char* argv[])
{
    // ── Qt 고해상도 DPI 설정 ───────────────────────────────
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("FBNeoRageX");
    app.setApplicationVersion(FBNRX_VERSION);
    app.setOrganizationName("FBNeoRageX");

    // ── 작업 디렉터리를 실행파일 위치로 고정 ──────────────
    QDir::setCurrent(QCoreApplication::applicationDirPath());

    // ── 기본 경로 확정 (반드시 QApplication 이후, load() 이전) ───
    //   gSettings 는 정적 초기화 시점에 생성되어 그때는 applicationDirPath()
    //   가 비어 있다 → 여기서 프로그램 폴더 기준으로 다시 설정한다.
    gSettings.initDefaults();
    gSettings.ensureDataDirs();   // 최초 실행: 폴더 구조를 미리 만들어 둔다

    // ── 크래시 진단 로그 파일 오픈 (실행파일 위치, 즉시 flush) ───
    // QApplication 생성 이후에 applicationDirPath()가 유효해짐.
    // C FILE* 사용 — QFile(QObject)을 전역 정적으로 쓰면 main() 전에
    // QThread::currentThread() 호출 → Windows TLS 크래시 발생하므로 사용 금지.
    //
    // ★ Windows: fopen()은 ANSI(CP949) 경로만 처리 → 한글 경로에서 파일 생성 실패.
    //   _wfopen() 사용 — UTF-16 wide string 경로 → 한글/특수문자 경로 모두 지원.
    {
#ifdef _WIN32
        std::wstring logPath =
            (QCoreApplication::applicationDirPath() + "/crash_log.txt").toStdWString();
        g_logFp = _wfopen(logPath.c_str(), L"w");
#else
        // 번들 루트(FBNeoRageX.sh 옆)에 남겨 사용자가 바로 찾을 수 있게 한다
        QByteArray logPath = (AppSettings::baseDir() + "/crash_log.txt").toUtf8();
        g_logFp = fopen(logPath.constData(), "w");
#endif
        if (g_logFp) {
            qInstallMessageHandler(persistentMsgHandler);
            qDebug("=== FBNeoRageX crash_log start ===");
#ifdef _WIN32
            SetUnhandledExceptionFilter(nativeCrashHandler);  // 네이티브 크래시 추적
#endif
        }
    }

    // ── 설정 로드 (OpenGL 포맷 설정보다 먼저 — VSync 반영) ──
    gSettings.load();

    // ── OpenGL 포맷 설정 ─────────────────────────────────
    // QSurfaceFormat::setDefaultFormat()은 QOpenGLWidget 생성 전이라면
    // QApplication 생성 후에도 적용 가능 (MainWindow 생성 이전)
    // Windows: CompatibilityProfile 2.1 (GLSL 1.20 호환)
    // Linux/SteamDeck: NoProfile (XWayland GLX CompatibilityProfile 미지원)
    QSurfaceFormat fmt;
#ifdef _WIN32
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    // RetroArch slang 셰이더(Mega Bezel 등)는 GLSL 330 이상이라야 돌아간다.
    //   ★ 그런데 3.3 을 요청했다가 실패하면 창이 아예 안 뜬다. 그러니 요청하기
    //     전에 실제로 만들어 보고, 안 되면 예전과 똑같이 2.1 로 간다.
    //     2.1 로 떨어져도 기존 GLSL 셰이더·베젤·화면 모드는 그대로 동작한다.
    {
        QSurfaceFormat probe;
        probe.setProfile(QSurfaceFormat::CompatibilityProfile);
        probe.setVersion(3, 3);
        probe.setDepthBufferSize(0);
        probe.setStencilBufferSize(0);

        QOffscreenSurface surf;
        surf.setFormat(probe);
        surf.create();

        QOpenGLContext probeCtx;
        probeCtx.setFormat(probe);
        bool got33 = false;
        if (surf.isValid() && probeCtx.create() && probeCtx.makeCurrent(&surf)) {
            const QSurfaceFormat real = probeCtx.format();
            got33 = (real.majorVersion() > 3)
                 || (real.majorVersion() == 3 && real.minorVersion() >= 3);
            // 화면이 안 나오는 PC 를 진단할 수 있게 그래픽 정보를 로그에 남긴다
            QOpenGLFunctions* gf = probeCtx.functions();
            qDebug("[gfx] OpenGL %s | %s | %s",
                   reinterpret_cast<const char*>(gf->glGetString(GL_VERSION)),
                   reinterpret_cast<const char*>(gf->glGetString(GL_RENDERER)),
                   reinterpret_cast<const char*>(gf->glGetString(GL_VENDOR)));
            probeCtx.doneCurrent();
        } else {
            qDebug("[gfx] OpenGL 3.3 프로브 실패 (surface=%d)", surf.isValid() ? 1 : 0);
        }
        fmt.setVersion(got33 ? 3 : 2, got33 ? 3 : 1);
    }
#endif
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);
    // VSync 설정
    // Linux(Steam Deck/GameScope): swapInterval 반드시 0 고정
    //   GameScope(Wayland 컴포지터)가 디스플레이 VSync를 자체 처리하므로
    //   애플리케이션 swapInterval=1이면 swapBuffers()가 최대 16.67ms 블록 →
    //   1ms 에뮬 타이머 발사 지연 → AFL 타이밍 붕괴 → 영상+오디오 미세 끊김
    //   (config.json 저장값 무시 — 하드코딩)
    // Windows: 사용자 설정 반영 (컴포지터 없이 직접 렌더링 → VSync 필요)
#ifdef Q_OS_LINUX
    fmt.setSwapInterval(0);
#else
    fmt.setSwapInterval(gSettings.videoVsync ? 1 : 0);
#endif
    // 진단용: 환경변수 FBNRX_VSYNC=0 이면 수직 동기를 끈다 (듀얼 모니터·내장 그래픽 문제 확인용)
    if (qEnvironmentVariable("FBNRX_VSYNC") == QLatin1String("0")) {
        fmt.setSwapInterval(0);
        qDebug("[gfx] FBNRX_VSYNC=0 → swapInterval 0");
    }
    QSurfaceFormat::setDefaultFormat(fmt);

    // ── 메인 윈도우 ──────────────────────────────────────
    for (QScreen* sc : QGuiApplication::screens())
        qDebug().noquote() << QString("[gfx] 화면 %1 %2x%3 dpr=%4 %5Hz")
                               .arg(sc->name()).arg(sc->size().width()).arg(sc->size().height())
                               .arg(sc->devicePixelRatio()).arg(sc->refreshRate());
    qDebug().noquote() << "[gfx] Qt" << QT_VERSION_STR << "platform" << QGuiApplication::platformName();
    MainWindow win;
    // 진단용: 환경변수 FBNRX_GL_TOP = core | none 이면 "창 전체를 GPU 로 합성하는 컨텍스트" 의 형식만 바꾼다.
    //   (게임 화면 위젯은 기존 형식 그대로. 게임 중 화면이 검게 나오는 PC 의 원인을 가려내는 용도)
    {
        const QString top = qEnvironmentVariable("FBNRX_GL_TOP");
        if (top == QLatin1String("core") || top == QLatin1String("none")) {
            QSurfaceFormat tf = QSurfaceFormat::defaultFormat();
            if (top == QLatin1String("core")) { tf.setProfile(QSurfaceFormat::CoreProfile); tf.setVersion(3, 3); }
            else                              { tf.setProfile(QSurfaceFormat::NoProfile);   tf.setVersion(2, 1); }
            win.setAttribute(Qt::WA_NativeWindow);
            win.winId();
            if (win.windowHandle()) win.windowHandle()->setFormat(tf);
            qDebug().noquote() << "[gfx] FBNRX_GL_TOP =" << top;
        }
    }
    win.show();

    int ret = app.exec();

    // ── 설정 저장 ────────────────────────────────────────
    gSettings.save();

    return ret;
}
