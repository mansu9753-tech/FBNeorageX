// main.cpp — FBNeoRageX C++ 진입점

#include <QApplication>
#include <QSurfaceFormat>
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
    app.setApplicationVersion("1.9");
    app.setOrganizationName("FBNeoRageX");

    // ── 작업 디렉터리를 실행파일 위치로 고정 ──────────────
    QDir::setCurrent(QCoreApplication::applicationDirPath());

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
        QByteArray logPath =
            (QCoreApplication::applicationDirPath() + "/crash_log.txt").toUtf8();
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
    fmt.setVersion(2, 1);
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
    QSurfaceFormat::setDefaultFormat(fmt);

    // ── 메인 윈도우 ──────────────────────────────────────
    MainWindow win;
    win.show();

    int ret = app.exec();

    // ── 설정 저장 ────────────────────────────────────────
    gSettings.save();

    return ret;
}
