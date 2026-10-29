// NeoRageXShellLobby.cpp — 멀티플레이 로비(방) 화면
//
//  방을 만들거나 들어가면 OPTIONS 상자 안에 이 화면이 뜬다 (포포루·웨짠식 대기방).
//   · 위쪽: 방에 있는 사람 카드 3장. 캐릭터 그림, 닉네임, 레벨. 레벨 10단위로 테두리 색과 효과가 바뀐다.
//     참가자가 들어오면 그 카드가 번쩍이며 "입장!" 이 뜬다. 3번 카드는 플레이어 또는 관전.
//   · 왼쪽 아래: 채팅 기록과 입력 줄
//   · 오른쪽: 방 설정(게임, 딜레이, 승수 표시, 내 자리)과 READY / 게임 시작 / 방 나가기
//  셸은 그리기와 입력만 맡고, 방 상태(누가 있는지, 레디인지)는 앱이 가지고 있다.

#include "NeoRageXShell.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QTimer>

#include "Avatar.h"

using namespace nrx;

namespace {
const QColor kHost (255, 214, 110);
const QColor kGuest(130, 224, 255);
const QColor kReady( 90, 255, 140);
const QColor kSys  (255, 214, 110);
const QColor kSlotEmpty(0, 0, 50, 170);
constexpr int kFlashMs = 2600;
}

// ── 설정 / 상태 ───────────────────────────────────────────────
void NeoRageXShell::setLobby(const LobbyView& v) {
    const bool was = m_lobby.active;
    m_lobby = v;
    for (int i = 0; i < 3 && i < v.members.size(); ++i) {
        if (v.members[i].present && v.members[i].joinSerial != m_flashSerial[i]) {
            m_flashSerial[i] = v.members[i].joinSerial;
            if (v.members[i].joinSerial > 1) m_flashClock[i].start();     // 입장 연출 시작
        }
    }
    if (!m_lobbyTimer) {
        m_lobbyTimer = new QTimer(this);
        m_lobbyTimer->setInterval(50);
        connect(m_lobbyTimer, &QTimer::timeout, this, [this] { if (lobbyVisible() || m_sideOn) update(); });
        m_lobbyClock.start();
    }
    if ((v.active || m_sideOn) && !m_lobbyTimer->isActive()) m_lobbyTimer->start();
    if (!v.active && !m_sideOn) m_lobbyTimer->stop();
    if (!v.active) { setChatFocus(false); m_chatBuf.clear(); m_chatPre.clear(); }
    if (v.active && !was) m_lobbySel = 0;
    update();
}

void NeoRageXShell::setPickMode(bool on, const QString& launchLabel) {
    m_pickMode = on;
    m_pickLabel = launchLabel;
    if (on) { closeMenu(); m_zone = Zone::List; }
    update();
}

void NeoRageXShell::setSideCard(const LobbyMember& m, bool on, const QString& caption) {
    m_sideMember = m;
    m_sideOn = on;
    m_sideCaption = caption;
    if (!m_lobbyTimer) {
        m_lobbyTimer = new QTimer(this);
        m_lobbyTimer->setInterval(50);
        connect(m_lobbyTimer, &QTimer::timeout, this, [this] { if (lobbyVisible() || m_sideOn) update(); });
        m_lobbyClock.start();
    }
    if (on && !m_lobbyTimer->isActive()) m_lobbyTimer->start();
    if (!on && !m_lobby.active) m_lobbyTimer->stop();
    update();
}

bool NeoRageXShell::lobbyVisible() const {
    return m_lobby.active && m_menuFrame >= 0 && m_openIdx >= 0 && menuOpenDone()
        && m_options[m_openIdx].id == QLatin1String("netplay");
}

QString NeoRageXShell::elideTo(const QString& s, int px, int q) const {
    if (textW(s, q) <= px) return s;
    QString t = s;
    while (!t.isEmpty() && textW(t + QStringLiteral(".."), q) > px) t.chop(1);
    return t + QStringLiteral("..");
}

