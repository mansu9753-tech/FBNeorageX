// Avatar.cpp — 아바타 합성, 부품 목록, 레벨 테두리

#include "Avatar.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace avatar {

namespace {

QString g_dir;                       // assets/avatars 가 있는 폴더 (없으면 빈 문자열)
QVector<Item> g_items;
QHash<QString, QImage> g_cache;      // 합성 결과
QHash<QString, QImage> g_files;      // 부품 PNG (없으면 null 그림으로 기억)

// 파일이 없을 때 쓰는 기본 부품 (catalog.json 이 없거나 비어 있어도 앱이 돌아가게)
QVector<Item> defaultCatalog() {
    auto I = [](const char* s, const char* id, const char* ko, const char* en, int lv) {
        Item it; it.slot = s; it.id = id; it.nameKo = QString::fromUtf8(ko); it.nameEn = en; it.minLevel = lv; return it;
    };
    return {
        I("body", "boy1",   "소년",       "BOY",        1),
        I("body", "girl1",  "소녀",       "GIRL",       1),
        I("body", "boy2",   "파이터 A",   "FIGHTER A",  20),
        I("body", "girl2",  "파이터 B",   "FIGHTER B",  20),
        I("body", "master", "마스터",     "MASTER",     50),
        I("top",  "tee1",   "흰 티셔츠",  "WHITE TEE",  1),
        I("top",  "tee2",   "빨간 티셔츠","RED TEE",    1),
        I("top",  "hood1",  "파란 후드",  "BLUE HOODIE",5),
        I("top",  "coat1",  "가죽 재킷",  "LEATHER JACKET", 15),
        I("top",  "gi1",    "도복",       "GI",         30),
        I("top",  "suit1",  "정장",       "SUIT",       60),
        I("hat",  "none",   "없음",       "NONE",       1),
        I("hat",  "cap1",   "야구 모자",  "CAP",        1),
        I("hat",  "band1",  "머리띠",     "HEADBAND",   10),
        I("hat",  "beanie", "비니",       "BEANIE",     20),
        I("hat",  "crown",  "왕관",       "CROWN",      70),
    };
}

QColor hashColor(const QString& s, int sat, int val) {
    uint h = qHash(s) % 360;
    return QColor::fromHsv(int(h), sat, val);
}

QImage loadPart(const QString& slot, const QString& id) {
    if (g_dir.isEmpty()) return QImage();
    const QString path = g_dir + "/" + slot + "/" + id + ".png";
    auto it = g_files.constFind(path);
    if (it != g_files.constEnd()) return it.value();
    QImage img;
    if (QFileInfo::exists(path)) {
        img.load(path);
        if (!img.isNull()) img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    g_files.insert(path, img);
    return img;
}

// 이미지가 없을 때의 임시 캐릭터 (몸·상의·모자를 부품 이름으로 색만 달리해 단순하게)
void drawPlaceholder(QPainter& p, const QString& slot, const QString& id) {
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor c = hashColor(id, 150, 215);
    if (slot == QLatin1String("body")) {
        const QColor skin = hashColor(id + "s", 70, 245);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 50, 80));
        p.drawRoundedRect(QRectF(78, 188, 100, 120), 14, 14);               // 다리/몸통 바탕
        p.setBrush(skin);
        p.drawEllipse(QRectF(88, 40, 80, 88));                              // 머리
        p.setBrush(hashColor(id + "h", 160, 90));
        QPainterPath hair; hair.addEllipse(QRectF(84, 30, 88, 60));
        p.drawPath(hair);                                                    // 머리카락
        p.setBrush(QColor(30, 30, 40));
        p.drawEllipse(QRectF(108, 84, 8, 10)); p.drawEllipse(QRectF(140, 84, 8, 10));   // 눈
        p.setBrush(skin);
        p.drawRoundedRect(QRectF(58, 150, 28, 80), 12, 12);                  // 팔
        p.drawRoundedRect(QRectF(170, 150, 28, 80), 12, 12);
    } else if (slot == QLatin1String("top")) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(74, 132, 108, 90), 16, 16);                 // 상의
        p.drawRoundedRect(QRectF(52, 140, 32, 62), 12, 12);
        p.drawRoundedRect(QRectF(172, 140, 32, 62), 12, 12);
        p.setBrush(c.darker(130));
        p.drawRect(QRectF(118, 132, 20, 30));                                // 목 둘레
    } else if (slot == QLatin1String("hat")) {
        if (id == QLatin1String("none")) return;
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        if (id == QLatin1String("crown")) {
            p.setBrush(QColor(255, 210, 60));
            QPolygonF poly; poly << QPointF(88, 50) << QPointF(98, 12) << QPointF(116, 38) << QPointF(128, 6)
                                 << QPointF(140, 38) << QPointF(158, 12) << QPointF(168, 50);
            p.drawPolygon(poly);
        } else if (id.startsWith(QLatin1String("band"))) {
            p.drawRect(QRectF(84, 46, 88, 14));
        } else {
            p.drawChord(QRectF(84, 14, 88, 70), 0, 180 * 16);               // 모자 윗부분
            p.drawRect(QRectF(84, 48, 120, 10));                             // 챙
        }
    }
}

}  // namespace

