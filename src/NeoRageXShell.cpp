// NeoRageXShell.cpp — NeoRageX 0.6b 메뉴 셸

#include "NeoRageXShell.h"

#include <QGuiApplication>
#include <QInputMethod>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

using namespace nrx;

namespace {
inline double clamp01(double t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
constexpr double STEP_MS = 1000.0 / 60.0;
}

NeoRageXShell::NeoRageXShell(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    m_options = {
        { "controls", "CONTROLS",         true,  {} },
        { "dirs",     "DIRECTORIES",      true,  {} },
        { "video",    "VIDEO OPTIONS",    true,  {} },
        { "audio",    "AUDIO OPTIONS",    true,  {} },
        { "machine",  "MACHINE SETTINGS", true,  {} },
        { "shots",    "SHOTS FACTORY",    false, {} },
    };

    chooseResolution();

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(8);
    connect(m_timer, &QTimer::timeout, this, &NeoRageXShell::tick);
}

NeoRageXShell::~NeoRageXShell() = default;

// ── 내용 ─────────────────────────────────────────────────────
void NeoRageXShell::setGames(const QStringList& games, const QVector<bool>& favs) {
    m_games = games;
    m_favorites = favs;
    if (m_sel >= m_games.size()) m_sel = m_games.isEmpty() ? -1 : m_games.size() - 1;
    m_top = 0;
    ensureSelectionVisible();
    update();
}

void NeoRageXShell::setOptionLabels(const QStringList& labels) {
    for (int i = 0; i < m_options.size() && i < labels.size(); ++i) m_options[i].label = labels[i];
    update();
}

void NeoRageXShell::setButtonLabels(const QStringList& labels) {
    m_btnLabels = labels;
    update();
}

void NeoRageXShell::log(const QString& line) {
    m_events.append(PixelFont::sanitize(line));
    if (m_events.size() > 400) m_events.removeFirst();
    update();
}

void NeoRageXShell::setBackdrop(const QImage& img) { m_backdrop = img; update(); }
void NeoRageXShell::setPreview(const QImage& img)  { m_preview  = img; update(); }

void NeoRageXShell::setOptions(const QVector<OptionEntry>& opts) {
    m_options = opts;
    m_openIdx = -1;
    m_menuFrame = -1;
    update();
}

void NeoRageXShell::setFilterTabs(const QVector<FilterTab>& tabs, const QString& cur) {
    m_filters = tabs;
    m_filterCur = cur;
    update();
}

QString NeoRageXShell::selectedGame() const {
    return (m_sel >= 0 && m_sel < m_games.size()) ? m_games.at(m_sel) : QString();
}

void NeoRageXShell::setSelectedIndex(int i, bool notify) {
    if (m_games.isEmpty()) { m_sel = -1; return; }
    m_sel = qBound(0, i, m_games.size() - 1);
    ensureSelectionVisible();
    if (notify) emit gameHighlighted(selectedGame(), m_sel);
    update();
}

void NeoRageXShell::ensureSelectionVisible() {
    const int rows = rowsVisible();
    if (m_sel < 0) return;
    if (m_sel < m_top) m_top = m_sel;
    if (m_sel >= m_top + rows) m_top = m_sel - rows + 1;
    m_top = qBound(0, m_top, qMax(0, m_games.size() - rows));
}

void NeoRageXShell::ensureSubVisible() {
    if (m_openIdx < 0) return;
    const int n = m_options[m_openIdx].sub.size();
    const int rows = subRowsVisible();
    m_subSel = qBound(0, m_subSel, qMax(0, n - 1));
    if (m_subSel < m_subTop) m_subTop = m_subSel;
    if (m_subSel >= m_subTop + rows) m_subTop = m_subSel - rows + 1;
    m_subTop = qBound(0, m_subTop, qMax(0, n - rows));
}

// ── 동작 ─────────────────────────────────────────────────────
void NeoRageXShell::boot() {
    m_frame = 0;
    m_menuFrame = -1;
    m_openIdx = -1;
    if (!m_running) {
        m_running = true;
        m_clock.start();
        m_accum = 0.0;
        m_timer->start();
    }
    update();
}

void NeoRageXShell::skipBoot() { m_frame = bootEnd(); update(); }
void NeoRageXShell::stop() { m_running = false; m_timer->stop(); }
bool NeoRageXShell::isIdle() const { return m_frame >= bootEnd(); }

void NeoRageXShell::openMenu(int i) {
    if (i < 0 || i >= m_options.size() || !m_options[i].enabled) return;
    m_openIdx  = i;
    m_optSel   = i;
    m_zone     = Zone::Options;
    m_hoverSub = -1;
    m_subTop   = 0;
    m_subSel   = 0;
    emit menuAboutToOpen(m_options[i].id);   // 지금 설정값으로 하위 항목을 채우게 한다
    snapSubSel(+1);
    m_menuFrame = 0;
    if (!m_running) { m_running = true; m_clock.start(); m_accum = 0; m_timer->start(); }
    update();
}

QString NeoRageXShell::openCategoryId() const {
    return (m_openIdx >= 0 && m_openIdx < m_options.size())
               ? m_options[m_openIdx].id : QString();
}

void NeoRageXShell::setSubItems(const QString& categoryId,
                                const QVector<SubItem>& items) {
    for (int i = 0; i < m_options.size(); ++i) {
        OptionEntry& o = m_options[i];
        if (o.id != categoryId) continue;
        o.sub = items;
        // 지금 열려 있는 목록이면 줄 수가 달라졌을 수 있으니 선택·스크롤을 다시 맞춘다
        if (i == m_openIdx) { snapSubSel(); ensureSubVisible(); }
        update();
        return;
    }
}

// i 번째 줄 또는 그 뒤에 선택할 수 있는 줄이 있는가
bool NeoRageXShell::subSelectableFrom(int i) const {
    if (m_openIdx < 0) return false;
    const int n = int(m_options[m_openIdx].sub.size());
    for (int k = qMax(0, i); k < n; ++k) if (subSelectable(k)) return true;
    return false;
}

bool NeoRageXShell::subSelectable(int i) const {
    if (m_openIdx < 0) return false;
    const auto& sub = m_options[m_openIdx].sub;
    return i >= 0 && i < sub.size() && !sub.at(i).info;
}

// 선택이 안내 줄에 걸려 있으면 dir 방향으로 가장 가까운 선택 가능 항목으로 옮긴다.
void NeoRageXShell::snapSubSel(int dir) {
    if (m_openIdx < 0) return;
    const int n = m_options[m_openIdx].sub.size();
    if (n == 0) { m_subSel = 0; return; }
    m_subSel = qBound(0, m_subSel, n - 1);
    if (subSelectable(m_subSel)) return;
    for (int i = m_subSel; i >= 0 && i < n; i += dir)
        if (subSelectable(i)) { m_subSel = i; return; }
    for (int i = m_subSel; i >= 0 && i < n; i -= dir)   // 반대쪽에서라도 찾는다
        if (subSelectable(i)) { m_subSel = i; return; }
}

// 옵션 항목이 맨 위로 올라가는 연출이 끝나 하위 목록과 단추가 보이는 상태인가
bool NeoRageXShell::menuOpenDone() const {
    return m_menuFrame >= TIMING.rise + TIMING.riseGap;
}

void NeoRageXShell::closeMenu() {
    m_menuFrame = -1;
    m_openIdx = -1;
    m_hoverSub = -1;
    m_hoverSpin = 0;
    m_hoverBack = 0;
    update();
}

// ── 타임라인 ─────────────────────────────────────────────────
NeoRageXShell::Phases NeoRageXShell::phases() const {
    const Timing& t = TIMING;
    Phases p;
    p.bg   = t.backdrop;
    p.box  = p.bg + t.box;
    p.hold = p.box + t.hold;
    p.tit  = p.hold + t.titles;
    return p;
}

int NeoRageXShell::filterRowsH() const {
    if (m_filters.isEmpty()) return 0;
    return bodyCellH() + 8 * m_L.s;
}

// 필터 단추 줄 아래에 검색창이 한 줄 붙는다.
int NeoRageXShell::filterBarH() const {
    return filterRowsH() + bodyCellH() + 8 * m_L.s;
}

QRect NeoRageXShell::searchRect() const {
    const Layout& L = m_L;
    const int s = L.s;
    const int x = L.gamelist.x() + L.border + 2 * s;
    const int y = L.gamelist.y() + L.border + filterRowsH() + s;
    const int right = L.scrollbar.x - 2 * s;
    return QRect(x, y, qMax(1, right - x), bodyCellH() + 4 * s);
}

bool NeoRageXShell::hitSearch(const QPoint& lp) const { return searchRect().contains(lp); }

// 검색어가 있을 때 오른쪽 끝의 x 자리
bool NeoRageXShell::hitSearchClear(const QPoint& lp) const {
    if (m_search.isEmpty()) return false;
    const QRect r = searchRect();
    return QRect(r.right() - bodyCellW() - 4 * m_L.s, r.y(), bodyCellW() + 4 * m_L.s, r.height()).contains(lp);
}

void NeoRageXShell::setSearchFocus(bool on) {
    if (m_searchFocus == on) return;
    m_searchFocus = on;
    if (!on) m_preedit.clear();
    setAttribute(Qt::WA_InputMethodEnabled, on);
    if (QGuiApplication::inputMethod()) QGuiApplication::inputMethod()->update(Qt::ImEnabled);
    update();
}

void NeoRageXShell::editSearch(const QString& t) {
    if (t == m_search) return;
    m_search = t;
    update();
    emit searchChanged(m_search);
}

// ── 검색창 ───────────────────────────────────────────────────
//   필터 단추 바로 아래. 어두운 반투명 상자에 얇은 파란 테두리, 입력 중에는 테두리가 밝아지고
//   글자 뒤에 커서(_)가 붙는다. 비어 있으면 안내 글자만 흐리게 보인다.
void NeoRageXShell::drawSearchBar(QPainter& p) {
    if (!seen(contentFrame(), TIMING.listLead)) return;
    const Layout& L = m_L;
    const int s = L.s;
    const QRect r = searchRect();

    p.fillRect(r, QColor(0, 0, 0, 120));
    const QColor edge = m_searchFocus ? QColor(96, 160, 255) : QColor(48, 84, 180);
    const int t = qMax(1, s / 2);
    p.fillRect(QRect(r.x(), r.y(), r.width(), t), edge);
    p.fillRect(QRect(r.x(), r.bottom() - t + 1, r.width(), t), edge);
    p.fillRect(QRect(r.x(), r.y(), t, r.height()), edge);
    p.fillRect(QRect(r.right() - t + 1, r.y(), t, r.height()), edge);

    const int cw = bodyCellW();
    const int tx = r.x() + 3 * s;
    const int ty = r.y() + (r.height() - bodyCellH()) / 2;
    const int clearW = m_search.isEmpty() ? 0 : cw + 4 * s;
    const int cells = qMax(1, (r.width() - 6 * s - clearW) / cw);

    if (m_search.isEmpty() && m_preedit.isEmpty() && !m_searchFocus) {
        text(p, m_searchHint, tx, ty, COLOR.textDim, bodyScale());
        return;
    }
    QString shown = m_search + m_preedit + (m_searchFocus ? QStringLiteral("_") : QString());
    text(p, PixelFont::fitTail(shown, cells), tx, ty, COLOR.text, bodyScale());
    if (!m_search.isEmpty())
        text(p, QStringLiteral("x"), r.right() - cw - 3 * s, ty, COLOR.textDim, bodyScale());
}

QRect NeoRageXShell::listArea() const {
    const QRect& b = m_L.gamelist;
    const int top = b.y() + m_L.border + filterBarH();
    return QRect(b.x() + m_L.border, top,
                 m_L.scrollbar.x - (b.x() + m_L.border),
                 (b.y() + b.height() - m_L.border) - top);
}

int NeoRageXShell::rowsVisible() const {
    return qMax(1, listArea().height() / listPitch());
}

int NeoRageXShell::eventRowsVisible() const {
    const QRect& b = m_L.events;
    return qMax(1, (b.height() - 2 * m_L.border) / listPitch());
}

int NeoRageXShell::bootEnd() const {
    const Timing& t = TIMING;
    const Phases p = phases();
    const int a = t.btnLead + 3;
    const int b = t.optLead + m_options.size() * t.optStag;
    const int c = t.listLead + qMin(rowsVisible(), int(m_games.size())) * t.listStag;
    const int d = t.evtLead + qMin(eventRowsVisible(), int(m_events.size())) * t.evtStag;
    return p.tit + qMax(qMax(a, b), qMax(c, d)) + 8;
}

int NeoRageXShell::contentFrame() const { return m_frame - phases().tit; }

void NeoRageXShell::tick() {
    if (!m_running) return;

    // 경과 시간을 60fps 고정 스텝으로 쪼갠다.
    m_accum += qMin(double(m_clock.restart()), 250.0);

    bool advanced = false;
    while (m_accum >= STEP_MS) {
        m_accum -= STEP_MS;
        if (m_menuFrame >= 0) {
            const OptionEntry& o = m_options[m_openIdx];
            const int total = TIMING.rise + TIMING.riseGap
                            + o.sub.size() * TIMING.subStag + 4;
            if (m_menuFrame < total) { ++m_menuFrame; advanced = true; }

            // 하위 항목이 없는 카테고리는 올라가기가 끝나는 순간 알린다
            if (o.sub.isEmpty() && m_menuFrame >= TIMING.rise + TIMING.riseGap) {
                const QString id = o.id;
                closeMenu();
                emit optionChosen(id, -1);
                advanced = true;
                break;
            }
        } else if (m_frame < bootEnd()) {
            ++m_frame;
            advanced = true;
        }
        // 선택한 게임 이름이 칸보다 길면 흘러가게 한다 (그릴 때만 다시 그리면 되므로 그때만 표시)
        if (m_sel != m_marqSel) { m_marqSel = m_sel; m_marqStep = 0; }
        else if (m_frame >= bootEnd() && marqueeOverflow(m_sel) > 0) { ++m_marqStep; advanced = true; }
    }
    if (advanced) update();
}

// ── 해상도 ───────────────────────────────────────────────────
void NeoRageXShell::chooseResolution() {
    // 창 크기 그대로 레이아웃을 푼다. 확대가 없으니 레터박스도 없다.
    const int w = qMax(BASE.W / 2, width());
    const int h = qMax(BASE.H / 2, height());
    const int s = qMax(1, qRound(double(w) / BASE.W));

    if (m_surface.isNull() || m_L.W != w || m_L.H != h || m_L.s != s) {
        m_L = nrx::layout(w, h, s);
        m_surface = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        ensureSelectionVisible();
    }
}

void NeoRageXShell::resizeEvent(QResizeEvent*) { chooseResolution(); update(); }

QPoint NeoRageXShell::toLogical(const QPoint& wp) const { return wp; }

// ── 그리기 도우미 ────────────────────────────────────────────
double NeoRageXShell::quant(double t) const {
    const int n = RULES.paletteSteps;
    return qRound(clamp01(t) * n) / double(n);
}

int NeoRageXShell::textW(const QString& s, int scale) const {
    return PixelFont::width(s, scale);
}

void NeoRageXShell::text(QPainter& p, const QString& s, int x, int y,
                         const QColor& c, int scale, int scaleY, bool retro) {
    m_font.draw(p, s, x, y, c, scale, scaleY, retro);
}

// 바깥 사각형 b 를 o 픽셀 안쪽(음수면 바깥쪽)으로 옮긴 링을, 바깥 둘레 기준 거리 d 까지 그린다.
//   모든 링이 같은 "바깥 좌표" 에서 끝나므로 띠의 끝이 진행 방향에 수직인 단면(형광펜 끝)이 된다.
//   (링마다 같은 길이를 그리면 안쪽 링이 앞서 가서 끝이 대각선으로 뾰족해진다)
void NeoRageXShell::ringRun(QPainter& p, const QRect& b, int o, double d) {
    const int x = b.x() + o, y = b.y() + o, w = b.width() - 2 * o, h = b.height() - 2 * o;
    if (w <= 0 || h <= 0) return;
    const int W = b.width(), H = b.height();
    auto seg = [&](double start, int len) { return int(qBound(0.0, std::round(d - start - o), double(len))); };
    const QBrush br = p.brush();
    int a;
    if (RULES.penCcw) {                       // 왼쪽 아래로 → 아래 오른쪽으로 → 오른쪽 위로 → 위 왼쪽으로
        if ((a = seg(0, h)) > 0)         p.fillRect(QRect(x, y, 1, a), br);
        if ((a = seg(H, w)) > 0)         p.fillRect(QRect(x, y + h - 1, a, 1), br);
        if ((a = seg(H + W, h)) > 0)     p.fillRect(QRect(x + w - 1, y + h - a, 1, a), br);
        if ((a = seg(2 * H + W, w)) > 0) p.fillRect(QRect(x + w - a, y, a, 1), br);
    } else {                                  // 위 오른쪽으로 → 오른쪽 아래로 → 아래 왼쪽으로 → 왼쪽 위로
        if ((a = seg(0, w)) > 0)         p.fillRect(QRect(x, y, a, 1), br);
        if ((a = seg(W, h)) > 0)         p.fillRect(QRect(x + w - 1, y, 1, a), br);
        if ((a = seg(W + H, w)) > 0)     p.fillRect(QRect(x + w - a, y + h - 1, a, 1), br);
        if ((a = seg(2 * W + H, h)) > 0) p.fillRect(QRect(x, y + h - a, 1, a), br);
    }
}

// 테두리 띠의 색을 픽셀 단위로 이어 붙인다.
//   PIPE 는 10단계뿐이라 예전처럼 s 픽셀 폭씩 끊어 칠하면 현대 모니터에서 계단(줄무늬)이
//   도드라진다. 인접 단계 사이를 보간해 한 픽셀마다 색이 매끄럽게 변하게 한다.
static int pipeShade(int i, int s) {
    const double pos = qBound(0.0, (i + 0.5) / s - 0.5, double(PIPE_N - 1));
    const int    lo  = int(pos);
    const int    hi  = qMin(PIPE_N - 1, lo + 1);
    return qRound(PIPE[lo] + (PIPE[hi] - PIPE[lo]) * (pos - lo));
}

void NeoRageXShell::tracePipe(QPainter& p, const QRect& b, double t) {
    const int T = m_L.border, s = m_L.s;
    const double len = 2.0 * (b.width() + b.height()) * clamp01(t);

    // ── 겹쳐 그리는 부드러운 선 ──────────────────────────────
    //   본 띠와 같은 길이만큼만 그려서 "그려지는 애니메이션" 이 그대로 따라온다.
    //   1) 바깥 번짐: 띠 바깥쪽으로 점점 옅어지는 반투명 링
    //   2) 안쪽 그림자: 띠 안쪽 가장자리에 옅은 링 (검은 채움과 띠 사이의 딱딱한 경계를 눅인다)
    const int halo = 2 * s;
    for (int k = halo; k >= 1; --k) {
        const double f = 1.0 - double(k - 1) / halo;          // 띠에 가까울수록 진하다
        p.setBrush(QColor(30, 70, 255, qRound(80 * f * f)));
        ringRun(p, b, -k, len);
    }

    for (int i = 0; i < T; ++i) {
        const int w = b.width() - 2 * i, h = b.height() - 2 * i;
        if (w <= 0 || h <= 0) break;
        p.setBrush(QColor(0, 0, pipeShade(i, s)));
        ringRun(p, b, i, len);
    }

    for (int j = 0; j < s * 2; ++j) {
        const int i = T + j;
        const int w = b.width() - 2 * i, h = b.height() - 2 * i;
        if (w <= 0 || h <= 0) break;
        const double f = 1.0 - double(j) / (s * 2);
        p.setBrush(QColor(20, 50, 200, qRound(90 * f * f)));
        ringRun(p, b, i, len);
    }
}

// ── 본 그림 ──────────────────────────────────────────────────
void NeoRageXShell::renderSurface() {
    QPainter p(&m_surface);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.setPen(Qt::NoPen);

    const Layout& L = m_L;
    const Phases ph = phases();

    p.fillRect(0, 0, L.W, L.H, Qt::black);

    if (!m_backdrop.isNull()) {
        const double bt = quant(clamp01(double(m_frame) / TIMING.backdrop));
        if (bt > 0) {
            p.setOpacity(bt);
            p.drawImage(QRect(0, 0, L.W, L.H), m_backdrop);
            p.setOpacity(1.0);
        }
    }

    const double wt = clamp01(double(m_frame - ph.bg) / TIMING.box);
    if (wt > 0) {
        if (wt >= 1.0) {
            // 테두리가 완성되면 안쪽을 반투명한 어둠으로 채운다. 배경 그림이 은은하게 비치되
            // 글자를 읽는 데 방해가 되지 않을 만큼만 어둡게 한다. 채움은 잠깐 동안 서서히
            // 짙어진다(테두리 완성 → 내용 등장 사이).
            const double ft = quant(clamp01(double(m_frame - ph.box) / TIMING.hold));
            const QColor fill(0, 0, 0, qRound(COLOR.panelAlpha * ft));
            for (const QRect* b : { &L.gamelist, &L.options, &L.preview, &L.events })
                p.fillRect(b->adjusted(L.border, L.border, -L.border, -L.border), fill);
        }
        for (const QRect* b : { &L.gamelist, &L.options, &L.preview, &L.events })
            tracePipe(p, *b, wt);
    }
    if (m_frame < ph.hold) return;

    // 제목은 테두리 띠 위에 얹힌다. 개수 표시는 빼고 이름만 둔다.
    const double tt = quant(clamp01(double(m_frame - ph.hold) / TIMING.titles));
    if (tt > 0) {
        const QColor tc(qRound(COLOR.text.red()   * tt),
                        qRound(COLOR.text.green() * tt),
                        qRound(COLOR.text.blue()  * tt));
        auto put = [&](const QRect& b, const QString& s) {
            text(p, s, b.x() + L.text.titleDX, b.y() + L.text.titleDY, tc, L.s, 1, true);
        };
        put(L.gamelist, QStringLiteral("GAMELIST"));
        put(L.options,  QStringLiteral("OPTIONS"));
        put(L.preview,  QStringLiteral("PREVIEW"));
        put(L.events,   QStringLiteral("EVENTS"));
        text(p, L.status.left,  L.status.leftX, L.status.y, tc, L.s, 1, true);
        text(p, L.status.right, L.status.rightX - textW(L.status.right, L.s),
             L.status.y, tc, L.s, 1, true);
    }
    if (m_frame < ph.tit) return;

    drawFilterBar(p);
    drawSearchBar(p);
    drawList(p);
    drawButtons(p);
    if (m_menuFrame >= 0) drawOptionsOpen(p); else drawOptionsIdle(p);
    drawEvents(p);
    drawPreview(p);
}

// ── 필터 단추 줄 ─────────────────────────────────────────────
//   원본에는 없지만 즐겨찾기와 기종 구분은 이 프로그램에 꼭 필요하다.
//   제목의 개수 표시를 빼고 그 자리를 이 줄에 내줬다.
void NeoRageXShell::drawFilterBar(QPainter& p) {
    if (m_filters.isEmpty()) return;
    if (!seen(contentFrame(), TIMING.listLead)) return;

    const Layout& L = m_L;
    const int s = L.s;
    const int y = L.gamelist.y() + L.border + 3 * s;
    const int h = bodyCellH() + 2 * s;
    int x = L.gamelist.x() + L.border + 2 * s;
    const int right = L.scrollbar.x - 2 * s;

    for (int i = 0; i < m_filters.size(); ++i) {
        const QString& lb = m_filters[i].label;
        const int w = textW(lb, bodyScale()) + 6 * s;
        if (x + w > right) break;                 // 자리가 모자라면 자른다
        const bool cur = (m_filters[i].id == m_filterCur);
        if (cur) p.fillRect(QRect(x, y, w, h), m_zone == Zone::Filter ? COLOR.selFill : QColor(0, 0, 150));
        if (cur && m_zone == Zone::Filter) {          // 커서가 여기 있으면 밝은 테두리를 두른다
            const QColor e(200, 220, 255);
            const int t = qMax(1, s / 2);
            p.fillRect(QRect(x, y, w, t), e);
            p.fillRect(QRect(x, y + h - t, w, t), e);
            p.fillRect(QRect(x, y, t, h), e);
            p.fillRect(QRect(x + w - t, y, t, h), e);
        }
        text(p, lb, x + 3 * s, y + s,
             (cur || i == m_hoverFilter) ? COLOR.pure : COLOR.textDim,
             bodyScale());
        x += w + 2 * s;
    }
}

int NeoRageXShell::hitFilter(const QPoint& lp) const {
    if (m_filters.isEmpty()) return -1;
    const Layout& L = m_L;
    const int s = L.s;
    const int y = L.gamelist.y() + L.border + 3 * s;
    const int h = bodyCellH() + 2 * s;
    if (lp.y() < y || lp.y() > y + h) return -1;
    int x = L.gamelist.x() + L.border + 2 * s;
    const int right = L.scrollbar.x - 2 * s;
    for (int i = 0; i < m_filters.size(); ++i) {
        const int w = textW(m_filters[i].label, bodyScale()) + 6 * s;
        if (x + w > right) break;
        if (lp.x() >= x && lp.x() < x + w) return i;
        x += w + 2 * s;
    }
    return -1;
}

// ── 게임 목록 ────────────────────────────────────────────────
// 목록 한 줄에서 이름이 들어갈 자리의 가로 시작점과 폭
static int listTextX(const QRect& area, int cellW, int s) { return area.x() + 2 * s + cellW * 2; }

int NeoRageXShell::marqueeOverflow(int gi) const {
    if (gi < 0 || gi >= m_games.size()) return 0;
    const QRect area = listArea();
    const int avail = area.right() + 1 - listTextX(area, bodyCellW(), m_L.s) - 2 * m_L.s;
    const int tw = PixelFont::width(PixelFont::gTo9(m_games.at(gi)), bodyScale());
    return qMax(0, tw - avail);
}

int NeoRageXShell::marqueeOffset(int overflow) const {
    // 한 바퀴: 대기(0.25초) → 왼쪽으로 흐름 → 끝에서 잠깐 멈춤(1초) → 처음으로 돌아가 다시
    constexpr int kDelay = 15, kHold = 60;
    constexpr double kPxPerStep = 0.8;                       // 초당 약 48px
    const int run   = int(overflow / kPxPerStep) + 1;
    const int cycle = kDelay + run + kHold;
    const int st    = m_marqStep % cycle;
    if (st < kDelay) return 0;
    return qMin(overflow, int((st - kDelay) * kPxPerStep));
}

void NeoRageXShell::drawList(QPainter& p) {
    const Layout& L = m_L;
    const QRect area = listArea();
    const int cf = contentFrame();
    const int rows = qMin(rowsVisible(), int(m_games.size()) - m_top);

    p.save();
    p.setClipRect(area);
    for (int i = 0; i < rows; ++i) {
        if (!seen(cf, TIMING.listLead + i * TIMING.listStag)) continue;
        const int gi = m_top + i;
        const int y = area.y() + i * listPitch();
        if (gi == m_sel)
            p.fillRect(QRect(area.x(), y - 1 * L.s, area.width(),
                             bodyCellH() + 2 * L.s),
                       m_zone == Zone::List ? COLOR.selFill : QColor(0, 0, 105));
        // 즐겨찾기는 앞에 * 를 붙여 표시한다 (8x8 면에 별 글리프가 없다)
        const bool fav = (gi < m_favorites.size()) && m_favorites.at(gi);
        const QColor c = fav ? QColor(255, 216, 96) : COLOR.text;
        if (fav) text(p, QStringLiteral("*"), area.x() + 2 * L.s, y, c, bodyScale());
        const int tx = listTextX(area, bodyCellW(), L.s);
        int shift = 0;
        if (gi == m_sel) {
            if (m_marqSel != m_sel) { m_marqSel = m_sel; m_marqStep = 0; }
            const int ov = marqueeOverflow(gi);
            if (ov > 0) shift = marqueeOffset(ov);
        }
        if (shift > 0) {                       // 스크롤 중에는 별표 자리 밑으로 글자가 들어가지 않게 자른다
            p.setClipRect(QRect(tx, area.y(), area.right() + 1 - tx, area.height()));
            text(p, PixelFont::gTo9(m_games.at(gi)), tx - shift, y, c, bodyScale());
            p.setClipRect(area);
        } else {
            text(p, PixelFont::gTo9(m_games.at(gi)), tx, y, c, bodyScale());
        }
    }
    p.restore();
    if (seen(cf, TIMING.listLead)) drawListScrollbar(p);
}

void NeoRageXShell::drawListScrollbar(QPainter& p) {
    const Layout& L = m_L;
    const auto& sb = L.scrollbar;
    const int rows = rowsVisible();
    const int tTop = sb.top + sb.arrow, tBot = sb.bottom - sb.arrow;

    // 트랙이 흰색이고 썸이 파랑이다 (흔히 반대로 만든다)
    p.fillRect(QRect(sb.x, tTop, sb.w, tBot - tTop), COLOR.track);
    drawSpinner(p, sb.x, sb.top, sb.w, sb.arrow, -1, false);
    drawSpinner(p, sb.x, sb.bottom - sb.arrow, sb.w, sb.arrow, 1, false);

    Q_UNUSED(rows);
    const SBar bar = listBar();
    const int ty = bar.thumb.y(), th = bar.thumb.height();
    p.fillRect(QRect(sb.x, ty, sb.w, th), COLOR.barFill);
    p.fillRect(QRect(sb.x, ty, sb.w, L.s), COLOR.barTop);
    p.fillRect(QRect(sb.x, ty + th - L.s, sb.w, L.s), COLOR.barBot);
}

// 스크롤바 기하. 그리는 코드와 마우스 조작이 같은 값을 쓰도록 한 곳에서 계산한다.
static QRect thumbRect(const QRect& track, int minThumb, int total, int rows, int top) {
    const int span = track.height();
    const double frac = total <= 0 ? 1.0 : qMin(1.0, double(rows) / total);
    const int th = qMax(minThumb, qRound(span * frac));
    const int maxTop = qMax(1, total - rows);
    const int ty = track.y() + qRound((span - th) * (double(top) / maxTop));
    return QRect(track.x(), ty, track.width(), th);
}

NeoRageXShell::SBar NeoRageXShell::listBar() const {
    const auto& sb = m_L.scrollbar;
    SBar b;
    b.on    = true;
    b.up    = QRect(sb.x, sb.top, sb.w, sb.arrow);
    b.down  = QRect(sb.x, sb.bottom - sb.arrow, sb.w, sb.arrow);
    b.track = QRect(sb.x, sb.top + sb.arrow, sb.w, (sb.bottom - sb.arrow) - (sb.top + sb.arrow));
    b.total = int(m_games.size());
    b.rows  = rowsVisible();
    b.top   = m_top;
    b.thumb = thumbRect(b.track, sb.minThumb, b.total, b.rows, b.top);
    return b;
}

NeoRageXShell::SBar NeoRageXShell::subBar() const {
    SBar b;
    if (m_menuFrame < 0 || m_openIdx < 0) return b;
    const OptionEntry& o = m_options[m_openIdx];
    if (o.sub.size() <= subRowsVisible()) return b;
    const QRect a = subArea();
    const int w = m_L.scrollbar.w;
    b.on    = true;
    b.track = QRect(a.x() + a.width() - w, a.y(), w, a.height());
    b.total = int(o.sub.size());
    b.rows  = subRowsVisible();
    b.top   = m_subTop;
    b.thumb = thumbRect(b.track, m_L.scrollbar.minThumb, b.total, b.rows, b.top);
    return b;
}

void NeoRageXShell::scrollBy(int which, int delta) {
    if (which == 0) {
        const int maxTop = qMax(0, int(m_games.size()) - rowsVisible());
        m_top = qBound(0, m_top + delta, maxTop);
    } else if (m_openIdx >= 0) {
        const int maxTop = qMax(0, int(m_options[m_openIdx].sub.size()) - subRowsVisible());
        m_subTop = qBound(0, m_subTop + delta, maxTop);
    }
    update();
}

void NeoRageXShell::startRepeat(int which, int delta) {
    scrollBy(which, delta);
    m_repBar = which; m_repDelta = delta;
    if (!m_repTimer) {
        m_repTimer = new QTimer(this);
        connect(m_repTimer, &QTimer::timeout, this, [this] {
            if (m_repBar < 0) { m_repTimer->stop(); return; }
            m_repTimer->setInterval(55);
            scrollBy(m_repBar, m_repDelta);
        });
    }
    m_repTimer->setInterval(350);     // 처음엔 잠깐 기다렸다가 반복한다
    m_repTimer->start();
}

// 화살표: 한 줄씩(누르고 있으면 반복), 트랙: 한 페이지, 썸: 끌기
bool NeoRageXShell::pressScrollbar(const QPoint& lp) {
    const int which = (m_menuFrame >= 0 && m_openIdx >= 0) ? 1 : 0;
    const SBar b = which ? subBar() : listBar();
    if (!b.on) return false;
    if (b.up.contains(lp))   { startRepeat(which, -1); return true; }
    if (b.down.contains(lp)) { startRepeat(which, +1); return true; }
    if (!b.track.contains(lp)) return false;
    if (b.thumb.contains(lp)) {
        m_dragBar = which;
        m_dragOff = lp.y() - b.thumb.y();
        return true;
    }
    scrollBy(which, lp.y() < b.thumb.y() ? -b.rows : b.rows);
    return true;
}

void NeoRageXShell::dragScrollbar(const QPoint& lp) {
    const SBar b = m_dragBar ? subBar() : listBar();
    if (!b.on) { m_dragBar = -1; return; }
    const int span = b.track.height() - b.thumb.height();
    const int maxTop = qMax(0, b.total - b.rows);
    if (span <= 0 || maxTop <= 0) return;
    const int ty = qBound(0, lp.y() - m_dragOff - b.track.y(), span);
    const int top = qRound(double(ty) * maxTop / span);
    if (m_dragBar == 0) m_top = top; else m_subTop = top;
    update();
}

void NeoRageXShell::mouseReleaseEvent(QMouseEvent*) {
    m_dragBar = -1;
    m_repBar = -1;
    if (m_repTimer) m_repTimer->stop();
}

// dir: -1 위, +1 아래, 2 왼쪽, 3 오른쪽
void NeoRageXShell::drawSpinner(QPainter& p, int x, int y, int w, int h,
                                int dir, bool hot) {
    const int s = m_L.s;
    p.fillRect(QRect(x, y, w, h), COLOR.barFill);
    p.fillRect(QRect(x, y, w, s), COLOR.barTop);
    p.fillRect(QRect(x, y + h - s, w, s), COLOR.barBot);

    const QColor tip = hot ? COLOR.pure : QColor(220, 228, 255);
    const double cx = x + w / 2.0, cy = y + h / 2.0;
    if (dir == -1 || dir == 1) {
        for (int i = 0; i < 3; ++i)
            p.fillRect(QRect(qRound(cx - (i + 0.5) * s),
                             qRound(cy + dir * (1 - i) * s),
                             (i * 2 + 1) * s, s), tip);
    } else {
        const int d = (dir == 2) ? -1 : 1;
        for (int i = 0; i < 3; ++i)
            p.fillRect(QRect(qRound(cx + d * (1 - i) * s),
                             qRound(cy - (i + 0.5) * s),
                             s, (i * 2 + 1) * s), tip);
    }
}

// ── 버튼 ─────────────────────────────────────────────────────
void NeoRageXShell::drawButtons(QPainter& p) {
    const Layout& L = m_L;
    if (!seen(contentFrame(), TIMING.btnLead)) return;
    for (int i = 0; i < L.buttons.size(); ++i) {
        const QRect& r = L.buttons[i].rect;
        p.fillRect(r, COLOR.btnFill);
        p.fillRect(QRect(r.x(), r.y(), r.width(), L.s), COLOR.btnTop);
        p.fillRect(QRect(r.x(), r.y() + r.height() - L.s, r.width(), L.s), COLOR.btnBot);
        const QString lb = (i < m_btnLabels.size()) ? m_btnLabels[i] : L.buttons[i].label;
        text(p, lb, r.x() + (r.width() - textW(lb, bodyScale())) / 2,
             r.y() + (r.height() - bodyCellH()) / 2,
             i == m_hoverBtn ? COLOR.pure : COLOR.text, bodyScale());
    }
}

// ── 옵션 카테고리 ────────────────────────────────────────────
int NeoRageXShell::optPitch() const {
    const Layout& L = m_L;
    const int gh = bodyCellH() * L.options_.scaleY;
    const int avail = L.options.height() - 2 * L.border;
    const int n = qMax(1, int(m_options.size()));
    if (n < 2) return gh + 10 * L.s;
    const int fit = (avail - gh) / (n - 1);
    return qBound(gh + 2 * L.s, fit, gh + 14 * L.s);
}

int NeoRageXShell::optFirstY() const {
    const Layout& L = m_L;
    const QRect& b = L.options;
    const int gh = bodyCellH() * L.options_.scaleY;
    const int span = (m_options.size() - 1) * optPitch() + gh;
    return qRound(b.y() + L.border + ((b.height() - 2 * L.border) - span) / 2.0);
}
int NeoRageXShell::optY(int i) const { return optFirstY() + i * optPitch(); }

int NeoRageXShell::optX(const QString& label, int scale) const {
    const QRect& b = m_L.options;
    return qRound(b.x() + (b.width() - textW(label, scale)) / 2.0);
}

void NeoRageXShell::drawOptionsIdle(QPainter& p) {
    const int cf = contentFrame(), sy = m_L.options_.scaleY;
    p.save();
    p.setClipRect(m_L.options.adjusted(m_L.border, m_L.border, -m_L.border, -m_L.border));
    for (int i = 0; i < m_options.size(); ++i) {
        if (!seen(cf, TIMING.optLead + i * TIMING.optStag)) continue;
        const OptionEntry& o = m_options[i];
        const bool cursor = (m_zone == Zone::Options && i == m_optSel && o.enabled);
        const QColor c = !o.enabled ? COLOR.textDim
                       : ((i == m_hoverOpt || cursor) ? COLOR.pure : COLOR.text);
        if (cursor) {
            const QRect ob = m_L.options.adjusted(m_L.border + 2 * m_L.s, 0, -(m_L.border + 2 * m_L.s), 0);
            p.fillRect(QRect(ob.x(), optY(i) - 3 * m_L.s, ob.width(),
                             bodyCellH() * sy + 6 * m_L.s), QColor(0, 0, 150));
        }
        text(p, o.label, optX(o.label, bodyScale()), optY(i), c, bodyScale(), sy);
    }
    p.restore();
}

// ── 하위 목록 ────────────────────────────────────────────────
// 하위 메뉴 맨 아래 가운데의 뒤로 가기 단추 (마우스로 돌아가는 길을 눈에 보이게)
int NeoRageXShell::backReserve() const {
    const int bh = m_L.buttons.isEmpty() ? 20 * m_L.s : m_L.buttons.first().rect.height();
    return bh + 6 * m_L.s;
}

QRect NeoRageXShell::backRect() const {
    const Layout& L = m_L;
    const int bh = L.buttons.isEmpty() ? 20 * L.s : L.buttons.first().rect.height();
    const int w = qMax(L.buttons.isEmpty() ? 80 * L.s : L.buttons.first().rect.width(),
                       textW(m_backLabel, bodyScale()) + 16 * L.s);
    const int cx = L.options.x() + L.options.width() / 2;
    const int y = L.options.y() + L.options.height() - L.border - bh - 3 * L.s;
    return QRect(cx - w / 2, y, w, bh);
}

QRect NeoRageXShell::subArea() const {
    const Layout& L = m_L;
    const int top = L.options.y() + L.border + 6 * L.s
                  + bodyCellH() * L.options_.scaleY + 8 * L.s;
    return QRect(L.options.x() + L.border, top,
                 L.options.width() - 2 * L.border,
                 (L.options.y() + L.options.height() - L.border) - top - backReserve());
}

int NeoRageXShell::subPitch() const { return bodyCellH() + 6 * m_L.s; }

int NeoRageXShell::subRowsVisible() const {
    return qMax(1, subArea().height() / subPitch());
}

int NeoRageXShell::subRowY(int row) const {
    return subArea().y() + row * subPitch();
}

// 값 좌우 단추 자리. dir -1 왼쪽, +1 오른쪽.
QRect NeoRageXShell::subSpinRect(int row, int dir) const {
    const Layout& L = m_L;
    const QRect a = subArea();
    const int bw = bodyCellW() + 4 * L.s;
    const int y  = subRowY(row) - L.s;
    const int h  = bodyCellH() + 2 * L.s;
    const int sbw = (m_openIdx >= 0 && m_options[m_openIdx].sub.size() > subRowsVisible())
                        ? L.scrollbar.w + 2 * L.s : 0;
    const int rightEdge = a.x() + a.width() - 2 * L.s - sbw;
    const int valueW = 13 * bodyCellW();
    if (dir < 0) return QRect(rightEdge - valueW - 2 * bw, y, bw, h);
    return QRect(rightEdge - bw, y, bw, h);
}

void NeoRageXShell::drawOptionsOpen(QPainter& p) {
    const Layout& L = m_L;
    const int sy = L.options_.scaleY;
    const OptionEntry& o = m_options[m_openIdx];

    const double rt = clamp01(double(m_menuFrame) / TIMING.rise);
    const int from = optY(m_openIdx);
    const int to   = L.options.y() + L.border + 6 * L.s;
    const int y    = qRound(from + (to - from) * rt);

    p.save();
    p.setClipRect(L.options.adjusted(L.border, L.border, -L.border, -L.border));

    if (rt < 1.0) {
        for (int i = 0; i < m_options.size(); ++i) {
            if (i == m_openIdx) continue;
            const OptionEntry& q = m_options[i];
            text(p, q.label, optX(q.label, bodyScale()), optY(i),
                 q.enabled ? COLOR.text : COLOR.textDim, bodyScale(), sy);
        }
    }
    text(p, o.label, optX(o.label, bodyScale()), y, COLOR.pure, bodyScale(), sy);
    if (rt < 1.0) { p.restore(); return; }

    const int sf = m_menuFrame - TIMING.rise - TIMING.riseGap;
    const QRect a = subArea();
    const int rows = qMin(subRowsVisible(), int(o.sub.size()) - m_subTop);

    for (int r = 0; r < rows; ++r) {
        const int i = m_subTop + r;
        if (sf < r * TIMING.subStag) continue;
        const SubItem& it = o.sub.at(i);
        const int ry = subRowY(r);
        const bool hot = !it.info && ((i == m_hoverSub) || (i == m_subSel));
        const QColor fg = it.heading ? QColor(120, 175, 255)
                        : it.info ? COLOR.textDim : (hot ? COLOR.pure : COLOR.text);
        const int cw = bodyCellW();

        if (hot)
            p.fillRect(QRect(a.x(), ry - L.s, a.width(), bodyCellH() + 2 * L.s),
                       QColor(0, 0, 150));

        const int labelX = a.x() + 4 * L.s;
        const int sbw = (o.sub.size() > subRowsVisible()) ? L.scrollbar.w + 2 * L.s : 0;
        const int rowRight = a.x() + a.width() - sbw - 2 * L.s;

        if (!it.value.isEmpty()) {
            const QRect lft = subSpinRect(r, -1);
            const QRect rgt = subSpinRect(r, +1);
            if (it.adjustable) {
                drawSpinner(p, lft.x(), lft.y(), lft.width(), lft.height(), 2,
                            hot && m_hoverSpin < 0);
                drawSpinner(p, rgt.x(), rgt.y(), rgt.width(), rgt.height(), 3,
                            hot && m_hoverSpin > 0);
            }
            // 라벨은 왼쪽 단추 앞까지만, 값은 두 단추 사이에 들어가게 자른다.
            //   DIP 이름이 길면 값과 겹쳐서 읽을 수 없게 된다.
            const int labelCells = qMax(1, (lft.x() - 3 * L.s - labelX) / cw);
            text(p, it.elideLeft ? PixelFont::fitTail(it.label, labelCells)
                                 : PixelFont::fit(it.label, labelCells),
                 labelX, ry, fg, bodyScale());

            const int valueCells = qMax(1, (rgt.x() - (lft.x() + lft.width()) - 2 * L.s) / cw);
            const QString v = PixelFont::fit(it.value, valueCells);
            const int mid = (lft.x() + lft.width() + rgt.x()) / 2;
            text(p, v, mid - textW(v, bodyScale()) / 2, ry, fg, bodyScale());
        } else {
            const int labelCells = qMax(1, (rowRight - labelX) / cw);
            text(p, it.elideLeft ? PixelFont::fitTail(it.label, labelCells)
                                 : PixelFont::fit(it.label, labelCells),
                 labelX, ry, fg, bodyScale());
        }
    }
    p.restore();

    // 뒤로 가기 단추: 메인 하단 버튼과 같은 모양
    {
        const QRect r = backRect();
        p.fillRect(r, COLOR.btnFill);
        p.fillRect(QRect(r.x(), r.y(), r.width(), L.s), COLOR.btnTop);
        p.fillRect(QRect(r.x(), r.y() + r.height() - L.s, r.width(), L.s), COLOR.btnBot);
        text(p, m_backLabel, r.x() + (r.width() - textW(m_backLabel, bodyScale())) / 2,
             r.y() + (r.height() - bodyCellH()) / 2,
             m_hoverBack ? COLOR.pure : COLOR.text, bodyScale());
    }

    // 하위 목록이 길면 게임 목록과 같은 모양의 스크롤바를 단다
    if (o.sub.size() > subRowsVisible()) {
        const int sbw = L.scrollbar.w;
        const int x = a.x() + a.width() - sbw;
        p.fillRect(QRect(x, a.y(), sbw, a.height()), COLOR.track);
        const SBar bar = subBar();
        const int th = bar.thumb.height(), ty = bar.thumb.y();
        p.fillRect(QRect(x, ty, sbw, th), COLOR.barFill);
        p.fillRect(QRect(x, ty, sbw, L.s), COLOR.barTop);
        p.fillRect(QRect(x, ty + th - L.s, sbw, L.s), COLOR.barBot);
    }
}

// ── 이벤트 / 프리뷰 ──────────────────────────────────────────
void NeoRageXShell::drawEvents(QPainter& p) {
    const Layout& L = m_L;
    const QRect& b = L.events;
    const int cf = contentFrame();
    const int maxRows = eventRowsVisible();
    const int start = qMax(0, int(m_events.size()) - maxRows);
    const QRect area = b.adjusted(L.border, L.border, -L.border, -L.border);

    p.save();
    p.setClipRect(area);
    for (int i = 0; start + i < m_events.size(); ++i) {
        if (!seen(cf, TIMING.evtLead + i * TIMING.evtStag)) continue;
        text(p, PixelFont::gTo9(m_events.at(start + i)),
             area.x() + 2 * L.s, area.y() + i * listPitch(),
             COLOR.text, bodyScale());
    }
    p.restore();
}

void NeoRageXShell::drawPreview(QPainter& p) {
    if (m_preview.isNull()) return;
    const Layout& L = m_L;
    const QRect inner = L.preview.adjusted(L.border, L.border, -L.border, -L.border);
    const QSize sz = m_preview.size().scaled(inner.size(), Qt::KeepAspectRatio);
    p.drawImage(QRect(inner.x() + (inner.width() - sz.width()) / 2,
                      inner.y() + (inner.height() - sz.height()) / 2,
                      sz.width(), sz.height()), m_preview);
}

void NeoRageXShell::paintEvent(QPaintEvent*) {
    if (m_surface.isNull()) return;
    renderSurface();
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(0, 0, m_surface);
}

// ── 히트 테스트 ──────────────────────────────────────────────
int NeoRageXShell::hitOption(const QPoint& lp) const {
    const Layout& L = m_L;
    const QRect& b = L.options;
    const int h = bodyCellH() * L.options_.scaleY;
    if (lp.x() < b.x() || lp.x() > b.x() + b.width()) return -1;
    for (int i = 0; i < m_options.size(); ++i) {
        const int y = optY(i);
        if (lp.y() >= y - 4 * L.s && lp.y() <= y + h + 4 * L.s) return i;
    }
    return -1;
}

int NeoRageXShell::hitSub(const QPoint& lp) const {
    if (m_menuFrame < 0 || m_openIdx < 0) return -1;
    const QRect a = subArea();
    if (!a.contains(lp)) return -1;
    const OptionEntry& o = m_options[m_openIdx];
    const int rows = qMin(subRowsVisible(), int(o.sub.size()) - m_subTop);
    for (int r = 0; r < rows; ++r) {
        const int y = subRowY(r);
        if (lp.y() >= y - m_L.s && lp.y() < y + bodyCellH() + m_L.s)
            return m_subTop + r;
    }
    return -1;
}

int NeoRageXShell::hitSubSpin(const QPoint& lp, int& dirOut) const {
    dirOut = 0;
    if (m_menuFrame < 0 || m_openIdx < 0) return -1;
    const OptionEntry& o = m_options[m_openIdx];
    const int rows = qMin(subRowsVisible(), int(o.sub.size()) - m_subTop);
    for (int r = 0; r < rows; ++r) {
        const int i = m_subTop + r;
        if (!o.sub.at(i).adjustable) continue;
        if (subSpinRect(r, -1).contains(lp)) { dirOut = -1; return i; }
        if (subSpinRect(r, +1).contains(lp)) { dirOut = +1; return i; }
    }
    return -1;
}

int NeoRageXShell::hitButton(const QPoint& lp) const {
    for (int i = 0; i < m_L.buttons.size(); ++i)
        if (m_L.buttons[i].rect.contains(lp)) return i;
    return -1;
}

int NeoRageXShell::hitGame(const QPoint& lp) const {
    const QRect a = listArea();
    if (!a.contains(lp)) return -1;
    const int rows = qMin(rowsVisible(), int(m_games.size()) - m_top);
    for (int i = 0; i < rows; ++i) {
        const int y = a.y() + i * listPitch();
        if (lp.y() >= y - m_L.s && lp.y() < y + bodyCellH() + m_L.s) return m_top + i;
    }
    return -1;
}

// ── 입력 ─────────────────────────────────────────────────────
void NeoRageXShell::mouseMoveEvent(QMouseEvent* e) {
    const QPoint lp = toLogical(e->pos());
    if (m_dragBar >= 0) { dragScrollbar(lp); return; }
    m_hoverBack = (m_menuFrame >= 0 && menuOpenDone() && backRect().contains(lp)) ? 1 : 0;
    const bool idle = isIdle();
    m_hoverOpt    = (m_menuFrame < 0 && idle) ? hitOption(lp) : -1;
    m_hoverSub    = (m_menuFrame >= 0) ? hitSub(lp) : -1;
    if (m_hoverSub >= 0 && !subSelectable(m_hoverSub)) m_hoverSub = -1;
    m_hoverBtn    = idle ? hitButton(lp) : -1;
    m_hoverFilter = (m_menuFrame < 0 && idle) ? hitFilter(lp) : -1;
    int dir = 0;
    if (m_menuFrame >= 0) hitSubSpin(lp, dir);
    m_hoverSpin = dir;
    setCursor(m_pointer);      // 사용자가 준 포인터 그림을 항상 쓴다
    update();
}

// 한글처럼 조합이 필요한 입력. 조합 중인 글자는 검색어 뒤에 임시로 보이고, 확정되면
//   검색어에 붙는다.
void NeoRageXShell::inputMethodEvent(QInputMethodEvent* e) {
    if (!m_searchFocus) { e->ignore(); return; }
    m_preedit = e->preeditString();
    if (!e->commitString().isEmpty()) editSearch(m_search + e->commitString());
    else update();
    e->accept();
}

QVariant NeoRageXShell::inputMethodQuery(Qt::InputMethodQuery q) const {
    switch (q) {
    case Qt::ImEnabled:          return m_searchFocus;
    case Qt::ImCursorRectangle:  return searchRect();
    case Qt::ImSurroundingText:  return m_search;
    case Qt::ImCursorPosition:   return int(m_search.size());
    default:                     return QWidget::inputMethodQuery(q);
    }
}

void NeoRageXShell::leaveEvent(QEvent*) {
    m_hoverOpt = m_hoverBtn = m_hoverSub = m_hoverFilter = -1;
    m_hoverSpin = 0;
    update();
}

void NeoRageXShell::wheelEvent(QWheelEvent* e) {
    if (!isIdle()) { e->ignore(); return; }
    const int step = (e->angleDelta().y() < 0) ? 3 : -3;

    if (m_menuFrame >= 0 && m_openIdx >= 0) {
        const int n = m_options[m_openIdx].sub.size();
        m_subTop = qBound(0, m_subTop + step, qMax(0, n - subRowsVisible()));
    } else {
        const int maxTop = qMax(0, int(m_games.size()) - rowsVisible());
        m_top = qBound(0, m_top + step, maxTop);
    }
    update();
    e->accept();
}

void NeoRageXShell::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    const QPoint lp = toLogical(e->pos());