// 방 만들기 전 미리보기 카드: 하위 목록 오른쪽 끝에 카드 한 장
int NeoRageXShell::sideReserve() const {
    if (!m_sideOn || m_lobby.active) return 0;
    if (m_openIdx < 0 || m_options[m_openIdx].id != QLatin1String("netplay")) return 0;
    const int w = m_L.options.width() - 2 * m_L.border;
    return qBound(110 * m_L.s, w * 28 / 100, 190 * m_L.s);
}

QRect NeoRageXShell::sideRect() const {
    const int w = sideReserve();
    if (w <= 0) return QRect();
    const int top = m_L.options.y() + m_L.border + 6 * m_L.s + optCellH() * m_L.options_.scaleY + 8 * m_L.s;
    const int right = m_L.options.right() - m_L.border - 4 * m_L.s;
    const int bottom = (m_L.options.y() + m_L.options.height() - m_L.border) - backReserve() - 4 * m_L.s;
    const int cw = w - 8 * m_L.s;
    const int ch = qMin(bottom - top - 2 * subCellH() - 4 * m_L.s, cw * 5 / 4 + 2 * subCellH() + 14 * m_L.s);
    return QRect(right - cw + 1, top, cw, qMax(ch, 60 * m_L.s));
}

QRect NeoRageXShell::lobbyOuter() const {
    const QRect& a = m_L.options; const QRect& e = m_L.events;
    return QRect(a.x(), a.y(), a.width(), e.y() + e.height() - a.y());
}

// ── 배치 ─────────────────────────────────────────────────────
NeoRageXShell::LobbyGeo NeoRageXShell::lobbyGeo() const {
    const Layout& L = m_L;
    const int s = L.s, ch = subCellH();
    LobbyGeo g;
    const QRect o = lobbyOuter();
    g.box = o.adjusted(L.border + 6 * s, L.border + 8 * s, -(L.border + 6 * s), -(L.border + 6 * s));

    g.title = QRect(g.box.x(), g.box.y(), g.box.width(), ch + 6 * s);
    const int top = g.title.bottom() + 6 * s;
    const int gap = 8 * s;
    const int rw = qBound(150 * s, g.box.width() * 33 / 100, 260 * s);       // 오른쪽 설정 폭
    const QRect left(g.box.x(), top, g.box.width() - rw - gap, g.box.bottom() - top);
    const QRect right(left.right() + gap, top, rw, left.height());

    // 카드 3장: 높이는 왼쪽 영역의 약 60% (나머지는 채팅)
    const int barH = ch + 10 * s;
    const int chatMin = 2 * (ch + 2 * s) + 6 * s;
    const int cardGap = 6 * s;
    const int cw = (left.width() - 2 * cardGap) / 3;
    int cardH = left.height() - barH - 4 * s - chatMin - 4 * s;
    cardH = qMax(cardH, 2 * ch + 40 * s);
    cardH = qMin(cardH, cw * 5 / 4 + 2 * ch + 12 * s);
    for (int i = 0; i < 3; ++i) {
        g.card[i] = QRect(left.x() + i * (cw + cardGap), left.y(), cw, cardH);
        const int th = 2 * ch + 8 * s;                                       // 카드 아래 글자 두 줄
        g.cardImg[i] = g.card[i].adjusted(3 * s, 3 * s, -3 * s, -th);
    }
    g.chatBar = QRect(left.x(), left.bottom() - barH, left.width(), barH);
    const int logTop = g.card[0].bottom() + 6 * s;
    g.chatLog = QRect(left.x(), logTop, left.width(), qMax(ch, g.chatBar.top() - 4 * s - logTop));

    g.panel = right;
    const int pitch = ch + 9 * s;
    int y = right.y() + ch + 8 * s;
    g.rowGame  = QRect(right.x(), y, right.width(), pitch); y += pitch;
    g.rowDelay = QRect(right.x(), y, right.width(), pitch); y += pitch;
    g.rowWin   = QRect(right.x(), y, right.width(), pitch); y += pitch;
    g.rowSeat  = QRect(right.x(), y, right.width(), pitch);

    g.btnLeave = QRect(right.x(), right.bottom() - (ch + 10 * s), right.width(), ch + 10 * s);
    g.btnMain  = QRect(right.x(), g.btnLeave.top() - 6 * s - (2 * ch + 8 * s), right.width(), 2 * ch + 8 * s);

    const int sp = ch + 2 * s;
    g.spinR = QRect(g.rowDelay.right() - sp - 2 * s, g.rowDelay.y() + (pitch - sp) / 2, sp, sp);
    g.spinL = QRect(g.spinR.x() - sp - 3 * s - textW(QStringLiteral("00f"), subQ()) - 8 * s,
                    g.spinR.y(), sp, sp);

    const int cwid = textW(QStringLiteral("[ %1 ]").arg(m_lobby.L.close), subQ()) + 4 * s;
    const int cpw  = textW(QStringLiteral("[ %1 ]").arg(m_lobby.L.copy), subQ()) + 4 * s;
    g.chipClose = QRect(g.title.right() - cwid, g.title.y(), cwid, g.title.height());
    g.chipCopy  = QRect(g.chipClose.x() - 6 * s - cpw, g.title.y(), cpw, g.title.height());
    return g;
}

