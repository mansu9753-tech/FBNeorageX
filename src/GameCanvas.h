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

    // ── 외부 셰이더 ──────────────────────────────────────
    bool    m_externalShader = false;
    QString m_pendingShaderPath;  // initializeGL 전에 세팅된 경우 보류
    // #pragma parameter 기본값 (RetroArch 파라미터 uniform 초기값)
    QHash<QString, float> m_pragmaDefaults;

    // ── 녹화 오버레이 ─────────────────────────────────────
    bool    m_recording = false;

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
