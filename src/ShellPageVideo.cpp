// ShellPageVideo.cpp — VIDEO OPTIONS 카테고리

#include "ShellPages.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

#include "AppSettings.h"
#include "GameViewIface.h"

namespace {

class VideoPage final : public ShellPage {
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("video"); }

    QVector<Row> rows() override {
        // 소프트웨어 렌더러로 돌 때는 셰이더·CRT·플래시 감소·VSYNC 를 쓸 수 없다 → 회색 안내 줄로 잠근다.
        const bool soft = m_h.softwareRenderer && m_h.softwareRenderer();
        auto lock = [soft](Row r) -> Row {
            if (!soft) return r;
            return Row::note(r.label, QStringLiteral("--"));
        };
        QVector<Row> out = {
            scaleMode(),
            toggle(QStringLiteral("SMOOTH FILTER"), gSettings.videoSmooth,
                   [this] { if (m_h.canvas) m_h.canvas->setSmooth(gSettings.videoSmooth); }),
            lock(toggle(QStringLiteral("CRT SCANLINE"), gSettings.videoCrtMode,
                        [this] { applyCrt(); })),
            lock(crtLevel()),
            lock(toggle(QStringLiteral("FLASH GUARD"), gSettings.videoFlashGuard,
                        [this] { applyFlash(); })),
            lock(flashLevel()),
            toggle(QStringLiteral("BEZEL"), gSettings.bezelEnabled,
                   [this] { if (m_h.applyBezel) m_h.applyBezel(); }),
            lock(toggle(QStringLiteral("VSYNC"), gSettings.videoVsync,
                        [this] { say(en() ? "VSync applies after restart."
                                          : "VSync 는 다시 시작해야 적용됩니다."); })),
            renderer(),
        };
        if (soft)
            out << Row::note(en() ? "SOFTWARE RENDERER: SHADER / CRT / FLASH GUARD / VSYNC ARE UNAVAILABLE"
                                  : "소프트웨어 렌더러: 셰이더 · CRT · 플래시 감소 · VSYNC 는 쓸 수 없습니다");
        out << fullscreen() << frameskip() << lock(shaderFile());
        if (!gSettings.videoShaderPath.isEmpty() && !soft)
            out << Row::action(en() ? "CLEAR SHADER" : "셰이더 해제", [this] {
                if (m_h.video.clearShader) m_h.video.clearShader();
            });
        out << lock(Row::action(QStringLiteral("SHADER PARAMS"),
                                [this] { if (m_h.openShaderParams) m_h.openShaderParams(); }));
        out << bezelRows();
        return out;
    }

private:
    // ── 공통 ─────────────────────────────────────────────
    void commit() { gSettings.save(); }

    // 게임 화면 그리기 방식. 게임을 켜면 화면이 검게 나오는 PC(구형 내장 그래픽 등)는 SOFTWARE 로 바꾼다.
    //   SOFTWARE 는 셰이더·CRT·플래시 감소를 쓸 수 없다. 다시 시작해야 적용된다.
    Row renderer() {
        const bool soft = gSettings.videoRenderer == QLatin1String("software");
        return Row::flip(QStringLiteral("RENDERER"), soft ? QStringLiteral("SOFTWARE") : QStringLiteral("OPENGL"),
                         [this, soft](int) {
            gSettings.videoRenderer = soft ? QStringLiteral("opengl") : QStringLiteral("software");
            commit();
            say(en() ? "Renderer applies after restart. SOFTWARE works on any PC but has no shaders / CRT."
                     : "렌더러는 다시 시작해야 적용됩니다. SOFTWARE 는 어떤 PC 에서도 보이지만 셰이더·CRT 는 못 씁니다.");
        });
    }
    void applyCrt() {
        if (m_h.canvas)
            m_h.canvas->setCrtMode(gSettings.videoCrtMode, gSettings.videoCrtIntensity);
    }
    // 플래시 감소(눈 보호): 화면이 갑자기 밝아지는 순간을 억제한다
    void applyFlash() {
        if (m_h.canvas)
            m_h.canvas->setFlashGuard(gSettings.videoFlashGuard,
                                      gSettings.videoFlashStrength / 100.0f);
    }
    static QString onOff(bool b) { return b ? QStringLiteral("ON") : QStringLiteral("OFF"); }

