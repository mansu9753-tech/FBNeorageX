// FrameLab.cpp — 프레임 단위 확인 / 캡처 화면

#include "FrameLab.h"

#include <QDateTime>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace {
const QColor kText(255, 255, 255), kDim(150, 160, 200), kBlue(0, 0, 200), kEdge(40, 70, 255);
}

FrameLab::FrameLab(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void FrameLab::open(const QVector<QImage>& frames, std::function<QImage()> stepper,
                    std::function<QString(const QImage&, int)> saver, bool korean) {
    m_frames = frames;
    m_step = std::move(stepper);
    m_save = std::move(saver);
    m_ko = korean;
    m_font.setUnified(korean);
    m_cur = m_base = qMax(0, int(m_frames.size()) - 1);
    m_note.clear();
    setFocus();
    update();
}

void FrameLab::navigate(Cmd c) {
    const int n = int(m_frames.size());
    switch (c) {
    case Cmd::Prev:   m_cur = qMax(0, m_cur - 1); break;
    case Cmd::Prev10: m_cur = qMax(0, m_cur - 10); break;
    case Cmd::First:  m_cur = 0; break;
    case Cmd::Last:   m_cur = qMax(0, n - 1); break;
    case Cmd::Next:
    case Cmd::Next10: {
        // 기록의 끝을 넘어가면 코어를 한 프레임씩 실행해 새 프레임을 만든다
        //   저장해 둘 수 있는 프레임(MAX_FRAMES)이 한계다. 한계에 닿으면 더 앞으로 가지 않고 안내만 한다.
        const int want = m_cur + (c == Cmd::Next ? 1 : 10);
        while (m_cur < want) {
            if (m_cur + 1 >= m_frames.size()) {
                if (m_frames.size() >= MAX_FRAMES) {
                    m_note = m_ko ? QStringLiteral("더 앞으로 갈 수 없습니다 (최대 %1프레임)").arg(MAX_FRAMES)
                                  : QStringLiteral("LIMIT REACHED (MAX %1 FRAMES)").arg(MAX_FRAMES);
                    m_noteUntil = QDateTime::currentMSecsSinceEpoch() + 2500;
                    break;
                }
                const QImage img = m_step ? m_step() : QImage();
                if (img.isNull()) break;
                m_frames.append(img);
            }
            ++m_cur;
        }
        break;
    }
    case Cmd::Save:
        if (m_cur >= 0 && m_cur < m_frames.size() && m_save) {
            const QString path = m_save(m_frames.at(m_cur), labelOf(m_cur));
            m_note = path.isEmpty() ? (m_ko ? QStringLiteral("저장 실패") : QStringLiteral("SAVE FAILED"))
                                    : (m_ko ? QStringLiteral("저장됨: ") : QStringLiteral("SAVED: ")) + path;
            m_noteUntil = QDateTime::currentMSecsSinceEpoch() + 4000;
        }
        break;
    case Cmd::Close:
        emit closed();
        return;
    }
    update();
}

FrameLab::Geo FrameLab::geometry() const {
    Geo g;
    g.scale = qMax(1, width() / 640);
    const int s = g.scale, m = 8 * s, W = width(), H = height();
    const int lineH = PixelFont::CELL_H * s;
    g.title = QRect(m, m, W - 2 * m, lineH);
    g.help  = QRect(m, H - m - lineH, W - 2 * m, lineH);

    const int cellW = (W - 2 * m) / COLS;
    const int cellH = cellW * 3 / 4 + lineH;                       // 썸네일 + 번호 줄
    const int gridTop = g.help.top() - m - ROWS * cellH;
    const int bigTop = g.title.bottom() + m;
    g.big = QRect(m, bigTop, W - 2 * m, qMax(40, gridTop - m - bigTop));

    g.first = qMax(0, m_cur - COLS * ROWS / 2);
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            g.cells << QRect(m + c * cellW, gridTop + r * cellH, cellW, cellH);
    return g;
}

void FrameLab::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    const Geo g = geometry();
    const int s = g.scale;
    const int n = int(m_frames.size());

    // 제목 줄
    m_font.draw(p, QStringLiteral("FRAME LAB"), g.title.x(), g.title.y(), kText, s, 1, true);
    const int lab = labelOf(m_cur);
    const QString info = (m_ko ? QStringLiteral("프레임 ") : QStringLiteral("FRAME "))
                       + (lab > 0 ? QStringLiteral("+") : QString()) + QString::number(lab)
                       + QStringLiteral("   (") + QString::number(n) + QStringLiteral("/")
                       + QString::number(MAX_FRAMES) + QStringLiteral(")");
    m_font.draw(p, info, g.title.right() - PixelFont::width(info, s), g.title.y(), kDim, s);

    // 큰 화면
    if (m_cur >= 0 && m_cur < n) {
        const QImage& im = m_frames.at(m_cur);
        const QSize t = im.size().scaled(g.big.size(), Qt::KeepAspectRatio);
        const QRect r(g.big.x() + (g.big.width() - t.width()) / 2,
                      g.big.y() + (g.big.height() - t.height()) / 2, t.width(), t.height());
        p.drawImage(r, im);
        p.setPen(QPen(kEdge, s));
        p.drawRect(r.adjusted(-s, -s, s - 1, s - 1));
    }

    // 작은 격자
    const int lineH = PixelFont::CELL_H * s;
    for (int i = 0; i < g.cells.size(); ++i) {
        const int idx = g.first + i;
        const QRect cell = g.cells.at(i).adjusted(s, s, -s, 0);
        const QRect thumb(cell.x(), cell.y(), cell.width() - s, cell.height() - lineH);
        if (idx < n) {
            const QImage& im = m_frames.at(idx);
            const QSize t = im.size().scaled(thumb.size(), Qt::KeepAspectRatio);
            const QRect r(thumb.x() + (thumb.width() - t.width()) / 2,
                          thumb.y() + (thumb.height() - t.height()) / 2, t.width(), t.height());
            p.drawImage(r, im);
            if (idx == m_cur) {
                p.setPen(QPen(kEdge, 2 * s));
                p.setBrush(Qt::NoBrush);
                p.drawRect(r.adjusted(-s, -s, s - 1, s - 1));
            }
        } else {
            p.fillRect(thumb, QColor(0, 0, 60));                       // 아직 없는 미래 프레임 (→ 로 만든다)
        }
        const int l = labelOf(idx);
        const QString t = (l > 0 ? QStringLiteral("+") : QString()) + QString::number(l);
        m_font.draw(p, t, cell.x() + (cell.width() - PixelFont::width(t, s)) / 2,
                    cell.bottom() - lineH + s, idx == m_cur ? kText : kDim, s);
    }

    // 안내 줄 / 저장 결과
    const bool note = QDateTime::currentMSecsSinceEpoch() < m_noteUntil;
    const QString help = note ? m_note
        : (m_ko ? QStringLiteral("좌우:프레임  PgUp/PgDn:10프레임  Enter:저장  Esc:닫기")
                : QStringLiteral("LEFT/RIGHT:FRAME  PGUP/PGDN:10  ENTER:SAVE  ESC:CLOSE"));
    m_font.draw(p, PixelFont::fit(help, g.help.width() / (PixelFont::CELL_W * s)),
                g.help.x(), g.help.y(), note ? QColor(255, 216, 96) : kDim, s);
    if (note) update();      // 안내가 사라질 때 다시 그린다
}

