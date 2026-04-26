// GameCanvas.cpp — OpenGL 게임 렌더링 (Phase 3: CRT + 스케일 모드 완전 구현)

#include "GameCanvas.h"
#include "EmulatorState.h"
#include "AppSettings.h"

#include <QOpenGLShader>
#include <QFile>
#include <QFileInfo>
#include <QVector4D>
#include <QRegularExpression>
#include <QPainter>
#include <QFont>
#include <QDebug>
#include <cstring>
#include <algorithm>
#include <cmath>

// ════════════════════════════════════════════════════════════
//  GLSL 1.20 쉐이더 — 호환성 프로파일 (Windows/SteamDeck 공통)
// ════════════════════════════════════════════════════════════
const char* GameCanvas::defaultVertSrc() {
    return
        "#version 120\n"
        "attribute vec2 aPos;\n"
        "attribute vec2 aUV;\n"
        "varying   vec2 vUV;\n"
        "void main() {\n"
        "    vUV         = aUV;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";
}

const char* GameCanvas::defaultFragSrc() {
    return
        "#version 120\n"
        "uniform sampler2D uTex;\n"
        "uniform bool      uCrtMode;\n"
        "uniform float     uCrtIntensity;\n"
        "uniform float     uTexH;\n"        // 텍스처 픽셀 높이
        "varying vec2      vUV;\n"
        "\n"
        "void main() {\n"
        "    vec4 col = texture2D(uTex, vUV);\n"
        "\n"
        "    if (uCrtMode) {\n"
        "        // ── 스캔라인 ────────────────────────────\n"
        "        float scanline = mod(floor(vUV.y * uTexH), 2.0);\n"
        "        float sl = mix(1.0 - uCrtIntensity * 0.75, 1.0, scanline);\n"
        "        col.rgb *= sl;\n"
        "\n"
        "        // ── RGB 마스크 (픽셀 격자 느낌) ───────────\n"
        "        float mask = mod(floor(vUV.x * uTexH * 1.333), 3.0);\n"
        "        vec3 rgb = vec3(\n"
        "            mask < 1.0 ? 1.0 : 0.85,\n"
        "            mask < 2.0 && mask >= 1.0 ? 1.0 : 0.85,\n"
        "            mask >= 2.0 ? 1.0 : 0.85\n"
        "        );\n"
        "        col.rgb *= mix(vec3(1.0), rgb, uCrtIntensity * 0.25);\n"
        "\n"
        "        // ── 비네팅 ──────────────────────────────\n"
        "        vec2  uv2 = vUV * 2.0 - 1.0;\n"
        "        float vig = 1.0 - dot(uv2 * 0.45, uv2 * 0.45);\n"
        "        vig = clamp(vig, 0.0, 1.0);\n"
        "        col.rgb *= mix(1.0, vig, uCrtIntensity * 0.35);\n"
        "\n"
        "        // ── 미세 블룸 (밝은 영역 번짐) ─────────────\n"
        "        float lum = dot(col.rgb, vec3(0.299, 0.587, 0.114));\n"
        "        col.rgb += col.rgb * lum * uCrtIntensity * 0.12;\n"
        "        col.rgb = clamp(col.rgb, 0.0, 1.0);\n"
        "    }\n"
        "\n"
        "    gl_FragColor = col;\n"
        "}\n";
}

// ── 생성자/소멸자 ─────────────────────────────────────────────
GameCanvas::GameCanvas(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background:#000000;");
}

GameCanvas::~GameCanvas() {
    makeCurrent();
    if (m_texId) { glDeleteTextures(1, &m_texId); m_texId = 0; }
    if (m_vbo)   { glDeleteBuffers(1, &m_vbo);    m_vbo   = 0; }
    doneCurrent();
}

