// GameCanvas.cpp — OpenGL 게임 렌더링 (Phase 3: CRT + 스케일 모드 완전 구현)

#include "GameCanvas.h"

#include <QElapsedTimer>
#include <QGuiApplication>
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
#include <functional>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QImage>
#include <QMatrix4x4>
#include "SlangShader.h"
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
        "uniform float     uFlashDim;\n"    // 플래시 감소: 1=정상, <1=어둡게
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
        "    // ── 플래시 감소 (눈 보호): 프레임 전체 균일 감쇠 ─────\n"
        "    //   흰 번쩍임 프레임에는 스프라이트가 없으므로 전체를 그대로\n"
        "    //   어둡게 눌러도 안전하다. 픽셀별 게이트/반전이 없으므로\n"
        "    //   캐릭터 색이 뒤집히거나 뒤틀리지 않는다.\n"
        "    col.rgb *= uFlashDim;\n"
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
    m_chain.release();
    m_chainActive = false;
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
void GameCanvas::setFlashGuard(bool on, float strength) {
    m_flashGuard    = on;
    m_flashStrength = std::clamp(strength, 0.0f, 1.0f);
    m_flashFrames   = 0;         // 재초기화
    m_baseLuma      = -1.0f;
    m_flashDim      = 1.0f;
}

// ── 플래시 감지 → 밝기 감쇠 계산 ─────────────────────────────
// 판정은 "이 프레임이 거의 전부 하얀가?" 단 하나뿐이다.
//   전체 번쩍임 프레임에는 배경/스프라이트가 그려지지 않아 화면이 균일한
//   흰 채움이 된다. 반대로 일반 화면은 아무리 밝아도 스프라이트/배경 때문에
//   흰 픽셀 비율이 이만큼 높아지지 않는다 → 이 하나로 충분히 구분된다.
// ★ 예전의 "직전 프레임 대비 급증" 조건을 없앴다. 번쩍임이 여러 프레임에
//   걸쳐 오르거나 연속 스트로브면 직전 프레임도 이미 밝아서 급증 조건이
//   성립하지 않아 대부분의 번쩍임을 놓쳤다(간헐적 동작의 주원인).
// 다운샘플링으로 가볍게 측정 (~2300샘플). uploadFrame 에서 호출.
void GameCanvas::computeFlashGuard() {
    if (!m_flashGuard) { m_flashDim = 1.0f; return; }

    const int w = static_cast<int>(gState.videoWidth);
    const int h = static_cast<int>(gState.videoHeight);
    if (w <= 0 || h <= 0 || gState.videoBuffer.isEmpty()) return;

    const uchar* buf = reinterpret_cast<const uchar*>(gState.videoBuffer.constData());
    const size_t pitch = gState.videoPitch;
    const bool xrgb = (gState.pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888);

    // 가로/세로 각각 ~48 지점만 샘플 (총 ~2300)
    const int stepX = std::max(1, w / 48);
    const int stepY = std::max(1, h / 48);
    const int brightCut = 200;      // "하얀" 픽셀 기준 (0~255, ~0.78)
    int bright = 0, n = 0;
    long long lumaSum = 0;          // 평균 휘도용 누적

    for (int y = 0; y < h; y += stepY) {
        const uchar* row = buf + static_cast<size_t>(y) * pitch;
        for (int x = 0; x < w; x += stepX) {
            int r, g, b;
            if (xrgb) {
                const uchar* px = row + static_cast<size_t>(x) * 4;  // BGRA
                b = px[0]; g = px[1]; r = px[2];
            } else {
                const uint16_t p = *reinterpret_cast<const uint16_t*>(
                                       row + static_cast<size_t>(x) * 2);  // RGB565
                r = ((p >> 11) & 0x1F) << 3;
                g = ((p >>  5) & 0x3F) << 2;
                b = ( p        & 0x1F) << 3;
            }
            int luma = (r * 77 + g * 150 + b * 29) >> 8;   // 0~255
            if (luma >= brightCut) ++bright;               // 밝은 픽셀 카운트
            lumaSum += luma;
            ++n;
        }
    }
    if (n == 0) { m_flashDim = 1.0f; return; }

    // 흰 픽셀 비율(전체 번쩍임이면 1.0 에 가깝다) + 이번 프레임 평균 휘도
    const float frac     = static_cast<float>(bright) / static_cast<float>(n);   // 0~1
    const float meanLuma = static_cast<float>(lumaSum) / (n * 255.0f);           // 0~1

    const float wholeThresh  = 0.85f;   // 화면 85% 이상이 하얘야 "전체 번쩍임"
    const int   sustainLimit = 90;      // 1.5초(60fps) 넘게 계속 하야면 번쩍임이
                                        // 아니라 원래 밝은 장면
    const float baseRate     = 1.0f / 15.0f;  // 베이스라인 EMA (최근 ~15프레임)

    const bool whiteFlash = (frac >= wholeThresh);
    if (whiteFlash) ++m_flashFrames;
    else            m_flashFrames = 0;

    const bool isFlash = whiteFlash && (m_flashFrames <= sustainLimit);

    if (!isFlash) {
        // ── 정상 프레임: 절대 손대지 않는다 (잔상 0) + 베이스라인 갱신 ──
        //   지속적으로 하얀 장면(sustainLimit 초과)도 여기로 와서 베이스라인에
        //   반영되므로, 원래 밝은 게임을 계속 어둡게 만들지 않는다.
        if (m_baseLuma < 0.0f) m_baseLuma = meanLuma;
        else                   m_baseLuma += (meanLuma - m_baseLuma) * baseRate;
        m_flashDim = 1.0f;
        return;
    }

    // ── 플래시 프레임: 최근 정상 프레임 평균 밝기에 맞춘다 ──────────
    //   ratio = 베이스라인 / 현재밝기  → 이 배율을 곱하면 번쩍임 프레임이
    //   주변 프레임과 같은 밝기가 되어 휘도 진동 자체가 사라진다.
    //   (고정 배율로 검게 만들면 진동 방향만 바뀔 뿐 여전히 번쩍인다)
    if (m_baseLuma < 0.0f || meanLuma <= 0.001f) { m_flashDim = 1.0f; return; }

    const float ratio = std::clamp(m_baseLuma / meanLuma, 0.0f, 1.0f);  // 밝게는 안 함
    m_flashDim = 1.0f - m_flashStrength * (1.0f - ratio);  // 강도 1.0 → ratio 그대로
}
void GameCanvas::setRotation(int rot) {
    m_rotation = rot;  // -1=auto, 0~3=manual
    if (m_glReady) { makeCurrent(); updateVertices(); doneCurrent(); }
    update();
}
// ════════════════════════════════════════════════════════════
//  다중 패스 slang (.slangp)
//   각 패스를 FBO 에 그리고 마지막 패스만 화면에 낸다.
//   RetroArch 규약:
//     Source        = 직전 패스 결과 (첫 패스는 게임 화면)
//     Original      = 게임 화면 원본
//     <alias>       = 그 이름을 가진 패스의 이번 프레임 결과
//     PassFeedbackN = N 번 패스의 "이전 프레임" 결과 (핑퐁 버퍼)
//     LUT 이름      = 프리셋의 textures 로 불러온 이미지
// ════════════════════════════════════════════════════════════

