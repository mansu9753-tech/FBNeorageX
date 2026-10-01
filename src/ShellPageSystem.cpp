// ShellPageSystem.cpp — SYSTEM 카테고리
//
//  옛 홈 화면 아래쪽에 있던 것들이다: 세이브 슬롯, SAVE / LOAD, SHOT, REC, FF,
//  그리고 EN/KO 언어 전환. 새 메뉴로 옮기면서 갈 곳이 없어 빠질 뻔했다.
//  거기에 옛 AUDIO 페이지 구석에 있던 "설정 초기화" 를 더했다.
//
//  대부분 F1~F12 핫키로도 되지만, 스팀덱처럼 키보드가 없는 환경에서는
//  이 메뉴가 유일한 수단이다.

#include "ShellPages.h"

#include <QElapsedTimer>

#include "AppSettings.h"
#include "EmulatorState.h"

namespace {

class SystemPage final : public ShellPage {
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("system"); }

    QVector<Row> rows() override {
        return {
            slot(),
            Row::action(QStringLiteral("SAVE STATE"),
                        [this] { if (m_h.saveState) m_h.saveState(); }),
            Row::action(QStringLiteral("LOAD STATE"),
                        [this] { if (m_h.loadState) m_h.loadState(); }),
            Row::action(QStringLiteral("SCREENSHOT"),
                        [this] { if (m_h.takeScreenshot) m_h.takeScreenshot(); }),
            recording(),
            fastForward(),
            Row::flip(QStringLiteral("1P / 2P PORT"),
                        (m_h.swapPlayers && m_h.swapPlayers()) ? QStringLiteral("2P") : QStringLiteral("1P"),
                        [this](int) { if (m_h.toggleSwap) m_h.toggleSwap(); }),
            Row::value_(QStringLiteral("TATE ROTATE"),
                        m_h.tateLabel ? m_h.tateLabel() : QStringLiteral("AUTO"),
                        [this](int) { if (m_h.toggleTate) m_h.toggleTate(); }),
            Row::action(QStringLiteral("RESET GAME"),
                        [this] { if (m_h.resetGame) m_h.resetGame(); }),
            Row::action(QStringLiteral("STOP GAME"),
                        [this] { if (m_h.stopGame) m_h.stopGame(); }),
            language(),
            intro(),
            resetDefaults(),
        };
    }

private:
    static QString onOff(bool b) { return b ? QStringLiteral("ON") : QStringLiteral("OFF"); }

    Row slot() {
        const int cur = m_h.stateSlot ? m_h.stateSlot() : 1;
        return Row::value_(QStringLiteral("SAVE SLOT"), QString::number(cur), [this](int d) {
            if (!m_h.stateSlot || !m_h.setStateSlot) return;
            // 슬롯은 1~8 이다
            m_h.setStateSlot(wrap(m_h.stateSlot() - 1, d, 8) + 1);
        });
    }

    Row recording() {
        const bool on = m_h.isRecording && m_h.isRecording();
        return Row::flip(QStringLiteral("RECORD"), onOff(on), [this](int) {
            if (m_h.toggleRecording) m_h.toggleRecording();
        });
    }

    Row fastForward() {
        return Row::flip(QStringLiteral("FAST FORWARD"), onOff(gState.fastForward),
                           [](int) { gState.fastForward = !gState.fastForward; });
    }

    Row intro() {
        return Row::flip(QStringLiteral("STARTUP INTRO"), onOff(gSettings.showIntro), [](int) {
            gSettings.showIntro = !gSettings.showIntro;
            gSettings.save();
        });
    }

    Row language() {
        return Row::flip(QStringLiteral("LANGUAGE"),
                           en() ? QStringLiteral("ENGLISH") : QStringLiteral("KOREAN"),
                           [this](int) { if (m_h.toggleLanguage) m_h.toggleLanguage(); });
    }

    // ── 설정 초기화 (되돌릴 수 없으므로 두 번 눌러야 실행) ─────
    //   별도 확인 창을 띄우지 않고 이 자리에서 확인한다. 첫 번째로 누르면 문구가
    //   바뀌고, 5초 안에 다시 누르면 실행한다.
    QElapsedTimer m_armedAt;
    bool armed() const { return m_armedAt.isValid() && m_armedAt.elapsed() < 5000; }

    Row resetDefaults() {
        const QString label = armed()
            ? (en() ? QStringLiteral("PRESS AGAIN TO CONFIRM")
                    : QStringLiteral("한 번 더 누르면 초기화"))
            : QStringLiteral("RESET DEFAULTS");
        return Row::action(label, [this] {
            if (!armed()) { m_armedAt.start(); return; }
            m_armedAt.invalidate();

            gSettings.resetToDefaults();
            gSettings.save();
            if (m_h.applyLiveSettings)  m_h.applyLiveSettings();
            if (m_h.applyBezel)         m_h.applyBezel();
            if (m_h.applySoundMode)     m_h.applySoundMode();
            say(en() ? "Settings reset to defaults" : "설정이 기본값으로 초기화됨");
        });
    }
};

}  // namespace

std::unique_ptr<ShellPage> makeSystemPage(const ShellHost& host) {
    return std::make_unique<SystemPage>(host);
}
