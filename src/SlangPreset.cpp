// SlangPreset.cpp — .slangp 파싱 (#reference 상속 포함)

#include "SlangPreset.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {

// 값 하나와, 그 값이 적혀 있던 파일의 폴더.
//   경로형 값(shader0, LUT 경로 등)은 이 폴더 기준으로 풀어야 한다.
struct KV {
    QString value;
    QString dir;
};
using KVMap = QHash<QString, KV>;

QString stripValue(QString val) {
    if (val.startsWith(QLatin1Char('"'))) {
        const int close = val.indexOf(QLatin1Char('"'), 1);
        return (close > 0) ? val.mid(1, close - 1) : val.mid(1);
    }
    const int c = val.indexOf(QLatin1Char('#'));
    if (c >= 0) val = val.left(c);
    // 따옴표가 없을 때만 // 주석도 잘라낸다 (경로에 // 가 들어갈 일은 없다)
    const int s = val.indexOf(QLatin1String("//"));
    if (s >= 0) val = val.left(s);
    return val.trimmed();
}

// 한 파일의 "key = value" 를 모은다. #reference 는 out 에 따로 담는다.
void scanFile(const QString& text, const QString& dir,
              KVMap& kv, QStringList& references) {
    const QStringList lines =
        text.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith(QLatin1String("#reference"))) {
            const QString p = stripValue(line.mid(10).trimmed());
            if (!p.isEmpty()) references.append(QDir::cleanPath(dir + QLatin1Char('/') + p));
            continue;
        }
        if (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1String("//")))
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed().toLower();
        if (key.isEmpty()) continue;
        kv.insert(key, KV{ stripValue(line.mid(eq + 1).trimmed()), dir });
    }
}

