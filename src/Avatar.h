#pragma once
// Avatar.h — 멀티플레이 캐릭터(아바타)와 레벨
//
//  · 아바타는 칸(slot)마다 부품 하나를 골라 겹쳐 그린다: body(몸) → face(얼굴·머리) → top(상의) → hat(모자) 순서.
//    catalog.json 항목에 "gender": "m"/"f" 를 적으면 그 성별의 몸을 골랐을 때만 나온다 (비우면 공용).
//  · 부품 목록과 "몇 레벨부터 쓸 수 있는가" 는 assets/avatars/catalog.json 에 적는다 (다시 빌드할 필요 없음).
//    이미지는 assets/avatars/<칸>/<부품>.png  (투명 배경, 256x320 권장, 모든 부품이 같은 기준 위치)
//    이미지가 없으면 코드로 그린 임시 캐릭터가 나온다.
//  · 레벨 테두리: 10레벨마다 색과 효과가 바뀐다 (이미지 없이 코드로 그린다).
//  · 레벨표는 서버(server/worker.js)와 같은 식이다: 레벨 L → L+1 에 필요한 경험치 = 2 + floor(L * 0.55).

#include <QColor>
#include <QHash>
#include <QImage>
#include <QMap>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QVector>

namespace avatar {

constexpr int kMaxLevel = 100;
constexpr int kImgW = 256, kImgH = 320;

// ── 레벨 ─────────────────────────────────────────────────────
inline int expNeeded(int level) { return 2 + int(level * 0.55); }
struct LevelInfo { int level = 1, into = 0, need = 2; };
LevelInfo levelFromExp(int exp);

// 테두리 단계 (0~9). 레벨 1~10 → 0, 11~20 → 1 ... 91~100 → 9
inline int tierOf(int level) { return qBound(0, (level - 1) / 10, 9); }
QColor tierColor(int tier);
QString tierName(int tier, bool english);

// ── 부품 ─────────────────────────────────────────────────────
struct Item {
    QString slot, id, nameKo, nameEn;
    QString gender;                        // "m" / "f" / 비어 있으면 공용
    int minLevel = 1;
};
using Parts = QMap<QString, QString>;      // 칸 → 부품 id

const QStringList& slotNames();                // {"body", "face", "top", "hat"}
QString slotLabel(const QString& slot, bool english);
const QVector<Item>& catalog();            // 앱이 켜질 때 한 번 읽는다
void    loadCatalog(const QString& appDir);
QVector<Item> itemsFor(const QString& slot);
QString genderOf(const Parts& p);                       // 고른 몸의 성별 ("m"/"f"/"")
QVector<Item> itemsFor(const QString& slot, const Parts& p);   // 고른 몸의 성별에 맞는 부품만
bool    allowed(const Item& it, int level);
Parts   sanitize(const Parts& p, int level);   // 쓸 수 없는 부품은 기본값으로 되돌린다
Parts   defaultParts();
QString partsToString(const Parts& p);          // "body=boy1,hat=cap1"
Parts   partsFromString(const QString& s);

// 합성한 캐릭터 그림 (kImgW x kImgH, 투명 배경). 같은 조합은 캐시에서 준다.
QImage  compose(const Parts& p);

// 카드 테두리를 그린다. rect 의 바깥 둘레에 thickness 픽셀 두께로. phaseMs 는 애니메이션용 시간(ms)
void drawFrame(QPainter& p, const QRect& r, int level, int thickness, qint64 phaseMs);

}  // namespace avatar