static GLint wrapToGl(SlangWrap w) {
    switch (w) {
    case SlangWrap::Repeat:        return GL_REPEAT;
    case SlangWrap::ClampToBorder: return GL_CLAMP_TO_BORDER;
    default:                       return GL_CLAMP_TO_EDGE;
    }
}

void GameCanvas::releaseMultiPass() {
    for (MultiPass& p : m_passes) {
        delete p.prog;
        delete p.fbo[0];
        delete p.fbo[1];
    }
    m_passes.clear();
    for (const LutTex& l : m_luts)
        if (l.id) glDeleteTextures(1, &l.id);
    m_luts.clear();
    m_multiPass = false;
}

bool GameCanvas::loadSlangPreset(const QString& presetPath) {
    QFile pf(presetPath);
    if (!pf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit glLogMessage("프리셋 파일 열기 실패: " + presetPath);
        return false;
    }
    const SlangPreset preset =
        parseSlangPreset(QString::fromUtf8(pf.readAll()),
                         QFileInfo(presetPath).absolutePath());
    if (!preset.ok) {
        emit glLogMessage("✖ " + preset.error);
        return false;
    }

    releaseMultiPass();
    m_presetParams.clear();

    // ── 패스별 셰이더 컴파일 ────────────────────────────────
    m_pragmaDefaults.clear();
    for (int i = 0; i < preset.passes.size(); ++i) {
        const SlangPass& sp = preset.passes[i];

        QFile sf(sp.path);
        if (!sf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit glLogMessage("셰이더 열기 실패: " + QFileInfo(sp.path).fileName());
            releaseMultiPass();
            return false;
        }
        const QString src = QString::fromUtf8(sf.readAll());

        // #pragma parameter 기본값은 모든 패스에서 모은다
        {
            static const QRegularExpression rePragma(
                R"(#pragma\s+parameter\s+(\w+)\s+"[^"]*"\s+([-\d.eE+]+))");
            auto it = rePragma.globalMatch(src);
            while (it.hasNext()) {
                auto m = it.next();
                m_pragmaDefaults[m.captured(1)] = m.captured(2).toFloat();
            }
        }

        const SlangTranslation t = translateSlang(src);
        if (!t.ok) {
            emit glLogMessage(QString("✖ 패스 %1 번역 실패 (%2): %3")
                              .arg(i).arg(QFileInfo(sp.path).fileName(), t.error));
            releaseMultiPass();
            return false;
        }

        MultiPass mp;
        mp.prog      = new QOpenGLShaderProgram;
        mp.alias     = sp.alias;
        mp.scaleType = sp.scaleType;
        mp.scale     = sp.scale;
        mp.linear    = sp.filterLinear;
        mp.wrap      = sp.wrap;
        // 이 패스가 자기 이전 프레임을 참조하면 핑퐁 버퍼가 필요하다
        mp.needsFeedback = src.contains(QString("PassFeedback%1").arg(i));
        mp.mipmapInput   = sp.mipmapInput;
        // 프리셋이 지정했거나, 셰이더가 부동소수점 포맷을 요구하면 float FBO
        mp.floatFbo      = sp.floatFbo || sp.srgbFbo
                           || src.contains("#pragma format R16G16B16A16_SFLOAT")
                           || src.contains("#pragma format R32G32B32A32_SFLOAT");

        if (!mp.prog->addShaderFromSourceCode(QOpenGLShader::Vertex, t.vert) ||
            !mp.prog->addShaderFromSourceCode(QOpenGLShader::Fragment, t.frag) ||
            !mp.prog->link()) {
            emit glLogMessage(QString("✖ 패스 %1 컴파일 실패 (%2):\n%3")
                              .arg(i).arg(QFileInfo(sp.path).fileName(),
                                          mp.prog->log()));
            delete mp.prog;
            releaseMultiPass();
            return false;
        }
        m_passes.append(mp);
    }

    // ── LUT 텍스처 ──────────────────────────────────────────
    for (const SlangLut& l : preset.luts) {
        QImage img(l.path);
        if (img.isNull()) {
            emit glLogMessage("⚠ LUT 이미지 로드 실패: " + QFileInfo(l.path).fileName());
            continue;
        }
        img = img.convertToFormat(QImage::Format_RGBA8888);
        LutTex lt;
        lt.name = l.name;
        glGenTextures(1, &lt.id);
        glBindTexture(GL_TEXTURE_2D, lt.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
        const GLint f = l.linear ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapToGl(l.wrap));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapToGl(l.wrap));
        m_luts.append(lt);
    }

    m_presetParams = preset.params;
    m_multiPass   = true;
    m_slangShader = true;
    m_shaderReady = true;
    emit glLogMessage(QString("✔ 다중 패스 셰이더 로드: %1 (%2패스)")
                      .arg(QFileInfo(presetPath).fileName())
                      .arg(m_passes.size()));
    return true;
}

