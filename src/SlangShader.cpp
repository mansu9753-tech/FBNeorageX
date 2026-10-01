// SlangShader.cpp — .slang → 데스크톱 GLSL 번역

#include "SlangShader.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace {

// uniform 블록 하나를 펼친 결과
struct UniformBlock {
    QString instance;          // "global" / "params" — 없으면 빈 문자열
    QStringList declarations;  // "uniform mat4 MVP;" 같은 줄들
};

// s 의 openIdx 위치가 '{' 일 때 짝이 맞는 '}' 위치를 돌려준다. 없으면 -1.
int matchBrace(const QString& s, int openIdx) {
    int depth = 0;
    for (int i = openIdx; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}') {
            if (--depth == 0) return i;
        }
    }
    return -1;
}

// layout(...) uniform Name { ... } inst;  형태를 전부 찾아 평평한 uniform 으로 바꾼다.
//   반환: 치환이 끝난 소스. blocks 에는 인스턴스 이름이 쌓인다.
QString flattenUniformBlocks(QString src, QList<UniformBlock>* blocks) {
    // layout( ... ) uniform <BlockName> {
    static const QRegularExpression reBlock(
        R"(layout\s*\([^)]*\)\s*uniform\s+(\w+)\s*\{)");

    forever {
        QRegularExpressionMatch m = reBlock.match(src);
        if (!m.hasMatch()) break;

        const int braceOpen = src.indexOf('{', m.capturedStart());
        const int braceEnd  = matchBrace(src, braceOpen);
        if (braceEnd < 0) break;               // 짝이 안 맞음 — 더 건드리지 않는다

        // 블록 뒤의 인스턴스 이름과 세미콜론까지 먹는다:  } global;
        int tail = braceEnd + 1;
        while (tail < src.size() && src[tail].isSpace()) ++tail;
        QString instance;
        while (tail < src.size() &&
               (src[tail].isLetterOrNumber() || src[tail] == '_')) {
            instance += src[tail];
            ++tail;
        }
        while (tail < src.size() && src[tail] != ';') ++tail;
        if (tail < src.size()) ++tail;         // ';' 포함

        // 멤버를 각각 uniform 선언으로
        UniformBlock ub;
        ub.instance = instance;
        const QString body = src.mid(braceOpen + 1, braceEnd - braceOpen - 1);
        for (QString line : body.split(';')) {
            line = line.trimmed();
            if (line.isEmpty()) continue;
            // 주석 줄 제거
            if (line.startsWith("//")) continue;
            ub.declarations << ("uniform " + line.simplified() + ";");
        }
        blocks->append(ub);

        src.replace(m.capturedStart(), tail - m.capturedStart(),
                    ub.declarations.join("\n"));
    }
    return src;
}

// 남아있는 layout(...) 한정자를 제거한다 (location / set / binding 등).
QString stripLayoutQualifiers(QString src) {
    static const QRegularExpression reLayout(R"(layout\s*\([^)]*\)\s*)");
    return src.remove(reLayout);
}

// "global." / "params." 같은 인스턴스 접두사를 지운다.
QString stripInstancePrefixes(QString src, const QList<UniformBlock>& blocks) {
    for (const UniformBlock& b : blocks) {
        if (b.instance.isEmpty()) continue;
        // 단어 경계에서만 — 다른 식별자의 일부를 건드리지 않게
        QRegularExpression re("\\b" + QRegularExpression::escape(b.instance) + "\\s*\\.\\s*");
        src.remove(re);
    }
    return src;
}

QString stripSlangPragmas(QString src) {
    static const QRegularExpression reFmt(R"([ \t]*#pragma\s+format\b[^\n]*\n?)");
    static const QRegularExpression reNam(R"([ \t]*#pragma\s+name\b[^\n]*\n?)");
    src.remove(reFmt);
    src.remove(reNam);
    return src;
}

