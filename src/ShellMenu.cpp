// ShellMenu.cpp — OPTIONS 메뉴 컨트롤러

#include "ShellMenu.h"
#include "NeoRageXShell.h"
#include "ShellText.h"

ShellMenu::ShellMenu(NeoRageXShell* shell, QObject* parent)
    : QObject(parent), m_shell(shell) {
    connect(m_shell, &NeoRageXShell::menuAboutToOpen, this, &ShellMenu::onAboutToOpen);
    connect(m_shell, &NeoRageXShell::subAdjusted,     this, &ShellMenu::onAdjusted);
    connect(m_shell, &NeoRageXShell::optionChosen,    this, &ShellMenu::onChosen);
}

void ShellMenu::addPage(std::unique_ptr<ShellPage> page, const QString& label,
                        bool enabled) {
    Entry e;
    e.page    = std::move(page);
    e.label   = label;
    e.enabled = enabled;
    m_entries.push_back(std::move(e));
}

void ShellMenu::install() {
    QVector<NeoRageXShell::OptionEntry> opts;
    for (const Entry& e : m_entries) {
        NeoRageXShell::OptionEntry o;
        o.id      = e.page->id();
        o.label   = tr(e.label);
        o.enabled = e.enabled;
        opts.append(o);          // 하위 행은 카테고리를 열 때 채운다
    }
    m_shell->setOptions(opts);
}

QString ShellMenu::tr(const QString& s) const {
    return shellText(s, m_korean && m_korean());
}

// 언어가 바뀐 뒤: 카테고리 이름, 하단 버튼, 검색창 안내, 열려 있는 하위 행을 새 언어로.
void ShellMenu::retranslate() {
    QStringList labels;
    for (const Entry& e : m_entries) labels << tr(e.label);
    m_shell->setOptionLabels(labels);
    m_shell->setButtonLabels({ tr(QStringLiteral("LAUNCH")), tr(QStringLiteral("IMPORT")),
                               tr(QStringLiteral("EXIT")) });
    m_shell->setSearchPlaceholder(tr(QStringLiteral("SEARCH")));
    m_shell->setBackLabel(tr(QStringLiteral("BACK")));
    m_shell->setUnifiedFont(m_korean && m_korean());
    refreshOpen();
}

ShellMenu::Entry* ShellMenu::find(const QString& id) {
    for (Entry& e : m_entries)
        if (e.page->id() == id) return &e;
    return nullptr;
}

// 페이지에서 행을 새로 만들어 셸에 밀어 넣는다.
//   행에는 동작(클로저)이 붙어 있으므로 화면에 올린 것과 같은 목록을
//   e.rows 에 들고 있어야 나중에 번호로 동작을 찾을 수 있다.
void ShellMenu::rebuild(Entry& e) {
    e.rows = e.page->rows();

    QVector<NeoRageXShell::SubItem> items;
    items.reserve(e.rows.size());
    for (const ShellPage::Row& r : e.rows) {
        NeoRageXShell::SubItem it;
        it.label      = tr(r.label);
        it.value      = tr(r.value);
        it.adjustable = bool(r.adjust);
        it.info       = r.info;
        it.heading    = r.isHeading;
        it.elideLeft  = r.elideLeft;
        items.append(it);
    }
    m_shell->setSubItems(e.page->id(), items);
}

void ShellMenu::onAboutToOpen(const QString& id) {
    if (Entry* e = find(id)) rebuild(*e);
}

void ShellMenu::refreshOpen() {
    const QString id = m_shell->openCategoryId();
    if (id.isEmpty()) return;
    if (Entry* e = find(id)) rebuild(*e);
}

void ShellMenu::onAdjusted(const QString& id, int index, int delta) {
    Entry* e = find(id);
    if (!e || index < 0 || index >= e->rows.size()) return;

    // 동작 안에서 rows 가 다시 만들어져도 실행 중인 클로저가 사라지지 않게
    // 복사본으로 실행한다.
    const auto fn = e->rows[index].adjust;
    if (!fn) return;
    fn(delta);
    rebuild(*e);
}

void ShellMenu::onChosen(const QString& id, int index) {
    Entry* e = find(id);
    if (!e) return;

    // 하위 행이 없는 카테고리를 골랐다 (올라가기 연출이 끝난 뒤 온다)
    if (index < 0) { e->page->openStandalone(); return; }

    if (index >= e->rows.size()) return;
    const auto fn = e->rows[index].activate;
    if (!fn) return;
    fn();
    // 페이지가 창을 열어 카테고리가 닫혔다면 다시 그릴 필요가 없다
    if (m_shell->openCategoryId() == id) rebuild(*e);
}
