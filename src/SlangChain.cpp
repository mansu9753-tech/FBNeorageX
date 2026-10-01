// SlangChain.cpp — RetroArch 셰이더 체인 실행기

#include "SlangChain.h"

#include <QFileInfo>
#include <QImage>
#include <QOpenGLContext>
#include <cstring>

// Qt 가 노출하는 GL 헤더 수준이 플랫폼마다 달라서, 쓰는 상수는 직접 정의해 둔다.
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

namespace {

constexpr int kUboBinding  = 0;
constexpr int kPushBinding = 1;

int wrapEnum(SlangWrap w) {
    switch (w) {
    case SlangWrap::Repeat:        return GL_REPEAT;
    case SlangWrap::ClampToBorder: return GL_CLAMP_TO_BORDER;
    default:                       return GL_CLAMP_TO_EDGE;
    }
}

// "이름Size" 에서 이름 부분만 떼어낸다. Size 로 끝나지 않으면 빈 문자열.
QString sizeBaseName(const QString& member) {
    if (!member.endsWith(QLatin1String("Size"))) return QString();
    return member.left(member.size() - 4);
}

// 끝에 붙은 숫자를 떼어 낸다. ("PassOutput12" → base="PassOutput", n=12)
bool splitTrailingNumber(const QString& s, const QString& prefix, int& n) {
    if (!s.startsWith(prefix)) return false;
    const QString rest = s.mid(prefix.size());
    if (rest.isEmpty()) return false;
    bool ok = false;
    const int v = rest.toInt(&ok);
    return ok && v >= 0 ? (n = v, true) : false;
}

}  // namespace

SlangChain::SlangChain()  { }
SlangChain::~SlangChain() { }

void SlangChain::addLog(const QString& s) {
    m_log.append(s);
    if (m_log.size() > 200) m_log.removeFirst();
}

bool SlangChain::resolveGlFunctions(QOpenGLContext* ctx) {
    if (!ctx) return false;
    auto get = [&](const char* n) { return ctx->getProcAddress(n); };

    p_glGenVertexArrays    = reinterpret_cast<void(*)(int, unsigned*)>(get("glGenVertexArrays"));
    p_glBindVertexArray    = reinterpret_cast<void(*)(unsigned)>(get("glBindVertexArray"));
    p_glDeleteVertexArrays = reinterpret_cast<void(*)(int, const unsigned*)>(get("glDeleteVertexArrays"));
    p_glGetUniformBlockIndex = reinterpret_cast<unsigned(*)(unsigned, const char*)>(get("glGetUniformBlockIndex"));
    p_glUniformBlockBinding  = reinterpret_cast<void(*)(unsigned, unsigned, unsigned)>(get("glUniformBlockBinding"));
    p_glBindBufferBase       = reinterpret_cast<void(*)(unsigned, unsigned, unsigned)>(get("glBindBufferBase"));
    p_glBlitFramebuffer      = reinterpret_cast<void(*)(int,int,int,int,int,int,int,int,unsigned,unsigned)>(get("glBlitFramebuffer"));
    p_glGenSamplers       = reinterpret_cast<void(*)(int, unsigned*)>(get("glGenSamplers"));
    p_glDeleteSamplers    = reinterpret_cast<void(*)(int, const unsigned*)>(get("glDeleteSamplers"));
    p_glBindSampler       = reinterpret_cast<void(*)(unsigned, unsigned)>(get("glBindSampler"));
    p_glSamplerParameteri = reinterpret_cast<void(*)(unsigned, unsigned, int)>(get("glSamplerParameteri"));
    p_glGenQueries          = reinterpret_cast<void(*)(int, unsigned*)>(get("glGenQueries"));
    p_glDeleteQueries       = reinterpret_cast<void(*)(int, const unsigned*)>(get("glDeleteQueries"));
    p_glBeginQuery          = reinterpret_cast<void(*)(unsigned, unsigned)>(get("glBeginQuery"));
    p_glEndQuery            = reinterpret_cast<void(*)(unsigned)>(get("glEndQuery"));
    p_glGetQueryObjectuiv   = reinterpret_cast<void(*)(unsigned, unsigned, unsigned*)>(get("glGetQueryObjectuiv"));
    p_glGetQueryObjectui64v = reinterpret_cast<void(*)(unsigned, unsigned, quint64*)>(get("glGetQueryObjectui64v"));

    // 유니폼 블록은 대체 수단이 없다. 이게 없으면 이 경로 자체를 못 쓴다.
    return p_glGetUniformBlockIndex && p_glUniformBlockBinding && p_glBindBufferBase;
}

// 필터/랩 조합마다 샘플러를 하나씩 만들어 재사용한다.
//   조합 수는 몇 개 안 되므로 전부 캐시해도 부담이 없다.
unsigned SlangChain::samplerFor(bool linear, bool mipmap, SlangWrap wrap) {
    if (!p_glGenSamplers) return 0;          // 못 쓰면 예전 방식으로 돌아간다
    const int key = (linear ? 1 : 0) | (mipmap ? 2 : 0) | (int(wrap) << 2);
    const auto it = m_samplers.constFind(key);
    if (it != m_samplers.constEnd()) return it.value();

    unsigned s = 0;
    p_glGenSamplers(1, &s);
    p_glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    p_glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER,
                          mipmap ? GL_LINEAR_MIPMAP_LINEAR
                                 : (linear ? GL_LINEAR : GL_NEAREST));
    p_glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wrapEnum(wrap));
    p_glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wrapEnum(wrap));
    m_samplers.insert(key, s);
    return s;
}