int NeoRageXShell::hitLobby(const QPoint& lp, int& deltaOut) const {
    deltaOut = 0;
    if (!lobbyVisible()) return -1;
    const LobbyGeo g = lobbyGeo();
    if (g.chipClose.contains(lp)) return 7;
    if (!m_lobby.roomCode.isEmpty() && g.chipCopy.contains(lp)) return 6;
    if (g.btnMain.contains(lp))  return 0;
    if (g.btnLeave.contains(lp)) return 5;
    if (g.rowDelay.contains(lp)) {
        if (m_lobby.delayEditable) {
            if (g.spinL.contains(lp)) deltaOut = -1;
            else if (g.spinR.contains(lp)) deltaOut = +1;
        }
        return 1;
    }
    if (g.rowWin.contains(lp))   return 2;
    if (g.rowSeat.contains(lp))  return 3;
    if (g.chatBar.contains(lp))  return 4;
    return -1;
}

// ── 카드 ─────────────────────────────────────────────────────
//   바깥 테두리(레벨 색) → 캐릭터 그림 → 아래 글자 두 줄 (닉네임 / 방장·READY·관전)
void NeoRageXShell::drawCard(QPainter& p, const QRect& r, const LobbyMember& m, int flash,
                             bool emptyOpen, const QString& emptyText) {
    const Layout& L = m_L;
    const int s = L.s, ch = subCellH(), q = subQ();
    const LobbyLabels& T = m_lobby.L;
    const int th = 2 * ch + 8 * s;
    const QRect img = r.adjusted(3 * s, 3 * s, -3 * s, -th);

    if (!m.present) {
        p.fillRect(r, kSlotEmpty);
        const QColor e(0, 60, 140);
        p.fillRect(QRect(r.x(), r.y(), r.width(), s), e);
        p.fillRect(QRect(r.x(), r.bottom() - s + 1, r.width(), s), e);
        p.fillRect(QRect(r.x(), r.y(), s, r.height()), e);
        p.fillRect(QRect(r.right() - s + 1, r.y(), s, r.height()), e);
        const QString w = emptyText.isEmpty() ? (emptyOpen ? T.open : T.waiting) : emptyText;
        const QString t = elideTo(w, r.width() - 8 * s, q);
        text(p, t, r.x() + (r.width() - textW(t, q)) / 2, r.y() + (r.height() - ch) / 2, COLOR.textDim, q);
        return;
    }

    QColor fill(0, 0, 90, 235);                                  // 바탕 (입장 연출 중엔 파랗게 번쩍)
    if (flash == 1) fill = QColor(0, 110, 255, 240);
    else if (flash == 2) fill = QColor(0, 50, 190, 240);
    p.fillRect(r, fill);
    QLinearGradient gr(img.topLeft(), img.bottomLeft());         // 그림 칸 바탕
    gr.setColorAt(0, QColor(40, 90, 200));
    gr.setColorAt(1, QColor(10, 30, 110));
    p.fillRect(img, gr);

    {   // 캐릭터 그림: 칸에 비율을 지켜 맞춘다
        const QImage pic = avatar::compose(avatar::partsFromString(m.parts));
        const QSize sz = pic.size().scaled(img.size(), Qt::KeepAspectRatio);
        const QRect dst(img.x() + (img.width() - sz.width()) / 2, img.bottom() - sz.height() + 1, sz.width(), sz.height());
        p.save();
        p.setClipRect(img);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (m.spectator) p.setOpacity(0.55);
        p.drawImage(dst, pic);
        p.restore();
    }

    avatar::drawFrame(p, r, m.level, qMax(2, s), m_lobbyClock.isValid() ? m_lobbyClock.elapsed() : 0);   // 레벨 테두리

    {   // 레벨 배지 (왼쪽 위)
        const QString lv = QStringLiteral("%1").arg(m.level);
        const int bw = textW(lv, q) + 10 * s, bh = ch + 4 * s;
        const QRect b(img.x() + 2 * s, img.y() + 2 * s, bw, bh);
        QColor tc = avatar::tierColor(avatar::tierOf(m.level));
        if (avatar::tierOf(m.level) == 9) tc = QColor::fromHsv(int((m_lobbyClock.elapsed() / 8) % 360), 160, 255);
        p.fillRect(b, QColor(0, 0, 0, 190));
        p.fillRect(QRect(b.x(), b.y(), b.width(), s), tc);
        text(p, lv, b.x() + 5 * s, b.y() + 2 * s, tc, q);
    }
    if (flash) {   // 입장! 표시
        const QString j = T.joined;
        const int jw = textW(j, q) + 10 * s;
        const QRect b(img.right() - jw - 2 * s, img.y() + 2 * s, jw, ch + 4 * s);
        p.fillRect(b, QColor(255, 255, 255, 230));
        text(p, j, b.x() + 5 * s, b.y() + 2 * s, QColor(0, 60, 200), q);
    }
    if (m.spectator) {   // 관전 표시
        const QString v = T.spectator;
        text(p, v, img.x() + (img.width() - textW(v, q)) / 2, img.y() + img.height() / 2 - ch / 2, COLOR.pure, q);
    }

    const int tx = r.x() + 4 * s;                                // 글자 두 줄
    const int ty = r.bottom() - th + 5 * s;
    text(p, elideTo(m.nick, r.width() - 8 * s, q), tx, ty, COLOR.pure, q);
    QString st; QColor stc = COLOR.textDim;
    if (m.host)            { st = T.hostTag; stc = kHost; }
    else if (m.spectator)  { st = T.spectator; }
    else if (m.ready)      { st = T.readyOn; stc = kReady; }
    else                   { st = T.waiting; }
    text(p, elideTo(st, r.width() - 8 * s, q), tx, ty + ch + 2 * s, stc, q);
}