// ── 옵션 세터 ────────────────────────────────────────────────
void GameCanvas::setScaleMode(const QString& mode) {
    m_scaleMode = mode;
    if (m_glReady) { makeCurrent(); updateVertices(); doneCurrent(); }
    update();
}
void GameCanvas::setSmooth(bool smooth) {
    m_smooth = smooth;
    if (m_glReady && m_texId) {
        makeCurrent();
        glBindTexture(GL_TEXTURE_2D, m_texId);
        GLint f = smooth ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        doneCurrent();
    }
    update();
}
void GameCanvas::setCrtMode(bool on, double intensity) {
    m_crtMode      = on;
    m_crtIntensity = intensity;
    update();
}
void GameCanvas::setRecording(bool on) {
    m_recording = on;
    update();
}
bool GameCanvas::setShaderPath(const QString& path) {
    if (!m_glReady) {
        // initializeGL() 전 — 보류
        m_pendingShaderPath = path;
        return true; // 보류 성공으로 간주
    }
    makeCurrent();
    bool ok = false;
    if (path.isEmpty()) {
        // 외부 셰이더 해제 → 기본 CRT 셰이더 복구
        m_prog.removeAllShaders();
        m_shaderReady    = false;
        m_externalShader = false;
        buildDefaultShader();
        emit glLogMessage("외부 셰이더 해제 — CRT 기본 셰이더 복구");
        ok = true;
    } else {
        if (parseAndLoadGlsl(path)) {
            m_externalShader = true;
            emit glLogMessage("✔ 외부 셰이더 로드: " + QFileInfo(path).fileName());
            ok = true;
        } else {
            // 실패 → 기본 셰이더 유지
            m_prog.removeAllShaders();
            m_shaderReady    = false;
            m_externalShader = false;
            buildDefaultShader();
            emit glLogMessage("✖ 셰이더 로드 실패 — CRT 기본 셰이더 유지: " + QFileInfo(path).fileName());
        }
    }
    doneCurrent();
    update(); // 다음 프레임에 새 셰이더 즉시 반영
    return ok;
}

bool GameCanvas::loadShader(const QString& vertPath, const QString& fragPath) {
    QFile vf(vertPath), ff(fragPath);
    if (!vf.open(QIODevice::ReadOnly | QIODevice::Text) ||
        !ff.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QString vs = vf.readAll(), fs = ff.readAll();
    makeCurrent();
    m_prog.removeAllShaders();
    m_shaderReady    = false;
    m_externalShader = false;
    bool ok = m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex,   vs)
           && m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fs)
           && m_prog.link();
    if (ok) { m_shaderReady = true; m_externalShader = true; }
    doneCurrent();
    return ok;
}