// ── GPU 타이머 ───────────────────────────────────────────────
//   측정 자체가 프레임을 막으면 안 되므로, 질의를 여러 개 돌려 쓰고
//   "이미 결과가 나온 것" 만 읽는다.
void SlangChain::beginGpuTimer() {
    if (!p_glBeginQuery || !p_glGetQueryObjectui64v) return;
    if (!m_queries[0]) {
        p_glGenQueries(kQueryRing, m_queries);
        for (int i = 0; i < kQueryRing; ++i) m_queryBusy[i] = false;
    }
    if (m_queryBusy[m_queryHead]) return;      // 아직 안 끝났으면 이번 프레임은 건너뛴다
    p_glBeginQuery(GL_TIME_ELAPSED, m_queries[m_queryHead]);
}

void SlangChain::endGpuTimer() {
    if (!p_glBeginQuery || !p_glGetQueryObjectui64v || !m_queries[0]) return;

    if (!m_queryBusy[m_queryHead]) {
        p_glEndQuery(GL_TIME_ELAPSED);
        m_queryBusy[m_queryHead] = true;
        m_queryHead = (m_queryHead + 1) % kQueryRing;
    }

    // 결과가 준비된 가장 오래된 질의를 거둔다
    for (int i = 0; i < kQueryRing; ++i) {
        const int idx = (m_queryHead + i) % kQueryRing;
        if (!m_queryBusy[idx]) continue;
        unsigned ready = 0;
        p_glGetQueryObjectuiv(m_queries[idx], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) break;
        quint64 ns = 0;
        p_glGetQueryObjectui64v(m_queries[idx], GL_QUERY_RESULT, &ns);
        m_queryBusy[idx] = false;
        m_gpuAccum += double(ns) / 1.0e6;
        ++m_gpuSamples;
    }

    if (m_gpuSamples >= 120) {                 // 약 2초마다 평균을 낸다
        m_gpuMs = m_gpuAccum / m_gpuSamples;
        m_gpuAccum = 0.0;
        m_gpuSamples = 0;

        // 60fps 예산은 16.7ms. 셰이더 혼자 그걸 넘기면 실제로 느려진다.
        if (m_warnCooldown > 0) --m_warnCooldown;

        // 셰이더를 새로 걸면 첫 측정값은 한 번 알려준다.
        //   "이 프리셋이 얼마나 무거운가" 를 눈으로 볼 수 있어야 고를 수 있다.
        if (!m_reportedOnce) {
            m_reportedOnce = true;
            m_perfWarning = QStringLiteral("셰이더 GPU 시간: 프레임당 %1 ms (패스 %2개, 60fps 한계 16.7 ms)")
                            .arg(m_gpuMs, 0, 'f', 1).arg(m_passes.size());
            m_warnCooldown = 15;
        }
        else if (m_gpuMs > 15.0 && m_warnCooldown == 0) {
            m_perfWarning = QStringLiteral(
                "셰이더가 GPU 시간을 프레임당 %1 ms 쓰고 있습니다 "
                "(60fps 한계 16.7 ms, 패스 %2개). 더 가벼운 프리셋을 쓰면 부드러워집니다.")
                .arg(m_gpuMs, 0, 'f', 1).arg(m_passes.size());
            m_warnCooldown = 15;               // 약 30초에 한 번만 알린다
        }
    }
}

bool SlangChain::takePerfWarning(QString& out) {
    if (m_perfWarning.isEmpty()) return false;
    out = m_perfWarning;
    m_perfWarning.clear();
    return true;
}

// ─────────────────────────────────────────────────────────────
//  로드
// ─────────────────────────────────────────────────────────────
bool SlangChain::load(const QString& presetPath, int glslVersion, QString& error) {
    release();

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) { error = QStringLiteral("OpenGL 컨텍스트가 없습니다."); return false; }
    f = ctx->functions();
    if (!resolveGlFunctions(ctx)) {
        error = QStringLiteral("이 그래픽 드라이버는 유니폼 버퍼(OpenGL 3.1)를 "
                               "지원하지 않아 slang 셰이더를 쓸 수 없습니다.");
        return false;
    }

    SlangPreset preset;
    const QString lower = presetPath.toLower();
    if (lower.endsWith(QLatin1String(".slangp"))) {
        preset = parseSlangPresetFile(presetPath);
        if (!preset.ok) { error = preset.error; release(); return false; }
    } else {
        // .slang 파일 하나만 고른 경우 — 단일 패스 프리셋처럼 다룬다.
        SlangPass p;
        p.path = presetPath;
        p.filterLinear = true;
        preset.passes.append(p);
        preset.ok = true;
    }
    if (preset.passes.isEmpty()) {
        error = QStringLiteral("프리셋에 패스가 없습니다.");
        release();
        return false;
    }

    if (!compilePasses(preset, glslVersion, error)) { release(); return false; }

    // ── LUT 로드 ─────────────────────────────────────────
    for (const SlangLut& l : preset.luts) {
        QImage img(l.path);
        if (img.isNull()) {
            error = QStringLiteral("LUT 이미지를 읽을 수 없습니다: %1")
                    .arg(QFileInfo(l.path).fileName());
            release();
            return false;
        }
        img = img.convertToFormat(QImage::Format_RGBA8888);
        Lut lut;
        lut.name = l.name;
        lut.size = img.size();
        f->glGenTextures(1, &lut.tex);
        f->glBindTexture(GL_TEXTURE_2D, lut.tex);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(), 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapEnum(l.wrap));
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapEnum(l.wrap));
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                           l.linear ? GL_LINEAR : GL_NEAREST);
        if (l.mipmap) {
            f->glGenerateMipmap(GL_TEXTURE_2D);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                               GL_LINEAR_MIPMAP_LINEAR);
        } else {
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                               l.linear ? GL_LINEAR : GL_NEAREST);
        }
        m_luts.append(lut);
    }
    f->glBindTexture(GL_TEXTURE_2D, 0);

    // ── alias 표 ─────────────────────────────────────────
    for (int i = 0; i < m_passes.size(); ++i) {
        QString a = m_passes[i].cfg.alias;
        if (a.isEmpty()) a = m_passes[i].sh.pragmaName;   // #pragma name 도 alias 다
        if (!a.isEmpty()) m_aliasToPass.insert(a.toLower(), i);
    }

    // ── 파라미터 기본값 → 프리셋 값으로 덮어쓰기 ─────────
    for (const Pass& p : m_passes)
        for (const SlangParamDecl& d : p.sh.parameters) {
            if (m_paramIndex.contains(d.name)) continue;
            m_paramIndex.insert(d.name, m_paramStore.size());
            m_paramStore.append(d.def);
            m_paramDecls.append(d);
        }
    // 프리셋 키는 소문자로 정규화돼 있으니, 소문자 색인을 한 번 만들어 두고 찾는다.
    //   (예전에는 파라미터 900개 x 프리셋 값 수백 개를 전부 훑었다)
    {
        QHash<QString, int> lower;
        for (auto it = m_paramIndex.constBegin(); it != m_paramIndex.constEnd(); ++it)
            lower.insert(it.key().toLower(), it.value());
        for (auto it = preset.params.constBegin(); it != preset.params.constEnd(); ++it) {
            const auto f2 = lower.constFind(it.key());
            if (f2 != lower.constEnd()) m_paramStore[f2.value()] = it.value();
        }
    }

    // ── 유니폼/텍스처 바인딩 계획 ────────────────────────
    for (int i = 0; i < m_passes.size(); ++i) {
        m_passes[i].isLast = (i == m_passes.size() - 1);
        planTextures(m_passes[i], i);
        planUniforms(m_passes[i], i);
    }

    // 피드백/히스토리 필요량 산출
    for (const Pass& p : m_passes)
        for (const TexBind& b : p.texBinds) {
            if (b.ref.kind == TexRef::PassFeedback
                && b.ref.idx >= 0 && b.ref.idx < m_passes.size())
                m_passes[b.ref.idx].pingPong = true;
            if (b.ref.kind == TexRef::History)
                m_historyDepth = qMax(m_historyDepth, b.ref.idx);
        }
    for (const Pass& p : m_passes)
        for (const UniPlan& u : p.uboPlan + p.pushPlan)
            if (u.kind == UniKind::TexSize && u.tex.kind == TexRef::History)
                m_historyDepth = qMax(m_historyDepth, u.tex.idx);

    // ── 정점 버퍼 ────────────────────────────────────────
    if (p_glGenVertexArrays) { p_glGenVertexArrays(1, &m_vao); p_glBindVertexArray(m_vao); }
    f->glGenBuffers(1, &m_vbo);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    f->glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    if (p_glBindVertexArray) p_glBindVertexArray(0);

    addLog(QStringLiteral("패스 %1개, LUT %2개, 파라미터 %3개, 히스토리 %4프레임")
           .arg(m_passes.size()).arg(m_luts.size())
           .arg(m_paramDecls.size()).arg(m_historyDepth));
    return true;
}

