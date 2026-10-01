// ShellPageControls.cpp — CONTROLS 카테고리
//
//  키보드 / 게임패드 / 아케이드 스틱의 버튼 배정을 보여 주고 다시 배정한다.
//  · 매핑을 저장하고 해석하는 로직은 MainWindow 에 그대로 있다 (ControlsApi 로만 부른다).
//    이 페이지는 "보여 주기" 와 "어느 줄을 눌렀는지 전달" 만 한다.
//  · 새 입력을 기다리는 창(키 캡처 / 패드 캡처)도 앱 쪽 것을 그대로 쓴다.
//    캡처 중에는 셸과 패드 핫키가 멈춘다 (m_captureActive + 모달 창).
//
//  화면은 다섯 구역으로 나눈다:  장치 / 키 매핑 / 저장 / 터보 / 핫키.

#include "ShellPages.h"

#include <QDateTime>

#include "AppSettings.h"

namespace {

// 게임 정보가 없을 때 쓰는 기본 터보 버튼 여섯 개: libretro 인덱스 + 이름
struct TurboBtn { int idx; const char* name; };
constexpr TurboBtn kTurbo[] = {
    {0, "A"}, {8, "B"}, {1, "C"}, {9, "D"}, {10, "CD"}, {11, "AB"}
};

// 고정 핫키 (읽기 전용 안내). 장치마다 따로 보여 준다.
struct HotkeyRow { const char* ko; const char* en; const char* keys; };

constexpr HotkeyRow kKeyboardHotkeys[] = {
    {"게임 <-> 메뉴",            "GAME/MENU",            "Tab"},
    {"게임 종료",                "EXIT",                 "Esc"},
    {"서비스 TEST",              "SERVICE",              "`"},
    {"패스트포워드",             "FAST FWD",             "F11"},
    {"전체화면",                 "FULLSCREEN",           "Alt+Enter"},
    {"스테이트 저장",            "SAVE STATE",           "Shift+F1-F8"},
    {"스테이트 불러오기",        "LOAD STATE",           "F1-F8"},
    {"스크린샷",                 "SCREENSHOT",           "F12"},
    {"프리뷰 이미지 저장",       "SAVE PREVIEW IMAGE",   "Ctrl+F12"},
    {"녹화",                     "RECORD",               "F9"},
    {"프리뷰 영상 녹화",         "RECORD PREVIEW",       "Ctrl+F9"},
    {"1P <-> 2P 스왑",           "SWAP 1P / 2P",         "F10"},
};

// 게임패드: SELECT 를 누른 채 다른 버튼 (RetroArch 방식). L3/R3 는 쓰지 않는다.
constexpr HotkeyRow kPadHotkeys[] = {
    {"게임 <-> 메뉴",            "GAME/MENU",            "SELECT+START"},
    {"게임 종료",                "EXIT",                 "SELECT+Y"},
    {"서비스 TEST",              "SERVICE",              "SELECT+L1"},
    {"패스트포워드",             "FAST FWD",             "SELECT+R1"},
    {"스테이트 저장",            "SAVE STATE",           "SELECT+A"},
    {"스테이트 불러오기",        "LOAD STATE",           "SELECT+B"},
    {"스크린샷",                 "SCREENSHOT",           "SELECT+X"},
    {"저장 슬롯 다음 (1~8)",     "NEXT STATE SLOT (1-8)","SELECT+RIGHT"},
    {"프리뷰 이미지 저장",       "SAVE PREVIEW IMAGE",   "SELECT+LEFT"},
    {"전체화면",                 "FULLSCREEN",           "SELECT+UP"},
    {"1P <-> 2P 스왑",           "SWAP 1P / 2P",         "SELECT+DOWN"},
    {"녹화",                     "RECORD",               "SELECT+L2"},
    {"프리뷰 영상 녹화",         "RECORD PREVIEW",       "SELECT+R2"},
};

// 아케이드 스틱: 게임패드와 같은 위치의 버튼 (1~4 = A B X Y, 5/6 = L1/R1, 7/8 = L2/R2, 9 = SELECT, 10 = START)
constexpr HotkeyRow kStickHotkeys[] = {
    {"게임 <-> 메뉴",            "GAME/MENU",            "BTN9+BTN10"},
    {"게임 종료",                "EXIT",                 "BTN9+BTN4"},
    {"서비스 TEST",              "SERVICE",              "BTN9+BTN5"},
    {"패스트포워드",             "FAST FWD",             "BTN9+BTN6"},
    {"스테이트 저장",            "SAVE STATE",           "BTN9+BTN1"},
    {"스테이트 불러오기",        "LOAD STATE",           "BTN9+BTN2"},
    {"스크린샷",                 "SCREENSHOT",           "BTN9+BTN3"},
    {"저장 슬롯 다음 (1~8)",     "NEXT STATE SLOT (1-8)","BTN9+RIGHT"},
    {"프리뷰 이미지 저장",       "SAVE PREVIEW IMAGE",   "BTN9+LEFT"},
    {"전체화면",                 "FULLSCREEN",           "BTN9+UP"},
    {"1P <-> 2P 스왑",           "SWAP 1P / 2P",         "BTN9+DOWN"},
    {"녹화",                     "RECORD",               "BTN9+BTN7"},
    {"프리뷰 영상 녹화",         "RECORD PREVIEW",       "BTN9+BTN8"},
};

class ControlsPage final : public ShellPage {
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("controls"); }

