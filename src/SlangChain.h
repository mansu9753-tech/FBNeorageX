#pragma once
// SlangChain.h — RetroArch 셰이더 체인 실행기
//
//  .slangp 프리셋 하나를 통째로 돌린다. RetroArch 가 정의한 시맨틱을
//  그대로 구현하므로 Mega Bezel 처럼 패스가 20개 넘고 히스토리·피드백·LUT 를
//  전부 쓰는 프리셋도 그대로 동작한다.
//
//  ── 지원하는 텍스처 시맨틱 ──────────────────────────────
//    Original            원본 게임 화면 (0번 패스 입력)
//    Source              직전 패스 출력 (0번 패스에서는 Original)
//    OriginalHistory1..N  N 프레임 전의 원본
//    PassOutput0..N       그 패스의 이번 프레임 출력 (앞 패스만)
//    PassFeedback0..N     그 패스의 지난 프레임 출력
//    <alias>              alias 를 붙인 패스의 출력
//    <alias>Feedback      그 패스의 지난 프레임 출력
//    User0..N / LUT 이름  프리셋 textures= 로 불러온 이미지
//
//  ── 지원하는 유니폼 ────────────────────────────────────
//    MVP, OutputSize, FinalViewportSize, FrameCount, FrameDirection,
//    Rotation, TotalSubFrames, CurrentSubFrame,
//    위 모든 텍스처의 <이름>Size (vec4: w, h, 1/w, 1/h),
//    셰이더가 선언한 모든 #pragma parameter
//
//  ── 지원하는 프리셋 항목 ────────────────────────────────
//    #reference 상속, shaderN, aliasN, scale_type[_x/_y]N, scale[_x/_y]N,
//    filter_linearN, wrap_modeN, float_framebufferN, srgb_framebufferN,
//    mipmap_inputN, frame_count_modN, textures= 와 그 _linear/_wrap_mode/_mipmap,
//    파라미터 값 덮어쓰기
//
//  GL 리소스를 직접 다루므로 반드시 올바른 컨텍스트가 current 인 상태에서
//  load()/render()/release() 를 불러야 한다.

#include <QByteArray>
#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include "SlangCompile.h"
#include "SlangPreset.h"

class QOpenGLContext;

class SlangChain {
public:
    SlangChain();
    ~SlangChain();

    // 프리셋을 읽어 파이프라인을 구성한다.
    //   glslVersion 은 현재 컨텍스트가 감당할 수 있는 최고 GLSL 버전.
    //   실패하면 false 를 돌려주고 내부 상태를 완전히 비운다 (부분 적용 없음).
    bool load(const QString& presetPath, int glslVersion, QString& error);

    void release();
    bool isLoaded() const { return !m_passes.isEmpty(); }

    // 한 프레임 그린다.
    //   inputTex   : 게임 화면 텍스처
    //   inputSize  : 그 텍스처의 실제 픽셀 크기
    //   destRect   : 최종 출력 위치 (위젯 좌표, 좌상단 원점)
    //   viewport   : 위젯 전체 크기
    //   targetFbo  : 최종 패스를 그릴 FBO (QOpenGLWidget 의 기본 FBO)
    void render(unsigned inputTex, const QSize& inputSize,
                const QRect& destRect, const QSize& viewport,
                unsigned targetFbo, int rotation);

    // 셰이더가 선언한 파라미터 목록 (UI 노출용). 패스 순서대로 중복 제거됨.
    const QVector<SlangParamDecl>& parameters() const { return m_paramDecls; }
    // 사용자가 값을 바꾼다. 다음 프레임부터 반영된다.
    void setParameter(const QString& name, float value);
    float parameterValue(const QString& name) const;

    QStringList log() const { return m_log; }
    int passCount() const { return m_passes.size(); }

    // ── 성능 측정 ────────────────────────────────────────
    //   GPU 타이머로 "이 셰이더 체인이 프레임당 몇 ms 를 쓰는지" 를 잰다.
    //   추측 대신 숫자를 보고 판단하기 위한 것이다.
    double lastGpuMs() const { return m_gpuMs; }
    // 셰이더가 프레임 예산을 넘겨 실제로 느려지고 있을 때만 한 번씩 채워진다.
    //   (평소에는 아무것도 남기지 않아 로그가 지저분해지지 않는다)
    bool takePerfWarning(QString& out);

private:
    // ── 텍스처 시맨틱 참조 ────────────────────────────────
    struct TexRef {
        enum Kind { None, Original, Source, History, PassOut, PassFeedback, Lut };
        Kind kind = None;
        int  idx  = 0;
    };