    if (!isIdle()) { skipBoot(); return; }

    // 오른쪽 버튼: 게임 위에서 즐겨찾기 토글. 옛 우클릭 메뉴(실행/즐겨찾기/치트)에서
    //   실행은 더블클릭·Enter 가, 치트는 CHEATS 메뉴가 대신한다.
    //   스팀덱은 L2 가 우클릭이라 트랙패드만으로 즐겨찾기를 할 수 있다.
    if (e->button() == Qt::RightButton) {
        if (m_menuFrame < 0) {
            const int gi = hitGame(lp);
            if (gi >= 0) emit favoriteToggled(gi);
        } else {
            closeMenu();
        }
        return;
    }

    if (m_menuFrame >= 0 && m_openIdx >= 0 && menuOpenDone() && backRect().contains(lp)) {
        closeMenu();
        return;
    }
    if (pressScrollbar(lp)) return;

    if (m_menuFrame >= 0) {
        int dir = 0;
        const int si = hitSubSpin(lp, dir);
        if (si >= 0 && dir != 0) {
            m_subSel = si;
            emit subAdjusted(m_options[m_openIdx].id, si, dir);
            return;
        }
        const int row = hitSub(lp);
        if (row >= 0) {
            if (m_options[m_openIdx].sub.at(row).info) return;   // 안내 줄은 눌러도 반응 없음
            m_subSel = row;
            const SubItem& it = m_options[m_openIdx].sub.at(row);
            // 값이 있는 항목은 누르면 한 칸 앞으로, 동작형은 그대로 알린다
            if (it.adjustable) emit subAdjusted(m_options[m_openIdx].id, row, +1);
            else               emit optionChosen(m_options[m_openIdx].id, row);
            return;
        }
        closeMenu();
        return;
    }

