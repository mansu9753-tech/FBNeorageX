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
    //   "화면 전체가 균일하게 밝아질 때만" 번쩍임으로 본다.
    //   판별: 밝은 픽셀이 화면의 몇 %인가(brightFrac). UI/이펙트는 화면 일부라
    //   비율이 낮고, 전체 번쩍임은 거의 모든 픽셀이 밝아 비율이 매우 높다.
    //   추가로 "직전 프레임 대비 갑자기" 그렇게 됐을 때만 → 밝은 스테이지 오검출 방지.
    bool    m_flashGuard    = false;   // 활성 여부
    float   m_flashStrength = 0.9f;    // 반전 강도 0~1
    float   m_prevBrightFrac= -1.0f;   // 직전 프레임 밝은 픽셀 비율 (-1=초기화 전)
    int     m_flashHold     = 0;       // 반전 유지 남은 프레임 (안전 상한)
    float   m_flashInvert   = 0.0f;    // 현재 프레임 반전 강도 (셰이더 전달)
    void    computeFlashGuard();       // 플래시 감지 → m_flashInvert 갱신

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