// ── 레벨 ─────────────────────────────────────────────────────
LevelInfo levelFromExp(int exp) {
    LevelInfo r;
    int rest = qMax(0, exp);
    while (r.level < kMaxLevel && rest >= expNeeded(r.level)) { rest -= expNeeded(r.level); ++r.level; }
    r.into = r.level >= kMaxLevel ? 0 : rest;
    r.need = r.level >= kMaxLevel ? 0 : expNeeded(r.level);
    return r;
}

QColor tierColor(int tier) {
    static const QColor c[10] = {
        {120, 150, 200}, {80, 210, 120}, {70, 210, 230}, {250, 220, 70}, {255, 150, 50},
        {255, 80, 80},   {190, 110, 255},{255, 120, 200},{255, 245, 190},{255, 255, 255},
    };
    return c[qBound(0, tier, 9)];
}

QString tierName(int tier, bool en) {
    static const char* ko[10] = { "브론즈", "그린", "아쿠아", "골드", "오렌지", "레드", "퍼플", "핑크", "플래티넘", "레전드" };
    static const char* eg[10] = { "BRONZE", "GREEN", "AQUA", "GOLD", "ORANGE", "RED", "PURPLE", "PINK", "PLATINUM", "LEGEND" };
    return en ? QString::fromLatin1(eg[qBound(0, tier, 9)]) : QString::fromUtf8(ko[qBound(0, tier, 9)]);
}

// ── 부품 목록 ────────────────────────────────────────────────
const QStringList& slotNames() {
    static const QStringList s = { "body", "face", "top", "hat" };
    return s;
}

QString slotLabel(const QString& slot, bool en) {
    if (slot == QLatin1String("body")) return en ? "BODY" : QString::fromUtf8("캐릭터");
    if (slot == QLatin1String("face")) return en ? "FACE" : QString::fromUtf8("얼굴");
    if (slot == QLatin1String("top"))  return en ? "TOP"  : QString::fromUtf8("상의");
    if (slot == QLatin1String("hat"))  return en ? "HAT"  : QString::fromUtf8("모자");
    return slot;
}

const QVector<Item>& catalog() {
    if (g_items.isEmpty()) g_items = defaultCatalog();
    return g_items;
}

void loadCatalog(const QString& appDir) {
    g_dir.clear();
    g_cache.clear();
    g_files.clear();
    for (const QString& d : { appDir + "/assets/avatars", appDir + "/avatars" })
        if (QFileInfo(d).isDir()) { g_dir = d; break; }
    g_items.clear();
    if (!g_dir.isEmpty()) {
        QFile f(g_dir + "/catalog.json");
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
            for (const QJsonValue& v : arr) {
                const QJsonObject o = v.toObject();
                Item it;
                it.slot = o["slot"].toString();
                it.id = o["id"].toString();
                it.nameKo = o["ko"].toString(it.id);
                it.nameEn = o["en"].toString(it.id);
                it.gender = o["gender"].toString();
                it.minLevel = qBound(1, o["level"].toInt(1), kMaxLevel);
                if (slotNames().contains(it.slot) && !it.id.isEmpty()) g_items.append(it);
            }
        }
    }
    if (g_items.isEmpty()) g_items = defaultCatalog();
}

QVector<Item> itemsFor(const QString& slot) {
    QVector<Item> out;
    for (const Item& it : catalog()) if (it.slot == slot) out.append(it);
    return out;
}

QString genderOf(const Parts& p) {
    const QString b = p.value("body");
    for (const Item& it : catalog()) if (it.slot == QLatin1String("body") && it.id == b) return it.gender;
    return QString();
}

QVector<Item> itemsFor(const QString& slot, const Parts& p) {
    const QString g = slot == QLatin1String("body") ? QString() : genderOf(p);   // 몸은 성별로 거르지 않는다 (남/여를 고르는 칸)
    QVector<Item> out;
    for (const Item& it : itemsFor(slot))
        if (it.gender.isEmpty() || g.isEmpty() || it.gender == g) out.append(it);
    return out;
}

bool allowed(const Item& it, int level) { return level >= it.minLevel; }

Parts defaultParts() {
    Parts p;
    for (const QString& s : slotNames()) {                  // body 가 먼저라서 이후 칸은 그 성별로 고른다
        const QVector<Item> v = itemsFor(s, p);
        if (!v.isEmpty()) p[s] = v.first().id;
    }
    return p;
}

