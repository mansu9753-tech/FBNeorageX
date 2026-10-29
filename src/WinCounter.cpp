// WinCounter.cpp — 세트 승수 집계 (RAM 카운터 2→3 감지)

#include "WinCounter.h"

#include <QLocale>

namespace {

// 네오지오 작업 RAM 은 0x100000 부터 64KB
constexpr uint32_t kRamBase = 0x100000;

struct Entry {
    const char* rom;
    uint32_t    p1ko;   // 1P 팀 쪽 값 (Count3: 쓰러진 캐릭터 수, Flag: 팀 전멸 표시)
    uint32_t    p2ko;   // 2P 팀 쪽 값
    bool        flag;   // false: 카운터가 2→3 이 되면 그 팀 전멸 / true: 값이 0→0xFF 가 되면 전멸
};

// 실제 게임을 돌려 확인한 주소 (1P 승 / 2P 승 / 번갈아 쓰러짐 / 타임오버 무승부 포함).
//   ※ 네오지오 RAM 은 16비트 단위로 바이트가 뒤집혀 있어 코어가 주는 배열 위치 기준이다.
const Entry kTable[] = {
    { "kof94",   0x10cff4, 0x10cfe8, false },
    { "kof95",   0x1081c1, 0x1083c1, true  },
    { "kof96",   0x10a861, 0x10a860, false },
    { "kof97",   0x10a854, 0x10a855, false },
    { "kof98",   0x10a859, 0x10a868, false },
    { "kof99",   0x10a806, 0x10a81d, false },
    { "kof2000", 0x10a806, 0x10a81d, false },
};

// 정식 롬이 아닌 한글패치·지역판·개조판 중, 같은 주소로 실제로 동작하는 것을 확인한 목록 (두 방향 모두 시험)
struct Alias { const char* rom; int base; };
const Alias kAliases[] = {
    { "kof2000bc", 6 }, { "kof2000n", 6 }, { "kof2000t", 6 }, { "kof2kfd", 6 },
    { "kof2kkr", 6 }, { "kof2kpls", 6 }, { "kof2kps2", 6 }, { "kof2ksp", 6 },
    { "kof2kuh", 6 }, { "kof2kxxx", 6 }, { "kof94br", 0 }, { "kof94bs", 0 },
    { "kof94kr", 0 }, { "kof94nr2", 0 }, { "kof94rz", 0 }, { "kof94te", 0 },
    { "kof94tea", 0 }, { "kof94uh", 0 }, { "kof95a", 1 }, { "kof95b", 1 },
    { "kof95bs", 1 }, { "kof95h", 1 }, { "kof95jq", 1 }, { "kof95kr", 1 },
    { "kof95sp", 1 }, { "kof95t", 1 }, { "kof95uh", 1 }, { "kof96a", 2 },
    { "kof96ae", 2 }, { "kof96cn", 2 }, { "kof96ep", 2 }, { "kof96h", 2 },
    { "kof96kr", 2 }, { "kof96pls", 2 }, { "kof96r", 2 }, { "kof96rp", 2 },
    { "kof96rss", 2 }, { "kof97ae", 3 }, { "kof97ae230625", 3 }, { "kof97bt", 3 },
    { "kof97cn", 3 }, { "kof97eb", 3 }, { "kof97evf", 3 }, { "kof97h", 3 },
    { "kof97k", 3 }, { "kof97kr", 3 }, { "kof97m", 3 }, { "kof97oro", 3 },
    { "kof97pla", 3 }, { "kof97plh", 3 }, { "kof97pls", 3 }, { "kof97ps", 3 },
    { "kof97rm", 3 }, { "kof97sp", 3 }, { "kof97st", 3 }, { "kof97t", 3 },
    { "kof97ts", 3 }, { "kof97ubp", 3 }, { "kof97uh", 3 }, { "kof97xt", 3 },
    { "kof98a", 4 }, { "kof98bc2nd", 4 }, { "kof98cb", 4 }, { "kof98cp", 4 },
    { "kof98eck", 4 }, { "kof98evo", 4 }, { "kof98h", 4 }, { "kof98k", 4 },
    { "kof98ka", 4 }, { "kof98kr", 4 }, { "kof98mix", 4 }, { "kof98pfe", 4 },
    { "kof99ae", 5 }, { "kof99cb", 5 }, { "kof99e", 5 }, { "kof99eur", 5 },
    { "kof99fd", 5 }, { "kof99h", 5 }, { "kof99k", 5 }, { "kof99ka", 5 },
    { "kof99kr", 5 }, { "kof99p", 5 }, { "kof99pls", 5 }, { "kof99rp", 5 },
    { "kof99sk", 5 }, { "kof99t", 5 }, { "kof99uh", 5 },
};

// ── 벨트스크롤 액션: 점수 (BCD 8자리, 68k 바이트 순서) ─────────
//   값은 코어가 주는 RAM 배열의 위치(바이트 스왑 상태 그대로)이다. 2P 주소는 라이브 플레이로 확인했다.
struct ScoreEntry { const char* rom; uint32_t p1, p2; };
const ScoreEntry kScoreTable[] = {
    { "dino",   0xb374, 0xb4f4 },
    { "wof",    0x7f8c, 0x7f90 },
    { "ffight", 0x85ec, 0x86ac },
};
struct ScoreAlias { const char* rom; int base; };
const ScoreAlias kScoreAliases[] = {
    { "dinofw", 0 }, { "dinoeh", 0 }, { "dino3j", 0 }, { "dino1v3", 0 },
    { "dinodw", 0 }, { "dinods", 0 }, { "dinoa", 0 }, { "dinohunt", 0 },
    { "dinoj", 0 }, { "dinoh", 0 }, { "dinojj", 0 }, { "dinojd", 0 },
    { "dinok", 0 }, { "dinopic", 0 }, { "dinopic2", 0 }, { "dinopic4", 0 },
    { "dinopic3", 0 }, { "dinoly", 0 }, { "dinopic5", 0 }, { "dinore", 0 },
    { "dinoplus", 0 }, { "dinorex", 0 }, { "dinorexu", 0 }, { "dinorexj", 0 },
    { "dinosj", 0 }, { "dinosdj", 0 }, { "dinoss", 0 }, { "dinot", 0 },
    { "dinosyn", 0 }, { "dinotpic", 0 }, { "dinotj", 0 }, { "dinotw", 0 },
    { "dinou", 0 }, { "dinowj", 0 }, { "dinoxz", 0 }, { "dinoyz", 0 },
    { "ffighta", 2 }, { "ffightae", 2 }, { "ffightaec2", 2 }, { "ffightaec2ds", 2 },
    { "ffightaec2k", 2 }, { "ffightaek", 2 }, { "ffightaemgc", 2 }, { "ffightfw", 2 },
    { "ffightj", 2 }, { "ffightj2", 2 }, { "ffightj1", 2 }, { "ffightj3", 2 },
    { "ffightj4", 2 }, { "ffightjh", 2 }, { "ffightk4", 2 }, { "ffightu", 2 },
    { "ffightu1", 2 }, { "ffightu2", 2 }, { "ffightua", 2 }, { "ffightub", 2 },
    { "ffightuc", 2 }, { "wof1v3", 1 }, { "wof1v3p", 1 }, { "wofa", 1 },
    { "wofabla", 1 }, { "wofabl", 1 }, { "wofah", 1 }, { "wofaha", 1 },
    { "wofahb", 1 }, { "wofc", 1 }, { "wofch", 1 }, { "wofchdx", 1 },
    { "wofchk", 1 }, { "wofchku", 1 }, { "wofhfh", 1 }, { "wofj", 1 },
    { "wofjh", 1 }, { "wofk103", 1 }, { "wofk", 1 }, { "wofkm3", 1 },
    { "wofmws", 1 }, { "wofmz1v3", 1 }, { "wofpic", 1 }, { "wofr1", 1 },
    { "wofr1bl", 1 }, { "wofsc", 1 }, { "wofsgzb", 1 }, { "wofsj", 1 },
    { "wofsja", 1 }, { "wofsjb", 1 }, { "wofsjs", 1 }, { "wofsjsa", 1 },
    { "wofsjsb", 1 }, { "wofssj", 1 }, { "wofsm", 1 }, { "wofssja", 1 },
    { "wofu", 1 }, { "wofwjq", 1 }, { "wofwp", 1 }, { "wofzl", 1 },
};

const ScoreEntry* findScore(const QString& rom) {
    for (const ScoreEntry& e : kScoreTable)
        if (rom.compare(QLatin1String(e.rom), Qt::CaseInsensitive) == 0) return &e;
    for (const ScoreAlias& a : kScoreAliases)
        if (rom.compare(QLatin1String(a.rom), Qt::CaseInsensitive) == 0) return &kScoreTable[a.base];
    return nullptr;
}

// BCD 8자리 (바이트 스왑) → 정수. 숫자가 아닌 값이 섞여 있으면 -1
int readBcd32(const uint8_t* ram, size_t size, uint32_t i) {
    if (i + 4 > size) return -1;
    const uint8_t b[4] = { ram[i + 1], ram[i], ram[i + 3], ram[i + 2] };
    int v = 0;
    for (uint8_t x : b) {
        const int hi = x >> 4, lo = x & 0xF;
        if (hi > 9 || lo > 9) return -1;
        v = v * 100 + hi * 10 + lo;
    }
    return v;
}

const Entry* find(const QString& rom) {
    for (const Entry& e : kTable)
        if (rom.compare(QLatin1String(e.rom), Qt::CaseInsensitive) == 0) return &e;
    for (const Alias& a : kAliases)
        if (rom.compare(QLatin1String(a.rom), Qt::CaseInsensitive) == 0) return &kTable[a.base];
    return nullptr;
}

}  // namespace