    // ── 유니폼 멤버 채우기 계획 ───────────────────────────
    enum class UniKind {
        MVP, OutputSize, FinalViewportSize, FrameCount, FrameDirection,
        Rotation, TotalSubFrames, CurrentSubFrame, TexSize, Constant
    };
    struct UniPlan {
        UniKind  kind;
        quint32  offset = 0;
        quint32  size   = 0;
        TexRef   tex;              // TexSize 일 때만
        int      paramIdx = -1;    // Constant(파라미터) 일 때 m_paramStore 인덱스
    };

    struct TexBind {
        TexRef   ref;
        int      unit = 0;
        int      loc  = -1;
        bool     linear = false;
        SlangWrap wrap = SlangWrap::ClampToEdge;
        bool     mipmap = false;
        unsigned sampler = 0;   // 샘플러 오브젝트 (필터/랩을 미리 굳혀 둔 것)
    };

    struct Pass {
        SlangPass     cfg;
        SlangCompiled sh;
        unsigned      prog = 0;

        // 렌더 타깃. 피드백 참조가 있는 패스만 두 벌을 잡는다.
        unsigned fbo[2] = {0, 0};
        unsigned tex[2] = {0, 0};
        bool     pingPong = false;
        int      cur = 0;              // 이번 프레임에 그린 쪽
        QSize    size;

        bool isLast = false;

        QVector<TexBind> texBinds;
        QVector<UniPlan> uboPlan, pushPlan;
        QByteArray       uboData, pushData;   // 매 프레임 재사용하는 버퍼
        unsigned         uboBuf = 0, pushBuf = 0;
        int              uboIndex = -1, pushIndex = -1;   // 유니폼 블록 인덱스

        // 블록 안에 매 프레임 값이 바뀌는 멤버(FrameCount 등)가 있는가.
        //   없으면 크기·파라미터가 바뀔 때만 올리면 된다. Mega Bezel 은 UBO 에
        //   파라미터 900개가 들어 있어서, 이걸 매 프레임 올리는 것과 아닌 것의
        //   차이가 아주 크다.
        bool uboDynamic = false, pushDynamic = false;
        bool uploaded   = false;   // 정적 블록을 한 번이라도 올렸는가

        // 새로 만든 렌더타깃은 첫 프레임에 한 번만 지운다.
        //   패스는 항상 타깃 전체를 덮으므로 매 프레임 클리어는 낭비다.
        bool needsClear[2] = {true, true};
    };

    struct Lut {
        QString  name;
        unsigned tex = 0;
        QSize    size;
    };

    // ── 내부 헬퍼 ────────────────────────────────────────
    bool  compilePasses(const SlangPreset& preset, int glslVersion, QString& error);
    bool  buildProgram(Pass& p, QString& error);
    void  planUniforms(Pass& p, int passIndex);
    void  planTextures(Pass& p, int passIndex);
    TexRef resolveTexture(const QString& name, int passIndex) const;
    QSize  texSizeOf(const TexRef& r, const QSize& originalSize,
                     const QSize& sourceSize) const;
    unsigned texIdOf(const TexRef& r, unsigned originalTex, unsigned sourceTex) const;

    void  allocTargets(const QSize& inputSize, const QSize& viewport);
    bool  ensureTarget(Pass& p, int slot, const QSize& size);
    void  pushHistory(unsigned inputTex, const QSize& inputSize);
    void  fillBlock(Pass& unusedPass, const QVector<UniPlan>& plan, QByteArray& data,
                    const QMatrix4x4& mvp, const QSize& outSize,
                    const QSize& viewport, const QSize& originalSize,
                    const QSize& sourceSize, int frameCountMod, int rotation);

    void  releaseHistory();
    void  addLog(const QString& s);

    // ── 상태 ─────────────────────────────────────────────
    QOpenGLFunctions* f = nullptr;
    QVector<Pass>     m_passes;
    QVector<Lut>      m_luts;
    QHash<QString,int> m_aliasToPass;     // alias(소문자) → 패스 번호
    QVector<SlangParamDecl> m_paramDecls;