    QVector<Row> rows() override {
        const ControlsApi& c = m_h.controls;
        QVector<Row> out;

        // ═══ 1. 장치 ═══════════════════════════════════════
        out << Row::heading(en() ? "[ DEVICE ]" : "[ 장치 ]");
        static const char* kDev[] = {"KEYBOARD", "GAMEPAD", "ARCADE STICK"};
        out << Row::value_(QStringLiteral("DEVICE"), QString::fromLatin1(kDev[m_dev]),
                           [this](int d) { m_dev = wrap(m_dev, d, 3); });

        if (m_dev == 1) {
            // 리매핑 대상 패드 + 지금 적용 중인 출처
            const QVector<ControlPad> pads = c.pads ? c.pads() : QVector<ControlPad>();
            if (!pads.isEmpty()) {
                int cur = 0;
                const int tgt = c.targetPad ? c.targetPad() : 0;
                for (int i = 0; i < pads.size(); ++i) if (pads[i].index == tgt) cur = i;
                out << Row::value_(QStringLiteral("TARGET PAD"), padLabel(pads[cur]),
                                   [this, pads, cur](int d) {
                                       if (!m_h.controls.setTargetPad) return;
                                       const int n = wrap(cur, d, int(pads.size()));
                                       m_h.controls.setTargetPad(pads[n].index);
                                   });
            }
            if (c.sourceText) out << Row::note(c.sourceText());

            // 플레이어 배정
            if (pads.isEmpty())
                out << Row::note(en() ? "NO GAMEPAD DETECTED" : "연결된 게임패드 없음");
            for (const ControlPad& p : pads) {
                const int idx = p.index, player = p.player;
                out << Row::value_(QStringLiteral("PLAYER  ") + padLabel(p),
                                   player == 0 ? QStringLiteral("OFF") : QStringLiteral("%1P").arg(player),
                                   [this, idx, player](int d) {
                                       if (m_h.controls.setPadPlayer)
                                           m_h.controls.setPadPlayer(idx, wrap(player, d, 5));
                                   });
            }
        }

        // 입력 방식(XInput / DInput): 게임패드와 스틱에만 뜻이 있다
        if (m_dev != 0) {
            static const char* kModes[]     = {"auto", "xinput", "winmm"};
            static const char* kModeLabel[] = {"AUTO", "XINPUT", "DINPUT"};
            const QString cur = c.inputMode ? c.inputMode() : QStringLiteral("auto");
            int mi = 0;
            for (int i = 0; i < 3; ++i) if (cur == QLatin1String(kModes[i])) mi = i;
            out << Row::value_(QStringLiteral("GAMEPAD MODE"), QString::fromLatin1(kModeLabel[mi]),
                               [this, mi](int d) {
                                   if (m_h.controls.setInputMode)
                                       m_h.controls.setInputMode(QString::fromLatin1(kModes[wrap(mi, d, 3)]));
                               });
        }

        // ═══ 2. 키 매핑 ════════════════════════════════════
        out << Row::note(QString());
        out << Row::heading(en() ? "[ BUTTON MAPPING ]" : "[ 키 매핑 ]");
        out << Row::note(en() ? "PICK A ROW, THEN PRESS THE NEW KEY / BUTTON (ESC = CANCEL)"
                              : "줄을 고르고 새 키/버튼을 누르세요 (Esc = 취소)");
        const QVector<ControlSlot> binds = c.bindings ? c.bindings(m_dev) : QVector<ControlSlot>();
        for (int i = 0; i < binds.size(); ++i) {
            const int dev = m_dev;
            out << Row::action(binds[i].action,
                               [this, dev, i] { if (m_h.controls.remap) m_h.controls.remap(dev, i); },
                               binds[i].bound);
        }
        {
            static const char* kResetName[] = {"RESET KEYBOARD", "RESET GAMEPAD", "RESET ARCADE STICK"};
            const int dev = m_dev;
            out << Row::action(QString::fromLatin1(kResetName[dev]),
                               [this, dev] { if (m_h.controls.resetDevice) m_h.controls.resetDevice(dev); });
        }

        // ═══ 3. 저장 ═══════════════════════════════════════
        out << Row::note(QString());
        out << Row::heading(en() ? "[ SAVE ]" : "[ 저장 ]");
        out << Row::note(en() ? "CHANGES ARE SAVED AUTOMATICALLY FOR THIS GAME (MAPPING + TURBO)"
                              : "바꾸면 이 게임에 자동 저장됩니다 (매핑 + 터보)");
        {
            const QString plat = c.platformLabel ? c.platformLabel() : QStringLiteral("OTHER");
            out << Row::action(saveAllLabel(plat),
                               [this] { if (m_h.controls.savePlatform) m_h.controls.savePlatform(); });
            out << Row::note(en() ? "KEYBOARD / GAMEPAD / STICK / TURBO. 6-BUTTON FIGHTERS ARE SAVED SEPARATELY"
                                  : "키보드·게임패드·스틱·터보 전부. 6버튼 격투는 따로 저장됩니다");
        }
        out << Row::action(confirmLabel(m_forgetArmedAt,
                                        en() ? "CLEAR THIS GAME'S SAVED CONTROLS" : "이 게임 저장값 지우기"),
                           [this] {
                               if (!armed(m_forgetArmedAt)) return;
                               if (m_h.controls.forgetGame) m_h.controls.forgetGame();
                           });
        if (m_dev == 1) {
            out << Row::action(confirmLabel(m_clearArmedAt,
                                            en() ? "CLEAR ALL PAD SETTINGS" : "모든 패드 설정 삭제"),
                               [this] {
                                   if (!armed(m_clearArmedAt)) return;
                                   if (m_h.controls.clearPadProfiles) m_h.controls.clearPadProfiles();
                               });
        }

        // ═══ 4. 터보 ═══════════════════════════════════════
        out << Row::note(QString());
        out << Row::heading(en() ? "[ TURBO ]" : "[ 터보 ]");
        // 이름·순서는 지금 게임의 컨트롤 표와 같다 (A B C D / 약손 중손 …). 게임 정보가 없으면 기본 여섯 개.
        QVector<QPair<int,QString>> tb = c.turboButtons ? c.turboButtons() : QVector<QPair<int,QString>>();
        if (tb.isEmpty())
            for (const TurboBtn& t : kTurbo) tb.append({t.idx, QString::fromLatin1(t.name)});
        for (const auto& t : tb) {
            const int idx = t.first;
            const bool on = c.turbo && c.turbo(idx);
            out << Row::flip(QStringLiteral("TURBO ") + t.second,
                               on ? QStringLiteral("ON") : QStringLiteral("OFF"),
                               [this, idx, on](int) { if (m_h.controls.setTurbo) m_h.controls.setTurbo(idx, !on); });
        }
        {
            const int per = c.turboPeriod ? c.turboPeriod() : 6;
            out << Row::value_(en() ? "TURBO PERIOD (FRAMES)" : "터보 주기 (프레임)", QString::number(per),
                               [this, per](int d) {
                                   if (m_h.controls.setTurboPeriod)
                                       m_h.controls.setTurboPeriod(clampStep(per, d, 1, 1, 30));
                               });
        }

        // ═══ 5. 고정 핫키 ══════════════════════════════════
        out << Row::note(QString());
        out << Row::heading(en() ? "[ HOTKEYS (FIXED) ]" : "[ 핫키 (고정) ]");
        const HotkeyRow* hk = kKeyboardHotkeys;
        int n = int(sizeof(kKeyboardHotkeys) / sizeof(kKeyboardHotkeys[0]));
        if (m_dev == 1) { hk = kPadHotkeys;   n = int(sizeof(kPadHotkeys)   / sizeof(kPadHotkeys[0])); }
        if (m_dev == 2) { hk = kStickHotkeys; n = int(sizeof(kStickHotkeys) / sizeof(kStickHotkeys[0])); }
        for (int i = 0; i < n; ++i)
            out << Row::note(QString::fromUtf8(en() ? hk[i].en : hk[i].ko), QString::fromUtf8(hk[i].keys));
        if (m_dev != 0) {
            out << Row::note(en() ? "HOLD SELECT, THEN PRESS THE OTHER BUTTON. SELECT ALONE = COIN"
                                  : "SELECT 를 누른 채 다른 버튼을 누릅니다. SELECT 만 누르면 코인");
            out << Row::note(en() ? "WHILE SELECT IS HELD, OTHER BUTTONS ARE NOT SENT TO THE GAME"
                                  : "SELECT 를 누르는 동안 다른 버튼은 게임에 전달되지 않습니다");
        }
        return out;
    }

private:
    static QString padLabel(const ControlPad& p) {
        return p.name.isEmpty() ? QStringLiteral("PAD %1").arg(p.index + 1) : p.name;
    }

