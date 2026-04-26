// CheatManager.cpp — INI 치트 로더 + RAM 직접 패치 (Python 치트 시스템 완전 이식)

#include "CheatManager.h"
#include "LibretroCore.h"
#include "libretro.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDir>
#include <QDebug>

CheatManager::CheatManager(QObject* parent)
    : QObject(parent)
{}

// ════════════════════════════════════════════════════════════
//  자동 로드 (게임 선택 시)
//  우선순위: cheats/{romName}.ini → 부모롬(접미사 제거) → 없으면 false
// ════════════════════════════════════════════════════════════
bool CheatManager::autoLoad(const QString& romName, const QString& cheatDir) {
    clearAll();
    m_platform = detectPlatform(romName);

    QString path = findIni(romName, cheatDir);
    if (path.isEmpty()) return false;

    bool ok = parseIni(path);
    if (ok) {
        m_loadedPath = path;
        emit cheatsLoaded(m_entries.size(), path);
        qDebug() << "CheatManager: autoLoad" << path
                 << "→" << m_entries.size() << "cheats";
    }
    return ok;
}

bool CheatManager::loadIni(const QString& path) {
    clearAll();
    bool ok = parseIni(path);
    if (ok) {
        m_loadedPath = path;
        emit cheatsLoaded(m_entries.size(), path);
    }
    return ok;
}

void CheatManager::clearAll() {
    m_entries.clear();
    m_loadedPath.clear();
    emit cheatsCleared();
}

void CheatManager::setActive(int index, bool active) {
    if (index >= 0 && index < m_entries.size())
        m_entries[index].active = active;
}

// ════════════════════════════════════════════════════════════
//  INI 파일 탐색 (부모롬 폴백)
// ════════════════════════════════════════════════════════════
QString CheatManager::findIni(const QString& romName, const QString& cheatDir) const {
    // 직접 탐색
    QString direct = cheatDir + "/" + romName.toLower() + ".ini";
    if (QFile::exists(direct)) return direct;

    // 부모롬 추정: 접미사를 1자씩 제거 (최소 3자까지)
    QString stem = romName.toLower();
    while (stem.length() > 3) {
        stem.chop(1);
        QString candidate = cheatDir + "/" + stem + ".ini";
        if (QFile::exists(candidate)) return candidate;
    }
    return {};
}

// ════════════════════════════════════════════════════════════
//  기판 감지 (바이트오더 계산용)
// ════════════════════════════════════════════════════════════
CheatPlatform CheatManager::detectPlatform(const QString& romName) const {
    // NeoGeo 대표 prefix
    static const QStringList neoPfx = {
        "kof","mslug","garou","samsho","rbff","fatfury","aof","wh",
        "nam1975","lbowling","blazstar","lastsold","neo","magdrop",
        "pbobblen","pbobble","neobombe","turfmast","lastblad","rotd"};
    // CPS 대표 prefix
    static const QStringList cpsPfx = {
        "sf","ssf","sfa","sfz","xmvsf","msh","mvsc","mvc","avsp","vsav",
        "knights","ffight","ghouls","strider","1941","1944","19xx",
        "progear","gigawing","mmatrix","cybots","cyvern","ddtod","ddsoma"};

    QString lc = romName.toLower();
    for (const QString& p : neoPfx)
        if (lc.startsWith(p)) return CheatPlatform::NeoGeo;
    for (const QString& p : cpsPfx)
        if (lc.startsWith(p)) return CheatPlatform::CPS;
    return CheatPlatform::Generic;
}

// ════════════════════════════════════════════════════════════
//  CPU주소 → RAM오프셋 변환
//  68000(NeoGeo/CPS) : (cpuAddr - base) ^ 1  (빅엔디언 워드 스왑)
// ════════════════════════════════════════════════════════════
uint32_t CheatManager::cpuAddrToOffset(uint32_t cpuAddr) const {
    switch (m_platform) {
    case CheatPlatform::NeoGeo: {
        constexpr uint32_t base = 0x100000;
        if (cpuAddr < base) return cpuAddr;
        return (cpuAddr - base) ^ 1;
    }
    case CheatPlatform::CPS: {
        constexpr uint32_t base = 0xFF0000;
        if (cpuAddr < base) return cpuAddr;
        return (cpuAddr - base) ^ 1;
    }
    default:
        return cpuAddr;
    }
}

