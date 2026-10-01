#pragma once
// PadMapping.h — 패드 매핑 "해석" 로직 (저장소 하나, 우선순위 하나)
//
//  예전에는 매핑이 세 군데(전역·기종/게임별·장치별)에 나뉘어 저장되고
//  서로를 덮어써서, 무엇이 적용될지 아무도 예측할 수 없었다.
//  이제 저장소는 padMaps 하나이고, 고르는 규칙은 이 파일에만 있다.
//
//  우선순위:  게임별 > 기종별 > 장치별 > 배치 기본값

#include <QHash>
#include <QString>
#include "ArcadeLayout.h"   // CoreSixButtons

using PadMap   = QHash<int, int>;
using PadStore = QHash<QString, PadMap>;

// ── 의미 기반 저장 (6버튼 격투 전용) ─────────────────────────
//   ★ libretro 인덱스는 게임마다 다르다.
//     같은 스트리트파이터 계열이라도
//        sf2ce  : 1=Weak Punch, 0=Weak Kick ...
//        sfzch  : 0=Weak Punch, 9=Weak Kick ...
//     그래서 "패드버튼 → 인덱스"로 저장하면 다른 게임에서 반드시 어긋난다.
//     대신 "패드버튼 → 약손/중손/강손/약발/중발/강발"을 저장하고,
//     실제 인덱스는 게임이 실행될 때 코어 정의에서 그때그때 채운다.
enum PadSemantic { SEM_LP = 0, SEM_MP, SEM_HP, SEM_LK, SEM_MK, SEM_HK, SEM_COUNT };
constexpr int PAD_SEM_BASE = 1000;
inline int  padSemId(int slot)  { return PAD_SEM_BASE + slot; }
inline bool padIsSem(int value) {
    return value >= PAD_SEM_BASE && value < PAD_SEM_BASE + SEM_COUNT;
}
inline int  padSemSlot(int value) { return value - PAD_SEM_BASE; }
QString padSemName(int slot, bool english);

// 저장된 표(의미 포함)를 이 게임의 실제 인덱스 표로 바꾼다.
//   코어 정의가 없으면 의미 항목은 빠진다 (엉뚱한 버튼이 눌리는 것보다 낫다).
PadMap materializePadMap(const PadMap& in, const CoreSixButtons& cb);

// 저장 키 (형식을 한 곳에서만 만든다)
//   패드는 장치마다 버튼 번호가 다를 수 있으므로 게임별·기종별 키에도 장치 이름과 배치를 넣는다.
//   platKey 는 "plat:cps#6btn" 처럼 기종+배치까지 담은 키다 (MainWindow::platScopeKey).
QString padScopeKeyGame(const QString& rom, const QString& device, PadLayout layout);
QString padScopeKeyPlat(const QString& platKey, const QString& device);
QString padScopeKeyDev (const QString& device, PadLayout layout);

// 어디서 온 표인지
enum class PadMapSource { Game, Platform, Device, Default };
QString padMapSourceLabel(PadMapSource s, bool english);

// 실제로 적용할 표를 고른다. dflt 는 배치 기본값(호출자가 만들어 넘긴다).
PadMap resolvePadMap(const PadStore& store,
                     const QString& device,
                     const QString& rom,
                     const QString& platKey,
                     PadLayout layout,
                     const PadMap& dflt,
                     PadMapSource* sourceOut = nullptr);