    // OriginalHistory 용 링버퍼 (0번이 가장 최근 = 한 프레임 전)
    QVector<unsigned> m_history;
    QVector<QSize>    m_historySize;
    int               m_historyDepth = 0;
    unsigned          m_blitFboSrc = 0, m_blitFboDst = 0;

    unsigned m_vao = 0, m_vbo = 0;
    quint64  m_frameCount = 0;
    QSize    m_lastInput, m_lastViewport;
    QStringList m_log;

    // 필터/랩 조합별 샘플러 오브젝트. 이게 없으면 텍스처를 바인딩할 때마다
    //   glTexParameteri 를 4번씩 불러야 하는데, 패스가 40개인 프리셋에서는
    //   프레임당 1000번이 넘어가 드라이버가 매번 텍스처 상태를 다시 검증한다.
    QHash<int, unsigned> m_samplers;
    unsigned samplerFor(bool linear, bool mipmap, SlangWrap wrap);
    int      m_maxUnitUsed = 0;

    // 크기·파라미터가 바뀌었으니 정적 유니폼 블록을 다시 올려야 한다는 표시
    bool m_uniformsDirty = true;

    // ── GPU 타이머 ───────────────────────────────────────
    //   질의를 여러 개 돌려가며 쓴다. 바로 결과를 읽으면 GPU 를 기다리게 되어
    //   측정 행위 자체가 느려지기 때문이다.
    static constexpr int kQueryRing = 4;
    unsigned m_queries[kQueryRing] = {0, 0, 0, 0};
    bool     m_queryBusy[kQueryRing] = {false, false, false, false};
    int      m_queryHead = 0;
    double   m_gpuMs = 0.0;        // 최근 평균 (ms)
    double   m_gpuAccum = 0.0;
    int      m_gpuSamples = 0;
    int      m_warnCooldown = 0;   // 경고 후 잠잠히 있을 프레임 수
    bool     m_reportedOnce = false;  // 첫 측정값을 알려줬는가
    QString  m_perfWarning;
    void     beginGpuTimer();
    void     endGpuTimer();

    // 파라미터 값은 이름 해시가 아니라 배열 인덱스로 찾는다.
    //   Mega Bezel 은 패스마다 유니폼 멤버가 900개씩이라, 프레임당 3만 번이 넘는
    //   QString 해시 조회가 발생해 스팀덱 CPU 를 그대로 잡아먹었다.
    QVector<float>     m_paramStore;
    QHash<QString,int> m_paramIndex;

    // 3.3 에서 추가로 필요한 함수들 (QOpenGLFunctions 에 없는 것만 직접 가져온다)
    void (*p_glGenVertexArrays)(int, unsigned*) = nullptr;
    void (*p_glBindVertexArray)(unsigned) = nullptr;
    void (*p_glDeleteVertexArrays)(int, const unsigned*) = nullptr;
    unsigned (*p_glGetUniformBlockIndex)(unsigned, const char*) = nullptr;
    void (*p_glUniformBlockBinding)(unsigned, unsigned, unsigned) = nullptr;
    void (*p_glBindBufferBase)(unsigned, unsigned, unsigned) = nullptr;
    void (*p_glBlitFramebuffer)(int, int, int, int, int, int, int, int,
                                unsigned, unsigned) = nullptr;
    void (*p_glGenSamplers)(int, unsigned*) = nullptr;
    void (*p_glDeleteSamplers)(int, const unsigned*) = nullptr;
    void (*p_glBindSampler)(unsigned, unsigned) = nullptr;
    void (*p_glSamplerParameteri)(unsigned, unsigned, int) = nullptr;
    void (*p_glGenQueries)(int, unsigned*) = nullptr;
    void (*p_glDeleteQueries)(int, const unsigned*) = nullptr;
    void (*p_glBeginQuery)(unsigned, unsigned) = nullptr;
    void (*p_glEndQuery)(unsigned) = nullptr;
    void (*p_glGetQueryObjectuiv)(unsigned, unsigned, unsigned*) = nullptr;
    void (*p_glGetQueryObjectui64v)(unsigned, unsigned, quint64*) = nullptr;
    bool resolveGlFunctions(QOpenGLContext* ctx);
};
