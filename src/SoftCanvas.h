#pragma once
// SoftCanvas.h — CPU 로 그리는 게임 화면 (OpenGL 을 쓰지 않는다)
//
//  QOpenGLWidget 이 창에 붙으면 Qt 가 창 전체를 GPU 로 합성한다. 일부 PC(구형 내장 그래픽 드라이버 등)는
//  그 합성이 실패해 메뉴까지 검은 화면이 된다. 이 위젯은 평범한 QPainter 로 프레임을 그리므로
//  그런 PC 에서도 게임이 보인다. 대신 셰이더·CRT·플래시 감소는 지원하지 않는다.
//  지원: 화면 비율(Fill/Fit/1:1), 부드럽게, TATE 회전, 베젤 겹치기, REC 표시.

#include <QPixmap>
#include <QRectF>
#include <QWidget>

#include "GameViewIface.h"

class SoftCanvas : public QWidget, public GameViewIface {
    Q_OBJECT
public:
    explicit SoftCanvas(QWidget* parent = nullptr);

    QWidget* widget() override { return this; }
    void setScaleMode(const QString& mode) override { m_scaleMode = mode; update(); }
    void setSmooth(bool smooth) override { m_smooth = smooth; update(); }
    void setCrtMode(bool, double) override {}
    bool setShaderPath(const QString& path) override;
    void setBezelImage(const QImage& img) override;
    void setRecording(bool on) override { m_recording = on; update(); }
    void setFlashGuard(bool, float) override {}
    void setRotation(int rot) override { m_rotation = rot; update(); }
    int  rotation() const override { return m_rotation; }
    QVector<SlangParamDecl> shaderParameters() const override { return {}; }
    float shaderParameter(const QString&) const override { return 0.0f; }
    void  setShaderParameter(const QString&, float) override {}
    QString shaderKey() const override { return QString(); }

signals:
    void glLogMessage(const QString& msg);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QRectF destRect(int fw, int fh, int vw, int vh) const;

    QString  m_scaleMode = QStringLiteral("Fit");
    bool     m_smooth = false;
    int      m_rotation = -1;
    bool     m_recording = false;
    QPixmap  m_bezel;
    QPixmap  m_bezelScaled;
    QSize    m_bezelScaledFor;
    QRectF   m_bezelWindow;                 // 베젤의 투명한 창 (0~1 비율)
};
