// SoftCanvas.cpp — CPU 로 그리는 게임 화면 (OpenGL 합성이 안 되는 PC 용)

#include "SoftCanvas.h"

#include <QFont>
#include <QPainter>
#include <QTransform>
#include <algorithm>

#include "EmulatorState.h"

SoftCanvas::SoftCanvas(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(0, 0);
    setFocusPolicy(Qt::StrongFocus);
}

bool SoftCanvas::setShaderPath(const QString& path) {
    if (!path.isEmpty())
        emit glLogMessage(QStringLiteral("⚠ 소프트웨어 렌더러에서는 셰이더를 쓸 수 없습니다 (VIDEO OPTIONS → RENDERER 를 OPENGL 로)"));
    return false;
}

void SoftCanvas::setBezelImage(const QImage& img) {
    m_bezel          = img.isNull() ? QPixmap() : QPixmap::fromImage(img);
    m_bezelScaled    = QPixmap();
    m_bezelScaledFor = QSize();
    m_bezelWindow    = detectBezelWindow(img);
    update();
}

QRectF SoftCanvas::destRect(int fw, int fh, int vw, int vh) const {
    if (m_scaleMode == QLatin1String("1:1"))
        return { (vw - fw) * 0.5, (vh - fh) * 0.5, double(fw), double(fh) };
    if (m_scaleMode == QLatin1String("Fill"))
        return { 0, 0, double(vw), double(vh) };
    const double s = std::min(double(vw) / fw, double(vh) / fh);       // Fit
    const double w = fw * s, h = fh * s;
    return { (vw - w) * 0.5, (vh - h) * 0.5, w, h };
}

void SoftCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    const int fw = int(gState.videoWidth), fh = int(gState.videoHeight);
    const int vw = width(), vh = height();
    if (fw > 0 && fh > 0 && !gState.videoBuffer.isEmpty() && vw > 0 && vh > 0) {
        // 코어가 준 마지막 프레임 (XRGB8888 이면 그대로, RGB565 면 32비트로 바꾼다)
        const bool x8888 = gState.pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888;
        QImage img(reinterpret_cast<const uchar*>(gState.videoBuffer.constData()), fw, fh,
                   int(gState.videoPitch), x8888 ? QImage::Format_RGB32 : QImage::Format_RGB16);
        if (!x8888) img = img.convertToFormat(QImage::Format_RGB32);

        // 회전 (TATE): 1 = 90°CCW, 2 = 180°, 3 = 90°CW
        int rot = (m_rotation >= 0) ? m_rotation : gState.videoRotation;
        rot &= 3;
        if (rot) {
            const int angle = (rot == 1) ? -90 : (rot == 2 ? 180 : 90);
            img = img.transformed(QTransform().rotate(angle));
        }

        // 베젤이 있으면 그 창 안에 맞춘다 (GameCanvas 와 같은 규칙)
        QRectF dr = destRect(img.width(), img.height(), vw, vh);
        if (m_bezelWindow.isValid()) {
            const double wx = m_bezelWindow.x() * vw, wy = m_bezelWindow.y() * vh;
            const int ww = std::max(1, int(std::lround(m_bezelWindow.width() * vw)));
            const int wh = std::max(1, int(std::lround(m_bezelWindow.height() * vh)));
            const QRectF inner = destRect(img.width(), img.height(), ww, wh);
            dr = QRectF(wx + inner.x(), wy + inner.y(), inner.width(), inner.height());
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_smooth);
        p.drawImage(dr, img);
    }

    if (!m_bezel.isNull()) {
        if (m_bezelScaled.isNull() || m_bezelScaledFor != size()) {
            m_bezelScaled    = m_bezel.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_bezelScaledFor = size();
        }
        p.drawPixmap(0, 0, m_bezelScaled);
    }
    if (m_recording) {
        p.fillRect(8, 8, 72, 22, QColor(0, 0, 0, 160));
        p.setPen(QColor(255, 60, 60));
        p.setFont(QFont("Courier New", 11, QFont::Bold));
        p.drawText(QRect(8, 8, 72, 22), Qt::AlignCenter, QStringLiteral("● REC"));
    }
}