    QString saveAllLabel(const QString& plat) const {
        if (en()) return QStringLiteral("SAVE FOR ALL %1 GAMES").arg(plat);
        const QString ko = plat == QLatin1String("NEOGEO") ? QStringLiteral("네오지오")
                         : plat == QLatin1String("CPS")    ? QStringLiteral("CPS")
                                                           : QStringLiteral("기타 기종");
        return ko + QStringLiteral(" 전체에 저장");
    }

    // 위험한 동작은 5초 안에 두 번 눌러야 실행한다 (SYSTEM 의 RESET DEFAULTS 와 같은 규칙)
    bool armed(qint64& at) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (at != 0 && now - at <= 5000) { at = 0; return true; }
        at = now;
        say(en() ? "Press again within 5 s to confirm" : "5초 안에 한 번 더 누르면 실행됩니다");
        return false;
    }
    QString confirmLabel(qint64 at, const QString& l) const {
        const bool live = at != 0 && QDateTime::currentMSecsSinceEpoch() - at <= 5000;
        return live ? (en() ? QStringLiteral("PRESS AGAIN TO CONFIRM")
                            : QStringLiteral("한 번 더 누르면 실행"))
                    : l;
    }

    int    m_dev = 0;             // 화면에 보이는 장치 (페이지 로컬 상태)
    qint64 m_clearArmedAt = 0;
    qint64 m_forgetArmedAt = 0;
};

}  // namespace

std::unique_ptr<ShellPage> makeControlsPage(const ShellHost& host) {
    return std::make_unique<ControlsPage>(host);
}
