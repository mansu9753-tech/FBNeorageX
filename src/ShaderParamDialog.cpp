// ShaderParamDialog.cpp — slang 셰이더 파라미터 편집 창

#include "ShaderParamDialog.h"
#include "GameViewIface.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace {

// 이 프로그램의 다른 창과 같은 느낌으로 맞춘다
const char* kDlgStyle =
    "QDialog{background:#000410;}"
    "QLabel{color:#88aacc;font-family:'Courier New';font-size:10px;}"
    "QLineEdit{background:#001133;color:#aaccff;border:1px solid #334488;"
    "          font-family:'Courier New';font-size:11px;padding:3px;}"
    "QDoubleSpinBox{background:#001133;color:#aaccff;border:1px solid #334488;"
    "               font-family:'Courier New';font-size:10px;}"
    "QScrollArea{background:#000410;border:1px solid #223366;}"
    "QPushButton{background:#001133;color:#88aacc;border:1px solid #334488;"
    "            font-family:'Courier New';font-size:10px;padding:5px 12px;}"
    "QPushButton:hover{background:#002255;color:#aaccff;}";

// 값 범위가 사실상 0 인 항목은 실제 파라미터가 아니라 구역 제목이다.
//   (Mega Bezel 이 "[ SCREEN SCALE ]:" 같은 제목을 이런 식으로 넣는다)
bool looksLikeTitle(const SlangParamDecl& d) {
    return (d.max - d.min) <= 0.0015f;
}

}  // namespace

ShaderParamDialog::ShaderParamDialog(GameViewIface* canvas, const QString& shaderKey,
                                     bool english, QWidget* parent)
    : QDialog(parent), m_canvas(canvas), m_shaderKey(shaderKey), m_en(english) {
    setWindowTitle(m_en ? "Shader Parameters — " + shaderKey
                        : "셰이더 파라미터 — " + shaderKey);
    setStyleSheet(kDlgStyle);
    resize(760, 560);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(6);

    // ── 검색 ─────────────────────────────────────────────
    QHBoxLayout* top = new QHBoxLayout;
    QLabel* sl = new QLabel(m_en ? "Search" : "검색");
    m_search = new QLineEdit;
    m_search->setPlaceholderText(
        m_en ? "type part of a name or description (e.g. aspect)"
             : "이름이나 설명 일부를 입력하세요 (예: aspect)");
    m_count = new QLabel;
    top->addWidget(sl);
    top->addWidget(m_search, 1);
    top->addWidget(m_count);
    root->addLayout(top);

    // ── 목록 ─────────────────────────────────────────────
    QScrollArea* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    m_listInner = new QWidget;
    m_listInner->setStyleSheet("background:#000410;");
    m_listLayout = new QVBoxLayout(m_listInner);
    m_listLayout->setContentsMargins(8, 8, 8, 8);
    m_listLayout->setSpacing(2);
    scroll->setWidget(m_listInner);
    root->addWidget(scroll, 1);

    // ── 버튼 ─────────────────────────────────────────────
    QHBoxLayout* bh = new QHBoxLayout;
    QPushButton* resetBtn = new QPushButton(m_en ? "Reset all to default"
                                                 : "전부 기본값으로");
    QPushButton* closeBtn = new QPushButton(m_en ? "Close" : "닫기");
    bh->addWidget(resetBtn);
    bh->addStretch();
    bh->addWidget(closeBtn);
    root->addLayout(bh);

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        for (const Row& r : m_rows) {
            if (r.isTitle || !m_canvas) continue;
            m_canvas->setShaderParameter(r.decl.name, r.decl.def);
        }
        emit resetRequested();
        // 화면의 조작부도 기본값으로 되돌린다
        buildRows();
        applyFilter(m_search->text());
    });
    connect(m_search, &QLineEdit::textChanged, this, &ShaderParamDialog::applyFilter);

    buildRows();
    applyFilter(QString());
}