// ── GLSL 120 으로 낮춘다 ─────────────────────────────────────
//   ★ 이 앱은 OpenGL 2.1 호환 프로파일을 요청한다 (main.cpp).
//     Windows 데스크톱 드라이버는 요청보다 높은 버전을 내주기 때문에 #version 130
//     으로 뽑아도 우연히 통과하지만, 스팀덱의 Mesa 는 요청한 2.1 을 그대로 준다.
//     그래서 130 으로 뽑으면 사양 밖이라 화면이 깨진다(아래 절반이 검게 나오는 등).
//     기존 .glsl 경로가 120 으로 고정해 잘 도는 것과 같은 이유로 120 을 target 으로 삼는다.
//
//   120 에는 in/out, texture(), 사용자 정의 out, uint 가 없으므로 전부 바꿔준다.
QString toGlsl120(QString src, bool vertexStage) {
    static const QRegularExpression reVer(R"([ \t]*#version\b[^\n]*\n?)");
    src.remove(reVer);

    // 사용자 정의 프래그먼트 출력 → gl_FragColor
    if (!vertexStage) {
        static const QRegularExpression reOut(
            R"(^[ \t]*out\s+vec4\s+(\w+)\s*;[ \t]*\n?)",
            QRegularExpression::MultilineOption);
        const QRegularExpressionMatch m = reOut.match(src);
        if (m.hasMatch()) {
            const QString name = m.captured(1);
            src.remove(reOut);
            if (name != QLatin1String("gl_FragColor")) {
                src.replace(QRegularExpression(
                                "\\b" + QRegularExpression::escape(name) + "\\b"),
                            "gl_FragColor");
            }
        }
    }

    // in / out → attribute / varying
    static const QRegularExpression reIn  (R"(^[ \t]*in\s+)",
                                           QRegularExpression::MultilineOption);
    static const QRegularExpression reOutV(R"(^[ \t]*out\s+)",
                                           QRegularExpression::MultilineOption);
    if (vertexStage) {
        src.replace(reIn,   "attribute ");
        src.replace(reOutV, "varying ");
    } else {
        src.replace(reIn,   "varying ");
    }

    // texture(...) / textureLod(...) → texture2D(...)
    static const QRegularExpression reTex   (R"(\btexture\s*\()");
    static const QRegularExpression reTexLod(R"(\btextureLod\s*\()");
    src.replace(reTexLod, "texture2D(");
    src.replace(reTex,    "texture2D(");

    // 120 에는 uint 가 없다 (주로 UBO 의 FrameCount)
    static const QRegularExpression reUint(R"(\buint\b)");
    src.replace(reUint, "int");

    return "#version 120\n" + src;
}

}  // namespace

// ════════════════════════════════════════════════════════════
SlangTranslation translateSlang(const QString& source) {
    SlangTranslation out;

    if (source.trimmed().isEmpty()) {
        out.error = QStringLiteral("셰이더 파일이 비어 있습니다.");
        return out;
    }

    const int vs = source.indexOf("#pragma stage vertex");
    const int fs = source.indexOf("#pragma stage fragment");
    if (vs < 0 || fs < 0 || fs < vs) {
        out.error = QStringLiteral(
            "slang 형식이 아닙니다 (#pragma stage vertex / fragment 를 찾을 수 없음).");
        return out;
    }

    // #pragma stage 이전은 양쪽이 공유하는 선언부다.
    const QString common = source.left(vs);
    QString vert = common + source.mid(vs, fs - vs);
    QString frag = common + source.mid(fs);

    static const QRegularExpression reStage(R"([ \t]*#pragma\s+stage\b[^\n]*\n?)");
    vert.remove(reStage);
    frag.remove(reStage);

    auto convert = [](QString s, bool vertexStage) {
        QList<UniformBlock> blocks;
        s = stripSlangPragmas(s);
        s = flattenUniformBlocks(s, &blocks);
        s = stripInstancePrefixes(s, blocks);
        s = stripLayoutQualifiers(s);
        s = toGlsl120(s, vertexStage);
        return s;
    };

    out.vert = convert(vert, true);
    out.frag = convert(frag, false);

    // 최소한의 형태 검증 — 번역이 통째로 빗나갔는지 거른다
    if (!out.vert.contains("gl_Position")) {
        out.error = QStringLiteral("정점 셰이더에 gl_Position 출력이 없습니다.");
        return out;
    }
    if (!out.frag.contains("main")) {
        out.error = QStringLiteral("프래그먼트 셰이더에 main() 이 없습니다.");
        return out;
    }

    out.ok = true;
    return out;
}

// ════════════════════════════════════════════════════════════
QString slangPresetSinglePass(const QString& presetText,
                              const QString& presetDir,
                              QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return QString(); };

    // shaders = N
    static const QRegularExpression reCount(R"RX(^\s*shaders\s*=\s*"?(\d+)"?)RX",
                                            QRegularExpression::MultilineOption);
    const QRegularExpressionMatch mc = reCount.match(presetText);
    if (!mc.hasMatch())
        return fail(QStringLiteral("프리셋에 shaders 항목이 없습니다."));

    const int count = mc.captured(1).toInt();
    if (count <= 0)
        return fail(QStringLiteral("프리셋에 셰이더가 없습니다."));
    if (count > 1)
        return fail(QStringLiteral(
            "다중 패스 프리셋(%1패스)은 지원하지 않습니다. "
            "단일 패스 .slang 파일을 직접 선택해 주세요.").arg(count));

    // shader0 = 경로
    static const QRegularExpression rePath(R"RX(^\s*shader0\s*=\s*"?([^"\r\n]+)"?)RX",
                                           QRegularExpression::MultilineOption);
    const QRegularExpressionMatch mp = rePath.match(presetText);
    if (!mp.hasMatch())
        return fail(QStringLiteral("프리셋에 shader0 경로가 없습니다."));

    QString rel = mp.captured(1).trimmed();
    if (rel.isEmpty())
        return fail(QStringLiteral("shader0 경로가 비어 있습니다."));

    QFileInfo fi(rel);
    const QString abs = fi.isAbsolute() ? QDir::cleanPath(rel)
                                        : QDir::cleanPath(presetDir + "/" + rel);
    if (err) err->clear();
    return abs;
}