// 패스 하나에 공통 유니폼을 채운다
void GameCanvas::setSlangUniforms(QOpenGLShaderProgram& pr, const QSize& srcSize,
                                  const QSize& outSize, const QMatrix4x4& mvp)
{
    if (pr.uniformLocation("MVP") >= 0) pr.setUniformValue("MVP", mvp);
    pr.setUniformValue("FrameCount",     (int)gState.frameCount);
    pr.setUniformValue("FrameDirection", 1);

    auto setSize = [&](const char* name, float w, float h) {
        const GLint loc = pr.uniformLocation(name);
        if (loc < 0) return;
        const float iw = w > 0.f ? 1.f / w : 0.f;
        const float ih = h > 0.f ? 1.f / h : 0.f;
        glUniform4f(loc, w, h, iw, ih);   // vec4 셰이더
        glUniform2f(loc, w, h);           // vec2 셰이더 (아니면 무시됨)
    };
    const float ow = float(gState.videoWidth  > 0 ? gState.videoWidth  : 1);
    const float oh = float(gState.videoHeight > 0 ? gState.videoHeight : 1);
    setSize("SourceSize",   float(srcSize.width()), float(srcSize.height()));
    setSize("OriginalSize", ow, oh);
    setSize("OutputSize",   float(outSize.width()), float(outSize.height()));
    setSize("TextureSize",  float(srcSize.width()), float(srcSize.height()));
    setSize("InputSize",    float(srcSize.width()), float(srcSize.height()));

    // #pragma parameter 기본값 → 그 위에 프리셋이 지정한 값을 덮어쓴다
    for (auto it = m_pragmaDefaults.constBegin();
         it != m_pragmaDefaults.constEnd(); ++it)
        pr.setUniformValue(qPrintable(it.key()), it.value());
    for (auto it = m_presetParams.constBegin();
         it != m_presetParams.constEnd(); ++it)
        if (pr.uniformLocation(qPrintable(it.key())) >= 0)
            pr.setUniformValue(qPrintable(it.key()), it.value());
}

