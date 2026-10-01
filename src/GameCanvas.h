#pragma once
// GameCanvas.h — OpenGL 게임 렌더링 위젯 (Phase 3 완전 구현 예정)

#include <QOpenGLWidget>
#include <QPixmap>
#include <QMatrix4x4>
#include <QVector>
#include <QOpenGLFramebufferObject>
#include "SlangPreset.h"
#include "SlangChain.h"
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QString>
#include <QHash>

class GameCanvas : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit GameCanvas(QWidget* parent = nullptr);
    ~GameCanvas() override;

    // 스케일 모드: "Fill" / "Fit" / "1:1"
    void setScaleMode(const QString& mode);
    void setSmooth(bool smooth);
    void setCrtMode(bool on, double intensity = 0.4);
    bool setShaderPath(const QString& path);  // true=성공/보류, false=컴파일 실패

    // ── 베젤(아케이드 프레임) 오버레이 ───────────────────────
    //   게임 화면 위에 PNG 를 덧그린다. 투명한 가운데 창으로 게임이 비친다.
    //   셰이더 파이프라인과 완전히 분리돼 있어 CRT/플래시 처리에 영향이 없다.
    void setBezelImage(const QImage& img);   // 빈 이미지면 해제
    bool hasBezel() const { return !m_bezel.isNull(); }
    void setRecording(bool on);   // REC 오버레이 토글

    // 플래시 감소 (눈 보호): 화면이 갑자기 밝아지는 순간(카운터/총구 화염)을
    //   감지해 그 프레임을 어둡게 처리 → 눈부심·눈 피로 감소.
    //   on=활성, strength 0.0~1.0 (클수록 더 어둡게)
    void setFlashGuard(bool on, float strength);

    // 회전 모드 (tate): 0=없음, 1=90°CCW, 2=180°, 3=90°CW
    // -1 = 자동(gState.videoRotation 사용)
    void setRotation(int rot);
    int  rotation() const { return m_rotation; }

    // ── slang 셰이더 파라미터 (RetroArch 의 "셰이더 파라미터" 에 해당) ──
    //   현재 걸린 프리셋이 선언한 파라미터 목록. 체인이 없으면 비어 있다.
    QVector<SlangParamDecl> shaderParameters() const;
    float shaderParameter(const QString& name) const;
    // 값을 바꾼다. 다음 프레임부터 화면에 반영된다.
    void  setShaderParameter(const QString& name, float value);
    // 지금 걸린 프리셋의 파일 이름 (설정 저장 키로 쓴다)
    QString shaderKey() const { return m_shaderKey; }

