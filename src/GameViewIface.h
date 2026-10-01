#pragma once
// GameViewIface.h — 게임 화면을 그리는 위젯이 지켜야 하는 공통 창구
//
//  GameCanvas (OpenGL, 셰이더·CRT·플래시 감소 지원)와 SoftCanvas (CPU 그리기, 어떤 PC 에서도 보임)가
//  같은 이름의 함수를 갖게 해서 MainWindow 는 어느 쪽인지 몰라도 된다.
//  OpenGL 합성이 안 되는 PC(구형 내장 그래픽 등)에서는 VIDEO OPTIONS → RENDERER 를 SOFTWARE 로 바꾼다.

#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>

#include "SlangCompile.h"

class QWidget;

// 베젤 PNG 에서 투명한 "창" 을 찾아 0~1 비율 사각형으로 돌려준다 (GameCanvas.cpp)
QRectF detectBezelWindow(const QImage& src);

class GameViewIface {
public:
    virtual ~GameViewIface() = default;

    virtual QWidget* widget() = 0;

    virtual void setScaleMode(const QString& mode) = 0;           // "Fill" / "Fit" / "1:1"
    virtual void setSmooth(bool smooth) = 0;
    virtual void setCrtMode(bool on, double intensity = 0.4) = 0;
    virtual bool setShaderPath(const QString& path) = 0;          // false = 쓸 수 없음/실패
    virtual void setBezelImage(const QImage& img) = 0;            // 빈 이미지면 해제
    virtual void setRecording(bool on) = 0;
    virtual void setFlashGuard(bool on, float strength) = 0;
    virtual void setRotation(int rot) = 0;                        // -1 = 코어 값, 0~3
    virtual int  rotation() const = 0;

    virtual QVector<SlangParamDecl> shaderParameters() const = 0;
    virtual float shaderParameter(const QString& name) const = 0;
    virtual void  setShaderParameter(const QString& name, float value) = 0;
    virtual QString shaderKey() const = 0;
};
