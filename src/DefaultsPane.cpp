#include "DefaultsPane.h"
#include "MainWindow.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFontComboBox>
#include <QComboBox>
#include <QColorDialog>
#include <QPainter>
#include <QPixmap>
#include <QIcon>

namespace vlip {

DefaultsPane::DefaultsPane(MainWindow* mw, QWidget* parent)
    : QWidget(parent), m_mw(mw) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);

    // Canvas group
    auto* canvas = new QGroupBox(tr("Canvas"), this);
    auto* cLay = new QFormLayout(canvas);
    m_w = new QSpinBox(canvas);
    m_w->setRange(64, 8192);
    m_w->setSingleStep(2);
    m_h = new QSpinBox(canvas);
    m_h->setRange(64, 8192);
    m_h->setSingleStep(2);
    m_fps = new QSpinBox(canvas);
    m_fps->setRange(1, 120);
    cLay->addRow(tr("Width:"), m_w);
    cLay->addRow(tr("Height:"), m_h);
    cLay->addRow(tr("FPS:"), m_fps);
    outer->addWidget(canvas);

    auto pushCanvas = [this]() {
        if (m_suspend) return;
        m_mw->setCanvas(m_w->value(), m_h->value(), m_fps->value());
    };
    connect(m_w, qOverload<int>(&QSpinBox::valueChanged), this, pushCanvas);
    connect(m_h, qOverload<int>(&QSpinBox::valueChanged), this, pushCanvas);
    connect(m_fps, qOverload<int>(&QSpinBox::valueChanged), this, pushCanvas);

    // Image defaults
    auto* imgs = new QGroupBox(tr("Images"), this);
    auto* iLay = new QFormLayout(imgs);
    m_imgDur = new QDoubleSpinBox(imgs);
    m_imgDur->setRange(0.1, 600.0);
    m_imgDur->setDecimals(2);
    m_imgDur->setSuffix(" s");
    auto* applyImgDur = new QPushButton(tr("Apply to all images"), imgs);
    auto* imgRow = new QHBoxLayout;
    imgRow->addWidget(m_imgDur);
    imgRow->addWidget(applyImgDur);
    iLay->addRow(tr("Default duration:"), imgRow);
    outer->addWidget(imgs);

    connect(m_imgDur, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [this](double v) {
            if (m_suspend) return;
            Defaults d = m_mw->project().defaults;
            d.imageDuration = v;
            m_mw->setDefaults(d);
        });
    connect(applyImgDur, &QPushButton::clicked, this, [this]() {
        m_mw->applyImageDurationToAll(m_imgDur->value());
    });

    // Video defaults
    auto* vids = new QGroupBox(tr("Videos (bulk)"), this);
    auto* vLay = new QFormLayout(vids);
    m_vidTrimStart = new QDoubleSpinBox(vids);
    m_vidTrimStart->setRange(0.0, 100000.0);
    m_vidTrimStart->setDecimals(3);
    m_vidTrimStart->setSuffix(" s");
    auto* applyTS = new QPushButton(tr("Trim N from START of every video"), vids);

    m_vidTrimEnd = new QDoubleSpinBox(vids);
    m_vidTrimEnd->setRange(0.0, 100000.0);
    m_vidTrimEnd->setDecimals(3);
    m_vidTrimEnd->setSuffix(" s");
    auto* applyTE = new QPushButton(tr("Trim N from END of every video"), vids);

    auto* tsRow = new QHBoxLayout;
    tsRow->addWidget(m_vidTrimStart);
    tsRow->addWidget(applyTS);
    auto* teRow = new QHBoxLayout;
    teRow->addWidget(m_vidTrimEnd);
    teRow->addWidget(applyTE);

    vLay->addRow(tr("Trim start:"), tsRow);
    vLay->addRow(tr("Trim end:"), teRow);

    outer->addWidget(vids);

    // Transitions
    auto* trans = new QGroupBox(tr("Transitions"), this);
    auto* tLay = new QFormLayout(trans);
    m_transition = new QDoubleSpinBox(trans);
    m_transition->setRange(0.0, 5.0);
    m_transition->setDecimals(2);
    m_transition->setSingleStep(0.1);
    m_transition->setSuffix(" s");
    m_transition->setToolTip(tr("Fade-out then fade-in between clips. 0 disables transitions."));
    tLay->addRow(tr("Fade duration:"), m_transition);
    outer->addWidget(trans);

    // Subtitles
    auto* subs = new QGroupBox(tr("Subtitles"), this);
    auto* sLay = new QFormLayout(subs);
    m_fontFamily = new QFontComboBox(subs);
    sLay->addRow(tr("Font:"), m_fontFamily);
    m_fontSize = new QSpinBox(subs);
    m_fontSize->setRange(0, 400);
    m_fontSize->setSuffix(" px");
    m_fontSize->setSpecialValueText(tr("auto (canvas-relative)"));
    sLay->addRow(tr("Font size:"), m_fontSize);
    m_fontColor = new QPushButton(tr("Pick…"), subs);
    sLay->addRow(tr("Text color:"), m_fontColor);
    m_bgColor = new QPushButton(tr("Pick…"), subs);
    m_bgColor->setToolTip(tr("Includes alpha — drag the alpha slider for a translucent box."));
    sLay->addRow(tr("Background color:"), m_bgColor);
    m_position = new QComboBox(subs);
    m_position->addItem(tr("Top"),    int(SubtitlePosition::Top));
    m_position->addItem(tr("Middle"), int(SubtitlePosition::Middle));
    m_position->addItem(tr("Bottom"), int(SubtitlePosition::Bottom));
    sLay->addRow(tr("Position:"), m_position);
    outer->addWidget(subs);

    auto pushSubtitle = [this]() {
        if (m_suspend) return;
        Defaults d = m_mw->project().defaults;
        d.subtitle.fontFamily = m_fontFamily->currentFont().family();
        d.subtitle.fontSizePx = m_fontSize->value();
        d.subtitle.position   = SubtitlePosition(m_position->currentData().toInt());
        // Colors are pushed by their pickers directly (see pickColor lambda).
        m_mw->setDefaults(d);
    };
    connect(m_fontFamily, &QFontComboBox::currentFontChanged, this,
            [pushSubtitle](const QFont&) { pushSubtitle(); });
    connect(m_fontSize, qOverload<int>(&QSpinBox::valueChanged), this,
            [pushSubtitle](int) { pushSubtitle(); });
    connect(m_position, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [pushSubtitle](int) { pushSubtitle(); });
    connect(m_fontColor, &QPushButton::clicked, this, [this]() {
        Defaults d = m_mw->project().defaults;
        pickColor(m_fontColor, d.subtitle.fontColor, /*alpha=*/false);
        m_mw->setDefaults(d);
    });
    connect(m_bgColor, &QPushButton::clicked, this, [this]() {
        Defaults d = m_mw->project().defaults;
        pickColor(m_bgColor, d.subtitle.bgColor, /*alpha=*/true);
        m_mw->setDefaults(d);
    });

    outer->addStretch(1);

    connect(applyTS, &QPushButton::clicked, this, [this]() {
        m_mw->trimVideoStartAll(m_vidTrimStart->value());
    });
    connect(applyTE, &QPushButton::clicked, this, [this]() {
        m_mw->trimVideoEndAll(m_vidTrimEnd->value());
    });
    connect(m_transition, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [this](double v) {
            if (m_suspend) return;
            Defaults d = m_mw->project().defaults;
            d.transitionSecs = v;
            m_mw->setDefaults(d);
        });

    refresh();
}