    if (hitSearchClear(lp)) { editSearch(QString()); return; }
    if (hitSearch(lp))      { setSearchFocus(true); return; }
    if (m_searchFocus)      setSearchFocus(false);      // 다른 곳을 누르면 입력을 끝낸다

    const int fi = hitFilter(lp);
    if (fi >= 0) { m_zone = Zone::Filter; emit filterChosen(m_filters[fi].id); return; }

    const int bi = hitButton(lp);
    if (bi >= 0) {
        const QString id = m_L.buttons[bi].id;
        if (id == QLatin1String("launch")) {
            if (m_sel >= 0) emit launchRequested(selectedGame());
        } else if (id == QLatin1String("import")) {
            emit importRequested();
        } else {
            emit exitRequested();
        }
        return;
    }

    const int oi = hitOption(lp);
    if (oi >= 0 && m_options[oi].enabled) { openMenu(oi); return; }

    const int gi = hitGame(lp);
    if (gi >= 0) {
        m_zone = Zone::List;
        m_sel = gi;
        update();
        emit gameSelected(selectedGame(), m_sel);
    }
}

void NeoRageXShell::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!isIdle() || m_menuFrame >= 0) return;
    if (hitGame(toLogical(e->pos())) >= 0 && m_sel >= 0)
        emit launchRequested(selectedGame());
}