// ── 그리기 ───────────────────────────────────────────────────
void NeoRageXShell::drawLobby(QPainter& p) {
    const Layout& L = m_L;
    const int s = L.s, ch = subCellH(), q = subQ();
    const LobbyGeo g = lobbyGeo();
    const LobbyLabels& T = m_lobby.L;

    {   // OPTIONS + EVENTS 를 한 상자로 덮는다 (EVENTS 로그는 방에 있는 동안 채팅 알림이 대신한다)
        const QRect o = lobbyOuter();
        p.fillRect(o, Qt::black);
        p.fillRect(o.adjusted(L.border, L.border, -L.border, -L.border), QColor(0, 0, 0, COLOR.panelAlpha));
        tracePipe(p, o, 1.0);
        text(p, T.room, o.x() + L.text.titleDX, o.y() + L.text.titleDY, COLOR.text, L.s * 8, 1, true);
    }

    auto button = [&](const QRect& r, const QString& label, bool hot, bool enabled, const QColor& tone) {
        QColor fill = enabled ? (tone.isValid() ? tone : COLOR.btnFill) : QColor(40, 40, 70);
        p.fillRect(r, fill);
        p.fillRect(QRect(r.x(), r.y(), r.width(), s), enabled ? COLOR.btnTop : QColor(70, 70, 100));
        p.fillRect(QRect(r.x(), r.y() + r.height() - s, r.width(), s), COLOR.btnBot);
        if (hot && enabled) {
            p.fillRect(QRect(r.x(), r.y(), r.width(), s), COLOR.pure);
            p.fillRect(QRect(r.x(), r.bottom() - s + 1, r.width(), s), COLOR.pure);
            p.fillRect(QRect(r.x(), r.y(), s, r.height()), COLOR.pure);
            p.fillRect(QRect(r.right() - s + 1, r.y(), s, r.height()), COLOR.pure);
        }
        const QString t = elideTo(label, r.width() - 8 * s, q);
        text(p, t, r.x() + (r.width() - textW(t, q)) / 2, r.y() + (r.height() - ch) / 2,
             enabled ? (hot ? COLOR.pure : COLOR.text) : COLOR.textDim, q);
    };

    {   // 제목 줄: ROOM  CODE
        QString head = m_lobby.roomCode.isEmpty() ? T.room : m_lobby.roomCode;
        text(p, head, g.title.x(), g.title.y() + 3 * s, QColor(120, 175, 255), q);
        if (m_lobby.connected && !m_lobby.status.isEmpty()) {
            const int sx = g.title.x() + textW(head, q) + 14 * s;
            const int room = (m_lobby.roomCode.isEmpty() ? g.chipClose.x() : g.chipCopy.x()) - sx - 6 * s;
            if (room > textW(m_lobby.status, q)) text(p, m_lobby.status, sx, g.title.y() + 3 * s, COLOR.textDim, q);
        }
        if (!m_lobby.roomCode.isEmpty()) {
            const QString c = QStringLiteral("[ %1 ]").arg(T.copy);
            text(p, c, g.chipCopy.x() + 2 * s, g.title.y() + 3 * s, m_lobbyHover == 6 ? COLOR.pure : COLOR.text, q);
        }
        const QString x = QStringLiteral("[ %1 ]").arg(T.close);
        text(p, x, g.chipClose.x() + 2 * s, g.title.y() + 3 * s, m_lobbyHover == 7 ? COLOR.pure : COLOR.textDim, q);
        p.fillRect(QRect(g.title.x(), g.title.bottom() - s, g.title.width(), s), QColor(0, 90, 200));
    }

    for (int i = 0; i < 3; ++i) {   // 사람 카드 3장
        const LobbyMember m = (i < m_lobby.members.size()) ? m_lobby.members[i] : LobbyMember();
        int flash = 0;
        if (m.present && m_flashClock[i].isValid() && m_flashClock[i].elapsed() < kFlashMs && m.joinSerial > 1)
            flash = 1 + int((m_flashClock[i].elapsed() / 130) % 2);
        drawCard(p, g.card[i], m, flash, i == 2, QString());
    }

    {   // 채팅 기록
        p.fillRect(g.chatLog, QColor(0, 0, 40, 150));
        p.save();
        p.setClipRect(g.chatLog);
        const int pad = 4 * s, lineH = ch + 2 * s;
        const int maxW = g.chatLog.width() - 2 * pad;
        struct Line { QString t; QColor c; };
        QVector<Line> lines;
        for (const LobbyChatLine& c : m_lobby.chat) {
            const QString head = c.kind == 2 ? QString() : (c.who + QStringLiteral(": "));
            const QColor col = c.kind == 2 ? kSys : (c.kind == 0 ? COLOR.pure : kGuest);
            QString rest = head + c.text;
            while (!rest.isEmpty()) {
                int n = rest.size();
                while (n > 1 && textW(rest.left(n), q) > maxW) --n;
                lines.append({ rest.left(n), col });
                rest = rest.mid(n);
            }
        }
        const int vis = qMax(1, (g.chatLog.height() - pad) / lineH);
        const int from = qMax(0, int(lines.size()) - vis);
        int y = g.chatLog.bottom() - pad - (lines.size() - from) * lineH + 2 * s;
        for (int i = from; i < lines.size(); ++i, y += lineH)
            text(p, lines[i].t, g.chatLog.x() + pad, y, lines[i].c, q);
        p.restore();
    }

    {   // 채팅 입력 줄
        const bool focus = m_chatFocus;
        p.fillRect(g.chatBar, focus ? QColor(0, 0, 140) : QColor(0, 0, 70));
        const QColor edge = (focus || (lobbyVisible() && m_lobbySel == 4)) ? COLOR.pure : QColor(0, 110, 220);
        p.fillRect(QRect(g.chatBar.x(), g.chatBar.y(), g.chatBar.width(), s), edge);
        p.fillRect(QRect(g.chatBar.x(), g.chatBar.bottom() - s + 1, g.chatBar.width(), s), edge);
        const int tx = g.chatBar.x() + 6 * s, ty = g.chatBar.y() + (g.chatBar.height() - ch) / 2;
        const QString shown = m_chatBuf + m_chatPre;
        if (shown.isEmpty() && !focus) {
            text(p, elideTo(T.chatHint, g.chatBar.width() - 12 * s, q), tx, ty, COLOR.textDim, q);
        } else {
            QString t = shown;
            const int room = g.chatBar.width() - 18 * s;
            while (!t.isEmpty() && textW(t, q) > room) t.remove(0, 1);
            text(p, t, tx, ty, COLOR.pure, q);
            if (focus && ((m_lobbyClock.elapsed() / 450) % 2 == 0))
                p.fillRect(QRect(tx + textW(t, q) + s, ty, 2 * s, ch), COLOR.pure);
        }
    }

    {   // 오른쪽: 방 설정
        text(p, T.settings, g.panel.x(), g.panel.y() + 2 * s, QColor(120, 175, 255), q);
        p.fillRect(QRect(g.panel.x(), g.panel.y() + ch + 4 * s, g.panel.width(), s), QColor(0, 90, 200));

        auto row = [&](const QRect& r, int item, const QString& label, bool dim) {
            const bool hot = (m_lobbyHover == item) || (m_lobbySel == item);
            if (hot && item >= 0) p.fillRect(r, QColor(0, 0, 150));
            const QColor fg = dim ? COLOR.textDim : (hot ? COLOR.pure : COLOR.text);
            text(p, label, r.x() + 4 * s, r.y() + (r.height() - ch) / 2, fg, q);
            return fg;
        };
        auto rightText = [&](const QRect& r, const QString& label, const QString& v, const QColor& c) {
            const int lx = r.x() + 4 * s + textW(label, q) + 8 * s;
            const QString t = elideTo(v, r.right() - lx - 4 * s, q);
            text(p, t, r.right() - textW(t, q) - 4 * s, r.y() + (r.height() - ch) / 2, c, q);
        };
        row(g.rowGame, -1, T.game, true);
        rightText(g.rowGame, T.game, m_lobby.gameLabel.isEmpty() ? QStringLiteral("-") : m_lobby.gameLabel, kHost);
        {   // DELAY
            const QColor fg = row(g.rowDelay, 1, T.delay, !m_lobby.delayEditable);
            const QString v = QStringLiteral("%1f").arg(m_lobby.delay);
            if (m_lobby.delayEditable) {
                drawSpinner(p, g.spinL.x(), g.spinL.y(), g.spinL.width(), g.spinL.height(), 2, m_lobbyHover == 1);
                drawSpinner(p, g.spinR.x(), g.spinR.y(), g.spinR.width(), g.spinR.height(), 3, m_lobbyHover == 1);
                const int mid = (g.spinL.right() + g.spinR.x()) / 2;
                text(p, v, mid - textW(v, q) / 2, g.rowDelay.y() + (g.rowDelay.height() - ch) / 2, fg, q);
            } else {
                text(p, v, g.rowDelay.right() - textW(v, q) - 4 * s, g.rowDelay.y() + (g.rowDelay.height() - ch) / 2, fg, q);
            }
        }
        {   // WIN COUNTER
            const QColor fg = row(g.rowWin, 2, T.win, false);
            rightText(g.rowWin, T.win, m_lobby.winCounter ? T.on : T.off, m_lobby.winCounter ? kReady : fg);
        }
        if (m_lobby.mySlot >= 1) {   // 내 자리: 3번 칸이면 플레이어/관전 전환 (대전 게임이면 관전 고정)
            const bool sw = m_lobby.seatSwitchable;
            const QColor fg = row(g.rowSeat, sw ? 3 : -1, T.seat, !sw);
            const QString v = m_lobby.mySpectator ? (sw ? T.seatSpec : T.seatFixed) : T.seatPlayer;
            rightText(g.rowSeat, T.seat, v, m_lobby.mySpectator ? COLOR.textDim : fg);
        }

        const bool selMain = (m_lobbySel == 0) || (m_lobbyHover == 0);
        if (m_lobby.isHost) {
            button(g.btnMain, T.start, selMain, m_lobby.canStart, QColor(0, 120, 40));
            if (!m_lobby.canStart && !T.startHint.isEmpty()) {
                const QString h = elideTo(T.startHint, g.btnMain.width(), q);
                text(p, h, g.btnMain.x() + (g.btnMain.width() - textW(h, q)) / 2, g.btnMain.y() - ch - 2 * s, COLOR.textDim, q);
            }
        } else {
            button(g.btnMain, m_lobby.myReady ? T.readyOn : T.ready, selMain, m_lobby.connected,
                   m_lobby.myReady ? QColor(0, 140, 60) : QColor(0, 70, 190));
        }
        button(g.btnLeave, T.leave, (m_lobbySel == 5) || (m_lobbyHover == 5), true, QColor(120, 20, 20));
    }
}