void GameCanvas::renderMultiPass() {
    if (m_passes.isEmpty()) return;

    // 회전을 고려한 원본 크기 (첫 패스가 만들어 낼 이미지 크기)
    int rot = (m_rotation >= 0) ? m_rotation : gState.videoRotation;
    rot &= 3;
    const bool swapWH = (rot == 1 || rot == 3);
    const int  gw = int(gState.videoWidth), gh = int(gState.videoHeight);
    const QSize originalSize(swapWH ? gh : gw, swapWH ? gw : gh);

    // ── 패스별 렌더 타깃 크기 결정 + FBO 준비 ────────────────
    QSize srcSize = originalSize;
    for (int i = 0; i < m_passes.size(); ++i) {
        MultiPass& p = m_passes[i];
        const bool last = (i == m_passes.size() - 1);

        QSize target;
        switch (p.scaleType) {
        case SlangScale::Viewport:
            target = QSize(int(width() * p.scale), int(height() * p.scale));
            break;
        case SlangScale::Absolute:
            target = QSize(int(p.scale), int(p.scale));
            break;
        default:
            target = QSize(int(srcSize.width() * p.scale),
                           int(srcSize.height() * p.scale));
            break;
        }
        target.setWidth (qMax(1, target.width()));
        target.setHeight(qMax(1, target.height()));
        p.size = target;

        if (!last) {
            const int nbuf = p.needsFeedback ? 2 : 1;
            for (int b = 0; b < nbuf; ++b) {
                if (!p.fbo[b] || p.fbo[b]->size() != target) {
                    delete p.fbo[b];
                    QOpenGLFramebufferObjectFormat fmt;
                    // 블러·블룸·누적 패스는 8비트로 그리면 계단·뭉개짐이 생긴다
                    if (p.floatFbo) fmt.setInternalTextureFormat(GL_RGBA16F);
                    if (p.mipmapInput) fmt.setMipmap(true);
                    p.fbo[b] = new QOpenGLFramebufferObject(target, fmt);
                }
            }
        }
        srcSize = target;
    }

    // ── 실제 렌더 ────────────────────────────────────────────
    QMatrix4x4 fullMvp;                 // [0,1] → NDC 전체
    fullMvp.translate(-1.0f, -1.0f);
    fullMvp.scale(2.0f, 2.0f);

    GLuint prevTex = m_texId;           // 첫 패스의 Source = 게임 화면
    srcSize = originalSize;

    for (int i = 0; i < m_passes.size(); ++i) {
        MultiPass& p = m_passes[i];
        const bool last = (i == m_passes.size() - 1);
        const int  wIdx = p.needsFeedback ? (p.cur ^ 1) : 0;   // 쓸 버퍼

        if (last) {
            QOpenGLFramebufferObject::bindDefault();
            glViewport(0, 0, width() * devicePixelRatioF(),
                             height() * devicePixelRatioF());
        } else {
            p.fbo[wIdx]->bind();
            glViewport(0, 0, p.size.width(), p.size.height());
        }

        QOpenGLShaderProgram& pr = *p.prog;
        pr.bind();

        // 텍스처 유닛 배정: 0=Source, 1=Original, 2.. = alias / feedback / LUT
        int unit = 0;
        auto bindTex = [&](const char* name, GLuint tex, bool linear, SlangWrap wrap) {
            if (pr.uniformLocation(name) < 0 || tex == 0) return;
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, tex);
            const GLint f = linear ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapToGl(wrap));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapToGl(wrap));
            pr.setUniformValue(name, unit);
            ++unit;
        };

        if (p.mipmapInput && prevTex) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, prevTex);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        bindTex("Source",   prevTex, p.linear, p.wrap);
        if (p.mipmapInput && pr.uniformLocation("Source") >= 0) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR_MIPMAP_LINEAR);
        }
        bindTex("Original", m_texId, p.linear, SlangWrap::ClampToEdge);

        // 앞선 패스들을 alias 이름으로
        for (int j = 0; j < i; ++j) {
            const MultiPass& q = m_passes[j];
            // ★ q.written(이번 프레임에 그린 버퍼)을 봐야 한다.
            //   예전에는 q.cur ^ 1 로 다시 계산했는데, 피드백 패스는 그리고 나서
            //   cur 을 뒤집기 때문에 뒤 패스가 "한 프레임 뒤진" 버퍼를 참조했다.
            if (q.alias.isEmpty() || !q.fbo[q.written]) continue;
            bindTex(qPrintable(q.alias), q.fbo[q.written]->texture(),
                    q.linear, q.wrap);
        }

        // 자기 이전 프레임 (PassFeedbackN)
        if (p.needsFeedback && p.fbo[p.cur]) {
            bindTex(qPrintable(QString("PassFeedback%1").arg(i)),
                    p.fbo[p.cur]->texture(), p.linear, p.wrap);
        }

        // LUT
        for (const LutTex& l : m_luts)
            bindTex(qPrintable(l.name), l.id, true, SlangWrap::ClampToBorder);

        setSlangUniforms(pr, srcSize,
                         last ? QSize(width(), height()) : p.size,
                         last ? m_slangMvp : fullMvp);

        // 정점: 첫 패스만 회전 UV(=m_vboUnit), 나머지는 항등 UV
        glBindBuffer(GL_ARRAY_BUFFER, (i == 0) ? m_vboUnit : m_unitVboMid);
        constexpr int stride = 4 * sizeof(float);
        GLint posLoc = pr.attributeLocation("Position");
        if (posLoc < 0) posLoc = pr.attributeLocation("VertexCoord");
        GLint uvLoc  = pr.attributeLocation("TexCoord");
        if (uvLoc < 0) uvLoc = pr.attributeLocation("texcoord");
        if (posLoc >= 0) {
            glEnableVertexAttribArray(posLoc);
            glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
        }
        if (uvLoc >= 0) {
            glEnableVertexAttribArray(uvLoc);
            glVertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void*>(2 * sizeof(float)));
        }

        if (last) glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (posLoc >= 0) glDisableVertexAttribArray(posLoc);
        if (uvLoc  >= 0) glDisableVertexAttribArray(uvLoc);
        pr.release();

        if (!last) {
            p.fbo[wIdx]->release();
            prevTex = p.fbo[wIdx]->texture();
            srcSize = p.size;
        }
    }

    // 모든 패스가 끝난 뒤에 피드백 버퍼를 교대한다.
    //   (도중에 바꾸면 뒤 패스의 alias 참조가 어긋난다)
    for (MultiPass& p : m_passes)
        if (p.needsFeedback) p.cur = p.written;

    glActiveTexture(GL_TEXTURE0);
}

