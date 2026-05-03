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
#include <QSignalBlocker>
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
    m_canvasPreset = new QComboBox(canvas);
    // userData: w * 1e9 + h * 1e3 + fps. Negative = "Custom" sentinel.
    auto encode = [](int w, int h, int fps) {
        return qint64(w) * 1'000'000'000LL + qint64(h) * 1'000LL + qint64(fps);
    };
    m_canvasPreset->addItem(tr("Custom"), qint64(-1));
    m_canvasPreset->addItem(tr("Full HD (1920×1080) — 30 fps"), encode(1920, 1080, 30));
    m_canvasPreset->addItem(tr("Full HD (1920×1080) — 60 fps"), encode(1920, 1080, 60));
    m_canvasPreset->addItem(tr("4K UHD (3840×2160) — 30 fps"), encode(3840, 2160, 30));
    m_canvasPreset->addItem(tr("4K UHD (3840×2160) — 60 fps"), encode(3840, 2160, 60));
    cLay->addRow(tr("Preset:"), m_canvasPreset);

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

    auto syncPresetCombo = [this]() {
        // Find a preset matching the current spinboxes; otherwise show "Custom".
        for (int i = 1; i < m_canvasPreset->count(); ++i) {
            qint64 enc = m_canvasPreset->itemData(i).toLongLong();
            int w = int(enc / 1'000'000'000LL);
            int h = int((enc / 1'000LL) % 1'000'000LL);
            int f = int(enc % 1'000LL);
            if (w == m_w->value() && h == m_h->value() && f == m_fps->value()) {
                m_canvasPreset->setCurrentIndex(i);
                return;
            }
        }
        m_canvasPreset->setCurrentIndex(0); // Custom
    };

    auto pushCanvas = [this, syncPresetCombo]() {
        if (m_suspend) return;
        m_mw->setCanvas(m_w->value(), m_h->value(), m_fps->value());
        QSignalBlocker b(m_canvasPreset);
        syncPresetCombo();
    };
    connect(m_w, qOverload<int>(&QSpinBox::valueChanged), this, pushCanvas);
    connect(m_h, qOverload<int>(&QSpinBox::valueChanged), this, pushCanvas);
    connect(m_fps, qOverload<int>(&QSpinBox::valueChanged), this, pushCanvas);

    connect(m_canvasPreset, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int idx) {
            if (m_suspend) return;
            qint64 enc = m_canvasPreset->itemData(idx).toLongLong();
            if (enc < 0) return;        // "Custom" — leave fields alone
            int w = int(enc / 1'000'000'000LL);
            int h = int((enc / 1'000LL) % 1'000'000LL);
            int f = int(enc % 1'000LL);
            // Suspend so each spinbox change doesn't fire pushCanvas thrice
            // and re-snap the combo to "Custom" mid-update.
            m_suspend = true;
            m_w->setValue(w);
            m_h->setValue(h);
            m_fps->setValue(f);
            m_suspend = false;
            m_mw->setCanvas(w, h, f);
        });

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
    int presetIdx = 0;  // Custom by default
    for (int i = 1; i < m_canvasPreset->count(); ++i) {
        qint64 enc = m_canvasPreset->itemData(i).toLongLong();
        int w = int(enc / 1'000'000'000LL);
        int h = int((enc / 1'000LL) % 1'000'000LL);
        int f = int(enc % 1'000LL);
        if (w == p.canvas.width && h == p.canvas.height && f == p.canvas.fps) {
            presetIdx = i; break;
        }
    }
    m_canvasPreset->setCurrentIndex(presetIdx);
    m_imgDur->setValue(p.defaults.imageDuration);
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