// 키를 Nav 로 옮기기만 한다. 실제 동작은 navigate() 한 곳에 있다.
void NeoRageXShell::keyPressEvent(QKeyEvent* e) {
    // Alt+Enter 는 전체화면 핫키다. 실행으로 착각하면 안 된다.
    if (e->modifiers() & Qt::AltModifier) { e->ignore(); return; }

    // ── 검색창 ──────────────────────────────────────────────
    //   입력 중에는 글자·백스페이스를 여기서 먹는다. 위/아래/페이지 이동은 그대로 목록을
    //   움직이므로 결과를 보며 바로 고를 수 있다. Enter·Esc 는 입력을 끝낸다.
    if (isIdle() && m_menuFrame < 0) {
        const Qt::KeyboardModifiers mods = e->modifiers();
        if (m_searchFocus) {
            switch (e->key()) {
            case Qt::Key_Escape:
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (!e->isAutoRepeat()) setSearchFocus(false);
                e->accept();
                return;
            case Qt::Key_Backspace:
                editSearch(m_search.left(m_search.size() - 1));
                e->accept();
                return;
            case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_PageUp: case Qt::Key_PageDown:
            case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Home: case Qt::Key_End:
                break;                                  // 아래의 일반 조작으로
            default:
                if ((mods & Qt::ControlModifier) && e->key() == Qt::Key_U) {
                    editSearch(QString());
                    e->accept();
                    return;
                }
                const QString t = e->text();
                if (!(mods & Qt::ControlModifier) && !t.isEmpty() && t.at(0).isPrint()) {
                    editSearch(m_search + t);
                    e->accept();
                    return;
                }
            }
        } else if ((e->key() == Qt::Key_BracketLeft || e->key() == Qt::Key_BracketRight) && !mods) {
            navigate(e->key() == Qt::Key_BracketLeft ? Nav::ZoneLeft : Nav::ZoneRight);
            e->accept();
            return;
        } else if ((e->key() == Qt::Key_Slash && !mods) ||
                   (e->key() == Qt::Key_F && (mods & Qt::ControlModifier))) {
            setSearchFocus(true);                        // / 또는 Ctrl+F
            e->accept();
            return;
        }
    }

    Nav a;
    switch (e->key()) {
    case Qt::Key_Up:       a = Nav::Up;       break;
    case Qt::Key_Down:     a = Nav::Down;     break;
    case Qt::Key_Left:     a = Nav::Left;     break;
    case Qt::Key_Right:    a = Nav::Right;    break;
    case Qt::Key_PageUp:   a = Nav::PageUp;   break;
    case Qt::Key_PageDown: a = Nav::PageDown; break;
    case Qt::Key_Home:     a = Nav::Home;     break;
    case Qt::Key_End:      a = Nav::End;      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:    a = Nav::Accept;   break;
    case Qt::Key_Space:    a = Nav::Favorite; break;
    case Qt::Key_Escape:   a = Nav::Back;     break;
    default:
        e->ignore();
        return;
    }

    // 누르고 있어도 실행이 연달아 나가면 안 된다
    if (a == Nav::Accept && e->isAutoRepeat()) { e->accept(); return; }

    if (navigate(a)) e->accept();
    else             e->ignore();     // 예: 메뉴가 안 열린 채 ESC → 앱이 종료 처리
}