// ── 베젤 오버레이 ────────────────────────────────────────────
//   셰이더가 아니라 위젯 위에 덧그린다 (paintGL 끝부분).
//   빈 이미지를 주면 해제된다.
// 베젤 PNG 에서 "뚫린 창"(투명 영역)의 위치를 찾는다.
//   가운데 가로줄·세로줄에서 가장 긴 투명 구간을 잡는다. 아케이드 베젤은
//   창이 가운데에 있으므로 이 방법이 단순하면서도 잘 맞고, 모서리의 둥근
//   투명 픽셀에 휘둘리지 않는다.
//   찾지 못하면 무효 사각형을 돌려준다.
static QRectF detectBezelWindow(const QImage& src) {
    if (src.isNull() || !src.hasAlphaChannel()) return QRectF();
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int W = img.width(), H = img.height();
    if (W < 8 || H < 8) return QRectF();

    // 한 줄에서 가장 긴 "투명" 구간을 찾는다 (alpha < 128)
    auto longestRun = [](const std::function<int(int)>& alphaAt, int n,
                         int* begin, int* end) {
        int bestB = -1, bestLen = 0, curB = -1;
        for (int i = 0; i < n; ++i) {
            if (alphaAt(i) < 128) {
                if (curB < 0) curB = i;
                const int len = i - curB + 1;
                if (len > bestLen) { bestLen = len; bestB = curB; }
            } else {
                curB = -1;
            }
        }
        if (bestLen <= 0) return false;
        *begin = bestB; *end = bestB + bestLen - 1;
        return true;
    };

    const int midY = H / 2, midX = W / 2;
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    const uchar* rowMid = img.constScanLine(midY);
    if (!longestRun([&](int x) { return int(rowMid[x * 4 + 3]); }, W, &x0, &x1))
        return QRectF();
    if (!longestRun([&](int y) { return int(img.constScanLine(y)[midX * 4 + 3]); },
                    H, &y0, &y1))
        return QRectF();

    const double w = double(x1 - x0 + 1), h = double(y1 - y0 + 1);
    // 너무 작거나(장식용 투명 점) 사실상 전체인 경우는 창으로 보지 않는다
    if (w < W * 0.20 || h < H * 0.20)          return QRectF();
    if (w > W * 0.995 && h > H * 0.995)        return QRectF();

    return QRectF(double(x0) / W, double(y0) / H, w / W, h / H);
}

void GameCanvas::setBezelImage(const QImage& img) {
    m_bezel          = img.isNull() ? QPixmap() : QPixmap::fromImage(img);
    m_bezelScaled    = QPixmap();      // 크기 캐시 무효화
    m_bezelScaledFor = QSize();
    m_bezelWindow    = detectBezelWindow(img);

    if (!img.isNull()) {
        if (m_bezelWindow.isValid())
            emit glLogMessage(QString("🖼 베젤 창 감지: 가로 %1% / 세로 %2% 영역에 화면을 맞춥니다")
                              .arg(m_bezelWindow.width()  * 100.0, 0, 'f', 0)
                              .arg(m_bezelWindow.height() * 100.0, 0, 'f', 0));
        else
            emit glLogMessage("⚠ 베젤에서 투명한 창을 찾지 못했습니다 — "
                              "화면 위에 그대로 덮습니다 (가운데가 투명한 PNG 를 쓰세요)");
    }

    if (m_glReady) { makeCurrent(); updateVertices(); doneCurrent(); }
    update();
}