void DefaultsPane::refresh() {
    m_suspend = true;
    const auto& p = m_mw->project();
    m_w->setValue(p.canvas.width);
    m_h->setValue(p.canvas.height);
    m_fps->setValue(p.canvas.fps);
    m_imgDur->setValue(p.defaults.imageDuration);
    m_vidTrimStart->setValue(p.defaults.videoTrimStart);
    m_vidTrimEnd->setValue(p.defaults.videoTrimEnd);
    m_transition->setValue(p.defaults.transitionSecs);

    if (!p.defaults.subtitle.fontFamily.isEmpty()) {
        m_fontFamily->setCurrentFont(QFont(p.defaults.subtitle.fontFamily));
    }
    m_fontSize->setValue(p.defaults.subtitle.fontSizePx);
    paintSwatch(m_fontColor, p.defaults.subtitle.fontColor);
    paintSwatch(m_bgColor,   p.defaults.subtitle.bgColor);
    int posIdx = m_position->findData(int(p.defaults.subtitle.position));
    if (posIdx >= 0) m_position->setCurrentIndex(posIdx);

    m_suspend = false;
}

void DefaultsPane::pickColor(QPushButton* btn, QColor& target, bool withAlpha) {
    QColorDialog::ColorDialogOptions opts;
    if (withAlpha) opts |= QColorDialog::ShowAlphaChannel;
    QColor c = QColorDialog::getColor(target, this,
                                      btn == m_fontColor ? tr("Subtitle text color")
                                                         : tr("Subtitle background color"),
                                      opts);
    if (c.isValid()) {
        target = c;
        paintSwatch(btn, c);
    }
}

void DefaultsPane::paintSwatch(QPushButton* btn, const QColor& c) {
    QPixmap pm(20, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    // Checkerboard background so alpha is visible.
    QColor sq1(220, 220, 220), sq2(170, 170, 170);
    for (int y = 0; y < pm.height(); y += 4) {
        for (int x = 0; x < pm.width(); x += 4) {
            p.fillRect(x, y, 4, 4, ((x + y) / 4) % 2 ? sq1 : sq2);
        }
    }
    p.fillRect(pm.rect(), c);
    p.setPen(QColor(60, 60, 60));
    p.drawRect(0, 0, pm.width() - 1, pm.height() - 1);
    btn->setIcon(QIcon(pm));
}

} // namespace vlip
