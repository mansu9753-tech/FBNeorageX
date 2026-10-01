// ShellPageNetplay.cpp — MULTIPLAYER 카테고리
//
//  HOST / JOIN / START / DISCONNECT 와 상태 표시. 연결 절차(STUN·릴레이·UPnP)는
//  앱 쪽에 그대로 있고, 이 페이지는 보여 주기와 "무엇을 눌렀는지 전달" 만 한다.
//  글자를 입력해야 하는 항목(룸 코드·IP·포트·릴레이)은 OS 입력창을 쓴다
//  (스팀덱에서는 화면 키보드가 뜬다).

#include "ShellPages.h"

#include <QInputDialog>
#include <QLineEdit>

namespace {

class NetplayPage final : public ShellPage {
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("netplay"); }

    QVector<Row> rows() override {
        const NetApi& n = m_h.net;
        QVector<Row> out;
        if (!n.state) return out;
        const NetState s = n.state();

        out << Row::note(s.status);
        if (!s.rtt.isEmpty()) out << Row::note(s.rtt);
        out << Row::note(QStringLiteral("YOUR IP"),
                         s.publicIp.isEmpty() ? QStringLiteral("...") : s.publicIp);

        // ── HOST ────────────────────────────────────────────
        out << Row::note(QStringLiteral("HOST"));
        out << Row::action(QStringLiteral("PORT"),
                           [this, s] {
                               bool ok = false;
                               const int v = QInputDialog::getInt(m_h.window, QStringLiteral("PORT"),
                                                                  QStringLiteral("UDP port (1024-65535)"),
                                                                  s.port, 1024, 65535, 1, &ok);
                               if (ok && m_h.net.setPort) m_h.net.setPort(v);
                           },
                           QString::number(s.port));
        out << Row::value_(QStringLiteral("DELAY"), QStringLiteral("%1f").arg(s.delay),
                           [this, s](int d) {
                               if (m_h.net.setDelay) m_h.net.setDelay(clampStep(s.delay, d, 1, 0, 8));
                           });
        out << Row::note(en() ? "RECOMMENDED: OVERSEAS 2-4f" : "권장: 해외 2~4f");
        if (s.canHost)
            out << Row::action(QStringLiteral("HOST GAME"), [this] { if (m_h.net.host) m_h.net.host(); });
        if (!s.roomCode.isEmpty()) {
            out << Row::action(QStringLiteral("ROOM CODE"),
                               [this] {
                                   if (!m_h.net.copyCode) return;
                                   m_h.net.copyCode();
                                   say(en() ? "Room code copied" : "룸 코드 복사됨");
                               },
                               s.roomCode + QStringLiteral("  COPY"));
        }
        if (s.canStart)
            out << Row::action(QStringLiteral("START GAME (HOST)"),
                               [this] { if (m_h.net.start) m_h.net.start(); });

        // ── JOIN ────────────────────────────────────────────
        out << Row::note(QStringLiteral("JOIN"));
        out << Row::action(QStringLiteral("ROOM CODE"),
                           [this, s] {
                               bool ok = false;
                               const QString v = QInputDialog::getText(
                                   m_h.window, QStringLiteral("ROOM CODE"),
                                   en() ? QStringLiteral("6-character room code") : QStringLiteral("6자 룸 코드"),
                                   QLineEdit::Normal, s.joinCode, &ok);
                               if (ok && m_h.net.setJoinCode) m_h.net.setJoinCode(v);
                           },
                           s.joinCode.isEmpty() ? QStringLiteral("------") : s.joinCode);
        out << Row::action(QStringLiteral("OR DIRECT IP"),
                           [this, s] {
                               bool ok = false;
                               const QString v = QInputDialog::getText(
                                   m_h.window, QStringLiteral("IP"),
                                   en() ? QStringLiteral("Host IP (used when room code is empty)")
                                        : QStringLiteral("호스트 IP (룸 코드가 비었을 때 사용)"),
                                   QLineEdit::Normal, s.joinIp, &ok);
                               if (ok && m_h.net.setJoinIp) m_h.net.setJoinIp(v);
                           },
                           s.joinIp);
        if (s.canJoin)
            out << Row::action(QStringLiteral("JOIN GAME"), [this] { if (m_h.net.join) m_h.net.join(); });

        // ── 연결 해제 / 릴레이 ───────────────────────────────
        if (s.canDisc)
            out << Row::action(QStringLiteral("DISCONNECT"),
                               [this] { if (m_h.net.disconnect) m_h.net.disconnect(); });

        out << Row::note(QStringLiteral("RELAY"));
        // 릴레이 주소는 계정 ID 를 포함하므로 실제 값을 화면에 절대 보여 주지 않는다.
        out << Row::action(en() ? QStringLiteral("RELAY SERVER") : QStringLiteral("릴레이 서버"),
                           [this] {
                               bool ok = false;
                               const QString v = QInputDialog::getText(
                                   m_h.window, QStringLiteral("RELAY"),
                                   en() ? QStringLiteral("Custom relay URL (empty = built-in)")
                                        : QStringLiteral("커스텀 릴레이 URL (비우면 내장 서버)"),
                                   QLineEdit::Password, QString(), &ok);
                               if (ok && m_h.net.setRelay) m_h.net.setRelay(v);
                           },
                           s.relayBuiltin ? (en() ? QStringLiteral("BUILT-IN") : QStringLiteral("내장"))
                                          : (en() ? QStringLiteral("CUSTOM") : QStringLiteral("커스텀")));

        out << Row::note(en() ? "HOST: SET DELAY, HOST GAME, SHARE THE CODE, PICK A GAME, START GAME"
                              : "호스트: 딜레이 → HOST GAME → 룸 코드 공유 → 게임 선택 → START GAME");
        out << Row::note(en() ? "JOIN: ENTER THE ROOM CODE, JOIN GAME (OR A DIRECT IP)"
                              : "참가: 룸 코드 입력 → JOIN GAME (또는 직접 IP)");
        out << Row::note(en() ? "ESC IN GAME ENDS BOTH GAMES / DISCONNECT RELEASES THE LINK"
                              : "게임 중 ESC = 양쪽 종료 / DISCONNECT = 연결 해제");
        return out;
    }
};

}  // namespace

std::unique_ptr<ShellPage> makeNetplayPage(const ShellHost& host) {
    return std::make_unique<NetplayPage>(host);
}