// ── 조작의 단일 진입점 ───────────────────────────────────────
bool NeoRageXShell::navigate(Nav a) {
    if (!isIdle()) { skipBoot(); return true; }      // 부팅 연출 중에는 아무 입력이나 건너뛴다

    // L / R: 커서를 한 칸씩 옮긴다. 필터(즐겨찾기·기종) → 게임 목록 → 옵션 메뉴
    if (a == Nav::ZoneLeft) {
        if (m_menuFrame >= 0) { closeMenu(); m_zone = Zone::List; }
        else if (m_zone == Zone::Options) m_zone = Zone::List;
        else if (m_zone == Zone::List && !m_filters.isEmpty()) m_zone = Zone::Filter;
        update();
        return true;
    }
    if (a == Nav::ZoneRight) {
        if (m_menuFrame < 0) {
            if (m_zone == Zone::Filter) m_zone = Zone::List;
            else if (m_zone == Zone::List) {
                m_zone = Zone::Options;
                if (m_optSel < 0 || m_optSel >= m_options.size()) m_optSel = 0;
            }
            update();
        }
        return true;
    }
    return (m_menuFrame >= 0 && m_openIdx >= 0) ? navigateMenu(a) : navigateList(a);
}

// 하위 메뉴가 열려 있을 때
bool NeoRageXShell::navigateMenu(Nav a) {
    const OptionEntry& o = m_options[m_openIdx];
    const int n = o.sub.size();
    const int page = qMax(1, subRowsVisible() - 1);

    switch (a) {
    // 마지막 선택 항목 뒤에 안내 줄만 남아 있으면(예: DIRECTORIES 의 고정 경로 설명) 커서가
    //   그 안내 줄 위로 한 줄씩 내려가며 화면을 스크롤한다. 안 그러면 패드·키보드로는
    //   끝의 안내 줄을 읽을 수 없다. 안내 줄 위에서는 강조 표시가 없고 A 도 동작하지 않는다.
    case Nav::Up:
        if (m_subSel > 0) {
            const bool inTail = !subSelectableFrom(m_subSel);
            --m_subSel;
            if (!inTail) snapSubSel(-1);
        }
        break;
    case Nav::Down:
        if (m_subSel < n - 1) {
            int next = m_subSel + 1;
            for (int i = next; i < n; ++i) if (subSelectable(i)) { next = i; break; }
            m_subSel = next;                       // 선택할 수 있는 줄이 없으면 바로 다음 줄로
        }
        break;
    case Nav::PageUp:   m_subSel = qMax(0, m_subSel - page);         snapSubSel(-1); break;
    case Nav::PageDown: m_subSel = qMin(n - 1, m_subSel + page);     snapSubSel(+1); break;
    case Nav::Home:     m_subSel = 0;                                snapSubSel(+1); break;
    case Nav::End:      m_subSel = qMax(0, n - 1);                                   break;

    case Nav::Left:
    case Nav::Right:
        // ◀ ▶ — 값이 있는 항목만 반응한다
        if (m_subSel < n && o.sub.at(m_subSel).adjustable)
            emit subAdjusted(o.id, m_subSel, a == Nav::Left ? -1 : +1);
        return true;

    case Nav::Accept:
        if (m_subSel < n && !o.sub.at(m_subSel).info) {
            if (o.sub.at(m_subSel).adjustable) emit subAdjusted(o.id, m_subSel, +1);
            else                               emit optionChosen(o.id, m_subSel);
        }
        return true;

    case Nav::Back:
        closeMenu();
        return true;

    case Nav::Favorite:
    case Nav::Search:
    case Nav::ZoneLeft:
    case Nav::ZoneRight:
        return false;
    }
    ensureSubVisible();
    update();
    return true;
}

