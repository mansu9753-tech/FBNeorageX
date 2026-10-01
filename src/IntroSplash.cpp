// IntroSplash.cpp — 시작 오프닝 (영상 파일 또는 내장 샘플 애니메이션)

#include "IntroSplash.h"

#include <QAudioOutput>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtMath>

#include "AppSettings.h"
#include "PreviewVideo.h"

namespace {

// ── 샘플 애니메이션 시간표 (ms) ─────────────────────────────
constexpr int T_BOX     = 300;    // 테두리 그리기 시작
constexpr int T_BOX_LEN = 1300;
constexpr int T_LETTER  = 1300;   // 글자 하나씩 나타나기 시작
constexpr int T_PER     = 110;    // 글자 하나 간격
constexpr int T_SUB     = 2600;   // 부제 나타남
constexpr int T_FADE    = 3800;   // 검게 사라짐 시작
constexpr int T_END     = 4400;

constexpr int CW = 480, CH = 270;                 // 샘플이 그려지는 낮은 해상도 면 (16:9)
const QString LOGO = QStringLiteral("FBNeoRageX");

double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

}  // namespace

IntroSplash::IntroSplash(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::BlankCursor);
    m_tick = new QTimer(this);
    m_tick->setInterval(16);
    connect(m_tick, &QTimer::timeout, this, [this] {
        if (!m_running) return;
        if (!m_video && m_clock.elapsed() >= T_END) { end(); return; }
        // 영상이 아예 시작하지 못하면(코덱 없음 등) 샘플로 대신한다
        if (m_video && m_frame.isNull() && m_clock.elapsed() > 4000) { begin(true); return; }
        update();
    });
}

IntroSplash::~IntroSplash() = default;

QString IntroSplash::findVideo(const QString& baseDir) {
    static const char* kExt[] = { "mp4", "webm", "mkv", "avi", "mov", "wmv" };
    for (const QString& dir : { baseDir + QStringLiteral("/assets/intro"), baseDir + QStringLiteral("/intro") })
        for (const char* e : kExt) {
            const QString f = dir + QStringLiteral("/intro.") + QLatin1String(e);
            if (QFileInfo::exists(f)) return f;
        }
    // 폴더에 없으면 실행 파일에 내장된 기본 오프닝을 임시 폴더로 꺼내서 쓴다 (영상 재생기는 파일 경로가 필요하다)
    QFile res(QStringLiteral(":/assets/intro/intro.mp4"));
    if (res.exists()) {
        const QString tmp = QDir::tempPath() + QStringLiteral("/fbneoragex_intro_") + QString::number(res.size()) + QStringLiteral(".mp4");
        if (QFileInfo(tmp).size() == res.size()) return tmp;
        if (res.open(QIODevice::ReadOnly)) {
            QFile out(tmp);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.write(res.readAll());
                out.close();
                return tmp;
            }
        }
    }
    return QString();
}

void IntroSplash::start(const QString& videoPath) {
    m_frame = QImage();
    m_running = true;
    m_clock.start();
    m_tick->start();
    if (videoPath.isEmpty()) { begin(true); return; }

    m_video = true;
#if HAVE_FFMPEG
    if (!m_decoder) {
        m_decoder = new PreviewVideo(this);
        connect(m_decoder, &PreviewVideo::frameReady, this, [this](const QImage& img) { m_frame = img; update(); });
        connect(m_decoder, &PreviewVideo::finished, this, [this] { end(); });
        connect(m_decoder, &PreviewVideo::failed, this, [this](const QString&) { begin(true); });
    }
    m_decoder->setVolume(gSettings.audioVolume);
    if (!m_decoder->open(videoPath)) begin(true);
#else
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_sink = new QVideoSink(this);
        m_player->setVideoSink(m_sink);
        auto* ao = new QAudioOutput(this);
        ao->setVolume(gSettings.audioVolume / 100.0);
        m_player->setAudioOutput(ao);
        connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& f) {
            if (!f.isValid()) return;
            m_frame = f.toImage();
            update();
        });
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
            if (s == QMediaPlayer::EndOfMedia) end();
        });
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString&) {
            begin(true);
        });
    }
    m_player->setSource(QUrl::fromLocalFile(videoPath));
    m_player->play();
#endif
}

// 재생을 멈추고 샘플로 돌아간다 (영상이 안 열릴 때)
void IntroSplash::begin(bool sample) {
    if (!sample) return;
    if (m_player) m_player->stop();
    if (m_decoder) m_decoder->stop();
    m_video = false;
    m_frame = QImage();
    m_clock.restart();
    update();
}

void IntroSplash::skip() { end(); }