// #reference 를 깊이 우선으로 따라간다.
//   부모를 먼저 넣고 자식 값으로 덮어써야 "자식이 이긴다" 가 성립한다.
bool loadInto(const QString& path, KVMap& kv, QSet<QString>& seen,
              QStringList& files, int depth, QString& err) {
    if (depth > 16) { err = QStringLiteral("#reference 중첩이 너무 깊습니다."); return false; }
    const QString abs = QDir::cleanPath(path);
    if (seen.contains(abs)) return true;          // 순환 참조는 무시
    seen.insert(abs);

    QFile f(abs);
    if (!f.open(QIODevice::ReadOnly)) {
        err = QStringLiteral("프리셋을 열 수 없습니다: %1").arg(QFileInfo(abs).fileName());
        return false;
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    files.append(abs);

    KVMap own;
    QStringList refs;
    scanFile(text, QFileInfo(abs).absolutePath(), own, refs);

    for (const QString& r : refs)
        if (!loadInto(r, kv, seen, files, depth + 1, err)) return false;

    for (auto it = own.constBegin(); it != own.constEnd(); ++it)
        kv.insert(it.key(), it.value());        // 자식이 부모를 덮는다
    return true;
}

bool toBool(const QString& v, bool dflt) {
    const QString s = v.trimmed().toLower();
    if (s == QLatin1String("true")  || s == QLatin1String("1")) return true;
    if (s == QLatin1String("false") || s == QLatin1String("0")) return false;
    return dflt;
}

SlangWrap toWrap(const QString& v, SlangWrap dflt) {
    const QString s = v.trimmed().toLower();
    if (s == QLatin1String("clamp_to_border")) return SlangWrap::ClampToBorder;
    if (s == QLatin1String("clamp_to_edge"))   return SlangWrap::ClampToEdge;
    if (s == QLatin1String("repeat"))          return SlangWrap::Repeat;
    if (s == QLatin1String("mirrored_repeat")) return SlangWrap::Repeat;
    return dflt;
}

SlangScale toScaleType(const QString& v, SlangScale dflt) {
    const QString s = v.trimmed().toLower();
    if (s == QLatin1String("viewport")) return SlangScale::Viewport;
    if (s == QLatin1String("absolute")) return SlangScale::Absolute;
    if (s == QLatin1String("source"))   return SlangScale::Source;
    return dflt;
}

QString absPath(const QString& rel, const QString& dir) {
    if (rel.isEmpty()) return QString();
    const QFileInfo fi(rel);
    return fi.isAbsolute() ? QDir::cleanPath(rel)
                           : QDir::cleanPath(dir + QLatin1Char('/') + rel);
}

// 패스 설정 키인지 판별한다 (파라미터와 구분하기 위해).
//   "접두사 + 숫자" 또는 "접두사 + _숫자" 형태면 패스 설정이다.
bool isPassKey(const QString& k) {
    static const QStringList prefixes = {
        QStringLiteral("shader"), QStringLiteral("alias"),
        QStringLiteral("scale_type_x"), QStringLiteral("scale_type_y"),
        QStringLiteral("scale_type"), QStringLiteral("scale_x"),
        QStringLiteral("scale_y"), QStringLiteral("scale"),
        QStringLiteral("filter_linear"), QStringLiteral("wrap_mode"),
        QStringLiteral("float_framebuffer"), QStringLiteral("srgb_framebuffer"),
        QStringLiteral("mipmap_input"), QStringLiteral("frame_count_mod"),
    };
    for (const QString& pre : prefixes) {
        if (!k.startsWith(pre)) continue;
        QString rest = k.mid(pre.size());
        if (rest.startsWith(QLatin1Char('_'))) rest = rest.mid(1);
        if (rest.isEmpty()) continue;
        bool allDigit = true;
        for (const QChar& c : rest) if (!c.isDigit()) { allDigit = false; break; }
        if (allDigit) return true;
    }
    return false;
}

// 모아 놓은 키/값을 프리셋 구조로 해석한다.
SlangPreset interpret(const KVMap& kv, const QString& fallbackDir) {
    SlangPreset out;

    auto raw  = [&](const QString& k) { return kv.value(k.toLower()).value; };
    auto dir  = [&](const QString& k) {
        const QString d = kv.value(k.toLower()).dir;
        return d.isEmpty() ? fallbackDir : d;
    };
    auto has  = [&](const QString& k) { return kv.contains(k.toLower()); };

    if (!has(QStringLiteral("shaders"))) {
        // 셰이더 목록은 없는데 값은 잔뜩 들어 있다면, 이 파일은 단독 프리셋이
        //   아니라 다른 프리셋이 #reference 로 불러 쓰는 "조각" 이다.
        //   (koko-aio 의 refs/*.slangp 가 그렇다) 오류처럼 보이면 혼란스러우니
        //   무엇을 고르면 되는지 알려준다.
        out.error = kv.size() > 1
            ? QStringLiteral("이 파일은 단독으로 쓰는 프리셋이 아니라 다른 프리셋이 "
                             "불러 쓰는 설정 조각입니다. 상위 폴더의 .slangp 를 "
                             "선택해 주세요.")
            : QStringLiteral("프리셋에 shaders 항목이 없습니다.");
        return out;
    }
    bool okNum = false;
    const int count = raw(QStringLiteral("shaders")).toInt(&okNum);
    if (!okNum || count <= 0) {
        out.error = QStringLiteral("프리셋의 shaders 값이 올바르지 않습니다.");
        return out;
    }

    for (int i = 0; i < count; ++i) {
        const QString n = QString::number(i);
        SlangPass p;

        const QString shaderKey = QStringLiteral("shader") + n;
        p.path = absPath(raw(shaderKey), dir(shaderKey));
        if (p.path.isEmpty()) {
            out.error = QStringLiteral("shader%1 경로가 없습니다.").arg(i);
            return out;
        }
        p.alias        = raw(QStringLiteral("alias") + n);
        p.filterLinear = toBool(raw(QStringLiteral("filter_linear") + n), false);
        // 기본값은 clamp_to_border (RetroArch 와 같다). 가장자리 색을 늘려 쓰지 않고 바깥은 검게 둔다.
        //   베젤이 화면을 다 덮지 않는 셰이더에서 테두리 픽셀이 바깥으로 번져 늘어지던 문제.
        p.wrap         = toWrap(raw(QStringLiteral("wrap_mode") + n), SlangWrap::ClampToBorder);
        p.floatFbo     = toBool(raw(QStringLiteral("float_framebuffer") + n), false);
        p.srgbFbo      = toBool(raw(QStringLiteral("srgb_framebuffer") + n), false);
        p.mipmapInput  = toBool(raw(QStringLiteral("mipmap_input") + n), false);
        p.frameCountMod = raw(QStringLiteral("frame_count_mod") + n).toInt();

        // ── 크기 ────────────────────────────────────────────
        //   scale_type / scale 이 양축 기본값, scale_type_x 등이 축별 덮어쓰기.
        //   "scale_0" 처럼 밑줄을 넣은 프리셋도 있어 두 표기를 모두 본다.
        auto numKey = [&](const QString& base) {
            QString v = raw(base + n);
            if (v.isEmpty()) v = raw(base + QLatin1Char('_') + n);
            return v;
        };
        const QString stBoth = numKey(QStringLiteral("scale_type"));
        const QString stX    = numKey(QStringLiteral("scale_type_x"));
        const QString stY    = numKey(QStringLiteral("scale_type_y"));
        const QString svBoth = numKey(QStringLiteral("scale"));
        const QString svX    = numKey(QStringLiteral("scale_x"));
        const QString svY    = numKey(QStringLiteral("scale_y"));

        p.hasScale = !(stBoth.isEmpty() && stX.isEmpty() && stY.isEmpty()
                       && svBoth.isEmpty() && svX.isEmpty() && svY.isEmpty());

        const SlangScale base = toScaleType(stBoth, SlangScale::Source);
        p.scaleTypeX = stX.isEmpty() ? base : toScaleType(stX, base);
        p.scaleTypeY = stY.isEmpty() ? base : toScaleType(stY, base);

        auto toScale = [](const QString& s, float dflt) {
            bool ok = false;
            const float f = s.toFloat(&ok);
            return (ok && f > 0.0f) ? f : dflt;
        };
        const float sBase = toScale(svBoth, 1.0f);
        p.scaleX = svX.isEmpty() ? sBase : toScale(svX, sBase);
        p.scaleY = svY.isEmpty() ? sBase : toScale(svY, sBase);

        p.scaleType = p.scaleTypeX;      // 예전 코드 호환용
        p.scale     = p.scaleX;

        out.passes.append(p);
    }

    // ── LUT 텍스처 ───────────────────────────────────────────
    //   textures = "a;b"  또는  "a"
    const QString texList = raw(QStringLiteral("textures"));
    if (!texList.isEmpty()) {
        const QStringList names =
            texList.split(QRegularExpression(QStringLiteral("[;,\\s]+")), Qt::SkipEmptyParts);
        for (const QString& rawName : names) {
            const QString name = rawName.trimmed();
            if (name.isEmpty()) continue;
            SlangLut lut;
            lut.name = name;
            lut.path = absPath(raw(name), dir(name));
            if (lut.path.isEmpty()) continue;       // 경로가 없으면 건너뛴다
            lut.linear = toBool(raw(name + QStringLiteral("_linear")), false);
            lut.mipmap = toBool(raw(name + QStringLiteral("_mipmap")), false);
            lut.wrap   = toWrap(raw(name + QStringLiteral("_wrap_mode")),
                                SlangWrap::ClampToBorder);
            out.luts.append(lut);
        }
    }

    // ── 파라미터 오버라이드 ──────────────────────────────────
    //   "예약어도 아니고 패스 설정도 아닌데 값이 숫자" 인 키를 파라미터로 본다.
    //   셰이더에 없는 이름이면 적용 단계에서 조용히 무시된다.
    {
        static const QSet<QString> reserved = {
            QStringLiteral("shaders"), QStringLiteral("textures"),
            QStringLiteral("feedback_pass"), QStringLiteral("parameters"),
        };
        QSet<QString> lutNames;
        for (const SlangLut& l : out.luts) lutNames.insert(l.name.toLower());

        for (auto it = kv.constBegin(); it != kv.constEnd(); ++it) {
            const QString& k = it.key();
            if (reserved.contains(k)) continue;
            if (isPassKey(k))         continue;
            if (lutNames.contains(k)) continue;
            if (k.endsWith(QLatin1String("_linear"))
             || k.endsWith(QLatin1String("_wrap_mode"))
             || k.endsWith(QLatin1String("_mipmap"))) continue;
            bool okF = false;
            const float v = it.value().value.toFloat(&okF);
            if (okF) out.params.insert(k, v);
        }
    }

    out.ok = true;
    return out;
}

}  // namespace

SlangPreset parseSlangPresetFile(const QString& path) {
    SlangPreset out;
    KVMap kv;
    QSet<QString> seen;
    QStringList files;
    QString err;
    if (!loadInto(path, kv, seen, files, 0, err)) { out.error = err; return out; }

    out = interpret(kv, QFileInfo(QDir::cleanPath(path)).absolutePath());
    out.sourceFiles = files;
    return out;
}

SlangPreset parseSlangPreset(const QString& presetText, const QString& presetDir) {
    KVMap kv;
    QStringList refs;
    scanFile(presetText, presetDir, kv, refs);
    SlangPreset out = interpret(kv, presetDir);
    if (!out.ok && !refs.isEmpty())
        out.error = QStringLiteral(
            "#reference 로 다른 프리셋을 상속하는 형식입니다. "
            "파일 경로 기반으로 열어야 부모 프리셋을 따라갈 수 있습니다.");
    return out;
}

// 파라미터 이름이 대소문자를 구분한다는 점에 유의.
//   프리셋 키는 소문자로 정규화해 두었으므로, 셰이더 쪽 이름과 맞출 때는
//   비교를 소문자로 해야 한다. (적용부에서 처리)