void FrameLab::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Left:     navigate(Cmd::Prev);   break;
    case Qt::Key_Right:    navigate(Cmd::Next);   break;
    case Qt::Key_PageUp:
    case Qt::Key_Up:       navigate(Cmd::Prev10); break;
    case Qt::Key_PageDown:
    case Qt::Key_Down:     navigate(Cmd::Next10); break;
    case Qt::Key_Home:     navigate(Cmd::First);  break;
    case Qt::Key_End:      navigate(Cmd::Last);   break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:    if (!e->isAutoRepeat()) navigate(Cmd::Save); break;
    case Qt::Key_Escape:
    case Qt::Key_Backspace: navigate(Cmd::Close); return;
    default: QWidget::keyPressEvent(e); return;
    }
    e->accept();
}

void FrameLab::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) { navigate(Cmd::Close); return; }
    const Geo g = geometry();
    for (int i = 0; i < g.cells.size(); ++i) {
        const int idx = g.first + i;
        if (g.cells.at(i).contains(e->pos()) && idx < m_frames.size()) {
            m_cur = idx;
            update();
            return;
        }
    }
    if (g.big.contains(e->pos())) navigate(Cmd::Save);          // 큰 화면을 누르면 저장
}

void FrameLab::wheelEvent(QWheelEvent* e) {
    if (e->angleDelta().y() > 0) navigate(Cmd::Prev);
    else if (e->angleDelta().y() < 0) navigate(Cmd::Next);
}
