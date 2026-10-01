// SlangCompile.cpp — .slang → SPIR-V → GLSL 330 (+ 리플렉션)

#include "SlangCompile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QSet>

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <spirv_cross/spirv_cross_c.h>

namespace {

// ─────────────────────────────────────────────────────────────
//  1. 전처리 — #include 재귀 해석
// ─────────────────────────────────────────────────────────────
//  Mega Bezel 은 공용 헬퍼를 #include 로 잘게 쪼개 놓았다. glslang 의
//  include 콜백을 쓰지 않고 직접 펼치는 이유는 C API 쪽 콜백 시그니처가
//  버전마다 달라 이식성이 떨어지기 때문이다.
bool expandIncludes(QString& text, const QString& dir,
                    QSet<QString>& active, int depth, QString& err) {
    if (depth > 32) { err = QStringLiteral("#include 중첩이 너무 깊습니다."); return false; }

    static const QRegularExpression re(
        QStringLiteral("^[ \t]*#[ \t]*include[ \t]+[\"<]([^\">]+)[\">][ \t]*$"));

    QString out;
    out.reserve(text.size() * 2);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const auto m = re.match(line);
        if (!m.hasMatch()) { out += line; out += QLatin1Char('\n'); continue; }

        const QString rel = m.captured(1);
        const QString abs = QDir::cleanPath(dir + QLatin1Char('/') + rel);
        if (active.contains(abs)) {         // 순환 include 는 조용히 건너뛴다
            out += QLatin1Char('\n');
            continue;
        }
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) {
            err = QStringLiteral("include 파일을 찾을 수 없습니다: %1").arg(rel);
            return false;
        }
        QString sub = QString::fromUtf8(f.readAll());
        f.close();
        sub.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        sub.replace(QLatin1Char('\r'),     QLatin1Char('\n'));
        active.insert(abs);
        if (!expandIncludes(sub, QFileInfo(abs).absolutePath(), active, depth + 1, err))
            return false;
        active.remove(abs);
        out += sub;
        if (!sub.endsWith(QLatin1Char('\n'))) out += QLatin1Char('\n');
    }
    text = out;
    return true;
}

// ─────────────────────────────────────────────────────────────
//  2. #pragma 추출 + 스테이지 분리
// ─────────────────────────────────────────────────────────────
struct Split {
    QString vert, frag;
    QString name, format;
    QVector<SlangParamDecl> params;
};

// #pragma parameter NAME "설명" 기본값 최소 최대 [단계]
//   설명에 공백이 들어가므로 따옴표를 먼저 떼어낸 뒤 나머지를 쪼갠다.
bool parseParamPragma(const QString& rest, SlangParamDecl& out) {
    QString s = rest.trimmed();
    const int sp = s.indexOf(QRegularExpression(QStringLiteral("\\s")));
    if (sp <= 0) return false;
    out.name = s.left(sp);
    s = s.mid(sp).trimmed();

    if (s.startsWith(QLatin1Char('"'))) {
        const int close = s.indexOf(QLatin1Char('"'), 1);
        if (close < 0) return false;
        out.desc = s.mid(1, close - 1);
        s = s.mid(close + 1).trimmed();
    }
    const QStringList nums =
        s.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (nums.size() < 3) return false;
    out.def  = nums[0].toFloat();
    out.min  = nums[1].toFloat();
    out.max  = nums[2].toFloat();
    out.step = (nums.size() > 3) ? nums[3].toFloat() : 0.01f;
    if (out.step <= 0.f) out.step = 0.01f;
    if (out.desc.isEmpty()) out.desc = out.name;
    return true;
}

Split splitStages(const QString& text) {
    Split s;
    int stage = 0;                       // 0=공통(양쪽), 1=vertex, 2=fragment

    // 줄 수를 유지해야 glslang 오류 줄번호가 원본과 맞는다.
    auto blank = [&] { s.vert += QLatin1Char('\n'); s.frag += QLatin1Char('\n'); };

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1String("#pragma"))) {
            const QString rest = t.mid(7).trimmed();
            if (rest.startsWith(QLatin1String("stage"))) {
                const QString which = rest.mid(5).trimmed();
                stage = which.startsWith(QLatin1String("vert")) ? 1
                      : which.startsWith(QLatin1String("frag")) ? 2 : 0;
                blank();
                continue;
            }
            if (rest.startsWith(QLatin1String("name"))) {
                s.name = rest.mid(4).trimmed().remove(QLatin1Char('"'));
                blank();
                continue;
            }
            if (rest.startsWith(QLatin1String("format"))) {
                s.format = rest.mid(6).trimmed();
                blank();
                continue;
            }
            if (rest.startsWith(QLatin1String("parameter"))) {
                SlangParamDecl p;
                if (parseParamPragma(rest.mid(9), p)) {
                    bool dup = false;
                    for (const SlangParamDecl& e : s.params)
                        if (e.name == p.name) { dup = true; break; }
                    if (!dup) s.params.append(p);
                }
                blank();
                continue;
            }
            blank();       // 그 밖의 #pragma 는 glslang 이 경고를 내므로 지운다
            continue;
        }

        if (stage == 0)      { s.vert += line; s.vert += QLatin1Char('\n');
                               s.frag += line; s.frag += QLatin1Char('\n'); }
        else if (stage == 1) { s.vert += line; s.vert += QLatin1Char('\n');
                               s.frag += QLatin1Char('\n'); }
        else                 { s.vert += QLatin1Char('\n');
                               s.frag += line; s.frag += QLatin1Char('\n'); }
    }
    return s;
}

