// ShellPageCheats.cpp — CHEATS 카테고리
//
//  치트는 두 종류가 있고 서로 배타적이다.
//    1. 코어 네이티브 치트 (fbneo-cheat-*): 코어가 CheatEnable 로 직접 적용한다.
//       주소·엔디언을 추측할 필요가 없어 기종과 무관하게 정확하다.
//    2. 수동 치트 엔진(CheatManager): INI 를 읽어 RAM 을 직접 쓴다.
//  네이티브 치트가 있으면 수동 엔진은 쓰지 않는다. 둘 다 돌리면 이중 적용으로
//  충돌한다. (그 스위치는 메인 루프가 nativecheats::any() 로 판단한다)

#include "ShellPages.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>

#include "AppSettings.h"
#include "CheatManager.h"
#include "EmulatorState.h"
#include "NativeCheats.h"

namespace {

class CheatsPage final : public ShellPage {
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("cheats"); }

    QVector<Row> rows() override {
        if (nativecheats::any()) return nativeRows();
        return manualRows();
    }

private:
    // ── 코어 네이티브 치트 ────────────────────────────────
    QVector<Row> nativeRows() {
        const QStringList keys = nativecheats::keys();
        QVector<Row> out;
        out << Row::note(QStringLiteral("%1 %2")
                             .arg(keys.size())
                             .arg(en() ? "CORE CHEATS (FBNEO ENGINE)"
                                       : "코어 네이티브 치트 (FBNEO 엔진)"));

        for (const QString& key : keys) {
            const QStringList opts = gState.variableOptions.value(key);
            if (opts.isEmpty()) continue;
            // 코어가 붙이는 "[Cheat][dino.ini] " 머리말은 지운다 (이미 이 게임의 치트만 보이므로 의미가 없다)
            static const QRegularExpression kTag(QStringLiteral(R"(^(\[[^\]]*\]\s*)+)"));
            QString desc = gState.variableDescriptions.value(key, key);
            desc.remove(kTag);
            if (desc.isEmpty()) desc = key;
            const QString cur  = gState.variables.value(key, opts.first());

            // 선택지가 둘뿐이면(끔/켬) 좌우 화살표는 의미가 없다 → 누르면 뒤집는 행으로 만든다.
            //   셋 이상이면(단계 선택) 기존처럼 ◀ ▶ 로 고른다.
            auto step = [this, key, desc](int d) {
                const QStringList o = gState.variableOptions.value(key);
                if (o.isEmpty()) return;
                int idx = o.indexOf(gState.variables.value(key, o.first()));
                if (idx < 0) idx = 0;
                const QString next = o.at(wrap(idx, d, int(o.size())));
                gState.variables[key] = next;
                gState.variablesUpdated.store(true);   // 코어가 다음 프레임에 반영
                say(QStringLiteral("🧩 치트: ") + desc + QStringLiteral(" → ") + next);
            };
            if (opts.size() == 2) out << Row::action(desc, [step] { step(+1); }, cur);
            else                  out << Row::value_(desc, cur, step);
        }
        return out;
    }

    // ── 수동 치트 엔진 ───────────────────────────────────
    QVector<Row> manualRows() {
        QVector<Row> out;
        CheatManager* cm = m_h.cheat;

        if (!cm || cm->count() == 0) {
            out << Row::note(emptyStatus());
            out << loadRow();
            return out;
        }

        out << Row::note(QStringLiteral("%1 %2 - %3")
                             .arg(cm->count())
                             .arg(en() ? "CHEATS" : "개")
                             .arg(QFileInfo(cm->loadedPath()).fileName().toUpper()));
        out << Row::action(QStringLiteral("ENABLE ALL"),  [this] { setAll(true);  });
        out << Row::action(QStringLiteral("DISABLE ALL"), [this] { setAll(false); });

        for (int i = 0; i < cm->count(); ++i) {
            const CheatEntry& e = cm->entries().at(i);
            // 적용/미적용 두 가지뿐이라 화살표 없이 누르면 뒤집는다
            out << Row::action(entryName(e), [this, i] { toggle(i); },
                               e.active ? QStringLiteral("ON") : QStringLiteral("OFF"));
        }
        out << loadRow();
        return out;
    }

    // "그룹 이름 > 옵션" 형식. 옵션이 그냥 Enabled 면 그룹 이름만 쓴다.
    static QString entryName(const CheatEntry& e) {
        const QString main = e.description.isEmpty() ? e.label : e.description;
        const bool showLabel = !e.label.isEmpty() && !e.description.isEmpty()
                            && !e.label.contains(QLatin1String("enabled"), Qt::CaseInsensitive);
        return showLabel ? main + QStringLiteral(" > ") + e.label : main;
    }

    QString emptyStatus() const {
        const bool running = gState.gameLoaded
                          || (m_h.loadedGame && !m_h.loadedGame().isEmpty());
        if (!running)
            return en() ? "CHEATS LOAD WHEN A GAME STARTS"
                        : "게임을 실행하면 치트가 자동 로드됩니다";
        const QString r = m_h.selectedGame ? m_h.selectedGame() : QString();
        return (en() ? "NO CHEATS (" : "치트 없음 (") + r + QStringLiteral(".ini)");
    }

    Row loadRow() {
        return Row::action(QStringLiteral("LOAD INI FILE..."), [this] {
            if (!m_h.cheat) return;
            const QString path = QFileDialog::getOpenFileName(
                m_h.window, en() ? "Select cheat INI" : "치트 INI 선택",
                gSettings.cheatPath, "Cheat INI (*.ini);;All Files (*)");
            if (!path.isEmpty()) m_h.cheat->loadIni(path);   // 로드 결과는 시그널로 로그에 남는다
        });
    }

    // ── 동작 ─────────────────────────────────────────────
    void toggle(int i) {
        CheatManager* cm = m_h.cheat;
        if (!cm || i < 0 || i >= cm->count()) return;
        const bool now = !cm->entries().at(i).active;
        cm->setActive(i, now);
        say((now ? QStringLiteral("치트 ON: ") : QStringLiteral("치트 OFF: "))
            + entryName(cm->entries().at(i)));
    }

    void setAll(bool on) {
        CheatManager* cm = m_h.cheat;
        if (!cm) return;
        for (int i = 0; i < cm->count(); ++i) cm->setActive(i, on);
        say(on ? QStringLiteral("치트 %1개 전체 활성화").arg(cm->count())
               : QStringLiteral("치트 %1개 전체 비활성화").arg(cm->count()));
    }
};

}  // namespace

std::unique_ptr<ShellPage> makeCheatsPage(const ShellHost& host) {
    return std::make_unique<CheatsPage>(host);
}
