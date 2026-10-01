// ShellPageMachine.cpp — MACHINE SETTINGS 카테고리 (DIP 스위치)
//
//  코어가 SET_VARIABLES 로 알려 준 옵션이 DIP 스위치다. 값을 고르면
//    1. 코어 변수를 즉시 바꾸고(다음 프레임에 반영),
//    2. 선택한 범위(이 게임 / 이 기종 전체)에 저장한다.
//  다음에 같은 게임을 실행하면 저장한 값이 자동으로 복원된다(복원은 게임 로드 쪽).
//
//  Region 콤보는 없다. 지역을 실제로 바꾸는 것은 게임의 DIP 스위치 안에 있는 Region 항목이다.

#include "ShellPages.h"

#include <QStringList>
#include <algorithm>

#include "AppSettings.h"
#include "EmulatorState.h"
#include "NativeCheats.h"

namespace {

class MachinePage final : public ShellPage {
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("machine"); }

    QVector<Row> rows() override {
        const QStringList keys = dipKeys();

        if (keys.isEmpty()) {
            return { Row::note(en() ? "START A GAME TO SEE DIP SWITCHES"
                                    : "게임을 실행하면 DIP 스위치가 표시됩니다") };
        }

        QVector<Row> out;
        out << scopeRow();
        out << Row::note(en() ? "SAVED NOW  SOME NEED A RESET"
                              : "즉시 저장  일부는 리셋 후 반영");
        for (const QString& key : keys) out << dipRow(key);
        return out;
    }

private:
    enum class Scope { Game, Platform };
    Scope m_scope = Scope::Game;

    // DIP 스위치 키 목록 (정렬). 치트 옵션은 CHEATS 카테고리가 따로 다룬다.
    static QStringList dipKeys() {
        QStringList keys;
        for (auto it = gState.variableOptions.constBegin();
             it != gState.variableOptions.constEnd(); ++it) {
            if (it.value().isEmpty()) continue;
            if (nativecheats::isNativeCheatKey(it.key())) continue;
            keys << it.key();
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    QString rom() const { return m_h.loadedGame ? m_h.loadedGame() : QString(); }
    QString platform() const {
        const QString r = rom();
        return (m_h.platformOf && !r.isEmpty()) ? m_h.platformOf(r) : QString();
    }

    Row scopeRow() {
        const QString shown = (m_scope == Scope::Platform && !platform().isEmpty())
                            ? platform().toUpper() + QStringLiteral(" ALL")
                            : QStringLiteral("THIS GAME");
        return Row::flip(QStringLiteral("SAVE SCOPE"), shown, [this](int) {
            // 기종을 모르면 "기종 전체" 를 고를 수 없다
            if (platform().isEmpty()) { m_scope = Scope::Game; return; }
            m_scope = (m_scope == Scope::Game) ? Scope::Platform : Scope::Game;
            say(m_scope == Scope::Platform
                    ? (en() ? "DIP scope: platform" : "머신세팅 저장 범위: 기종별")
                    : (en() ? "DIP scope: this game" : "머신세팅 저장 범위: 게임별"));
        });
    }

    Row dipRow(const QString& key) {
        const QStringList options = gState.variableOptions.value(key);
        const QString desc = gState.variableDescriptions.value(key, key);
        const QString cur  = gState.variables.value(key, options.first());

        return Row::pick(options.size() == 2, desc, cur, [this, key](int d) {
            const QStringList opts = gState.variableOptions.value(key);
            if (opts.isEmpty()) return;
            const QString now = gState.variables.value(key, opts.first());
            int idx = opts.indexOf(now);
            if (idx < 0) idx = 0;
            const QString next = opts.at(wrap(idx, d, int(opts.size())));

            // 코어 변수를 바꾸면 다음 프레임에 코어가 읽어 간다
            gState.variables[key] = next;
            gState.variablesUpdated.store(true);

            // 게임을 모르면(=실행 중이 아니면) 저장하지 않는다. 빈 키로 남으면 쓰레기가 된다.
            const QString r = rom();
            if (r.isEmpty()) return;
            if (m_scope == Scope::Platform && !platform().isEmpty())
                gSettings.machineVarsByPlatform[platform()][key] = next;
            else
                gSettings.machineVars[r][key] = next;
            gSettings.save();
        });
    }
};

}  // namespace

std::unique_ptr<ShellPage> makeMachinePage(const ShellHost& host) {
    return std::make_unique<MachinePage>(host);
}