    // bool 설정 하나를 뒤집는 행. after 는 뒤집은 직후에 할 일.
    Row toggle(const QString& label, bool& flag, std::function<void()> after) {
        return Row::flip(label, onOff(flag), [this, &flag, after](int) {
            flag = !flag;
            if (after) after();
            commit();
        });
    }

    // ── 개별 행 ──────────────────────────────────────────
    Row scaleMode() {
        // 저장값(Fill/Fit/1:1)과 화면 표시(FULL/4:3/1:1)는 다르다.
        //   저장값을 바꾸면 예전 config.json 이 깨진다.
        static const char* stored[3] = { "Fill", "Fit", "1:1" };
        static const char* shown [3] = { "FULL", "4:3", "1:1" };
        int cur = 0;
        for (int i = 0; i < 3; ++i)
            if (gSettings.videoScaleMode == QLatin1String(stored[i])) cur = i;
        return Row::value_(QStringLiteral("SCALE MODE"), QLatin1String(shown[cur]),
                           [this](int d) {
            int c = 0;
            for (int i = 0; i < 3; ++i)
                if (gSettings.videoScaleMode == QLatin1String(stored[i])) c = i;
            gSettings.videoScaleMode = QLatin1String(stored[wrap(c, d, 3)]);
            if (m_h.canvas) m_h.canvas->setScaleMode(gSettings.videoScaleMode);
            commit();
        });
    }

    Row crtLevel() {
        const int pct = qRound(gSettings.videoCrtIntensity * 100);
        return Row::value_(QStringLiteral("CRT LEVEL"),
                           QString::number(pct) + QStringLiteral("%"), [this](int d) {
            const int v = clampStep(qRound(gSettings.videoCrtIntensity * 100), d, 5, 0, 100);
            gSettings.videoCrtIntensity = v / 100.0;
            applyCrt();
            commit();
        });
    }

    Row flashLevel() {
        return Row::value_(QStringLiteral("FLASH LEVEL"),
                           QString::number(gSettings.videoFlashStrength) + QStringLiteral("%"),
                           [this](int d) {
            gSettings.videoFlashStrength =
                clampStep(gSettings.videoFlashStrength, d, 5, 0, 100);
            applyFlash();
            commit();
        });
    }

    Row fullscreen() {
        const bool on = m_h.isFullscreen && m_h.isFullscreen();
        return Row::flip(QStringLiteral("FULLSCREEN"), onOff(on), [this](int) {
            if (m_h.toggleFullscreen) m_h.toggleFullscreen();
        });
    }

    Row frameskip() {
        const int v = gSettings.videoFrameskip;
        return Row::value_(QStringLiteral("FRAMESKIP"),
                           v < 0 ? QStringLiteral("AUTO") : QString::number(v),
                           [this](int d) {
            // -1(AUTO) 0 1 2 3 4 5 를 돌린다
            gSettings.videoFrameskip = wrap(gSettings.videoFrameskip + 1, d, 7) - 1;
            commit();
        });
    }

    Row shaderFile() {
        const QString& p = gSettings.videoShaderPath;
        return Row::action(QStringLiteral("SHADER"), [this] { pickShader(); },
                           p.isEmpty() ? QStringLiteral("NONE")
                                       : QFileInfo(p).fileName().toUpper());
    }