// ─────────────────────────────────────────────────────────────
//  3. glslang: Vulkan GLSL → SPIR-V
// ─────────────────────────────────────────────────────────────
QMutex g_glslangMutex;
bool   g_glslangInit = false;

bool toSpirv(const QByteArray& src, glslang_stage_t stage,
             QVector<unsigned>& spv, QString& err) {
    QMutexLocker lock(&g_glslangMutex);
    if (!g_glslangInit) { glslang_initialize_process(); g_glslangInit = true; }

    glslang_input_t input {};
    input.language                          = GLSLANG_SOURCE_GLSL;
    input.stage                             = stage;
    input.client                            = GLSLANG_CLIENT_VULKAN;
    input.client_version                    = GLSLANG_TARGET_VULKAN_1_0;
    input.target_language                   = GLSLANG_TARGET_SPV;
    input.target_language_version           = GLSLANG_TARGET_SPV_1_0;
    input.code                              = src.constData();
    input.default_version                   = 450;
    input.default_profile                   = GLSLANG_CORE_PROFILE;
    input.force_default_version_and_profile = 0;
    input.forward_compatible                = 0;
    input.messages                          = GLSLANG_MSG_DEFAULT_BIT;
    input.resource                          = glslang_default_resource();

    glslang_shader_t* sh = glslang_shader_create(&input);
    if (!sh) { err = QStringLiteral("glslang shader 생성 실패"); return false; }

    auto fail = [&](const char* what) {
        const char* a = glslang_shader_get_info_log(sh);
        const char* b = glslang_shader_get_info_debug_log(sh);
        err = QStringLiteral("%1: %2 %3").arg(QLatin1String(what),
              QString::fromUtf8(a ? a : ""), QString::fromUtf8(b ? b : "")).trimmed();
        glslang_shader_delete(sh);
        return false;
    };

    if (!glslang_shader_preprocess(sh, &input)) return fail("전처리 실패");
    if (!glslang_shader_parse(sh, &input))      return fail("컴파일 실패");

    glslang_program_t* pr = glslang_program_create();
    glslang_program_add_shader(pr, sh);
    if (!glslang_program_link(pr, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        const char* a = glslang_program_get_info_log(pr);
        err = QStringLiteral("링크 실패: %1").arg(QString::fromUtf8(a ? a : ""));
        glslang_program_delete(pr);
        glslang_shader_delete(sh);
        return false;
    }
    glslang_program_SPIRV_generate(pr, stage);
    const size_t n = glslang_program_SPIRV_get_size(pr);
    spv.resize(int(n));
    if (n > 0) glslang_program_SPIRV_get(pr, spv.data());

    glslang_program_delete(pr);
    glslang_shader_delete(sh);
    if (n == 0) { err = QStringLiteral("SPIR-V 생성 결과가 비었습니다."); return false; }
    return true;
}

// ─────────────────────────────────────────────────────────────
//  4. SPIRV-Cross: SPIR-V → GLSL 330 + 리플렉션
// ─────────────────────────────────────────────────────────────
void readBlock(spvc_compiler comp, const spvc_reflected_resource& r, SlangBlock& out) {
    out.present = true;
    out.binding = spvc_compiler_get_decoration(comp, r.id, SpvDecorationBinding);

    spvc_type t = spvc_compiler_get_type_handle(comp, r.base_type_id);
    size_t sz = 0;
    if (spvc_compiler_get_declared_struct_size(comp, t, &sz) == SPVC_SUCCESS)
        out.size = quint32(sz);

    const unsigned n = spvc_type_get_num_member_types(t);
    for (unsigned i = 0; i < n; ++i) {
        SlangMember m;
        const char* nm = spvc_compiler_get_member_name(comp, r.base_type_id, i);
        m.name = QString::fromUtf8(nm ? nm : "");
        unsigned off = 0;
        if (spvc_compiler_type_struct_member_offset(comp, t, i, &off) == SPVC_SUCCESS)
            m.offset = off;
        size_t msz = 0;
        if (spvc_compiler_get_declared_struct_member_size(comp, t, i, &msz) == SPVC_SUCCESS)
            m.size = quint32(msz);
        if (!m.name.isEmpty()) out.members.append(m);
    }
}

bool toGlslGL(const QVector<unsigned>& spv, int version, QByteArray& glsl,
              SlangBlock* ubo, SlangBlock* push, QStringList* samplers,
              QString& err) {
    spvc_context ctx = nullptr;
    if (spvc_context_create(&ctx) != SPVC_SUCCESS) {
        err = QStringLiteral("SPIRV-Cross 컨텍스트 생성 실패"); return false;
    }
    struct Guard { spvc_context c; ~Guard() { spvc_context_destroy(c); } } guard{ctx};

    spvc_parsed_ir ir = nullptr;
    if (spvc_context_parse_spirv(ctx, spv.constData(), size_t(spv.size()), &ir)
            != SPVC_SUCCESS) {
        err = QStringLiteral("SPIR-V 파싱 실패: %1")
              .arg(QString::fromUtf8(spvc_context_get_last_error_string(ctx)));
        return false;
    }
    spvc_compiler comp = nullptr;
    if (spvc_context_create_compiler(ctx, SPVC_BACKEND_GLSL, ir,
            SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &comp) != SPVC_SUCCESS) {
        err = QStringLiteral("GLSL 백엔드 생성 실패"); return false;
    }

    spvc_compiler_options opt = nullptr;
    spvc_compiler_create_compiler_options(comp, &opt);
    spvc_compiler_options_set_uint(opt, SPVC_COMPILER_OPTION_GLSL_VERSION, unsigned(version));
    spvc_compiler_options_set_bool(opt, SPVC_COMPILER_OPTION_GLSL_ES, SPVC_FALSE);
    spvc_compiler_options_set_bool(opt, SPVC_COMPILER_OPTION_GLSL_VULKAN_SEMANTICS, SPVC_FALSE);
    // push_constant 도 그냥 유니폼 블록으로 뽑는다. 그래야 UBO 와 같은 방식으로
    //   오프셋을 계산해 버퍼 하나로 올릴 수 있다.
    spvc_compiler_options_set_bool(opt,
        SPVC_COMPILER_OPTION_GLSL_EMIT_PUSH_CONSTANT_AS_UNIFORM_BUFFER, SPVC_TRUE);
    // layout(binding=) 은 4.2 부터다. 3.3 컨텍스트에서도 돌아야 하므로 끄고,
    //   샘플러/유니폼 블록은 이름으로 직접 바인딩한다.
    spvc_compiler_options_set_bool(opt,
        SPVC_COMPILER_OPTION_GLSL_ENABLE_420PACK_EXTENSION, SPVC_FALSE);
    spvc_compiler_install_compiler_options(comp, opt);

    spvc_resources res = nullptr;
    if (spvc_compiler_create_shader_resources(comp, &res) != SPVC_SUCCESS) {
        err = QStringLiteral("리소스 조회 실패"); return false;
    }

    const spvc_reflected_resource* list = nullptr;
    size_t count = 0;

    // 블록 이름을 우리가 정해 둔다. 뒤에서 glGetUniformBlockIndex 로 찾아야 하는데
    //   SPIRV-Cross 가 이름을 손볼 수 있어 추측하면 안 되기 때문이다.
    if (ubo) {
        spvc_resources_get_resource_list_for_type(res,
            SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &list, &count);
        if (count > 0) {
            spvc_compiler_set_name(comp, list[0].base_type_id, "RA_UBO");
            readBlock(comp, list[0], *ubo);
            ubo->glslName = QStringLiteral("RA_UBO");
        }
    }
    if (push) {
        spvc_resources_get_resource_list_for_type(res,
            SPVC_RESOURCE_TYPE_PUSH_CONSTANT, &list, &count);
        if (count > 0) {
            spvc_compiler_set_name(comp, list[0].base_type_id, "RA_PUSH");
            readBlock(comp, list[0], *push);
            push->glslName = QStringLiteral("RA_PUSH");
        }
    }

    QVector<spvc_variable_id> samplerIds;
    if (samplers) {
        spvc_resources_get_resource_list_for_type(res,
            SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &list, &count);
        for (size_t i = 0; i < count; ++i) samplerIds.append(list[i].id);
    }

    const char* out = nullptr;
    if (spvc_compiler_compile(comp, &out) != SPVC_SUCCESS || !out) {
        err = QStringLiteral("GLSL 변환 실패: %1")
              .arg(QString::fromUtf8(spvc_context_get_last_error_string(ctx)));
        return false;
    }
    glsl = QByteArray(out);

    // 이름은 변환이 끝난 뒤에 읽어야 최종 이름이 나온다.
    if (samplers)
        for (spvc_variable_id id : samplerIds) {
            const char* nm = spvc_compiler_get_name(comp, id);
            if (nm && *nm) samplers->append(QString::fromUtf8(nm));
        }
    return true;
}

}  // namespace

