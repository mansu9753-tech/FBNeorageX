// ShellPageAudio.cpp — AUDIO OPTIONS 카테고리

#include "ShellPages.h"

#include "AppSettings.h"
#include "AudioManager.h"
#include "SoundMode.h"

namespace {

class AudioPage final : public ShellPage {
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("audio"); }

    QVector<Row> rows() override {
        return {
            volume(),
            soundScope(),
            soundMode(),
            sampleRate(),
            buffer(),
        };
    }

private:
    // 사운드 모드를 저장할 범위. 화면에만 있는 상태라 페이지가 들고 있는다.
    //   (값 자체의 진실은 gSettings 의 세 저장소다)
    enum class Scope { Global, Platform, Game };
    Scope m_scope = Scope::Global;

    // ── 지금 보고 있는 게임과 기종 ─────────────────────────
    QString rom() const {
        QString r = m_h.loadedGame ? m_h.loadedGame() : QString();
        if (r.isEmpty() && m_h.selectedGame) r = m_h.selectedGame();
        return r;
    }
    QString platform() const {
        const QString r = rom();
        return (m_h.platformOf && !r.isEmpty()) ? m_h.platformOf(r) : QString();
    }

    // 고를 수 있는 범위 목록. 게임이나 기종을 모르면 그 범위는 뺀다.
    QVector<Scope> availableScopes() const {
        QVector<Scope> v { Scope::Global };
        if (!platform().isEmpty()) v << Scope::Platform;
        if (!rom().isEmpty())      v << Scope::Game;
        return v;
    }
    Scope effectiveScope() const {
        const auto v = availableScopes();
        return v.contains(m_scope) ? m_scope : Scope::Global;
    }

    void commit() {
        gSettings.save();
    }

    // ── VOLUME ──────────────────────────────────────────
    Row volume() {
        return Row::value_(QStringLiteral("VOLUME"),
                           QString::number(gSettings.audioVolume) + QStringLiteral("%"),
                           [this](int d) {
            gSettings.audioVolume = clampStep(gSettings.audioVolume, d, 5, 0, 100);
            // AudioManager 는 0.0~1.0 을 받는다. 퍼센트를 그대로 넘기면 1.0 으로
            // 잘려서 어떤 값을 골라도 실제 음량이 100% 로 고정된다.
            if (m_h.audio) m_h.audio->setVolume(gSettings.audioVolume / 100.0);
            commit();
        });
    }

    // ── SOUND SCOPE / SOUND MODE ────────────────────────
    Row soundScope() {
        QString shown;
        switch (effectiveScope()) {
        case Scope::Global:   shown = QStringLiteral("ALL GAMES");                     break;
        case Scope::Platform: shown = platform().toUpper() + QStringLiteral(" ALL");   break;
        case Scope::Game:     shown = QStringLiteral("THIS GAME");                     break;
        }
        return Row::value_(QStringLiteral("SOUND SCOPE"), shown, [this](int d) {
            const auto v = availableScopes();
            m_scope = v.at(wrap(int(v.indexOf(effectiveScope())), d, int(v.size())));
        });
    }

    // 이 범위에 저장된 모드. 없으면 상위 범위에서 물려받는 값을 돌려준다
    // (지금 실제로 들리는 소리와 화면 표시가 항상 일치하도록).
    QString modeKeyAtScope() const {
        const QString r = rom(), pl = platform();
        QString cur;
        switch (effectiveScope()) {
        case Scope::Game:     cur = gSettings.soundModeByGame.value(r);         break;
        case Scope::Platform: cur = gSettings.soundModeByPlatform.value(pl);    break;
        case Scope::Global:   cur = gSettings.soundMode;                        break;
        }
        if (!cur.isEmpty()) return cur;
        switch (effectiveScope()) {
        case Scope::Game:     return gSettings.resolvedSoundMode({}, pl);
        case Scope::Platform: return gSettings.resolvedSoundMode({}, {});
        default:              return QStringLiteral("original");
        }
    }

    Row soundMode() {
        const SoundModeId cur = soundModeFromKey(modeKeyAtScope());
        return Row::value_(QStringLiteral("SOUND MODE"),
                           soundModeLabel(cur, true).toUpper(), [this](int d) {
            const auto& all = soundModeAll();
            const SoundModeId now = soundModeFromKey(modeKeyAtScope());
            int idx = 0;
            for (size_t i = 0; i < all.size(); ++i) if (all[i] == now) idx = int(i);
            const QString key = soundModeKey(all[size_t(wrap(idx, d, int(all.size())))]);

            switch (effectiveScope()) {
            case Scope::Game:     gSettings.soundModeByGame[rom()]             = key; break;
            case Scope::Platform: gSettings.soundModeByPlatform[platform()]    = key; break;
            case Scope::Global:   gSettings.soundMode                          = key; break;
            }
            commit();
            if (m_h.applySoundMode) m_h.applySoundMode();
        });
    }

    // ── 시작할 때만 반영되는 값 ─────────────────────────
    Row sampleRate() {
        static const int rates[3] = { 44100, 48000, 96000 };
        return Row::value_(QStringLiteral("SAMPLE RATE"),
                           QString::number(gSettings.audioSampleRate), [this](int d) {
            int cur = 1;
            for (int i = 0; i < 3; ++i) if (gSettings.audioSampleRate == rates[i]) cur = i;
            gSettings.audioSampleRate = rates[wrap(cur, d, 3)];
            commit();
            say(en() ? "Sample rate applies after restart."
                     : "샘플레이트는 다시 시작해야 적용됩니다.");
        });
    }

    Row buffer() {
        return Row::value_(QStringLiteral("BUFFER"),
                           QString::number(gSettings.audioBufferMs) + QStringLiteral(" MS"),
                           [this](int d) {
            gSettings.audioBufferMs = clampStep(gSettings.audioBufferMs, d, 20, 20, 200);
            commit();
            say(en() ? "Buffer applies after restart."
                     : "버퍼는 다시 시작해야 적용됩니다.");
        });
    }
};

}  // namespace

std::unique_ptr<ShellPage> makeAudioPage(const ShellHost& host) {
    return std::make_unique<AudioPage>(host);
}