    // 셰이더 파일 선택: RetroArch 셰이더를 그대로 쓴다 (.glsl / .slang / .slangp).
    //   기본 위치는 shaders/ 폴더. 컴파일이 실패하면 앱이 설정을 되돌리고 안내한다.
    void pickShader() {
        QString startDir = gSettings.videoShaderPath;
        if (startDir.isEmpty()) startDir = AppSettings::baseDir() + QStringLiteral("/shaders");
        const QString p = QFileDialog::getOpenFileName(
            m_h.window, en() ? QStringLiteral("Select shader") : QStringLiteral("셰이더 선택"), startDir,
            en() ? QStringLiteral("Shaders (*.glsl *.slang *.slangp *.vert *.frag);;"
                                  "GLSL (*.glsl *.vert *.frag);;slang (*.slang *.slangp);;All Files (*)")
                 : QStringLiteral("셰이더 (*.glsl *.slang *.slangp *.vert *.frag);;"
                                  "GLSL (*.glsl *.vert *.frag);;slang (*.slang *.slangp);;모든 파일 (*)"));
        if (p.isEmpty()) return;
        if (m_h.video.setShader) m_h.video.setShader(p);
    }

    // ── 베젤 이미지 ───────────────────────────────────────
    //   어디에 적용할지(이 게임 / 이 기종 / 모든 게임) 고르고, 그 범위에 파일을 배정한다.
    QVector<Row> bezelRows() {
        static const char* kScope[3] = { "game", "plat", "all" };
        const VideoApi& api = m_h.video;
        QVector<Row> out;
        const QString scope = QLatin1String(kScope[m_bezelScope]);
        out << Row::value_(en() ? "BEZEL APPLIES TO" : "베젤 적용 범위",
                           api.bezelScopeLabel ? api.bezelScopeLabel(scope) : scope,
                           [this](int d) { m_bezelScope = wrap(m_bezelScope, d, 3); });
        out << Row::action(en() ? "CHOOSE BEZEL IMAGE" : "베젤 이미지 선택", [this] { pickBezel(); });
        out << Row::action(en() ? "CLEAR BEZEL ASSIGNMENT" : "베젤 배정 해제", [this] {
            const QString key = bezelKey();
            if (key.isEmpty()) return;
            if (m_h.video.clearBezel) m_h.video.clearBezel(key);
        });
        if (api.bezelInfo) out << Row::note(api.bezelInfo());
        out << Row::note(en() ? "OR BY FILE NAME: bezels/{rom}.png > {platform}.png > default.png"
                              : "또는 파일 이름: bezels/{롬}.png > {기종}.png > default.png");
        return out;
    }

    QString bezelKey() const {
        static const char* kScope[3] = { "game", "plat", "all" };
        return m_h.video.bezelKey ? m_h.video.bezelKey(QLatin1String(kScope[m_bezelScope])) : QString();
    }

    void pickBezel() {
        const QString dir = AppSettings::baseDir() + QStringLiteral("/bezels");
        const QString f = QFileDialog::getOpenFileName(
            m_h.window, en() ? QStringLiteral("Choose bezel image") : QStringLiteral("베젤 이미지 선택"), dir,
            en() ? QStringLiteral("Images (*.png *.jpg *.bmp)") : QStringLiteral("이미지 (*.png *.jpg *.bmp)"));
        if (f.isEmpty()) return;
        const QString key = bezelKey();
        if (key.isEmpty()) {
            say(en() ? "Select a game first" : "게임을 먼저 선택하세요");
            return;
        }
        // bezels/ 안의 파일이면 파일명만 저장한다 (폴더를 옮겨도 유지되도록)
        const QFileInfo fi(f);
        const QString value = (fi.absolutePath() == QDir(dir).absolutePath()) ? fi.fileName() : f;
        if (m_h.video.assignBezel) m_h.video.assignBezel(key, value);
    }

    int m_bezelScope = 2;      // 0=이 게임 1=이 기종 2=모든 게임 (기본은 모든 게임)
};

}  // namespace

std::unique_ptr<ShellPage> makeVideoPage(const ShellHost& host) {
    return std::make_unique<VideoPage>(host);
}