signals:
    void glLogMessage(const QString& msg);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    // ── OpenGL 리소스 ────────────────────────────────────
    GLuint m_vao        = 0;
    GLuint m_vbo        = 0;
    GLuint m_texId      = 0;
    bool   m_glReady    = false;

    // ── 쉐이더 ──────────────────────────────────────────
    QOpenGLShaderProgram m_prog;
    bool m_shaderReady = false;

    // ── 옵션 ─────────────────────────────────────────────
    QString m_scaleMode    = "Fill";
    bool    m_smooth       = false;
    bool    m_crtMode      = false;
    double  m_crtIntensity = 0.4;

    // ── 회전 (tate) ──────────────────────────────────────
    // -1=자동(gState.videoRotation), 0~3=수동 고정
    int     m_rotation = -1;

    // ── 외부 셰이더 ──────────────────────────────────────
    bool    m_externalShader = false;
    QString m_pendingShaderPath;  // initializeGL 전에 세팅된 경우 보류
    // #pragma parameter 기본값 (RetroArch 파라미터 uniform 초기값)
    QHash<QString, float> m_pragmaDefaults;

    // ── 녹화 오버레이 ─────────────────────────────────────
    bool    m_recording = false;

    // ── 플래시 감소 (눈 보호) ─────────────────────────────
    //   "화면 전체가 하얗게 번쩍이는 프레임"만 잡아서 통째로 어둡게 한다.
    //   근거: 전체 번쩍임 프레임에는 배경/스프라이트가 그려지지 않고 화면이
    //   거의 균일한 흰 채움이다 → 프레임 전체를 균일하게 어둡게 해도 안전하며,
    //   색반전과 달리 캐릭터 색이 뒤틀리지 않는다.
    //   완화 방식은 Xbox XAG 118 / WCAG 2.3.1 권고인 "밝은 부분과 어두운 부분의
    //   대비를 줄인다"(= 휘도 감쇠)를 따른다. (반전은 표준에 없는 방식이었음)
    // 완화 방식은 FlashGuard(arXiv:2507.19692) 의 temporal averaging 을 휘도에
    // 적용한 것: 흰 프레임을 "최근 정상 프레임들의 평균 밝기"로 끌어내린다.
    //   ★ 고정 배율로 검게 만드는 hard replacement 는 쓰지 않는다. 그러면 빠른
    //     스트로브에서 흰→정상 진동이 검정→정상 진동으로 바뀔 뿐이라 여전히
    //     번쩍이고 어색하다. 베이스라인에 맞추면 진동 자체가 사라진다.
    //   ★ 램프(서서히 복귀)도 쓰지 않는다. 플래시 프레임에만 정확히 적용해야
    //     "뒤늦게 어두워지는" 잔상이 생기지 않는다.
    bool    m_flashGuard    = false;   // 활성 여부
    float   m_flashStrength = 0.9f;    // 보정 강도 0~1 (1=베이스라인에 완전히 맞춤)
    int     m_flashFrames   = 0;       // 연속 흰 화면 프레임 수 (지속 밝은 씬 판별용)
    float   m_baseLuma      = -1.0f;   // 최근 '정상' 프레임 평균 휘도 EMA (-1=미확립)
    float   m_flashDim      = 1.0f;    // 이번 프레임 밝기 배율 (1=손대지 않음)
    void    computeFlashGuard();       // 플래시 감지 → m_flashDim 갱신

    // ── 내부 ────────────────────────────────────────────
    void uploadFrame();
    void buildDefaultShader();
    void updateVertices();
    QRectF calcDestRect(int frameW, int frameH, int viewW, int viewH) const;

    // 외부 RetroArch .glsl 셰이더 파싱 및 컴파일
    bool parseAndLoadGlsl(const QString& path);
    bool parseAndLoadSlang(const QString& path);   // RetroArch .slang / .slangp
    bool compileProgram(const QString& vertSrc, const QString& fragSrc);

    // slang 셰이더용 — 위치가 [0,1] 인 정점 버퍼 + [0,1]→화면 변환
    unsigned int m_vboUnit = 0;
    QMatrix4x4   m_slangMvp;
    bool         m_slangShader = false;

    // ── 다중 패스 slang (.slangp) ────────────────────────────
    //   기존 단일 패스 경로는 그대로 두고, 다중 패스 프리셋일 때만 이쪽을 탄다.
    //   실패하면 전부 해제하고 기본 셰이더로 돌아간다.
    struct MultiPass {
        QOpenGLShaderProgram*     prog = nullptr;
        QOpenGLFramebufferObject* fbo[2] = {nullptr, nullptr};  // 피드백용 핑퐁
        int       cur     = 0;      // 이전 프레임 결과를 담고 있는 버퍼 (피드백 원본)
        int       written = 0;      // 이번 프레임에 실제로 그린 버퍼
        QString   alias;
        SlangScale scaleType = SlangScale::Source;
        float     scale  = 1.0f;
        bool      linear = false;
        SlangWrap wrap   = SlangWrap::ClampToEdge;
        bool      needsFeedback = false;   // PassFeedbackN 을 참조하는가
        bool      floatFbo    = false;     // 부동소수점 렌더타깃
        bool      mipmapInput = false;     // 입력에 밉맵 생성
        QSize     size;
    };
    QVector<MultiPass> m_passes;
    struct LutTex { QString name; GLuint id = 0; };
    QVector<LutTex>    m_luts;
    QHash<QString,float> m_presetParams;   // 프리셋이 지정한 파라미터 값
    bool   m_multiPass = false;
    GLuint m_unitVboMid = 0;      // 중간 패스용 (위치 [0,1] + UV 항등)

    // ── RetroArch 완전 호환 경로 (glslang + SPIRV-Cross) ───
    //   OpenGL 3.3 이상이면 이쪽을 먼저 시도한다. Mega Bezel 처럼 GLSL 450
    //   기능을 쓰는 셰이더도 그대로 돌아간다. 실패하면 아래 예전 경로로
    //   자동으로 내려가므로, 지금까지 되던 셰이더가 안 되는 일은 없다.
    SlangChain m_chain;
    bool   m_chainActive = false;
    QString m_shaderKey;           // 지금 걸린 프리셋 파일 이름
    int    m_glslVersion = 0;      // 이 컨텍스트가 감당하는 GLSL 버전 (0=불가)
    QRectF m_destRect;             // 마지막으로 계산한 출력 사각형 (논리 좌표)

    bool loadSlangPreset(const QString& presetPath);
    void releaseMultiPass();
    void renderMultiPass();
    void setSlangUniforms(QOpenGLShaderProgram& pr, const QSize& srcSize,
                          const QSize& outSize, const QMatrix4x4& mvp);

    QPixmap m_bezel;         // 원본 (해제 시 null)
    // 베젤의 "뚫린 창" 위치 (이미지 기준 0~1 정규화). 못 찾으면 무효.
    //   게임 화면을 이 안에 맞춰 넣어야 잘리지 않는다.
    QRectF  m_bezelWindow;
    QPixmap m_bezelScaled;   // 현재 위젯 크기에 맞춰 캐시한 것
    QSize   m_bezelScaledFor;

    // 플랫폼별 기본 쉐이더 소스
    static const char* defaultVertSrc();
    static const char* defaultFragSrc();
};
