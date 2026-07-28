#pragma once
// GameCanvas.h — OpenGL 게임 렌더링 위젯 (Phase 3 완전 구현 예정)

#include <QOpenGLWidget>
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
    void setRecording(bool on);   // REC 오버레이 토글

    // 플래시 감소 (눈 보호): 화면이 갑자기 밝아지는 순간(카운터/총구 화염)을
    //   감지해 그 프레임을 어둡게 처리 → 눈부심·눈 피로 감소.
    //   on=활성, strength 0.0~1.0 (클수록 더 어둡게)
    void setFlashGuard(bool on, float strength);

    // 회전 모드 (tate): 0=없음, 1=90°CCW, 2=180°, 3=90°CW
    // -1 = 자동(gState.videoRotation 사용)
    void setRotation(int rot);
    int  rotation() const { return m_rotation; }

    // 외부에서 커스텀 GLSL 쉐이더 로드
    bool loadShader(const QString& vertPath, const QString& fragPath);

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

    // 플랫폼별 기본 쉐이더 소스
    static const char* defaultVertSrc();
    static const char* defaultFragSrc();
};
