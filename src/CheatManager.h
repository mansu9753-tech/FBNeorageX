#pragma once
// CheatManager.h — INI 치트 로더 + RAM 직접 패치
// Python 버전 치트 시스템 완전 이식

#include <QObject>
#include <QList>
#include <QString>

// ── 치트 패치 1개 (주소 + 값) ────────────────────────────────
struct CheatPatch {
    uint32_t offset = 0;  // 실제 RAM 오프셋 (cpu_addr 변환 후)
    uint8_t  value  = 0;
};

// ── 치트 항목 (1개 치트 = N개의 패치) ───────────────────────
struct CheatEntry {
    QString           description;  // cheat "..." 그룹 이름 (치트 설명)
    QString           label;        // 옵션 레이블 (예: "Enabled", "Uncensored Game")
    QList<CheatPatch> patches;
    bool              active  = false;
};

// ── 타겟 기판 (바이트 오더 계산용) ───────────────────────────
enum class CheatPlatform { NeoGeo, CPS, Generic };

class LibretroCore;

class CheatManager : public QObject {
    Q_OBJECT
public:
    explicit CheatManager(QObject* parent = nullptr);

    // ── 로딩 ─────────────────────────────────────────────────
    bool loadIni(const QString& path);
    bool autoLoad(const QString& romName, const QString& cheatDir);
    void clearAll();

    // ── 항목 접근 ────────────────────────────────────────────
    const QList<CheatEntry>& entries() const { return m_entries; }
    int count() const { return m_entries.size(); }
    void setActive(int index, bool active);

    // ── 매 프레임 호출 — BIOS 딜레이 300프레임 후 RAM 패치 ───
    void applyFrame(LibretroCore* core, int frameCount, int loadFrame);

    // ── 로드된 파일명 ────────────────────────────────────────
    QString loadedPath() const { return m_loadedPath; }

signals:
    void cheatsLoaded(int count, const QString& path);
    void cheatsCleared();

private:
    QList<CheatEntry> m_entries;
    QString           m_loadedPath;
    CheatPlatform     m_platform = CheatPlatform::Generic;

    // ── 내부 ─────────────────────────────────────────────────
    bool parseIni(const QString& path);
    QString findIni(const QString& romName, const QString& cheatDir) const;
    CheatPlatform detectPlatform(const QString& romName) const;

    // CPU 주소 → RAM 오프셋
    uint32_t cpuAddrToOffset(uint32_t cpuAddr) const;
};