Parts sanitize(const Parts& in, int level) {
    Parts out;
    for (const QString& s : slotNames()) {                  // 몸을 먼저 확정한 뒤, 그 성별에 맞는 부품만 허용
        const QVector<Item> cand = itemsFor(s, out);
        if (cand.isEmpty()) continue;
        QString pick;
        for (const Item& it : cand) if (it.id == in.value(s) && allowed(it, level)) { pick = it.id; break; }
        if (pick.isEmpty()) {                               // 쓸 수 없으면 쓸 수 있는 첫 부품 (없으면 첫 부품)
            pick = cand.first().id;
            for (const Item& it : cand) if (allowed(it, level)) { pick = it.id; break; }
        }
        out[s] = pick;
    }
    return out;
}

QString partsToString(const Parts& p) {
    QStringList l;
    for (const QString& s : slotNames()) if (p.contains(s)) l << s + "=" + p[s];
    return l.join(",");
}

Parts partsFromString(const QString& str) {
    Parts p;
    for (const QString& kv : str.split(',', Qt::SkipEmptyParts)) {
        const int eq = kv.indexOf('=');
        if (eq > 0 && slotNames().contains(kv.left(eq))) p[kv.left(eq)] = kv.mid(eq + 1).left(24);
    }
    return p;
}

QImage compose(const Parts& p) {
    const QString key = partsToString(p);
    auto it = g_cache.constFind(key);
    if (it != g_cache.constEnd()) return it.value();
    QImage out(kImgW, kImgH, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter pa(&out);
    for (const QString& s : slotNames()) {               // body → top → hat
        const QString id = p.value(s);
        if (id.isEmpty()) continue;
        const QImage img = loadPart(s, id);
        if (!img.isNull()) pa.drawImage(QRect(0, 0, kImgW, kImgH), img);
        else drawPlaceholder(pa, s, id);
    }
    pa.end();
    if (g_cache.size() > 64) g_cache.clear();
    g_cache.insert(key, out);
    return out;
}

// ── 레벨 테두리 ──────────────────────────────────────────────
//   단계마다 색이 다르고, 단계가 오를수록 효과가 붙는다.
//     0~2 : 단색 이중 테두리     3~5 : + 테두리를 따라 도는 빛     6~8 : + 모서리 반짝임     9 : 무지개로 색이 흐른다
void drawFrame(QPainter& p, const QRect& r, int level, int t, qint64 ms) {
    const int tier = tierOf(level);
    QColor base = tierColor(tier);
    if (tier == 9) base = QColor::fromHsv(int((ms / 8) % 360), 160, 255);
    p.save();
    p.setPen(Qt::NoPen);
    t = qMax(1, t);
    const QColor dark = base.darker(210);
    p.fillRect(QRect(r.x(), r.y(), r.width(), t), base);
    p.fillRect(QRect(r.x(), r.bottom() - t + 1, r.width(), t), base);
    p.fillRect(QRect(r.x(), r.y(), t, r.height()), base);
    p.fillRect(QRect(r.right() - t + 1, r.y(), t, r.height()), base);
    const QRect in = r.adjusted(t, t, -t, -t);                         // 안쪽 어두운 선 (이중 테두리)
    p.fillRect(QRect(in.x(), in.y(), in.width(), 1), dark);
    p.fillRect(QRect(in.x(), in.bottom(), in.width(), 1), dark);
    p.fillRect(QRect(in.x(), in.y(), 1, in.height()), dark);
    p.fillRect(QRect(in.right(), in.y(), 1, in.height()), dark);

    if (tier >= 3) {                                                    // 테두리를 따라 도는 빛
        const int per = 2 * (r.width() + r.height());
        const int len = qMax(16, per / 8);
        const int pos = int((ms / 14) % per);
        const QColor glow = tier == 9 ? QColor(255, 255, 255) : base.lighter(170);
        for (int i = 0; i < len; i += 2) {
            int d = (pos + i) % per, x, y;
            if (d < r.width())                       { x = r.x() + d; y = r.y(); }
            else if ((d -= r.width()) < r.height())  { x = r.right() - t + 1; y = r.y() + d; }
            else if ((d -= r.height()) < r.width())  { x = r.right() - d; y = r.bottom() - t + 1; }
            else { d -= r.width(); x = r.x(); y = r.bottom() - d; }
            QColor g = glow; g.setAlpha(255 - i * 255 / len);
            p.fillRect(QRect(x, y, t, t), g);
        }
    }
    if (tier >= 6) {                                                    // 모서리 반짝임
        const int ph = int((ms / 180) % 4);
        const QPoint cs[4] = { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() };
        for (int i = 0; i < 4; ++i) {
            if (((i + ph) % 4) > 1) continue;
            const QPoint c = cs[i];
            const int a = t * 3;
            p.fillRect(QRect(c.x() - a, c.y() - t / 2, a * 2, qMax(1, t / 2 + 1)), QColor(255, 255, 255, 230));
            p.fillRect(QRect(c.x() - t / 2, c.y() - a, qMax(1, t / 2 + 1), a * 2), QColor(255, 255, 255, 230));
        }
    }
    p.restore();
}

}  // namespace avatar