bool GameCanvas::setShaderPath(const QString& path) {
    if (!m_glReady) {
        // initializeGL() 전 — 보류
        m_pendingShaderPath = path;
        return true; // 보류 성공으로 간주
    }
    makeCurrent();
    // 셰이더를 바꾸면 완전 호환 체인은 무조건 먼저 내린다.
    //   (성공하면 parseAndLoadSlang 이 다시 올린다)
    if (m_chainActive) { m_chain.release(); m_chainActive = false; m_shaderKey.clear(); }
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
        const QString ext = QFileInfo(path).suffix().toLower();
        const bool isSlang = (ext == "slang" || ext == "slangp");
        if (isSlang ? parseAndLoadSlang(path) : parseAndLoadGlsl(path)) {
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

QVector<SlangParamDecl> GameCanvas::shaderParameters() const {
    return m_chainActive ? m_chain.parameters() : QVector<SlangParamDecl>();
}

float GameCanvas::shaderParameter(const QString& name) const {
    return m_chainActive ? m_chain.parameterValue(name) : 0.0f;
}

void GameCanvas::setShaderParameter(const QString& name, float value) {
    if (!m_chainActive) return;
    m_chain.setParameter(name, value);
    update();
}

// ── RetroArch .slang 파싱 및 컴파일 ─────────────────────────
//   .slang 은 Vulkan GLSL 이라 그대로는 데스크톱 GL 에서 못 쓴다.
//   SlangShader.cpp 가 소스 수준으로 번역해 준다 (단일 패스 한정).
bool GameCanvas::parseAndLoadSlang(const QString& path) {
    QString shaderPath = path;

    // ── 1순위: RetroArch 완전 호환 경로 ─────────────────────
    //   glslang 으로 SPIR-V 를 거쳐 GLSL 330+ 로 뽑아낸다. 히스토리·피드백·
    //   LUT·파라미터까지 전부 지원하므로 Mega Bezel 계열도 그대로 돌아간다.
    releaseMultiPass();
    m_chain.release();
    m_chainActive = false;
    if (m_glslVersion >= 330) {
        // Mega Bezel 처럼 패스가 수십 개인 프리셋은 그래픽 드라이버가 셰이더를
        //   컴파일하는 데 수십 초가 걸린다. 그동안 창이 멈춘 것처럼 보이므로
        //   최소한 커서로라도 "작업 중" 을 알린다.
        QGuiApplication::setOverrideCursor(Qt::BusyCursor);
        QElapsedTimer compileTimer;
        compileTimer.start();

        QString err;
        const bool chainOk = m_chain.load(path, m_glslVersion, err);
        const qint64 ms = compileTimer.elapsed();
        QGuiApplication::restoreOverrideCursor();

        if (chainOk) {
            m_chainActive   = true;
            m_shaderKey     = QFileInfo(path).fileName();

            // 사용자가 예전에 바꿔 둔 파라미터 값을 되살린다
            const auto saved = gSettings.shaderParams.value(m_shaderKey);
            for (auto it = saved.constBegin(); it != saved.constEnd(); ++it)
                m_chain.setParameter(it.key(), it.value());
            if (!saved.isEmpty())
                emit glLogMessage(QString("셰이더 파라미터 %1개 복원").arg(saved.size()));

            m_multiPass     = false;
            m_slangShader   = true;
            m_shaderReady   = true;
            for (const QString& l : m_chain.log()) emit glLogMessage("  " + l);
            emit glLogMessage(QString("slang 체인 로드 완료 — 패스 %1개 (%2초)")
                              .arg(m_chain.passCount())
                              .arg(ms / 1000.0, 0, 'f', 1));
            return true;
        }
        // 실패하면 아래 예전 경로로 내려간다. 이유는 남긴다.
        emit glLogMessage("slang 전체 경로 실패 → 간이 경로로 시도: " + err.left(300));
        m_chain.release();
    }

    // .slangp 프리셋은 패스가 몇 개든 다중 패스 경로로 처리한다
    if (QFileInfo(path).suffix().compare("slangp", Qt::CaseInsensitive) == 0)
        return loadSlangPreset(path);

    QFile f(shaderPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit glLogMessage("셰이더 파일 열기 실패: " + shaderPath);
        return false;
    }
    const QString src = QString::fromUtf8(f.readAll());

    // #pragma parameter 기본값은 glsl 과 형식이 같다
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

    const SlangTranslation t = translateSlang(src);
    if (!t.ok) {
        emit glLogMessage("✖ slang 번역 실패: " + t.error);
        return false;
    }
    releaseMultiPass();
    m_slangShader = true;
    return compileProgram(t.vert, t.frag);
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
    // 셰이더 파라미터·소스 덤프는 개발용이라 화면 로그에는 남기지 않는다.
    //   (문제 추적이 필요하면 crash_log 에서 확인)
    for (auto it = m_pragmaDefaults.constBegin(); it != m_pragmaDefaults.constEnd(); ++it)
        qDebug() << "[shader] pragma" << it.key() << "=" << (double)it.value();
    qDebug() << "[shader] vert" << vertSrc.size() << "frag" << fragSrc.size();

    releaseMultiPass();
    m_slangShader = false;
    return compileProgram(vertSrc, fragSrc);
}

// ── 컴파일 & 링크 (glsl/slang 공용) ──────────────────────────
bool GameCanvas::compileProgram(const QString& vertSrc, const QString& fragSrc) {
    // 셰이더 문제가 생겼을 때 환경을 바로 알 수 있게 남긴다 (crash_log 전용).
    //   .slang 을 #version 130 으로 뽑았다가 Mesa(스팀덱)에서 화면이 깨진 적이 있어,
    //   실제 컨텍스트가 무엇인지 기록해 두는 게 중요하다.
    qDebug() << "[shader] GL" << (const char*)glGetString(GL_VERSION)
             << "| GLSL" << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION)
             << "| viewport" << width() << "x" << height();

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

    // slang 용 [0,1] 위치 버퍼 (updateVertices 에서 채운다)
    glGenBuffers(1, &m_vboUnit);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboUnit);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

    // 다중 패스의 중간 패스용 — 위치 [0,1], UV 항등 (고정값)
    static const float midVerts[] = {
        0.f, 0.f,  0.f, 0.f,
        1.f, 0.f,  1.f, 0.f,
        0.f, 1.f,  0.f, 1.f,
        1.f, 1.f,  1.f, 1.f,
    };
    glGenBuffers(1, &m_unitVboMid);
    glBindBuffer(GL_ARRAY_BUFFER, m_unitVboMid);
    glBufferData(GL_ARRAY_BUFFER, sizeof(midVerts), midVerts, GL_STATIC_DRAW);

    // ── 이 컨텍스트가 감당하는 GLSL 버전 판정 ────────────────
    //   3.3 미만이면 slang 신경로를 아예 끄고 예전 경로만 쓴다.
    {
        const QSurfaceFormat sf = context() ? context()->format() : QSurfaceFormat();
        const int v = sf.majorVersion() * 100 + sf.minorVersion() * 10;
        m_glslVersion = (v >= 330) ? qMin(v, 450) : 0;
        emit glLogMessage(QString("OpenGL %1.%2 — slang 전체 기능 %3")
                          .arg(sf.majorVersion()).arg(sf.minorVersion())
                          .arg(m_glslVersion ? "사용 가능" : "사용 불가(3.3 필요)"));
    }

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
        return { 0, 0, (double)vw, (double)vh };

    // "Fit" (기본) — 종횡비 유지, 레터박스
    double scale = std::min((double)vw / fw, (double)vh / fh);
    double w = fw * scale, h = fh * scale;
    return { (vw - w) * 0.5, (vh - h) * 0.5, w, h };
}

