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
    m_suspend = false;
}

} // namespace vlip