// ── RetroArch .glsl 파싱 및 컴파일 ───────────────────────────
bool GameCanvas::parseAndLoadGlsl(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit glLogMessage("셰이더 파일 열기 실패: " + path);
        return false;
    }
    QString src = QString::fromUtf8(f.readAll());

    // ── #pragma parameter 기본값 추출 ───────────────────
    // 형식: #pragma parameter NAME "Label" default_val min max step
    // GLSL uniform 기본값은 항상 0이므로, RetroArch 기본값을 직접 파싱해서 설정해야 함
    // (예: GAMMA_INPUT=2.0, MASK_INTENSITY=1.0 → 이것이 0이면 화면이 검정이 될 수 있음)
    m_pragmaDefaults.clear();
    {
        static const QRegularExpression rePragma(
            R"(#pragma\s+parameter\s+(\w+)\s+"[^"]*"\s+([-\d.eE+]+))");
        auto it = rePragma.globalMatch(src);
        while (it.hasNext()) {
            auto m = it.next();
            m_pragmaDefaults[m.captured(1)] = m.captured(2).toFloat();
        }
    }

    QString vertSrc, fragSrc;

    // ── #version 정규화 헬퍼 ────────────────────────────
    // COMPAT_* 매크로를 attribute/varying/texture2D(GLSL 1.20 스타일)로 전개했으므로
    // 반드시 #version 120 으로 고정.
    //   - #version 130+ 로 컴파일하면 #if __VERSION__ >= 130 분기가 활성화되어
    //     in/out 키워드(혹은 비어있는 define 블록)와 attribute/varying이 충돌 가능.
    //   - #version 120 이면 해당 #if 분기가 비활성화 → attribute/varying이 표준 키워드.
    static const QRegularExpression reVer(R"([ \t]*#version\b[^\n]*\n?)");
    const QString versionLine = "#version 120";
    auto normalizeVersion = [&](QString& s) {
        s.remove(reVer);          // 기존 #version 전부 제거
        s = s.trimmed();
        s.prepend(versionLine + "\n");  // 맨 앞에 하나만
    };

    // ── 포맷 감지 & 분리 ─────────────────────────────────
    // RetroArch GLSL: #pragma stage / #if defined 이전에 공용 선언부(uniform, varying 등) 존재
    // → 분리 시 공용부를 양쪽에 포함해야 TEX0, filterWidth 등이 정의됨

    if (src.contains("#pragma stage vertex")) {
        // RetroArch #pragma stage 포맷
        int vs = src.indexOf("#pragma stage vertex");
        int fs = src.indexOf("#pragma stage fragment");
        if (vs < 0 || fs < 0) {
            emit glLogMessage("셰이더: #pragma stage 구조 오류");
            return false;
        }
        // 공용 선언부: #pragma stage vertex 이전 내용 (양쪽에 공유)
        QString commonSrc = (vs > 0) ? src.mid(0, vs) : QString();
        vertSrc = commonSrc + src.mid(vs, fs - vs);
        static const QRegularExpression rePsV(R"(#pragma\s+stage\s+vertex\b[^\n]*\n?)");
        vertSrc.remove(rePsV);
        fragSrc = commonSrc + src.mid(fs);
        static const QRegularExpression rePsF(R"(#pragma\s+stage\s+fragment\b[^\n]*\n?)");
        fragSrc.remove(rePsF);
    } else if (src.contains("#if defined(VERTEX)")) {
        // libretro #if defined 포맷
        int vs = src.indexOf("#if defined(VERTEX)");
        int fs = src.indexOf("#elif defined(FRAGMENT)");
        int ef = src.lastIndexOf("#endif");
        if (vs < 0 || fs < 0) {
            emit glLogMessage("셰이더: #if defined 구조 오류");
            return false;
        }
        // 공용 선언부: #if defined(VERTEX) 이전 내용 (양쪽에 공유)
        QString commonSrc = (vs > 0) ? src.mid(0, vs) : QString();
        vertSrc = commonSrc + src.mid(vs + 19, fs - vs - 19);
        fragSrc = commonSrc + src.mid(fs + 23, ef > fs ? ef - fs - 23 : -1);
    } else {
        // fragment-only → passthrough vertex 사용
        vertSrc = defaultVertSrc();
        fragSrc = src;
    }

    // ── RetroArch COMPAT_* 매크로 전처리 ─────────────────────
    // 상당수 RetroArch GLSL이 #if __VERSION__ >= 130 / #else 블록으로 COMPAT_* 매크로를 정의.
    // 우리 정규식은 드라이버 전처리 전 텍스트에서 실행되므로 직접 전개 필요.
    //   COMPAT_ATTRIBUTE → attribute
    //   COMPAT_VARYING   → varying
    //   COMPAT_TEXTURE   → texture2D
    //   COMPAT_PRECISION → (empty, desktop에서는 precision 한정자 불필요)
    {
        // COMPAT_* 의 #define 라인 제거 (전개 후 재정의 충돌 방지)
        static const QRegularExpression reCompatDef(
            R"([ \t]*#define\s+COMPAT_(?:ATTRIBUTE|VARYING|TEXTURE|PRECISION)\b[^\n]*\n?)");
        // 매크로 확장 (GLSL 1.20 기준값으로 고정)
        static const QRegularExpression reCA(R"(\bCOMPAT_ATTRIBUTE\b)");
        static const QRegularExpression reCV(R"(\bCOMPAT_VARYING\b)");
        static const QRegularExpression reCT(R"(\bCOMPAT_TEXTURE\b)");
        static const QRegularExpression reCP(R"(\bCOMPAT_PRECISION\b)");

        // vertex: COMPAT_ATTRIBUTE → attribute, 나머지도 전개
        vertSrc.remove(reCompatDef);
        vertSrc.replace(reCA, "attribute");
        vertSrc.replace(reCV, "varying");
        vertSrc.replace(reCT, "texture2D");
        vertSrc.remove(reCP);

        // fragment: attribute 없음, 나머지 전개
        fragSrc.remove(reCompatDef);
        fragSrc.replace(reCV, "varying");
        fragSrc.replace(reCT, "texture2D");
        fragSrc.remove(reCP);
    }

    // Fragment 셰이더에 attribute 선언은 불가 — 공용부에 있더라도 제거
    // (COMPAT_ATTRIBUTE는 fragSrc에서 확장하지 않으므로 원문 그대로 남을 수 있음)
    static const QRegularExpression reCompatAttrLine(R"([ \t]*COMPAT_ATTRIBUTE\b[^\n]*\n?)");
    static const QRegularExpression reAttrLine(R"([ \t]*attribute\b[^\n]*\n?)");
    fragSrc.remove(reCompatAttrLine);
    fragSrc.remove(reAttrLine);

    // ── RetroArch 속성명 → 내부 속성명 정규화 ──────────────
    // Vertex shader

    // [1] 속성 선언: VertexCoord → aPos, TexCoord → aUV
    //     GLSL 1.20: attribute  (COMPAT_ATTRIBUTE → attribute 전개 후)
    //     GLSL 1.30+: in  (COMPAT_ATTRIBUTE → in이었던 경우도 포함)
    static const QRegularExpression reVAVec4(
        R"(\b(?:attribute|in)\s+vec[24]\s+VertexCoord\b)");
    static const QRegularExpression reVAVec2(
        R"(\b(?:attribute|in)\s+vec[24]\s+TexCoord\b)");
    vertSrc.replace(reVAVec4, "attribute vec2 aPos");
    vertSrc.replace(reVAVec2, "attribute vec2 aUV");

    // [2] MVPMatrix uniform 선언 제거 (mat4(1.0) 인라인으로 대체 — 선언 유지 시 컴파일 오류 방지)
    static const QRegularExpression reMVPDecl(R"(\buniform\s+mat4\s+MVPMatrix\b[^;]*;[ \t]*)");
    vertSrc.remove(reMVPDecl);

    // [3] "MVPMatrix * VertexCoord" → NDC 직접 변환
    static const QRegularExpression reMVP(R"(\bMVPMatrix\s*\*\s*VertexCoord\b)");
    vertSrc.replace(reMVP, "vec4(aPos, 0.0, 1.0)");

    // [4] 남은 VertexCoord 사용처 → vec4(aPos, 0.0, 1.0)
    static const QRegularExpression reVC(R"(\bVertexCoord\b)");
    vertSrc.replace(reVC, "vec4(aPos, 0.0, 1.0)");

    // [5] 남은 MVPMatrix 사용처 → mat4(1.0)
    static const QRegularExpression reMVPRef(R"(\bMVPMatrix\b)");
    vertSrc.replace(reMVPRef, "mat4(1.0)");

    // [6] TexCoord 속성 사용처 → aUV  (※ TexCoord23 등 다른 이름은 word-boundary로 보호됨)
    static const QRegularExpression reTC(R"(\bTexCoord\b)");
    vertSrc.replace(reTC, "aUV");

    // Fragment shader — 텍스처 uniform 이름 정규화 (Texture / Source 모두 uTex 로)
    static const QRegularExpression reFTex(R"(\buniform\s+sampler2D\s+(?:Texture|Source)\b)");
    static const QRegularExpression reFTexUse(R"(\btexture2D\s*\(\s*(?:Texture|Source)\b)");
    static const QRegularExpression reFTexUse2(R"(\b(?:Texture|Source)\b)");
    fragSrc.replace(reFTex,     "uniform sampler2D uTex");
    fragSrc.replace(reFTexUse,  "texture2D(uTex");
    fragSrc.replace(reFTexUse2, "uTex");

    // ── Color 어트리뷰트 → 흰색 고정 ─────────────────────────────
    // "COL0 = Color;" 패턴: Color 어트리뷰트는 VBO에 제공되지 않으므로 기본값 (0,0,0,1)
    // → COL0.rgb = (0,0,0) → 일부 셰이더에서 gl_FragColor = texColor * COL0.rgb → 검정
    // "COL0 = Color;" → "COL0 = vec4(1.0);" 로 교체하여 어트리뷰트 의존성 제거
    {
        static const QRegularExpression reColAssign(
            R"(\bCOL0\s*=\s*Color\s*;)");
        vertSrc.replace(reColAssign, "COL0 = vec4(1.0);");
    }

    // varying 이름 (TEX0, TexCoord23 등) — 보존 (양쪽 공용부에서 동일 선언됨)

    // ── #version 정규화 (중복 제거 후 맨 앞에 하나만) ──────
    normalizeVersion(vertSrc);
    normalizeVersion(fragSrc);

    // ── 디버그: pragma parameter 기본값 목록 ──────────────────────
    if (!m_pragmaDefaults.isEmpty()) {
        QString plog = "── #pragma parameter 기본값 ──────\n";
        for (auto it = m_pragmaDefaults.constBegin(); it != m_pragmaDefaults.constEnd(); ++it)
            plog += QString("  %1 = %2\n").arg(it.key()).arg((double)it.value());
        emit glLogMessage(plog);
    }

    // ── 디버그: 처리된 셰이더 소스 로그 출력 ─────────────────────
    // 앞부분(주석+pragma)을 건너뛰고 실제 GLSL 코드(마지막 3000자)를 표시
    emit glLogMessage(
        QString("── VERT 소스 (%1자, 마지막 3000자) ──────\n").arg(vertSrc.size()) +
        vertSrc.right(3000));
    emit glLogMessage(
        QString("── FRAG 소스 (%1자, 마지막 3000자) ──────\n").arg(fragSrc.size()) +
        fragSrc.right(3000));

    // ── 컴파일 & 링크 ────────────────────────────────────
    m_prog.removeAllShaders();
    m_shaderReady = false;

    if (!m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc)) {
        emit glLogMessage("외부 셰이더 Vert 오류:\n" + m_prog.log());
        return false;
    }
    if (!m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc)) {
        emit glLogMessage("외부 셰이더 Frag 오류:\n" + m_prog.log());
        return false;
    }
    if (!m_prog.link()) {
        emit glLogMessage("외부 셰이더 링크 오류:\n" + m_prog.log());
        return false;
    }
    m_shaderReady = true;
    return true;
}