// ── 방 만들기 전 "내 캐릭터" 카드 (MULTIPLAYER 목록 오른쪽) ──────────
//   카드 한 장 + 그 아래 경험치 막대와 단계 이름
void NeoRageXShell::drawSideCard(QPainter& p) {
    const QRect r = sideRect();
    if (r.isEmpty()) return;
    const int s = m_L.s, ch = subCellH(), q = subQ();
    drawCard(p, r, m_sideMember, 0, false, QString());
    const int tier = avatar::tierOf(m_sideMember.level);
    QColor tc = avatar::tierColor(tier);
    int y = r.bottom() + 4 * s;
    const QString tn = avatar::tierName(tier, false);
    text(p, elideTo(m_sideCaption.isEmpty() ? tn : m_sideCaption, r.width(), q), r.x(), y, tc, q);
    y += ch + 3 * s;
    if (m_sideMember.need > 0) {                                   // 경험치 막대
        const QRect bar(r.x(), y, r.width(), qMax(4 * s, ch / 2));
        p.fillRect(bar, QColor(0, 0, 60));
        p.fillRect(QRect(bar.x(), bar.y(), bar.width() * m_sideMember.into / qMax(1, m_sideMember.need), bar.height()), tc);
        p.fillRect(QRect(bar.x(), bar.y(), bar.width(), s), QColor(255, 255, 255, 90));
    }
}