void ShaderParamDialog::buildRows() {
    // 기존 줄을 모두 지우고 새로 만든다
    for (const Row& r : m_rows) if (r.widget) r.widget->deleteLater();
    m_rows.clear();
    while (QLayoutItem* item = m_listLayout->takeAt(0)) delete item;

    if (!m_canvas) return;
    const QVector<SlangParamDecl> decls = m_canvas->shaderParameters();

    for (const SlangParamDecl& d : decls) {
        // 값 범위가 0 인데 설명까지 비어 있으면 그냥 여백용 항목이다.
        //   (Mega Bezel 의 *_EMPTY_LINE) 목록에 내봐야 이름만 덩그러니 남는다.
        if (looksLikeTitle(d) && d.desc.trimmed().isEmpty()) continue;

        Row row;
        row.decl     = d;
        row.isTitle  = looksLikeTitle(d);
        row.haystack = (d.name + ' ' + d.desc).toLower();

        QWidget* line = new QWidget;
        line->setStyleSheet("background:transparent;");
        QHBoxLayout* h = new QHBoxLayout(line);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);

        if (row.isTitle) {
            // 구역 제목 — 조작부 없이 눈에 띄게만
            QLabel* t = new QLabel(d.desc.trimmed().isEmpty() ? d.name : d.desc.trimmed());
            t->setStyleSheet("color:#4488ff;font-family:'Courier New';font-size:10px;"
                             "font-weight:bold;padding-top:6px;");
            h->addWidget(t, 1);
        } else {
            QLabel* nameLbl = new QLabel(d.desc.trimmed().isEmpty() ? d.name : d.desc.trimmed());
            nameLbl->setToolTip(d.name);
            nameLbl->setMinimumWidth(330);
            nameLbl->setMaximumWidth(330);
            nameLbl->setWordWrap(false);

            QSlider* slider = new QSlider(Qt::Horizontal);
            // 슬라이더는 정수만 다루므로 step 단위로 눈금을 나눈다
            const float span = d.max - d.min;
            const int   ticks = qBound(1, int(std::lround(span / qMax(0.0001f, d.step))), 100000);
            slider->setRange(0, ticks);

            QDoubleSpinBox* spin = new QDoubleSpinBox;
            spin->setRange(d.min, d.max);
            spin->setSingleStep(d.step);
            // 스텝이 1 이상이면 정수형 값이다 (예: 모드 선택)
            spin->setDecimals(d.step >= 1.0f ? 0 : 3);
            spin->setFixedWidth(90);

            const float cur = m_canvas->shaderParameter(d.name);
            spin->setValue(cur);
            slider->setValue(int(std::lround((cur - d.min) / span * ticks)));

            const QString pname = d.name;
            const float   pmin = d.min, pstep = d.step;

            connect(slider, &QSlider::valueChanged, this,
                    [this, spin, pmin, span, ticks](int v) {
                        const double val = pmin + double(v) / ticks * span;
                        if (qFuzzyCompare(val + 1.0, spin->value() + 1.0)) return;
                        spin->setValue(val);          // spin 쪽에서 실제 적용한다
                    });
            connect(spin, &QDoubleSpinBox::valueChanged, this,
                    [this, slider, pname, pmin, span, ticks](double v) {
                        const int tick = int(std::lround((v - pmin) / span * ticks));
                        if (slider->value() != tick) {
                            QSignalBlocker b(slider);
                            slider->setValue(tick);
                        }
                        if (m_canvas) m_canvas->setShaderParameter(pname, float(v));
                        emit parameterChanged(pname, float(v));
                    });
            Q_UNUSED(pstep);

            h->addWidget(nameLbl);
            h->addWidget(slider, 1);
            h->addWidget(spin);
        }

        row.widget = line;
        m_listLayout->addWidget(line);
        m_rows.append(row);
    }
    m_listLayout->addStretch();
}

void ShaderParamDialog::applyFilter(const QString& text) {
    const QString needle = text.trimmed().toLower();
    int shown = 0;
    for (const Row& r : m_rows) {
        if (!r.widget) continue;
        // 검색 중에는 구역 제목을 숨긴다 (걸러진 결과 사이에 끼면 헷갈린다)
        const bool visible = needle.isEmpty() ? true
                           : (!r.isTitle && r.haystack.contains(needle));
        r.widget->setVisible(visible);
        if (visible && !r.isTitle) ++shown;
    }
    m_count->setText(m_en ? QString("%1 shown").arg(shown)
                          : QString("%1개 표시").arg(shown));
}