bool SlangChain::compilePasses(const SlangPreset& preset, int glslVersion,
                               QString& error) {
    for (int i = 0; i < preset.passes.size(); ++i) {
        Pass p;
        p.cfg = preset.passes[i];
        p.sh  = compileSlangFile(p.cfg.path, glslVersion);
        if (!p.sh.ok) {
            error = QStringLiteral("패스 %1 — %2").arg(i).arg(p.sh.error);
            return false;
        }
        // #pragma format 이 부동소수점을 요구하면 프리셋 설정보다 우선한다
        if (p.sh.pragmaFormat.contains(QLatin1String("SFLOAT")))
            p.cfg.floatFbo = true;
        m_passes.append(std::move(p));
    }
    for (Pass& p : m_passes)
        if (!buildProgram(p, error)) return false;
    return true;
}

bool SlangChain::buildProgram(Pass& p, QString& error) {
    auto compile = [&](unsigned type, const QByteArray& src, unsigned& out) {
        out = f->glCreateShader(type);
        const char* s = src.constData();
        const int   n = src.size();
        f->glShaderSource(out, 1, &s, &n);
        f->glCompileShader(out);
        int ok = 0;
        f->glGetShaderiv(out, GL_COMPILE_STATUS, &ok);
        if (ok) return true;
        char buf[4096] = {0};
        int len = 0;
        f->glGetShaderInfoLog(out, sizeof(buf) - 1, &len, buf);
        error = QStringLiteral("%1 %2 셰이더 컴파일 실패: %3")
                .arg(QFileInfo(p.cfg.path).fileName(),
                     type == GL_VERTEX_SHADER ? QStringLiteral("정점")
                                              : QStringLiteral("프래그먼트"),
                     QString::fromUtf8(buf, len));
        return false;
    };

    unsigned vs = 0, fs = 0;
    if (!compile(GL_VERTEX_SHADER, p.sh.vertexGlsl, vs)) { f->glDeleteShader(vs); return false; }
    if (!compile(GL_FRAGMENT_SHADER, p.sh.fragmentGlsl, fs)) {
        f->glDeleteShader(vs); f->glDeleteShader(fs); return false;
    }
    p.prog = f->glCreateProgram();
    f->glAttachShader(p.prog, vs);
    f->glAttachShader(p.prog, fs);
    // SPIRV-Cross 는 정점 입력에 layout(location=) 을 그대로 남기지만,
    //   혹시 빠지는 경우를 대비해 이름으로도 못 박아 둔다.
    f->glBindAttribLocation(p.prog, 0, "Position");
    f->glBindAttribLocation(p.prog, 1, "TexCoord");
    f->glLinkProgram(p.prog);
    f->glDeleteShader(vs);
    f->glDeleteShader(fs);

    int ok = 0;
    f->glGetProgramiv(p.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[4096] = {0};
        int len = 0;
        f->glGetProgramInfoLog(p.prog, sizeof(buf) - 1, &len, buf);
        error = QStringLiteral("%1 링크 실패: %2")
                .arg(QFileInfo(p.cfg.path).fileName(), QString::fromUtf8(buf, len));
        return false;
    }

    // 유니폼 블록 → 바인딩 포인트 연결
    if (p.sh.ubo.present) {
        const unsigned idx = p_glGetUniformBlockIndex(p.prog,
                                 p.sh.ubo.glslName.toUtf8().constData());
        if (idx != 0xFFFFFFFFu) {
            p_glUniformBlockBinding(p.prog, idx, kUboBinding);
            p.uboIndex = int(idx);
            f->glGenBuffers(1, &p.uboBuf);
            p.uboData.resize(int((p.sh.ubo.size + 15) & ~15u));
        }
    }
    if (p.sh.push.present) {
        const unsigned idx = p_glGetUniformBlockIndex(p.prog,
                                 p.sh.push.glslName.toUtf8().constData());
        if (idx != 0xFFFFFFFFu) {
            p_glUniformBlockBinding(p.prog, idx, kPushBinding);
            p.pushIndex = int(idx);
            f->glGenBuffers(1, &p.pushBuf);
            p.pushData.resize(int((p.sh.push.size + 15) & ~15u));
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────
//  시맨틱 해석
// ─────────────────────────────────────────────────────────────
SlangChain::TexRef SlangChain::resolveTexture(const QString& name, int passIndex) const {
    TexRef r;
    int n = 0;

    if (name == QLatin1String("Original")) { r.kind = TexRef::Original; return r; }
    if (name == QLatin1String("Source"))   { r.kind = TexRef::Source;   return r; }

    if (splitTrailingNumber(name, QStringLiteral("OriginalHistory"), n)) {
        // OriginalHistory0 은 Original 과 같다
        if (n == 0) { r.kind = TexRef::Original; return r; }
        r.kind = TexRef::History; r.idx = n; return r;
    }
    if (splitTrailingNumber(name, QStringLiteral("PassFeedback"), n)) {
        r.kind = TexRef::PassFeedback; r.idx = n; return r;
    }
    if (splitTrailingNumber(name, QStringLiteral("PassOutput"), n)) {
        r.kind = TexRef::PassOut; r.idx = n; return r;
    }
    if (splitTrailingNumber(name, QStringLiteral("User"), n)) {
        if (n < m_luts.size()) { r.kind = TexRef::Lut; r.idx = n; return r; }
    }

    // LUT 이름
    for (int i = 0; i < m_luts.size(); ++i)
        if (m_luts[i].name == name) { r.kind = TexRef::Lut; r.idx = i; return r; }

    // alias — "<alias>Feedback" 도 받는다
    if (name.endsWith(QLatin1String("Feedback"))) {
        const QString base = name.left(name.size() - 8).toLower();
        const auto it = m_aliasToPass.constFind(base);
        if (it != m_aliasToPass.constEnd()) {
            r.kind = TexRef::PassFeedback; r.idx = it.value(); return r;
        }
    }
    {
        const auto it = m_aliasToPass.constFind(name.toLower());
        if (it != m_aliasToPass.constEnd()) {
            // 자기 자신을 alias 로 참조하면 그건 피드백이다
            r.kind = (it.value() == passIndex) ? TexRef::PassFeedback : TexRef::PassOut;
            r.idx  = it.value();
            return r;
        }
    }
    return r;   // None
}

void SlangChain::planTextures(Pass& p, int passIndex) {
    int unit = 0;
    for (const QString& name : p.sh.textures) {
        const int loc = f->glGetUniformLocation(p.prog, name.toUtf8().constData());
        if (loc < 0) continue;                 // 최적화로 사라진 샘플러
        TexBind b;
        b.ref  = resolveTexture(name, passIndex);
        b.unit = unit++;
        b.loc  = loc;
        if (b.ref.kind == TexRef::None) {
            addLog(QStringLiteral("알 수 없는 텍스처 이름: %1 (검은색으로 처리)").arg(name));
        }

        // 필터/랩 규칙:
        //   Source·Original 은 지금 패스 설정을, 앞 패스 출력은 그 패스 설정을,
        //   LUT 는 자기 설정을 따른다. (RetroArch 와 같은 규칙)
        switch (b.ref.kind) {
        case TexRef::PassOut:
        case TexRef::PassFeedback:
            if (b.ref.idx >= 0 && b.ref.idx < m_passes.size()) {
                b.linear = m_passes[b.ref.idx].cfg.filterLinear;
                b.wrap   = m_passes[b.ref.idx].cfg.wrap;
            }
            break;
        case TexRef::Lut:
            b.linear = true;                   // LUT 는 로드할 때 이미 설정했다
            break;
        default:
            b.linear = p.cfg.filterLinear;
            b.wrap   = p.cfg.wrap;
            break;
        }
        // mipmap_input 은 "이 패스의 입력(Source)에 밉맵을 만들어라" 는 뜻이다
        b.mipmap = p.cfg.mipmapInput && (b.ref.kind == TexRef::Source);
        b.sampler = samplerFor(b.linear, b.mipmap, b.wrap);
        m_maxUnitUsed = qMax(m_maxUnitUsed, b.unit);
        p.texBinds.append(b);
    }

    // 샘플러 유니폼(= 텍스처 유닛 번호)은 프로그램에 남는 상태다.
    //   매 프레임 다시 지정할 필요가 없어 여기서 한 번만 넣는다.
    f->glUseProgram(p.prog);
    for (const TexBind& b : p.texBinds) f->glUniform1i(b.loc, b.unit);
    f->glUseProgram(0);
}

void SlangChain::planUniforms(Pass& p, int passIndex) {
    auto build = [&](const SlangBlock& blk, QVector<UniPlan>& plan) {
        if (!blk.present) return;
        for (const SlangMember& m : blk.members) {
            UniPlan u;
            u.offset = m.offset;
            u.size   = m.size;

            if (m.name == QLatin1String("MVP"))               u.kind = UniKind::MVP;
            else if (m.name == QLatin1String("OutputSize"))   u.kind = UniKind::OutputSize;
            else if (m.name == QLatin1String("FinalViewportSize"))
                                                              u.kind = UniKind::FinalViewportSize;
            else if (m.name == QLatin1String("FrameCount"))    u.kind = UniKind::FrameCount;
            else if (m.name == QLatin1String("FrameDirection"))u.kind = UniKind::FrameDirection;
            else if (m.name == QLatin1String("Rotation"))      u.kind = UniKind::Rotation;
            else if (m.name == QLatin1String("TotalSubFrames"))u.kind = UniKind::TotalSubFrames;
            else if (m.name == QLatin1String("CurrentSubFrame"))
                                                              u.kind = UniKind::CurrentSubFrame;
            else {
                const QString base = sizeBaseName(m.name);
                const TexRef r = base.isEmpty() ? TexRef()
                                                : resolveTexture(base, passIndex);
                if (!base.isEmpty() && r.kind != TexRef::None) {
                    u.kind = UniKind::TexSize;
                    u.tex  = r;
                } else {
                    // #pragma parameter 값 — 매 프레임 이름으로 찾지 않도록
                    //   여기서 배열 인덱스로 바꿔 둔다.
                    u.kind = UniKind::Constant;
                    u.paramIdx = m_paramIndex.value(m.name, -1);
                }
            }
            plan.append(u);
        }
    };
    build(p.sh.ubo,  p.uboPlan);
    build(p.sh.push, p.pushPlan);

    // 매 프레임 값이 바뀌는 멤버가 있는 블록만 매 프레임 올린다.
    auto isDynamic = [](const QVector<UniPlan>& plan) {
        for (const UniPlan& u : plan)
            if (u.kind == UniKind::FrameCount || u.kind == UniKind::FrameDirection
             || u.kind == UniKind::CurrentSubFrame || u.kind == UniKind::TotalSubFrames)
                return true;
        return false;
    };
    p.uboDynamic  = isDynamic(p.uboPlan);
    p.pushDynamic = isDynamic(p.pushPlan);
}

// ─────────────────────────────────────────────────────────────
//  렌더 타깃
// ─────────────────────────────────────────────────────────────
bool SlangChain::ensureTarget(Pass& p, int slot, const QSize& size) {
    if (p.tex[slot] && p.size == size) return true;

    if (p.tex[slot]) { f->glDeleteTextures(1, &p.tex[slot]); p.tex[slot] = 0; }
    if (p.fbo[slot]) { f->glDeleteFramebuffers(1, &p.fbo[slot]); p.fbo[slot] = 0; }

    const int internal = p.cfg.floatFbo ? GL_RGBA16F
                       : p.cfg.srgbFbo  ? GL_SRGB8_ALPHA8
                                        : GL_RGBA8;

    f->glGenTextures(1, &p.tex[slot]);
    f->glBindTexture(GL_TEXTURE_2D, p.tex[slot]);
    f->glTexImage2D(GL_TEXTURE_2D, 0, internal, size.width(), size.height(), 0,
                    GL_RGBA, p.cfg.floatFbo ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    f->glGenFramebuffers(1, &p.fbo[slot]);
    f->glBindFramebuffer(GL_FRAMEBUFFER, p.fbo[slot]);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, p.tex[slot], 0);
    const unsigned st = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        addLog(QStringLiteral("렌더 타깃 생성 실패 (%1x%2)")
               .arg(size.width()).arg(size.height()));
        return false;
    }
    return true;
}

void SlangChain::allocTargets(const QSize& inputSize, const QSize& viewport) {
    QSize src = inputSize;
    for (int i = 0; i < m_passes.size(); ++i) {
        Pass& p = m_passes[i];

        QSize target;
        auto axis = [&](SlangScale t, float s, int srcLen, int viewLen) {
            switch (t) {
            case SlangScale::Viewport: return int(viewLen * s);
            case SlangScale::Absolute: return int(s);
            default:                   return int(srcLen * s);
            }
        };
        if (p.isLast && !p.cfg.hasScale) {
            // 마지막 패스는 크기 지정이 없으면 화면 전체로 나간다
            target = viewport;
        } else {
            target = QSize(axis(p.cfg.scaleTypeX, p.cfg.scaleX, src.width(),  viewport.width()),
                           axis(p.cfg.scaleTypeY, p.cfg.scaleY, src.height(), viewport.height()));
        }
        target.setWidth (qMax(1, target.width()));
        target.setHeight(qMax(1, target.height()));

        if (!p.isLast) {
            // 크기가 바뀌었으면 기존 타깃을 버리고 다시 만든다.
            //   ("slots" 는 Qt 매크로라 변수 이름으로 쓰면 안 된다)
            const int bufCount = p.pingPong ? 2 : 1;
            if (p.size != target)
                for (int s = 0; s < 2; ++s) {
                    if (p.tex[s]) { f->glDeleteTextures(1, &p.tex[s]);     p.tex[s] = 0; }
                    if (p.fbo[s]) { f->glDeleteFramebuffers(1, &p.fbo[s]); p.fbo[s] = 0; }
                }
            p.size = target;
            for (int s = 0; s < bufCount; ++s)
                if (!p.tex[s]) { ensureTarget(p, s, target); p.needsClear[s] = true; }
        } else {
            p.size = target;
        }
        src = target;
    }
    m_lastInput     = inputSize;
    m_lastViewport  = viewport;
    m_uniformsDirty = true;      // 크기가 바뀌었으니 *Size 유니폼을 다시 올린다
}

// ─────────────────────────────────────────────────────────────
//  히스토리
// ─────────────────────────────────────────────────────────────
void SlangChain::releaseHistory() {
    if (f && !m_history.isEmpty())
        f->glDeleteTextures(m_history.size(), m_history.constData());
    m_history.clear();
    m_historySize.clear();
    if (f && m_blitFboSrc) { f->glDeleteFramebuffers(1, &m_blitFboSrc); m_blitFboSrc = 0; }
    if (f && m_blitFboDst) { f->glDeleteFramebuffers(1, &m_blitFboDst); m_blitFboDst = 0; }
}

void SlangChain::pushHistory(unsigned inputTex, const QSize& inputSize) {
    if (m_historyDepth <= 0 || !p_glBlitFramebuffer) return;

    if (m_history.size() != m_historyDepth) {
        releaseHistory();
        m_history.resize(m_historyDepth);
        m_historySize.resize(m_historyDepth);
        f->glGenTextures(m_historyDepth, m_history.data());
        for (int i = 0; i < m_historyDepth; ++i) m_historySize[i] = QSize();
    }
    if (!m_blitFboSrc) f->glGenFramebuffers(1, &m_blitFboSrc);
    if (!m_blitFboDst) f->glGenFramebuffers(1, &m_blitFboDst);

    // 가장 오래된 것을 재활용해 맨 앞으로 옮긴다 (링버퍼 회전)
    const unsigned reuse = m_history.last();
    for (int i = m_history.size() - 1; i > 0; --i) {
        m_history[i]     = m_history[i - 1];
        m_historySize[i] = m_historySize[i - 1];
    }
    m_history[0] = reuse;

    if (m_historySize[0] != inputSize) {
        f->glBindTexture(GL_TEXTURE_2D, reuse);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, inputSize.width(),
                        inputSize.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_historySize[0] = inputSize;
    }

    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_blitFboSrc);
    f->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, inputTex, 0);
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blitFboDst);
    f->glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, m_history[0], 0);
    p_glBlitFramebuffer(0, 0, inputSize.width(), inputSize.height(),
                        0, 0, inputSize.width(), inputSize.height(),
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

// ─────────────────────────────────────────────────────────────
//  텍스처/크기 조회
// ─────────────────────────────────────────────────────────────
unsigned SlangChain::texIdOf(const TexRef& r, unsigned originalTex,
                             unsigned sourceTex) const {
    switch (r.kind) {
    case TexRef::Original: return originalTex;
    case TexRef::Source:   return sourceTex;
    case TexRef::History:
        return (r.idx - 1 >= 0 && r.idx - 1 < m_history.size())
                   ? m_history[r.idx - 1] : originalTex;
    case TexRef::PassOut:
        return (r.idx >= 0 && r.idx < m_passes.size())
                   ? m_passes[r.idx].tex[m_passes[r.idx].cur] : 0;
    case TexRef::PassFeedback:
        if (r.idx < 0 || r.idx >= m_passes.size()) return 0;
        // 핑퐁을 쓰면 반대쪽이 지난 프레임, 아니면 지난 프레임이 곧 지금 내용
        return m_passes[r.idx].pingPong ? m_passes[r.idx].tex[m_passes[r.idx].cur ^ 1]
                                        : m_passes[r.idx].tex[0];
    case TexRef::Lut:
        return (r.idx >= 0 && r.idx < m_luts.size()) ? m_luts[r.idx].tex : 0;
    default:
        return 0;
    }
}

QSize SlangChain::texSizeOf(const TexRef& r, const QSize& originalSize,
                            const QSize& sourceSize) const {
    switch (r.kind) {
    case TexRef::Original: return originalSize;
    case TexRef::Source:   return sourceSize;
    case TexRef::History:
        return (r.idx - 1 >= 0 && r.idx - 1 < m_historySize.size()
                && m_historySize[r.idx - 1].isValid())
                   ? m_historySize[r.idx - 1] : originalSize;
    case TexRef::PassOut:
    case TexRef::PassFeedback:
        return (r.idx >= 0 && r.idx < m_passes.size()) ? m_passes[r.idx].size : QSize(1, 1);
    case TexRef::Lut:
        return (r.idx >= 0 && r.idx < m_luts.size()) ? m_luts[r.idx].size : QSize(1, 1);
    default:
        return QSize(1, 1);
    }
}

// ─────────────────────────────────────────────────────────────
//  유니폼 블록 채우기
// ─────────────────────────────────────────────────────────────
void SlangChain::fillBlock(Pass& /*p*/, const QVector<UniPlan>& plan, QByteArray& data,
                           const QMatrix4x4& mvp, const QSize& outSize,
                           const QSize& viewport, const QSize& originalSize,
                           const QSize& sourceSize, int frameCountMod, int rotation) {
    if (data.isEmpty()) return;
    char* base = data.data();

    auto putVec4 = [&](quint32 off, const QSize& s) {
        if (off + 16 > quint32(data.size())) return;
        const float w = float(qMax(1, s.width())), h = float(qMax(1, s.height()));
        const float v[4] = { w, h, 1.0f / w, 1.0f / h };
        memcpy(base + off, v, sizeof(v));
    };
    auto putU32 = [&](quint32 off, quint32 v) {
        if (off + 4 > quint32(data.size())) return;
        memcpy(base + off, &v, 4);
    };
    auto putI32 = [&](quint32 off, qint32 v) {
        if (off + 4 > quint32(data.size())) return;
        memcpy(base + off, &v, 4);
    };
    auto putF32 = [&](quint32 off, float v) {
        if (off + 4 > quint32(data.size())) return;
        memcpy(base + off, &v, 4);
    };

    quint64 fc = m_frameCount;
    if (frameCountMod > 0) fc %= quint64(frameCountMod);

    for (const UniPlan& u : plan) {
        switch (u.kind) {
        case UniKind::MVP:
            if (u.offset + 64 <= quint32(data.size()))
                memcpy(base + u.offset, mvp.constData(), 64);
            break;
        case UniKind::OutputSize:        putVec4(u.offset, outSize);   break;
        case UniKind::FinalViewportSize: putVec4(u.offset, viewport);  break;
        case UniKind::FrameCount:        putU32(u.offset, quint32(fc)); break;
        case UniKind::FrameDirection:    putI32(u.offset, 1);          break;
        case UniKind::Rotation:          putU32(u.offset, quint32(rotation & 3)); break;
        case UniKind::TotalSubFrames:    putU32(u.offset, 1);          break;
        case UniKind::CurrentSubFrame:   putU32(u.offset, 1);          break;
        case UniKind::TexSize:
            putVec4(u.offset, texSizeOf(u.tex, originalSize, sourceSize));
            break;
        case UniKind::Constant:
            putF32(u.offset, (u.paramIdx >= 0 && u.paramIdx < m_paramStore.size())
                                 ? m_paramStore[u.paramIdx] : 0.0f);
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  렌더
// ─────────────────────────────────────────────────────────────
void SlangChain::render(unsigned inputTex, const QSize& inputSize,
                        const QRect& destRect, const QSize& viewport,
                        unsigned targetFbo, int rotation) {
    if (m_passes.isEmpty() || !f) return;
    if (inputSize.isEmpty() || viewport.isEmpty() || destRect.isEmpty()) return;

    // ★ 셰이더에게 알려 주는 "뷰포트" 는 창 전체가 아니라 실제로 그려 넣을
    //   사각형이어야 한다. 둘이 어긋나면, 셰이더가 창 비율에 맞춰 계산한 배치를
    //   다른 비율의 사각형에 욱여넣게 되어 결과가 통째로 찌그러진다.
    //   (Mega Bezel 을 화면 모드 Fit 으로 쓰면 베젤 프레임까지 눌려 보였다)
    //   이렇게 맞춰 두면 화면 모드가 그대로 셰이더에 반영된다:
    //     Fill → 창 전체를 셰이더가 채운다
    //     Fit  → 4:3 영역 안에서 셰이더가 제 비율대로 그린다
    //     1:1  → 원본 크기 영역 안에서 그린다
    const QSize shaderVp = destRect.size();

    if (inputSize != m_lastInput || shaderVp != m_lastViewport)
        allocTargets(inputSize, shaderVp);

    // ── 정점: 위치는 [0,1] 단위 사각형, UV 는 회전 반영 ────
    //   RetroArch slang 셰이더는 Position 이 [0,1] 이라고 전제하고 MVP 로 옮긴다.
    //   꼭짓점 순서: BL, BR, TL, TR
    float ubl,vbl,ubr,vbr,utl,vtl,utr,vtr;
    switch (rotation & 3) {
    case 1:  ubl=0; vbl=0;  ubr=0; vbr=1;  utl=1; vtl=0;  utr=1; vtr=1; break;
    case 2:  ubl=1; vbl=0;  ubr=0; vbr=0;  utl=1; vtl=1;  utr=0; vtr=1; break;
    case 3:  ubl=1; vbl=1;  ubr=1; vbr=0;  utl=0; vtl=1;  utr=0; vtr=0; break;
    default: ubl=0; vbl=1;  ubr=1; vbr=1;  utl=0; vtl=0;  utr=1; vtr=0; break;
    }
    const float vertsFirst[16] = {
        0.f,0.f, ubl,vbl,   1.f,0.f, ubr,vbr,
        0.f,1.f, utl,vtl,   1.f,1.f, utr,vtr,
    };
    static const float vertsMid[16] = {
        0.f,0.f, 0.f,0.f,   1.f,0.f, 1.f,0.f,
        0.f,1.f, 0.f,1.f,   1.f,1.f, 1.f,1.f,
    };

    // 모든 패스가 같은 변환을 쓴다: [0,1] → NDC 전체.
    //   마지막 패스는 glViewport 를 목적 사각형으로 잡아 위치를 맞춘다.
    //   (예전에는 MVP 로 밀어 넣었는데, 그러면 셰이더가 아는 OutputSize 와
    //    실제로 칠해지는 픽셀 수가 달라져 배치가 어긋났다)
    QMatrix4x4 fullMvp;
    fullMvp.translate(-1.0f, -1.0f);
    fullMvp.scale(2.0f, 2.0f);

    beginGpuTimer();

    if (p_glBindVertexArray) p_glBindVertexArray(m_vao);

    f->glDisable(GL_BLEND);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_CULL_FACE);

    unsigned srcTex  = inputTex;
    QSize    srcSize = inputSize;

    for (int i = 0; i < m_passes.size(); ++i) {
        Pass& p = m_passes[i];
        const int wIdx = p.pingPong ? (p.cur ^ 1) : 0;

        if (p.isLast) {
            f->glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
            // GL 뷰포트는 왼쪽 아래가 원점이라 y 를 뒤집어 준다
            f->glViewport(destRect.x(),
                          viewport.height() - (destRect.y() + destRect.height()),
                          destRect.width(), destRect.height());
        } else {
            if (!p.fbo[wIdx]) { addLog(QStringLiteral("패스 %1 렌더타깃 없음").arg(i)); break; }
            f->glBindFramebuffer(GL_FRAMEBUFFER, p.fbo[wIdx]);
            f->glViewport(0, 0, p.size.width(), p.size.height());
            // 패스는 타깃 전체를 덮으므로 매 프레임 지울 필요가 없다.
            //   새로 만든 버퍼만 한 번 지워 초기 쓰레기 값을 없앤다.
            if (p.needsClear[wIdx]) {
                f->glClearColor(0.f, 0.f, 0.f, 1.f);
                f->glClear(GL_COLOR_BUFFER_BIT);
                p.needsClear[wIdx] = false;
            }
        }

        f->glUseProgram(p.prog);

        // ── 텍스처 ────────────────────────────────────────
        //   필터/랩은 샘플러 오브젝트에 굳혀 뒀으므로 바인딩만 하면 된다.
        //   (예전에는 텍스처마다 glTexParameteri 를 4번씩 불러서, 패스 40개
        //    프리셋이면 프레임당 1000번이 넘었다)
        for (const TexBind& b : p.texBinds) {
            const unsigned id = texIdOf(b.ref, inputTex, srcTex);
            f->glActiveTexture(GL_TEXTURE0 + b.unit);
            f->glBindTexture(GL_TEXTURE_2D, id);
            if (id && b.mipmap) f->glGenerateMipmap(GL_TEXTURE_2D);
            if (p_glBindSampler) {
                p_glBindSampler(b.unit, b.sampler);
            } else if (id) {
                // 샘플러 오브젝트를 못 쓰는 드라이버용 폴백
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                   b.linear ? GL_LINEAR : GL_NEAREST);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                   b.mipmap ? GL_LINEAR_MIPMAP_LINEAR
                                            : (b.linear ? GL_LINEAR : GL_NEAREST));
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapEnum(b.wrap));
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapEnum(b.wrap));
            }
        }

        // ── 유니폼 블록 ───────────────────────────────────
        const QSize outSize = p.isLast ? shaderVp : p.size;
        const QMatrix4x4& mvp = fullMvp;

        // 매 프레임 바뀌는 값이 없는 블록은 다시 올리지 않는다.
        //   Mega Bezel 의 UBO 에는 파라미터가 900개(약 4KB) 들어 있는데,
        //   그게 패스마다 매 프레임 올라가고 있었다.
        const bool refresh = m_uniformsDirty || !p.uploaded;
        if (p.uboIndex >= 0) {
            if (p.uboDynamic || refresh) {
                fillBlock(p, p.uboPlan, p.uboData, mvp, outSize, shaderVp,
                          inputSize, srcSize, p.cfg.frameCountMod, rotation);
                f->glBindBuffer(GL_UNIFORM_BUFFER, p.uboBuf);
                f->glBufferData(GL_UNIFORM_BUFFER, p.uboData.size(),
                                p.uboData.constData(), GL_STREAM_DRAW);
            }
            p_glBindBufferBase(GL_UNIFORM_BUFFER, kUboBinding, p.uboBuf);
        }
        if (p.pushIndex >= 0) {
            if (p.pushDynamic || refresh) {
                fillBlock(p, p.pushPlan, p.pushData, mvp, outSize, shaderVp,
                          inputSize, srcSize, p.cfg.frameCountMod, rotation);
                f->glBindBuffer(GL_UNIFORM_BUFFER, p.pushBuf);
                f->glBufferData(GL_UNIFORM_BUFFER, p.pushData.size(),
                                p.pushData.constData(), GL_STREAM_DRAW);
            }
            p_glBindBufferBase(GL_UNIFORM_BUFFER, kPushBinding, p.pushBuf);
        }
        p.uploaded = true;

        // ── 정점 ──────────────────────────────────────────
        f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        f->glBufferData(GL_ARRAY_BUFFER, sizeof(vertsFirst),
                        (i == 0) ? vertsFirst : vertsMid, GL_DYNAMIC_DRAW);
        constexpr int stride = 4 * sizeof(float);
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
        f->glEnableVertexAttribArray(1);
        f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                                 reinterpret_cast<void*>(2 * sizeof(float)));

        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        f->glDisableVertexAttribArray(0);
        f->glDisableVertexAttribArray(1);

        if (!p.isLast) {
            p.cur   = wIdx;
            srcTex  = p.tex[wIdx];
            srcSize = p.size;
        }
    }

    // 샘플러를 물려 둔 채 나가면 기본 셰이더나 QPainter 오버레이가 영향을 받는다
    if (p_glBindSampler)
        for (int u = 0; u <= m_maxUnitUsed; ++u) p_glBindSampler(u, 0);

    f->glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    f->glUseProgram(0);
    if (p_glBindVertexArray) p_glBindVertexArray(0);
    f->glActiveTexture(GL_TEXTURE0);
    m_uniformsDirty = false;

    pushHistory(inputTex, inputSize);
    endGpuTimer();
    ++m_frameCount;
}