// ── VBO 꼭짓점 갱신 (회전 포함) ──────────────────────────────
void GameCanvas::updateVertices() {
    int fw = static_cast<int>(gState.videoWidth);
    int fh = static_cast<int>(gState.videoHeight);
    if (fw <= 0 || fh <= 0) return;

    int vw = width(), vh = height();
    if (vw <= 0 || vh <= 0) return;

    // 실제 적용 회전: -1이면 코어 보고값(gState.videoRotation) 사용
    int rot = (m_rotation >= 0) ? m_rotation : gState.videoRotation;
    rot &= 3;  // 0~3

    // 회전이 90° or 270°이면 논리 width/height 교환 (aspect ratio 계산용)
    bool swapWH = (rot == 1 || rot == 3);
    int  logW   = swapWH ? fh : fw;
    int  logH   = swapWH ? fw : fh;

    QRectF dr = calcDestRect(logW, logH, vw, vh);

    // ── 베젤이 있으면 그 "뚫린 창" 안에 맞춘다 ────────────────
    //   예전에는 화면 전체 기준으로 배치하고 베젤을 위에 덮어서, 베젤 창이
    //   작으면 게임 화면의 위아래·양옆이 프레임에 가려 잘렸다.
    //   창 안쪽에 비율을 유지한 채 넣으므로 잘리지도, 늘어나지도 않는다.
    if (m_bezelWindow.isValid()) {
        const double wx = m_bezelWindow.x()      * vw;
        const double wy = m_bezelWindow.y()      * vh;
        const int    ww = qMax(1, qRound(m_bezelWindow.width()  * vw));
        const int    wh = qMax(1, qRound(m_bezelWindow.height() * vh));
        // ★ 창을 "화면"으로 보고 스케일 모드를 그대로 적용한다.
        //   Fill 이면 창을 꽉 채우고, Fit 이면 창 안에서 비율 유지,
        //   1:1 이면 원본 크기로 창 가운데. (예전엔 무조건 Fit 이라
        //   Fill 을 골라도 창 안에 레터박스가 생겼다)
        const QRectF inner = calcDestRect(logW, logH, ww, wh);
        dr = QRectF(wx + inner.x(), wy + inner.y(),
                    inner.width(), inner.height());
    }

    m_destRect = dr;   // slang 체인이 마지막 패스를 여기에 그린다

    // NDC 변환 (OpenGL Y축: 위=+1)
    float x0 = static_cast<float>(dr.x() / vw * 2.0 - 1.0);
    float x1 = static_cast<float>((dr.x() + dr.width())  / vw * 2.0 - 1.0);
    float y0 = static_cast<float>(1.0 - dr.y() / vh * 2.0);
    float y1 = static_cast<float>(1.0 - (dr.y() + dr.height()) / vh * 2.0);

    // UV 매핑: 회전에 따라 텍스처 샘플링 방향 변경
    // 꼭짓점 순서: BL(좌하), BR(우하), TL(좌상), TR(우상)
    float u_bl, v_bl, u_br, v_br, u_tl, v_tl, u_tr, v_tr;
    switch (rot) {
    case 0:  // 0° — 정상
        u_bl=0; v_bl=1;  u_br=1; v_br=1;
        u_tl=0; v_tl=0;  u_tr=1; v_tr=0;
        break;
    case 1:  // 90°CCW — 세로형 게임 표준 (DonPachi, 1942 등)
        // UV: (u,v) → (1-v, u)  / 회전 중심 (0.5,0.5) 기준
        u_bl=0; v_bl=0;  u_br=0; v_br=1;
        u_tl=1; v_tl=0;  u_tr=1; v_tr=1;
        break;
    case 2:  // 180° — 상하 반전
        u_bl=1; v_bl=0;  u_br=0; v_br=0;
        u_tl=1; v_tl=1;  u_tr=0; v_tr=1;
        break;
    case 3:  // 270°CCW (=90°CW) — 일부 세로형 게임
        // UV: (u,v) → (v, 1-u)
        u_bl=1; v_bl=1;  u_br=1; v_br=0;
        u_tl=0; v_tl=1;  u_tr=0; v_tr=0;
        break;
    default:
        u_bl=0; v_bl=1;  u_br=1; v_br=1;
        u_tl=0; v_tl=0;  u_tr=1; v_tr=0;
        break;
    }

    float verts[] = {
        x0, y1,  u_bl, v_bl,   // 좌하
        x1, y1,  u_br, v_br,   // 우하
        x0, y0,  u_tl, v_tl,   // 좌상
        x1, y0,  u_tr, v_tr,   // 우상
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    // ── slang 용 정점: 위치를 [0,1] 단위 사각형으로 ──────────
    //   ★ RetroArch slang 셰이더는 Position 이 [0,1] 이라고 전제하고,
    //     MVP 가 그것을 화면 좌표로 옮긴다. 실제로 newpixie-mini 는
    //     "1.0 - Position.y" 로 상하를 뒤집는데, 우리처럼 NDC(-1~1)를 주면
    //     y 가 0~2 가 되어 화면 위쪽 절반만 나온다.
    //   → slang 일 때는 이 버퍼와 아래 MVP 를 쓴다.
    float unitVerts[] = {
        0.f, 0.f,  u_bl, v_bl,
        1.f, 0.f,  u_br, v_br,
        0.f, 1.f,  u_tl, v_tl,
        1.f, 1.f,  u_tr, v_tr,
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_vboUnit);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(unitVerts), unitVerts);

    // [0,1] → 화면 사각형 변환 (slang 의 MVP 로 넘긴다)
    m_slangMvp.setToIdentity();
    m_slangMvp.translate(x0, y1);
    m_slangMvp.scale(x1 - x0, y0 - y1);
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

    // 플래시 감소: 새 프레임 밝기 측정 → m_brightness 갱신
    computeFlashGuard();

    gState.frameReady.store(false, std::memory_order_release);
}