// ── 조작 ─────────────────────────────────────────────────────
void NeoRageXShell::lobbyActivate(int item, int delta) {
    switch (item) {
    case 0: emit lobbyAction(QStringLiteral("main"), 0); break;
    case 1: if (m_lobby.delayEditable) emit lobbyAction(QStringLiteral("delay"), delta ? delta : +1); break;
    case 2: emit lobbyAction(QStringLiteral("win"), 0); break;
    case 3: if (m_lobby.seatSwitchable) emit lobbyAction(QStringLiteral("seat"), 0); break;
    case 4: setChatFocus(true); m_lobbySel = 4; update(); break;
    case 5: emit lobbyAction(QStringLiteral("leave"), 0); break;
    case 6: emit lobbyAction(QStringLiteral("copy"), 0); break;
    case 7: closeMenu(); break;
    }
}

bool NeoRageXShell::lobbyNavigate(Nav a) {
    switch (a) {
    case Nav::Up:    m_lobbySel = qMax(0, m_lobbySel - 1); break;
    case Nav::Down:  m_lobbySel = qMin(5, m_lobbySel + 1); break;
    case Nav::Home:  m_lobbySel = 0; break;
    case Nav::End:   m_lobbySel = 5; break;
    case Nav::Left:  if (m_lobbySel == 1) lobbyActivate(1, -1); return true;
    case Nav::Right: if (m_lobbySel == 1) lobbyActivate(1, +1); return true;
    case Nav::Accept: lobbyActivate(m_lobbySel, +1); return true;
    case Nav::Search: emit lobbyAction(QStringLiteral("chatprompt"), 0); return true;   // 게임패드: 화면 키보드
    case Nav::Back:  closeMenu(); return true;                                         // 방은 그대로 두고 닫기만
    default: return false;
    }
    update();
    return true;
}

