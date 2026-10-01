// ArcadeLayout.cpp — 6버튼 격투 배치 판별

#include "ArcadeLayout.h"
#include "ArcadeLayout_data.h"   // kSixButtonRoms (자동 생성)

#include <algorithm>
#include <cstring>

namespace {

// 목록에 없는 클론/신규 롬까지 잡기 위한 접두사 보조 판정.
//   코어를 새로 갱신했는데 목록을 아직 안 뽑았을 때의 안전망이다.
//   (오탐을 피하려고 6버튼 격투가 확실한 계열만 넣는다)
const char* const kSixButtonPrefixes[] = {
    "sf2",      // 스트리트파이터 II 전 계열
    "sfa",      // 스트리트파이터 알파
    "sfz",      // 스트리트파이터 제로
    "ssf2",     // 슈퍼 스트리트파이터 II
    "hsf2",     // 하이퍼 스트리트파이터 II
    "sfiii",    // 스트리트파이터 III
    "sfex",     // 스트리트파이터 EX
    "dstlk",    // 뱀파이어 (Darkstalkers)
    "nwarr",    // 뱀파이어 헌터
    "vhunt",    // 뱀파이어 헌터 (일본판)
    "vsav",     // 뱀파이어 세이비어
    "vampj",    // 뱀파이어 (일본판)
    "msh",      // 마블 슈퍼 히어로즈 (+ mshvsf)
    "mvsc",     // 마블 VS 캡콤
    "xmvsf",    // 엑스맨 VS 스트리트파이터
    "xmcota",   // 엑스맨 칠드런 오브 디 아톰
    "cybots",   // 사이버보츠
    "ringdst",  // 링 오브 디스트럭션
    "sailormn", // 미소녀전사 세일러문
};

bool hasSixButtonPrefix(const QString& lc) {
    for (const char* p : kSixButtonPrefixes)
        if (lc.startsWith(QLatin1String(p))) return true;
    return false;
}

}  // namespace

bool isSixButtonFighter(const QString& rom) {
    if (rom.isEmpty()) return false;
    const QString lc = rom.trimmed().toLower();
    const QByteArray key = lc.toLatin1();

    // 1) 코어 소스에서 뽑은 정확한 목록 (이진 탐색)
    const char* const* end = kSixButtonRoms + kSixButtonRomCount;
    const char* const* it = std::lower_bound(
        kSixButtonRoms, end, key.constData(),
        [](const char* a, const char* b) { return std::strcmp(a, b) < 0; });
    if (it != end && std::strcmp(*it, key.constData()) == 0) return true;

    // 2) 목록에 없는 클론 대비 접두사 판정
    return hasSixButtonPrefix(lc);
}

PadLayout padLayoutOf(const QString& rom) {
    return isSixButtonFighter(rom) ? PadLayout::SixButton : PadLayout::Standard;
}

QString padLayoutLabel(PadLayout l, bool english) {
    if (l == PadLayout::SixButton)
        return english ? "6-BUTTON FIGHTER (LP MP HP / LK MK HK)"
                       : "6버튼 격투 (약중강손 / 약중강발)";
    return english ? "STANDARD (NeoGeo / beat-em-up)"
                   : "일반 (네오지오 · 벨트스크롤)";
}

// ── 코어 버튼 정의 파싱 ──────────────────────────────────────
CoreSixButtons parseCoreSixButtons(const QHash<int, QString>& desc) {
    CoreSixButtons cb;
    if (desc.isEmpty()) return cb;

    // "P1 Weak Punch" / "Weak Punch" 둘 다 대응. 3연타 매크로는 제외한다.
    auto find = [&](const QString& needle) -> int {
        for (auto it = desc.constBegin(); it != desc.constEnd(); ++it) {
            const QString& d = it.value();
            if (d.contains("3x", Qt::CaseInsensitive)) continue;
            if (d.contains(needle, Qt::CaseInsensitive)) return it.key();
        }
        return -1;
    };

    cb.lp = find("Weak Punch");
    cb.mp = find("Medium Punch");
    cb.hp = find("Strong Punch");
    cb.lk = find("Weak Kick");
    cb.mk = find("Medium Kick");
    cb.hk = find("Strong Kick");

    cb.valid = (cb.lp >= 0 && cb.mp >= 0 && cb.hp >= 0 &&
                cb.lk >= 0 && cb.mk >= 0 && cb.hk >= 0);
    return cb;
}