// ── OpenGL 초기화 ─────────────────────────────────────────────
void GameCanvas::initializeGL() {
    initializeOpenGLFunctions();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // ── 텍스처 ─────────────────────────────────────────────
    glGenTextures(1, &m_texId);
    glBindTexture(GL_TEXTURE_2D, m_texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // ── VBO ─────────────────────────────────────────────────
    // 초기 데이터: 전체화면 쿼드 (updateVertices 에서 교체됨)
    static const float verts[] = {
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 0.0f,
    };
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

    buildDefaultShader();
    m_glReady = true;

    // initializeGL() 전에 세팅된 외부 셰이더 처리
    if (!m_pendingShaderPath.isEmpty()) {
        QString p = m_pendingShaderPath;
        m_pendingShaderPath.clear();
        setShaderPath(p);
    }
}

void GameCanvas::resizeGL(int w, int h) {
    // w, h 는 물리 픽셀(device pixels) — HiDPI 대응을 위해 반드시 이 값 사용
    glViewport(0, 0, w, h);
    updateVertices();
}

// ── 스케일 계산 ───────────────────────────────────────────────
QRectF GameCanvas::calcDestRect(int fw, int fh, int vw, int vh) const {
    if (m_scaleMode == "1:1")
        return { (vw - fw) * 0.5, (vh - fh) * 0.5, (double)fw, (double)fh };

    if (m_scaleMode == "Fill")
        // Fill = 화면 전체를 채우도록 비균등 스트레치 (잘림 없음, 비율 변형 허용)
        return { 0, 0, (double)vw, (double)vh };

    // "Fit" (기본) — 종횡비 유지, 레터박스
    double scale = std::min((double)vw / fw, (double)vh / fh);
    double w = fw * scale, h = fh * scale;
    return { (vw - w) * 0.5, (vh - h) * 0.5, w, h };
}

// ── VBO 꼭짓점 갱신 ──────────────────────────────────────────
void GameCanvas::updateVertices() {
    int fw = static_cast<int>(gState.videoWidth);
    int fh = static_cast<int>(gState.videoHeight);
    if (fw <= 0 || fh <= 0) return;

    int vw = width(), vh = height();
    if (vw <= 0 || vh <= 0) return;

    QRectF dr = calcDestRect(fw, fh, vw, vh);

    // NDC 변환: OpenGL Y축은 위가 +1
    float x0 = static_cast<float>(dr.x() / vw * 2.0 - 1.0);
    float x1 = static_cast<float>((dr.x() + dr.width())  / vw * 2.0 - 1.0);
    float y0 = static_cast<float>(1.0 - dr.y() / vh * 2.0);         // 상단
    float y1 = static_cast<float>(1.0 - (dr.y() + dr.height()) / vh * 2.0);  // 하단

    float verts[] = {
        x0, y1,  0.0f, 1.0f,   // 좌하
        x1, y1,  1.0f, 1.0f,   // 우하
        x0, y0,  0.0f, 0.0f,   // 좌상
        x1, y0,  1.0f, 0.0f,   // 우상
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
}

// ── 프레임 업로드 ────────────────────────────────────────────
void GameCanvas::uploadFrame() {
    if (!gState.frameReady.load(std::memory_order_acquire)) return;

    int w = static_cast<int>(gState.videoWidth);
    int h = static_cast<int>(gState.videoHeight);
    if (w <= 0 || h <= 0 || gState.videoBuffer.isEmpty()) return;

    // 크기가 변했으면 VBO 재계산
    static int lastW = 0, lastH = 0;
    if (w != lastW || h != lastH) {
        lastW = w; lastH = h;
        updateVertices();
    }

    glBindTexture(GL_TEXTURE_2D, m_texId);

    // 필터
    GLint filter = m_smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

    size_t pitch = gState.videoPitch;
    if (gState.pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
        int rowLen = static_cast<int>(pitch / 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLen != w ? rowLen : 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE,
                     gState.videoBuffer.constData());
    } else {
        // RGB565
        int rowLen = static_cast<int>(pitch / 2);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLen != w ? rowLen : 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                     GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                     gState.videoBuffer.constData());
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    gState.frameReady.store(false, std::memory_order_release);
}

// ── 렌더링 ───────────────────────────────────────────────────
void GameCanvas::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_glReady || !m_shaderReady || !gState.gameLoaded) return;

    uploadFrame();

    m_prog.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texId);

    if (m_externalShader) {
        // ── 텍스처 샘플러 ───────────────────────────────────────
        m_prog.setUniformValue("uTex",    0);   // 내부 이름 (우리가 rename한 것)
        m_prog.setUniformValue("Texture", 0);   // RetroArch 구형 표준
        m_prog.setUniformValue("Source",  0);   // RetroArch 신형 표준

        // ── 프레임 카운터 ────────────────────────────────────────
        m_prog.setUniformValue("FrameCount",     (int)gState.frameCount);
        m_prog.setUniformValue("FrameDirection", 1);   // 1=정방향 재생

        // ── 크기 유니폼 — vec2/vec4 이중 설정 ───────────────────
        // 구형 RetroArch 셰이더: uniform vec2 OutputSize  (x=w, y=h)
        // 신형 RetroArch 셰이더: uniform vec4 OutputSize  (x=w, y=h, z=1/w, w=1/h)
        //
        // glUniform4f를 vec2 유니폼에 → GL_INVALID_OPERATION → 유니폼이 (0,0)으로 유지됨
        //   → 셰이더 내부 "1.0 / OutputSize.x" = inf → texture2D(uTex, inf) → 단색 화면
        //
        // 해결: glUniform4f(vec4 셰이더용) 먼저, glUniform2f(vec2 셰이더용) 뒤에 호출
        //   - vec4 유니폼: glUniform4f 성공, glUniform2f → GL_INVALID_OPERATION → 무시 ✓
        //   - vec2 유니폼: glUniform4f → GL_INVALID_OPERATION → 무시, glUniform2f 성공 ✓
        float tw = gState.videoWidth  > 0 ? (float)gState.videoWidth  : 1.0f;
        float th = gState.videoHeight > 0 ? (float)gState.videoHeight : 1.0f;
        float vw = (float)width(),  vh = (float)height();
        float inv_tw = tw > 0.f ? 1.f / tw : 0.f;
        float inv_th = th > 0.f ? 1.f / th : 0.f;
        float inv_vw = vw > 0.f ? 1.f / vw : 0.f;
        float inv_vh = vh > 0.f ? 1.f / vh : 0.f;

        auto setSize = [&](const char* name,
                           float w, float h, float iw, float ih) {
            GLint loc = m_prog.uniformLocation(name);
            if (loc < 0) return;
            // vec4 먼저 — vec4 유니폼이면 성공, vec2이면 INVALID_OPERATION(무시)
            glUniform4f(loc, w, h, iw, ih);
            // vec2 뒤에 — vec2 유니폼이면 성공(앞의 실패를 덮음), vec4이면 INVALID_OPERATION(무시)
            glUniform2f(loc, w, h);
        };

        setSize("OutputSize",  vw, vh, inv_vw, inv_vh);
        setSize("TextureSize", tw, th, inv_tw, inv_th);
        setSize("InputSize",   tw, th, inv_tw, inv_th);
        setSize("SourceSize",  tw, th, inv_tw, inv_th);  // RetroArch 신형 별칭

        // ── Color 어트리뷰트 기본값 → 흰색(1,1,1,1) ─────────────
        // "attribute vec4 Color"는 VBO에 제공되지 않아 기본값 (0,0,0,1) 적용됨
        // 일부 셰이더: COL0 = Color; → gl_FragColor = texColor * COL0.rgb → 검정 화면
        // 해결: glVertexAttrib4f 로 비활성 어레이의 상수값을 흰색으로 설정
        {
            GLint colorLoc = m_prog.attributeLocation("Color");
            if (colorLoc >= 0)
                glVertexAttrib4f(colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
        }

        // ── #pragma parameter 기본값 ────────────────────────────
        // GLSL uniform 기본값은 0 → RetroArch 파라미터 기본값을 직접 주입
        // (GAMMA_INPUT=0 → pow(x,0)=1.0 → 흰 화면; InputGamma=0 → 1/0=inf → 단색)
        for (auto it = m_pragmaDefaults.constBegin(); it != m_pragmaDefaults.constEnd(); ++it)
            m_prog.setUniformValue(qPrintable(it.key()), it.value());
    } else {
        m_prog.setUniformValue("uTex",          0);
        m_prog.setUniformValue("uCrtMode",      m_crtMode);
        m_prog.setUniformValue("uCrtIntensity", (float)m_crtIntensity);
        m_prog.setUniformValue("uTexH",         (float)gState.videoHeight);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // attribute 이름: 내부명 우선, RetroArch 이름 폴백
    GLint posLoc = m_prog.attributeLocation("aPos");
    if (posLoc < 0) posLoc = m_prog.attributeLocation("VertexCoord");
    if (posLoc < 0) posLoc = m_prog.attributeLocation("position");
    GLint uvLoc  = m_prog.attributeLocation("aUV");
    if (uvLoc  < 0) uvLoc  = m_prog.attributeLocation("TexCoord");
    if (uvLoc  < 0) uvLoc  = m_prog.attributeLocation("texcoord");
    constexpr int stride = 4 * sizeof(float);

    if (posLoc >= 0) {
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    }
    if (uvLoc >= 0) {
        glEnableVertexAttribArray(uvLoc);
        glVertexAttribPointer(uvLoc,  2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(2 * sizeof(float)));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (posLoc >= 0) glDisableVertexAttribArray(posLoc);
    if (uvLoc  >= 0) glDisableVertexAttribArray(uvLoc);

    m_prog.release();

    // ── 녹화 오버레이 ─────────────────────────────────────
    if (m_recording) {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        // 반투명 배경
        p.fillRect(8, 8, 72, 22, QColor(0, 0, 0, 160));
        // 빨간 점 + REC 텍스트
        p.setPen(QColor(255, 60, 60));
        p.setFont(QFont("Courier New", 11, QFont::Bold));
        p.drawText(QRect(8, 8, 72, 22), Qt::AlignCenter, "\u25CF REC");
        p.end();
    }
}

// ── 쉐이더 빌드 ─────────────────────────────────────────────
void GameCanvas::buildDefaultShader() {
    if (!m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, defaultVertSrc())) {
        emit glLogMessage("Vertex shader: " + m_prog.log()); return;
    }
    if (!m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, defaultFragSrc())) {
        emit glLogMessage("Fragment shader: " + m_prog.log()); return;
    }
    if (!m_prog.link()) {
        emit glLogMessage("Shader link: " + m_prog.log()); return;
    }
    m_shaderReady = true;
    emit glLogMessage("✔ OpenGL 쉐이더 준비 완료");
}