// ─────────────────────────────────────────────────────────────
SlangCompiled compileSlangSource(const QString& source, const QString& srcDir,
                                 const QString& displayName, int glslVersion) {
    SlangCompiled c;

    // 줄바꿈 정규화. RetroArch 셰이더 팩은 CRLF 로 배포되는 것이 많은데,
    //   '\r' 이 줄 끝에 남으면 "#include ..." 같은 줄 단위 패턴이 전부 어긋난다.
    QString text = source;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'),     QLatin1Char('\n'));

    QSet<QString> active;
    QString err;
    if (!expandIncludes(text, srcDir, active, 0, err)) {
        c.error = QStringLiteral("%1: %2").arg(displayName, err);
        return c;
    }

    // 진단용: 환경변수가 설정돼 있으면 #include 를 다 펼친 소스를 남긴다.
    //   셰이더가 안 돌 때 사용자에게 이 파일을 받으면 원인을 바로 볼 수 있다.
    if (qEnvironmentVariableIsSet("FBNRX_SLANG_DUMP")) {
        QFile d(qEnvironmentVariable("FBNRX_SLANG_DUMP") + QLatin1Char('/')
                + displayName + QStringLiteral(".expanded.glsl"));
        if (d.open(QIODevice::WriteOnly)) d.write(text.toUtf8());
    }

    const Split s = splitStages(text);
    c.pragmaName   = s.name;
    c.pragmaFormat = s.format;
    c.parameters   = s.params;

    if (s.vert.trimmed().isEmpty() || s.frag.trimmed().isEmpty()) {
        c.error = QStringLiteral("%1: vertex/fragment 스테이지를 찾지 못했습니다.")
                  .arg(displayName);
        return c;
    }

    QVector<unsigned> spvV, spvF;
    if (!toSpirv(s.vert.toUtf8(), GLSLANG_STAGE_VERTEX, spvV, err)) {
        c.error = QStringLiteral("%1 [vertex] %2").arg(displayName, err);
        return c;
    }
    if (!toSpirv(s.frag.toUtf8(), GLSLANG_STAGE_FRAGMENT, spvF, err)) {
        c.error = QStringLiteral("%1 [fragment] %2").arg(displayName, err);
        return c;
    }

    // 유니폼 블록 선언은 두 스테이지가 공유한다. 다만 스테이지마다 실제로
    //   쓰는 멤버가 달라 리플렉션 결과도 달라질 수 있어 병합한다.
    SlangBlock uboV, pushV;
    if (!toGlslGL(spvV, glslVersion, c.vertexGlsl, &uboV, &pushV, nullptr, err)) {
        c.error = QStringLiteral("%1 [vertex] %2").arg(displayName, err);
        return c;
    }
    if (!toGlslGL(spvF, glslVersion, c.fragmentGlsl, &c.ubo, &c.push, &c.textures, err)) {
        c.error = QStringLiteral("%1 [fragment] %2").arg(displayName, err);
        return c;
    }
    auto merge = [](SlangBlock& dst, const SlangBlock& src) {
        if (!src.present) return;
        if (!dst.present) { dst = src; return; }
        dst.size = qMax(dst.size, src.size);
        for (const SlangMember& m : src.members)
            if (!dst.find(m.name)) dst.members.append(m);
    };
    merge(c.ubo,  uboV);
    merge(c.push, pushV);

    c.ok = true;
    return c;
}

SlangCompiled compileSlangFile(const QString& path, int glslVersion) {
    SlangCompiled c;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        c.error = QStringLiteral("셰이더 파일을 열 수 없습니다: %1").arg(path);
        return c;
    }
    const QString src = QString::fromUtf8(f.readAll());
    return compileSlangSource(src, QFileInfo(path).absolutePath(),
                              QFileInfo(path).fileName(), glslVersion);
}