// 메뉴가 닫혀 있을 때 — 게임 목록
bool NeoRageXShell::navigateList(Nav a) {
    // 커서가 필터 줄(ALL / FAV / 기종…)에 있으면: 좌우로 필터를 고르고, 아래로 내려가면 목록.
    if (m_zone == Zone::Filter) {
        int cur = 0;
        for (int i = 0; i < m_filters.size(); ++i) if (m_filters[i].id == m_filterCur) cur = i;
        switch (a) {
        case Nav::Left:
            if (cur > 0) emit filterChosen(m_filters[cur - 1].id);
            break;
        case Nav::Right:
            if (cur + 1 < m_filters.size()) emit filterChosen(m_filters[cur + 1].id);
            break;
        case Nav::Home:
            if (!m_filters.isEmpty()) emit filterChosen(m_filters.first().id);
            break;
        case Nav::End:
            if (!m_filters.isEmpty()) emit filterChosen(m_filters.last().id);
            break;
        case Nav::Down:
        case Nav::Accept:
        case Nav::Back:
            m_zone = Zone::List;
            break;
        case Nav::Search:
            emit searchRequested();
            break;
        default: break;
        }
        update();
        return true;
    }

    // 커서가 옵션 메뉴에 있으면 그쪽을 조작한다. A 는 여기서 옵션을 열고, 목록에 있을 때만
    //   게임을 실행하므로 둘이 서로 간섭하지 않는다.
    if (m_zone == Zone::Options) {
        const int n = int(m_options.size());
        auto step = [&](int dir) {
            for (int k = 0; k < n; ++k) {
                m_optSel = (m_optSel + dir + n) % n;
                if (m_options[m_optSel].enabled) break;
            }
        };
        switch (a) {
        case Nav::Up:   step(-1); break;
        case Nav::Down: step(+1); break;
        case Nav::Home: m_optSel = 0; if (n && !m_options[0].enabled) step(+1); break;
        case Nav::End:  m_optSel = n - 1; if (n && !m_options[n - 1].enabled) step(-1); break;
        case Nav::Accept:
            if (m_optSel >= 0 && m_optSel < n) openMenu(m_optSel);
            return true;
        case Nav::Back:
            m_zone = Zone::List;          // 한 단계 물러나 목록으로
            update();
            return true;
        default:
            return true;                  // 좌우·즐겨찾기·검색은 옵션 메뉴에서는 쓰지 않는다
        }
        update();
        return true;
    }

    const int total = int(m_games.size());
    const int rows  = rowsVisible();

    switch (a) {
    case Nav::Up:       m_sel = (m_sel < 0) ? total - 1 : qMax(0, m_sel - 1);            break;
    case Nav::Down:     m_sel = (m_sel < 0) ? 0         : qMin(total - 1, m_sel + 1);    break;
    // 좌우는 페이지 이동이다 (게임패드 D-패드 좌우가 원래 그랬다)
    case Nav::Left:
    case Nav::PageUp:   m_sel = qMax(0, qMax(0, m_sel) - rows);                          break;
    case Nav::Right:
    case Nav::PageDown: m_sel = qMin(total - 1, qMax(0, m_sel) + rows);                  break;
    case Nav::Home:     m_sel = total ? 0 : -1;                                          break;
    case Nav::End:      m_sel = total - 1;                                               break;

    case Nav::Accept:
        if (m_sel >= 0) emit launchRequested(selectedGame());
        return true;

    case Nav::Favorite:
        if (m_sel >= 0) emit favoriteToggled(m_sel);
        return true;

    case Nav::Search:
        emit searchRequested();
        return true;

    case Nav::ZoneLeft:
    case Nav::ZoneRight:
        return true;        // navigate() 가 먼저 처리한다

    case Nav::Back:
        return false;       // 메뉴가 없으면 ESC 는 앱이 처리한다
    }
    if (total == 0) return true;
    m_sel = qBound(0, m_sel, total - 1);
    ensureSelectionVisible();
    update();
    emit gameHighlighted(selectedGame(), m_sel);
    return true;
}
