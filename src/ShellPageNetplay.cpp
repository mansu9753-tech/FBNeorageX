// ShellPageNetplay.cpp — MULTIPLAYER 카테고리
//
//  HOST / JOIN / START / DISCONNECT 와 상태 표시. 연결 절차(STUN·릴레이·UPnP)는
//  앱 쪽에 그대로 있고, 이 페이지는 보여 주기와 "무엇을 눌렀는지 전달" 만 한다.
//  글자를 입력해야 하는 항목(룸 코드·IP·포트·릴레이)은 OS 입력창을 쓴다
//  (스팀덱에서는 화면 키보드가 뜬다).

#include "ShellPages.h"
#include "AppSettings.h"

#include <functional>

namespace {

class NetplayPage final : public ShellPage {
    QString signup_hint(bool signup) const {
        return signup ? (en() ? "ID: 3-16 LETTERS/DIGITS/_   PASSWORD: 6+ CHARS" : "아이디 영문·숫자·밑줄 3~16자, 비밀번호 6자 이상")
                      : (en() ? "NO ACCOUNT YET? CHOOSE SIGN UP" : "계정이 없으면 회원가입을 누르세요");
    }
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("netplay"); }

    QVector<Row> rows() override {
        const NetApi& n = m_h.net;
        QVector<Row> out;
        if (!n.state) return out;
        const NetState s = n.state();

        // 방에 있을 때는 이 목록 대신 방 화면이 열린다.
        const NetProfileView pv = n.profile ? n.profile() : NetProfileView();

        // ── 로그인 전: 로그인 화면 / 회원가입 화면만 보인다 ──
        if (!pv.loggedIn) {
            const NetAuthView av = n.auth ? n.auth() : NetAuthView();
            auto act = [this](const QString& label, std::function<void()> fn, const QString& val = QString()) {
                return Row::action(label, [fn] { if (fn) fn(); }, val);
            };
            if (!av.signup) {
                out << Row::heading(en() ? "LOGIN" : "로그인");
                out << act(en() ? "ID" : "아이디", [this] { if (m_h.net.authEdit) m_h.net.authEdit("id"); },
                           av.id.isEmpty() ? (en() ? QStringLiteral("(ENTER)") : QStringLiteral("(입력)")) : av.id);
                out << act(en() ? "PASSWORD" : "비밀번호", [this] { if (m_h.net.authEdit) m_h.net.authEdit("pw"); },
                           av.pwMask.isEmpty() ? (en() ? QStringLiteral("(ENTER)") : QStringLiteral("(입력)")) : av.pwMask);
                out << act(en() ? "LOG IN" : "로그인", [this] { if (m_h.net.authSubmit) m_h.net.authSubmit(); },
                           av.busy ? (en() ? QStringLiteral("...") : QStringLiteral("연결 중...")) : QString());
                out << act(en() ? "SIGN UP" : "회원가입", [this] { if (m_h.net.authMode) m_h.net.authMode(true); });
            } else {
                out << Row::heading(en() ? "SIGN UP" : "회원가입");
                out << act(en() ? "ID" : "아이디", [this] { if (m_h.net.authEdit) m_h.net.authEdit("id"); },
                           av.id.isEmpty() ? (en() ? QStringLiteral("(ENTER)") : QStringLiteral("(입력)")) : av.id);
                out << act(en() ? "PASSWORD" : "비밀번호", [this] { if (m_h.net.authEdit) m_h.net.authEdit("pw"); },
                           av.pwMask.isEmpty() ? (en() ? QStringLiteral("(ENTER)") : QStringLiteral("(입력)")) : av.pwMask);
                out << act(en() ? "NICKNAME" : "닉네임", [this] { if (m_h.net.authEdit) m_h.net.authEdit("nick"); },
                           av.nick.isEmpty() ? (en() ? QStringLiteral("(ENTER)") : QStringLiteral("(입력)")) : av.nick);
                out << Row::value_(en() ? "GENDER" : "성별", av.genderLabel, [this](int d) { if (m_h.net.authGender) m_h.net.authGender(d); });
                out << act(en() ? "CREATE ACCOUNT" : "가입하기", [this] { if (m_h.net.authSubmit) m_h.net.authSubmit(); },
                           av.busy ? (en() ? QStringLiteral("...") : QStringLiteral("연결 중...")) : QString());
                out << act(en() ? "BACK TO LOGIN" : "로그인 화면으로", [this] { if (m_h.net.authMode) m_h.net.authMode(false); });
            }
            if (!av.message.isEmpty()) out << Row::note(av.message);
            else out << Row::note(signup_hint(av.signup));
            return out;
        }

        // ── 로그인 후: 로비(방 목록) + 방 만들기 + 내 프로필 ──
        out << Row::heading(en() ? "LOBBY - ROOMS" : "로비 — 방 목록");
        {
            const QVector<NetRoomRow> rooms = n.rooms ? n.rooms() : QVector<NetRoomRow>();
            if (rooms.isEmpty())
                out << Row::note(en() ? "NO ROOMS YET - CREATE ONE" : "열려 있는 방이 없습니다 — 방을 만들어 보세요");
            for (const NetRoomRow& r : rooms) {
                const QString game = r.game.isEmpty() ? (en() ? QStringLiteral("(NO GAME)") : QStringLiteral("(게임 미정)")) : r.game;
                const QString label = QStringLiteral("%1  -  %2").arg(r.host, game);      // 방장 닉네임 - 게임
                if (r.joinable)
                    out << Row::action(label, [this, code = r.code] { if (m_h.net.joinRoom) m_h.net.joinRoom(code); }, r.seats);
                else
                    out << Row::note(label, QStringLiteral("%1 [%2]").arg(r.seats, r.reason));
            }
            out << Row::action(en() ? QStringLiteral("REFRESH") : QStringLiteral("새로고침"),
                               [this] { if (m_h.net.refreshRooms) m_h.net.refreshRooms(); });
        }
        out << Row::action(en() ? QStringLiteral("CREATE ROOM") : QStringLiteral("방 만들기"),
                           [this, s] { if (s.canHost && m_h.net.hostPick) m_h.net.hostPick(); });
        out << Row::note(en() ? "ROOMS NEED YOUR OWN COPY OF THE GAME ROM" : "방의 게임 ROM 이 내 PC 에 있어야 입장할 수 있습니다");

        // ── 내 프로필: 계정, 닉네임, 캐릭터 꾸미기 ──
        out << Row::note("");
        out << Row::heading(en() ? "MY PROFILE" : "내 프로필");
        out << Row::action(en() ? QStringLiteral("ACCOUNT") : QStringLiteral("계정 정보"),
                           [this] { if (m_h.net.logout) m_h.net.logout(); },
                           QStringLiteral("%1  Lv%2  %3").arg(pv.accountId).arg(pv.level)
                               .arg(en() ? QStringLiteral("LOGOUT") : QStringLiteral("로그아웃")));
        out << Row::action(en() ? QStringLiteral("NICKNAME") : QStringLiteral("닉네임"),
                           [this] { if (m_h.net.editNick) m_h.net.editNick(); }, pv.nick);
        for (const NetProfileView::Slot& sl : pv.slotList) {
            out << Row::value_(sl.label, sl.value, [this, key = sl.key](int d) {
                                   if (m_h.net.cyclePart) m_h.net.cyclePart(key, d);
                               });
            if (!sl.nextUnlock.isEmpty()) out << Row::note(sl.nextUnlock);
        }
        out << Row::note(en() ? "LEVEL RISES WITH SET WINS IN KOF GAMES (1 WIN = 1 EXP)"
                              : "KOF 대전 게임에서 세트를 이기면 경험치가 오릅니다 (1승 = 1)");
        return out;
    }
};

}  // namespace

std::unique_ptr<ShellPage> makeNetplayPage(const ShellHost& host) {
    return std::make_unique<NetplayPage>(host);
}
