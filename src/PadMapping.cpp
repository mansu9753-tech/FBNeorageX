// PadMapping.cpp — 패드 매핑 해석

#include "PadMapping.h"

static QString layoutSuffix(PadLayout layout) {
    return layout == PadLayout::SixButton ? QStringLiteral("#6btn") : QStringLiteral("#std");
}
QString padScopeKeyGame(const QString& rom, const QString& device, PadLayout layout) {
    if (rom.isEmpty() || device.isEmpty()) return QString();
    return QStringLiteral("game:") + rom + QStringLiteral("|") + device + layoutSuffix(layout);
}
QString padScopeKeyPlat(const QString& platKey, const QString& device) {
    if (platKey.isEmpty() || device.isEmpty()) return QString();
    return platKey + QStringLiteral("|") + device;
}
QString padScopeKeyDev(const QString& device, PadLayout layout) {
    if (device.isEmpty()) return QString();
    return QStringLiteral("dev:") + device + layoutSuffix(layout);
}

QString padMapSourceLabel(PadMapSource s, bool english) {
    switch (s) {
    case PadMapSource::Game:     return english ? "Game"     : "게임별";
    case PadMapSource::Platform: return english ? "Platform" : "기종별";
    case PadMapSource::Device:   return english ? "Device"   : "장치별";
    default:                     return english ? "Default"  : "기본값";
    }
}

PadMap resolvePadMap(const PadStore& store,
                     const QString& device,
                     const QString& rom,
                     const QString& platKey,
                     PadLayout layout,
                     const PadMap& dflt,
                     PadMapSource* sourceOut)
{
    auto take = [&](const QString& key, PadMapSource src, PadMap* out) -> bool {
        if (key.isEmpty()) return false;
        auto it = store.constFind(key);
        if (it == store.constEnd() || it.value().isEmpty()) return false;
        if (sourceOut) *sourceOut = src;
        *out = it.value();
        return true;
    };

    PadMap m;
    if (take(padScopeKeyGame(rom, device, layout), PadMapSource::Game,     &m)) return m;
    if (take(padScopeKeyPlat(platKey, device),     PadMapSource::Platform, &m)) return m;
    if (take(padScopeKeyDev(device, layout),  PadMapSource::Device,   &m)) return m;

    if (sourceOut) *sourceOut = PadMapSource::Default;
    return dflt;
}

// ── 의미 이름 / 실제 인덱스로 변환 ───────────────────────────
QString padSemName(int slot, bool english) {
    switch (slot) {
    case SEM_LP: return english ? "LP  Weak Punch"   : "LP  약손";
    case SEM_MP: return english ? "MP  Medium Punch" : "MP  중손";
    case SEM_HP: return english ? "HP  Strong Punch" : "HP  강손";
    case SEM_LK: return english ? "LK  Weak Kick"    : "LK  약발";
    case SEM_MK: return english ? "MK  Medium Kick"  : "MK  중발";
    case SEM_HK: return english ? "HK  Strong Kick"  : "HK  강발";
    default:     return QStringLiteral("?");
    }
}

PadMap materializePadMap(const PadMap& in, const CoreSixButtons& cb) {
    PadMap out;
    for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
        const int v = it.value();
        if (!padIsSem(v)) { out.insert(it.key(), v); continue; }
        if (!cb.valid) continue;                 // 아직 코어 정의를 모른다
        int idx = -1;
        switch (padSemSlot(v)) {
        case SEM_LP: idx = cb.lp; break;
        case SEM_MP: idx = cb.mp; break;
        case SEM_HP: idx = cb.hp; break;
        case SEM_LK: idx = cb.lk; break;
        case SEM_MK: idx = cb.mk; break;
        case SEM_HK: idx = cb.hk; break;
        default: break;
        }
        if (idx >= 0) out.insert(it.key(), idx);
    }
    return out;
}