// ─────────────────────────────────────────────────────────────
void SlangChain::setParameter(const QString& name, float value) {
    const auto it = m_paramIndex.constFind(name);
    if (it == m_paramIndex.constEnd()) return;
    m_paramStore[it.value()] = value;
    m_uniformsDirty = true;      // 정적 블록을 다시 올려야 한다
}

float SlangChain::parameterValue(const QString& name) const {
    const auto it = m_paramIndex.constFind(name);
    return (it != m_paramIndex.constEnd()) ? m_paramStore[it.value()] : 0.0f;
}

void SlangChain::release() {
    if (f) {
        for (Pass& p : m_passes) {
            if (p.prog) f->glDeleteProgram(p.prog);
            for (int s = 0; s < 2; ++s) {
                if (p.tex[s]) f->glDeleteTextures(1, &p.tex[s]);
                if (p.fbo[s]) f->glDeleteFramebuffers(1, &p.fbo[s]);
            }
            if (p.uboBuf)  f->glDeleteBuffers(1, &p.uboBuf);
            if (p.pushBuf) f->glDeleteBuffers(1, &p.pushBuf);
        }
        for (Lut& l : m_luts) if (l.tex) f->glDeleteTextures(1, &l.tex);
        if (p_glDeleteSamplers)
            for (unsigned sm : m_samplers) p_glDeleteSamplers(1, &sm);
        if (p_glDeleteQueries && m_queries[0]) p_glDeleteQueries(kQueryRing, m_queries);
        if (m_vbo) f->glDeleteBuffers(1, &m_vbo);
        releaseHistory();
        if (m_vao && p_glDeleteVertexArrays) p_glDeleteVertexArrays(1, &m_vao);
    }
    m_passes.clear();
    m_luts.clear();
    m_aliasToPass.clear();
    m_samplers.clear();
    m_paramStore.clear();
    m_paramIndex.clear();
    m_paramDecls.clear();
    m_uniformsDirty = true;
    m_maxUnitUsed = 0;
    for (int i = 0; i < kQueryRing; ++i) { m_queries[i] = 0; m_queryBusy[i] = false; }
    m_queryHead = 0;
    m_gpuMs = m_gpuAccum = 0.0;
    m_gpuSamples = m_warnCooldown = 0;
    m_reportedOnce = false;
    m_perfWarning.clear();
    m_historyDepth = 0;
    m_vao = m_vbo = 0;
    m_frameCount = 0;
    m_lastInput = m_lastViewport = QSize();
}