// ════════════════════════════════════════════════════════════
//  INI 파싱
//  포맷:   N "Label", offset, addr, val [, offset, addr, val ...]
//          첫 번째 offset(=0) 은 메모리 공간 선택자 (무시)
//          이후 addr/val 쌍으로 패치 적용
//
//  Python 이식 포맷: j+=3 단위로 (offset, addr, val) 3개가 한 그룹
// ════════════════════════════════════════════════════════════
bool CheatManager::parseIni(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "CheatManager: 파일 열기 실패" << path;
        return false;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);

    // 행 단위 파싱
    // 포맷: N "Label", 0, ADDR, VAL [, 0, ADDR, VAL ...]
    // N = 0 (disabled) 은 건너뜀
    // 주의: 내부에 )" 시퀀스 포함 → 커스텀 구분자 "re" 사용
    static const QRegularExpression re(
        R"re(^\s*(\d+)\s+"([^"]+)"\s*,\s*(.*))re");
    static const QRegularExpression numRe(R"re(\b(0[xX][0-9A-Fa-f]+|\d+)\b)re");
    // cheat "그룹명" 행 감지
    static const QRegularExpression cheatNameRe(R"re(^cheat\s+"([^"]*)")re");

    QString currentGroup;  // 현재 cheat "..." 그룹 이름

    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.startsWith(';') || line.startsWith("//") || line.isEmpty())
            continue;

        // cheat "그룹명" 행 → 현재 그룹 이름 갱신
        QRegularExpressionMatch gm = cheatNameRe.match(line);
        if (gm.hasMatch()) {
            currentGroup = gm.captured(1).trimmed();
            continue;
        }

        QRegularExpressionMatch m = re.match(line);
        if (!m.hasMatch()) continue;

        int    n     = m.captured(1).toInt();
        QString lbl  = m.captured(2).trimmed();
        QString rest = m.captured(3);

        // index 0 또는 "Disabled" 레이블은 건너뜀
        if (n == 0) continue;
        if (lbl.contains("disabled", Qt::CaseInsensitive)) continue;

        // 숫자 토큰 추출
        QList<uint32_t> nums;
        auto it = numRe.globalMatch(rest);
        while (it.hasNext()) {
            QString tok = it.next().captured(1);
            bool ok;
            uint32_t v = tok.startsWith("0x") || tok.startsWith("0X")
                ? tok.mid(2).toUInt(&ok, 16)
                : tok.toUInt(&ok, 10);
            if (ok) nums.append(v);
        }

        // 3개가 한 그룹: (offset, addr, val)
        // offset = 0 이면 SYSTEM_RAM / offset = 1 이면 VIDEO_RAM 등
        // 여기서는 offset==0 만 처리 (SYSTEM_RAM)
        CheatEntry entry;
        entry.description = currentGroup;  // cheat "..." 그룹 이름
        entry.label       = lbl;           // 옵션 레이블 (예: "Enabled", "Uncensored Game")
        for (int j = 0; j + 2 < nums.size(); j += 3) {
            uint32_t offset = nums[j];
            uint32_t addr   = nums[j + 1];
            uint8_t  val    = static_cast<uint8_t>(nums[j + 2]);
            if (offset != 0) continue;  // SYSTEM_RAM 이외 건너뜀
            CheatPatch patch;
            patch.offset = cpuAddrToOffset(addr);
            patch.value  = val;
            entry.patches.append(patch);
        }

        if (!entry.patches.isEmpty())
            m_entries.append(entry);
    }
    return !m_entries.isEmpty();
}

// ════════════════════════════════════════════════════════════
//  매 프레임 적용 — BIOS 딜레이 300 프레임 후부터
// ════════════════════════════════════════════════════════════
void CheatManager::applyFrame(LibretroCore* core, int frameCount, int loadFrame) {
    if (m_entries.isEmpty()) return;
    if (frameCount - loadFrame < 300) return;  // BIOS 자가진단 보호

    void*  ram     = core->getMemoryData(RETRO_MEMORY_SYSTEM_RAM);
    size_t ramSize = core->getMemorySize(RETRO_MEMORY_SYSTEM_RAM);
    if (!ram || ramSize == 0) return;

    auto* ramBytes = static_cast<uint8_t*>(ram);
    for (const CheatEntry& e : m_entries) {
        if (!e.active) continue;
        for (const CheatPatch& p : e.patches) {
            if (p.offset < ramSize)
                ramBytes[p.offset] = p.value;
        }
    }
}