void IntroSplash::end() {
    if (!m_running) return;
    m_running = false;
    m_tick->stop();
    if (m_player) m_player->stop();
    if (m_decoder) m_decoder->stop();
    emit finished();
}

void IntroSplash::keyPressEvent(QKeyEvent*)     { skip(); }
void IntroSplash::mousePressEvent(QMouseEvent*) { skip(); }

// 파란 테두리 띠를 펜으로 그리듯 그린다. 끝은 진행 방향에 수직인 단면이다.
//   (메뉴 셸의 상자 그리기와 같은 방식: 바깥 둘레 기준 거리 d 까지 모든 링이 같은 지점에서 끝난다)
void IntroSplash::band(QPainter& p, const QRect& b, double t, int thick) const {
    const double d = 2.0 * (b.width() + b.height()) * clamp01(t);
    const int W = b.width(), H = b.height();
    for (int i = 0; i < thick; ++i) {
        const int x = b.x() + i, y = b.y() + i, w = W - 2 * i, h = H - 2 * i;
        if (w <= 0 || h <= 0) break;
        const double k = thick > 1 ? double(i) / (thick - 1) : 0.0;
        const QColor c(qRound(20 + 30 * (1 - k)), qRound(40 + 60 * (1 - k)), qRound(120 + 135 * (1 - k)));
        auto seg = [&](double start, int len) { return int(qBound(0.0, std::round(d - start - i), double(len))); };
        int a;
        if ((a = seg(0, w)) > 0)         p.fillRect(QRect(x, y, a, 1), c);
        if ((a = seg(W, h)) > 0)         p.fillRect(QRect(x + w - 1, y, 1, a), c);
        if ((a = seg(W + H, w)) > 0)     p.fillRect(QRect(x + w - a, y + h - 1, a, 1), c);
        if ((a = seg(2 * W + H, h)) > 0) p.fillRect(QRect(x, y + h - a, 1, a), c);
    }
}

void IntroSplash::paintSample(QPainter& p) {
    if (m_canvas.size() != QSize(CW, CH)) m_canvas = QImage(CW, CH, QImage::Format_ARGB32_Premultiplied);
    m_canvas.fill(Qt::black);
    QPainter q(&m_canvas);
    q.setPen(Qt::NoPen);
    const int ms = int(m_clock.elapsed());

    const int sc = 4;                                     // 로고 글자 배율
    const int tw = PixelFont::width(LOGO, sc), th = PixelFont::CELL_H * sc;
    const QRect box(CW / 2 - (tw + 60) / 2, CH / 2 - (th + 44) / 2, tw + 60, th + 44);

    // 테두리 띠
    band(q, box, clamp01(double(ms - T_BOX) / T_BOX_LEN), 6);

    // 로고 글자: 하나씩 나타나며 잠깐 하얗게 번쩍인다
    const int tx = box.x() + (box.width() - tw) / 2, ty = box.y() + (box.height() - th) / 2;
    for (int i = 0; i < LOGO.size(); ++i) {
        const int since = ms - (T_LETTER + i * T_PER);
        if (since < 0) break;
        const double f = clamp01(since / 160.0);          // 0 = 번쩍, 1 = 제 색
        const QColor c(qRound(255 - 120 * f), qRound(255 - 60 * f), 255);
        m_font.draw(q, LOGO.mid(i, 1), tx + i * PixelFont::CELL_W * sc, ty, c, sc);
    }

    // 부제
    const double st = clamp01(double(ms - T_SUB) / 500.0);
    if (st > 0) {
        const QString sub = QStringLiteral("FINALBURN NEO FRONTEND  2026");
        const QColor c(qRound(150 * st), qRound(170 * st), qRound(255 * st));
        m_font.draw(q, sub, (CW - PixelFont::width(sub, 1)) / 2, box.bottom() + 16, c, 1);
    }

    // 끝에서 검게 사라진다
    const double fo = clamp01(double(ms - T_FADE) / (T_END - T_FADE));
    if (fo > 0) q.fillRect(0, 0, CW, CH, QColor(0, 0, 0, qRound(255 * fo)));
    q.end();

    // 비율을 지켜 창 가운데에 키운다 (도트가 뭉개지지 않게 최근접)
    const QSize t = m_canvas.size().scaled(size(), Qt::KeepAspectRatio);
    p.drawImage(QRect((width() - t.width()) / 2, (height() - t.height()) / 2, t.width(), t.height()), m_canvas);
}

void IntroSplash::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_video) {
        if (m_frame.isNull()) return;
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QSize t = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
        p.drawImage(QRect((width() - t.width()) / 2, (height() - t.height()) / 2, t.width(), t.height()), m_frame);
        return;
    }
    paintSample(p);
}