bool WinCounter::supported(const QString& romName) { return find(romName) != nullptr || findScore(romName) != nullptr; }

void WinCounter::setGame(const QString& romName) {
    const Entry* e = find(romName);
    const ScoreEntry* se = e ? nullptr : findScore(romName);
    m_mode = e ? Mode::Fight : (se ? Mode::Score : Mode::None);
    m_a1 = e ? e->p1ko : (se ? se->p1 : 0);
    m_a2 = e ? e->p2ko : (se ? se->p2 : 0);
    m_flag = e ? e->flag : false;
    m_s = State();
}

void WinCounter::reset() {
    m_s.w1 = m_s.w2 = 0;
}

bool WinCounter::update(const uint8_t* ram, size_t size) {
    if (m_mode == Mode::None || !ram) return false;
    if (m_mode == Mode::Score) {
        const int a = readBcd32(ram, size, m_a1), b = readBcd32(ram, size, m_a2);
        if (a >= 0) { m_s.s1 = a; if (a > m_s.max1) m_s.max1 = a; }
        if (b >= 0) { m_s.s2 = b; if (b > m_s.max2) m_s.max2 = b; }
        return false;
    }
    const size_t i1 = m_a1 - kRamBase, i2 = m_a2 - kRamBase;
    if (i1 >= size || i2 >= size) return false;
    const uint8_t v1 = ram[i1], v2 = ram[i2];

    bool changed = false;
    if (m_s.armed) {
        const bool lost1 = m_flag ? (m_s.prev1 == 0 && v1 == 0xFF) : (m_s.prev1 == 2 && v1 == 3);   // 1P 팀 전멸
        const bool lost2 = m_flag ? (m_s.prev2 == 0 && v2 == 0xFF) : (m_s.prev2 == 2 && v2 == 3);   // 2P 팀 전멸
        if (lost1 != lost2) {                              // 동시에 전멸(무승부)은 세지 않는다
            if (lost2) ++m_s.w1; else ++m_s.w2;
            changed = true;
        }
    }
    m_s.prev1 = v1;
    m_s.prev2 = v2;
    m_s.armed = true;
    return changed;
}

void WinCounter::resync(const uint8_t* ram, size_t size) {
    if (m_mode != Mode::Fight || !ram) return;
    const size_t i1 = m_a1 - kRamBase, i2 = m_a2 - kRamBase;
    if (i1 >= size || i2 >= size) return;
    m_s.prev1 = ram[i1];
    m_s.prev2 = ram[i2];
    m_s.armed = true;
}

QString WinCounter::text() const {
    if (m_mode == Mode::Score) {
        const QLocale en(QLocale::English);
        return QStringLiteral("1P %1    2P %2").arg(en.toString(m_s.s1), en.toString(m_s.s2));
    }
    return QStringLiteral("1P  %1 : %2  2P").arg(m_s.w1).arg(m_s.w2);
}
