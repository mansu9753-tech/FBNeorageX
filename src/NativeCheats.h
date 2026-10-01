#pragma once
// NativeCheats.h — 코어가 등록한 네이티브 치트 옵션을 찾는 헬퍼
//
//  FBNeo 코어는 <system>/fbneo/cheats/{rom}.ini 를 읽어 치트를 코어 옵션으로
//  등록한다. 키 형식: fbneo-cheat-<n>-<드라이버>-<옵션명>
//
//  이 판별이 두 곳에서 필요하다.
//    · CHEATS 메뉴 페이지가 목록을 보여줄 때
//    · 메인 루프가 "수동 치트 엔진(RAM 직접 쓰기)을 돌려도 되는가" 를 정할 때
//      (네이티브 치트가 있는데 수동 엔진까지 돌리면 이중 적용으로 충돌한다)
//  판별 규칙이 한 곳에만 있어야 두 곳이 어긋나지 않는다.

#include <QString>
#include <QStringList>

#include "EmulatorState.h"

namespace nativecheats {

inline constexpr const char* kPrefix = "fbneo-cheat-";

// 등록된 네이티브 치트 키를 정렬해 돌려준다. 없으면 빈 목록.
inline QStringList keys() {
    QStringList out;
    for (auto it = gState.variableOptions.constBegin();
         it != gState.variableOptions.constEnd(); ++it)
        if (it.key().startsWith(QLatin1String(kPrefix))) out << it.key();
    out.sort();
    return out;
}

inline bool any() {
    for (auto it = gState.variableOptions.constBegin();
         it != gState.variableOptions.constEnd(); ++it)
        if (it.key().startsWith(QLatin1String(kPrefix))) return true;
    return false;
}

// DIP 스위치 목록에서는 네이티브 치트를 뺀다 (CHEATS 메뉴에서 따로 다룬다)
inline bool isNativeCheatKey(const QString& key) {
    return key.startsWith(QLatin1String(kPrefix));
}

}  // namespace nativecheats