void NeoRageXShell::setChatFocus(bool on) {
    if (m_chatFocus == on) return;
    m_chatFocus = on;
    if (!on) m_chatPre.clear();
    if (QInputMethod* im = QGuiApplication::inputMethod()) {
        im->update(Qt::ImEnabled | Qt::ImCursorRectangle);
        if (on) im->show(); else im->hide();
    }
    update();
}

void NeoRageXShell::lobbyChatSubmit() {
    const QString t = m_chatBuf.trimmed();
    m_chatBuf.clear();
    m_chatPre.clear();
    if (!t.isEmpty()) emit lobbyChatSend(t);
    update();
}

// 채팅 입력. 입력 줄에 초점이 있으면 글자를 받고, 없을 때 글자를 치면 자동으로 초점이 옮겨 간다.
bool NeoRageXShell::lobbyKey(QKeyEvent* e) {
    if (!lobbyVisible()) return false;
    const Qt::KeyboardModifiers mods = e->modifiers();
    if (m_chatFocus) {
        switch (e->key()) {
        case Qt::Key_Escape:
            setChatFocus(false); update(); e->accept(); return true;
        case Qt::Key_Return: case Qt::Key_Enter:
            if (!e->isAutoRepeat()) lobbyChatSubmit();
            e->accept(); return true;
        case Qt::Key_Backspace:
            m_chatBuf.chop(1); update(); e->accept(); return true;
        case Qt::Key_Up: case Qt::Key_Down:
            setChatFocus(false); return false;
        default: break;
        }
        const QString t = e->text();
        if (!(mods & Qt::ControlModifier) && !t.isEmpty() && t.at(0).isPrint()) {
            if (m_chatBuf.size() < 200) m_chatBuf += t;
            update(); e->accept(); return true;
        }
        e->accept();
        return true;
    }
    const QString t = e->text();
    if (!(mods & (Qt::ControlModifier | Qt::AltModifier)) && !t.isEmpty() && t.at(0).isPrint()
        && t.at(0) != QLatin1Char('[') && t.at(0) != QLatin1Char(']') && t.at(0) != QLatin1Char(' ')) {
        setChatFocus(true); m_lobbySel = 4;
        m_chatBuf += t;
        update(); e->accept(); return true;
    }
    return false;
}
