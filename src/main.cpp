// main.cpp — FBNeoRageX C++ 진입점

#include <QApplication>
#include <QSurfaceFormat>
#include <QDir>
#include <QStandardPaths>

#include "MainWindow.h"
#include "AppSettings.h"

int main(int argc, char* argv[])
{
    // ── Qt 고해상도 DPI 설정 ───────────────────────────────
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("FBNeoRageX");
    app.setApplicationVersion("1.8");
    app.setOrganizationName("FBNeoRageX");

    // ── 작업 디렉터리를 실행파일 위치로 고정 ──────────────
    QDir::setCurrent(QCoreApplication::applicationDirPath());

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
    // VSync: 설정값 반영 (기본 On)
    // Off 시 QOpenGLWidget swapBuffers 블록 해제 → 타이머 기반 AFL이 정확하게 동작
    fmt.setSwapInterval(gSettings.videoVsync ? 1 : 0);
    QSurfaceFormat::setDefaultFormat(fmt);

    // ── 메인 윈도우 ──────────────────────────────────────
    MainWindow win;
    win.show();

    int ret = app.exec();

    // ── 설정 저장 ────────────────────────────────────────
    gSettings.save();

    return ret;
}