// ── 렌더링 ───────────────────────────────────────────────────
void GameCanvas::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_glReady || !m_shaderReady || !gState.gameLoaded) return;

    uploadFrame();

    // RetroArch 완전 호환 체인이 살아 있으면 그쪽이 전부 그린다
    if (m_chainActive) {
        const double dpr = devicePixelRatioF();
        const QSize  vp(int(width() * dpr), int(height() * dpr));
        const QRect  dst(int(m_destRect.x() * dpr),      int(m_destRect.y() * dpr),
                         int(m_destRect.width() * dpr),  int(m_destRect.height() * dpr));
        int rot = (m_rotation >= 0) ? m_rotation : gState.videoRotation;
        m_chain.render(m_texId,
                       QSize(int(gState.videoWidth), int(gState.videoHeight)),
                       dst.isEmpty() ? QRect(0, 0, vp.width(), vp.height()) : dst,
                       vp, defaultFramebufferObject(), rot & 3);

        // 셰이더가 실제로 프레임 예산을 넘길 때만 알린다 (평소엔 조용하다)
        QString warn;
        if (m_chain.takePerfWarning(warn)) emit glLogMessage("⚠ " + warn);
    } else if (m_multiPass) {
        renderMultiPass();
    } else {
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
        // slang 은 Position 이 [0,1] 이라고 전제하므로 MVP 가 화면 배치를 담당한다.
        //   (일반 glsl 셰이더는 정점이 이미 NDC 라 단위행렬)
        if (m_prog.uniformLocation("MVP") >= 0)
            m_prog.setUniformValue("MVP",
                m_slangShader ? m_slangMvp : QMatrix4x4());
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
        // CRT scanline: 회전 시 스캔라인 방향을 화면 기준으로 유지
        int rot = (m_rotation >= 0) ? m_rotation : gState.videoRotation;
        bool swapped = (rot == 1 || rot == 3);
        float texLines = swapped
            ? (float)gState.videoWidth   // 90°/270°: 화면 세로방향 = 원본 width
            : (float)gState.videoHeight; // 0°/180°: 화면 세로방향 = 원본 height
        m_prog.setUniformValue("uTex",          0);
        m_prog.setUniformValue("uCrtMode",      m_crtMode);
        m_prog.setUniformValue("uCrtIntensity", (float)m_crtIntensity);
        m_prog.setUniformValue("uTexH",         texLines);
        m_prog.setUniformValue("uFlashDim",     m_flashDim);     // 플래시 감소(밝기 감쇠)
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // attribute 이름: 내부명 우선, RetroArch 이름 폴백
    GLint posLoc = m_prog.attributeLocation("aPos");
    if (posLoc < 0) posLoc = m_prog.attributeLocation("VertexCoord");
    if (posLoc < 0) posLoc = m_prog.attributeLocation("position");
    if (posLoc < 0) posLoc = m_prog.attributeLocation("Position");   // slang
    GLint uvLoc  = m_prog.attributeLocation("aUV");
    if (uvLoc  < 0) uvLoc  = m_prog.attributeLocation("TexCoord");
    if (uvLoc  < 0) uvLoc  = m_prog.attributeLocation("texcoord");
    constexpr int stride = 4 * sizeof(float);
    // slang 이면 [0,1] 위치 버퍼를 쓴다
    glBindBuffer(GL_ARRAY_BUFFER, m_slangShader ? m_vboUnit : m_vbo);

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
    }   // 단일 패스 경로 끝

    // ── 베젤 + 녹화 오버레이 (QPainter 한 패스) ─────────────
    //   베젤은 셰이더가 아니라 위젯 위에 덧그린다. 알파가 있는 PNG 를 그대로
    //   쓸 수 있고, 셰이더 경로를 건드리지 않아 CRT/플래시 처리와 무관하다.
    if (!m_bezel.isNull() || m_recording) {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);

        if (!m_bezel.isNull()) {
            // 위젯 크기가 바뀔 때만 다시 스케일 (매 프레임 스케일은 낭비)
            if (m_bezelScaled.isNull() || m_bezelScaledFor != size()) {
                m_bezelScaled    = m_bezel.scaled(size(), Qt::IgnoreAspectRatio,
                                                  Qt::SmoothTransformation);
                m_bezelScaledFor = size();
            }
            p.drawPixmap(0, 0, m_bezelScaled);
        }

        if (m_recording) {
            p.fillRect(8, 8, 72, 22, QColor(0, 0, 0, 160));
            p.setPen(QColor(255, 60, 60));
            p.setFont(QFont("Courier New", 11, QFont::Bold));
            p.drawText(QRect(8, 8, 72, 22), Qt::AlignCenter, "\u25CF REC");
        }
        p.end();
    }
}

// ── 쉐이더 빌드 ─────────────────────────────────────────────
void GameCanvas::buildDefaultShader() {
    m_slangShader = false;   // 기본 셰이더는 NDC 정점을 쓴다
    releaseMultiPass();
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
