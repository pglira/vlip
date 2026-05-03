#include "DefaultsPane.h"
#include "MainWindow.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QLabel>
#include <QFontComboBox>
#include <QComboBox>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QColorDialog>
#include <QTimeZone>
#include <QPainter>
#include <QPixmap>
#include <QIcon>

namespace vlip {

DefaultsPane::DefaultsPane(MainWindow* mw, QWidget* parent)
    : QWidget(parent), m_mw(mw) {
    // The pane is a QScrollArea wrapping an inner content widget. All
    // group boxes are children of `inner`; users get a vertical scrollbar
    // when the dock is shorter than the contents.
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget;
    scroll->setWidget(inner);
    root->addWidget(scroll);

    auto* outer = new QVBoxLayout(inner);
    outer->setContentsMargins(8, 8, 8, 8);

    // Canvas group
    auto* canvas = new QGroupBox(tr("Canvas"), inner);
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
    m_subtitleDuration = new QDoubleSpinBox(subs);
    m_subtitleDuration->setRange(0.0, 600.0);
    m_subtitleDuration->setDecimals(2);
    m_subtitleDuration->setSingleStep(0.5);
    m_subtitleDuration->setSuffix(" s");
    m_subtitleDuration->setSpecialValueText(tr("full segment"));
    m_subtitleDuration->setToolTip(tr(
        "How long the subtitle stays on screen at the start of each item.\n"
        "0 = visible for the whole image / video segment."));
    sLay->addRow(tr("Visible for:"), m_subtitleDuration);
    outer->addWidget(subs);

    auto pushSubtitle = [this]() {
        if (m_suspend) return;
        Defaults d = m_mw->project().defaults;
        d.subtitle.fontFamily = m_fontFamily->currentFont().family();
        d.subtitle.fontSizePx = m_fontSize->value();
        d.subtitle.position   = SubtitlePosition(m_position->currentData().toInt());
        d.subtitle.visibleSecs = m_subtitleDuration->value();
        // Colors are pushed by their pickers directly (see pickColor lambda).
        m_mw->setDefaults(d);
    };
    connect(m_fontFamily, &QFontComboBox::currentFontChanged, this,
            [pushSubtitle](const QFont&) { pushSubtitle(); });
    connect(m_fontSize, qOverload<int>(&QSpinBox::valueChanged), this,
            [pushSubtitle](int) { pushSubtitle(); });
    connect(m_position, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [pushSubtitle](int) { pushSubtitle(); });
    connect(m_subtitleDuration, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [pushSubtitle](double) { pushSubtitle(); });
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

    // Text clips
    auto* tcs = new QGroupBox(tr("Text clips"), this);
    auto* tcLay = new QFormLayout(tcs);
    m_tcFont = new QFontComboBox(tcs);
    tcLay->addRow(tr("Font:"), m_tcFont);
    m_tcFontSize = new QSpinBox(tcs);
    m_tcFontSize->setRange(0, 400);
    m_tcFontSize->setSuffix(" px");
    m_tcFontSize->setSpecialValueText(tr("auto (canvas-relative)"));
    tcLay->addRow(tr("Font size:"), m_tcFontSize);
    m_tcFontColor = new QPushButton(tr("Pick…"), tcs);
    tcLay->addRow(tr("Text color:"), m_tcFontColor);
    m_tcVAlign = new QComboBox(tcs);
    m_tcVAlign->addItem(tr("Top"),    int(VerticalAlign::Top));
    m_tcVAlign->addItem(tr("Middle"), int(VerticalAlign::Middle));
    m_tcVAlign->addItem(tr("Bottom"), int(VerticalAlign::Bottom));
    tcLay->addRow(tr("Vertical align:"), m_tcVAlign);
    m_tcDuration = new QDoubleSpinBox(tcs);
    m_tcDuration->setRange(0.1, 600.0);
    m_tcDuration->setDecimals(2);
    m_tcDuration->setSuffix(" s");
    auto* applyTcDur = new QPushButton(tr("Apply to all text clips"), tcs);
    auto* tcDurRow = new QHBoxLayout;
    tcDurRow->addWidget(m_tcDuration);
    tcDurRow->addWidget(applyTcDur);
    tcLay->addRow(tr("Default duration:"), tcDurRow);
    outer->addWidget(tcs);

    auto pushTextClip = [this]() {
        if (m_suspend) return;
        Defaults d = m_mw->project().defaults;
        d.textClip.fontFamily   = m_tcFont->currentFont().family();
        d.textClip.fontSizePx   = m_tcFontSize->value();
        d.textClip.verticalAlign = VerticalAlign(m_tcVAlign->currentData().toInt());
        d.textClip.defaultDuration = m_tcDuration->value();
        m_mw->setDefaults(d);
    };
    connect(m_tcFont, &QFontComboBox::currentFontChanged, this,
            [pushTextClip](const QFont&) { pushTextClip(); });
    connect(m_tcFontSize, qOverload<int>(&QSpinBox::valueChanged), this,
            [pushTextClip](int) { pushTextClip(); });
    connect(m_tcVAlign, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [pushTextClip](int) { pushTextClip(); });
    connect(m_tcDuration, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [pushTextClip](double) { pushTextClip(); });
    connect(m_tcFontColor, &QPushButton::clicked, this, [this]() {
        Defaults d = m_mw->project().defaults;
        pickColor(m_tcFontColor, d.textClip.fontColor, /*alpha=*/false);
        m_mw->setDefaults(d);
    });
    connect(applyTcDur, &QPushButton::clicked, this, [this]() {
        m_mw->applyTextClipDurationToAll(m_tcDuration->value());
    });

    // Date stamp (per-item timestamp burned into a corner of the canvas)
    auto* ds = new QGroupBox(tr("Date stamp"), this);
    auto* dsLay = new QFormLayout(ds);
    m_dsActive = new QCheckBox(tr("Burn date/time into images and videos"), ds);
    m_dsActive->setToolTip(tr(
        "Format: DD.MM.YYYY HH:MM. Each item shows its own timestamp.\n"
        "Text clips are not stamped."));
    dsLay->addRow(m_dsActive);
    m_dsFont = new QFontComboBox(ds);
    dsLay->addRow(tr("Font:"), m_dsFont);
    m_dsFontSize = new QSpinBox(ds);
    m_dsFontSize->setRange(0, 400);
    m_dsFontSize->setSuffix(" px");
    m_dsFontSize->setSpecialValueText(tr("auto (canvas-relative)"));
    dsLay->addRow(tr("Font size:"), m_dsFontSize);
    m_dsCorner = new QComboBox(ds);
    m_dsCorner->addItem(tr("Top-left"),     int(Corner::TopLeft));
    m_dsCorner->addItem(tr("Top-right"),    int(Corner::TopRight));
    m_dsCorner->addItem(tr("Bottom-left"),  int(Corner::BottomLeft));
    m_dsCorner->addItem(tr("Bottom-right"), int(Corner::BottomRight));
    dsLay->addRow(tr("Corner:"), m_dsCorner);
    m_dsMargin = new QSpinBox(ds);
    m_dsMargin->setRange(0, 1000);
    m_dsMargin->setSuffix(" px");
    dsLay->addRow(tr("Margin:"), m_dsMargin);
    m_timeZone = new QComboBox(ds);
    m_timeZone->setEditable(true);
    m_timeZone->addItem(tr("(system local)"), QString());
    for (const QByteArray& id : QTimeZone::availableTimeZoneIds()) {
        m_timeZone->addItem(QString::fromUtf8(id), QString::fromUtf8(id));
    }
    m_timeZone->setToolTip(tr("IANA time-zone id used to render the date/time."));
    dsLay->addRow(tr("Time zone:"), m_timeZone);
    outer->addWidget(ds);

    auto pushDatestamp = [this]() {
        if (m_suspend) return;
        Defaults d = m_mw->project().defaults;
        d.datestamp.active     = m_dsActive->isChecked();
        d.datestamp.fontFamily = m_dsFont->currentFont().family();
        d.datestamp.fontSizePx = m_dsFontSize->value();
        d.datestamp.corner     = Corner(m_dsCorner->currentData().toInt());
        d.datestamp.marginPx   = m_dsMargin->value();
        // Combo's userData stores the IANA id (or empty for "system local").
        // For an editable combo the user might also type something — fall
        // back to currentText() if userData is empty AND the field's been
        // edited.
        QString tz = m_timeZone->currentData().toString();
        if (tz.isEmpty()) {
            QString typed = m_timeZone->currentText().trimmed();
            if (typed != tr("(system local)")) tz = typed;
        }
        d.timeZone = tz.toUtf8();
        m_mw->setDefaults(d);
    };
    connect(m_dsActive, &QCheckBox::toggled, this, [pushDatestamp](bool) { pushDatestamp(); });
    connect(m_dsFont, &QFontComboBox::currentFontChanged, this,
            [pushDatestamp](const QFont&) { pushDatestamp(); });
    connect(m_dsFontSize, qOverload<int>(&QSpinBox::valueChanged), this,
            [pushDatestamp](int) { pushDatestamp(); });
    connect(m_dsCorner, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [pushDatestamp](int) { pushDatestamp(); });
    connect(m_dsMargin, qOverload<int>(&QSpinBox::valueChanged), this,
            [pushDatestamp](int) { pushDatestamp(); });
    connect(m_timeZone, &QComboBox::currentTextChanged, this,
            [pushDatestamp](const QString&) { pushDatestamp(); });

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
    m_subtitleDuration->setValue(p.defaults.subtitle.visibleSecs);

    if (!p.defaults.textClip.fontFamily.isEmpty()) {
        m_tcFont->setCurrentFont(QFont(p.defaults.textClip.fontFamily));
    }
    m_tcFontSize->setValue(p.defaults.textClip.fontSizePx);
    paintSwatch(m_tcFontColor, p.defaults.textClip.fontColor);
    int vIdx = m_tcVAlign->findData(int(p.defaults.textClip.verticalAlign));
    if (vIdx >= 0) m_tcVAlign->setCurrentIndex(vIdx);
    m_tcDuration->setValue(p.defaults.textClip.defaultDuration);

    m_dsActive->setChecked(p.defaults.datestamp.active);
    if (!p.defaults.datestamp.fontFamily.isEmpty()) {
        m_dsFont->setCurrentFont(QFont(p.defaults.datestamp.fontFamily));
    }
    m_dsFontSize->setValue(p.defaults.datestamp.fontSizePx);
    int cIdx = m_dsCorner->findData(int(p.defaults.datestamp.corner));
    if (cIdx >= 0) m_dsCorner->setCurrentIndex(cIdx);
    m_dsMargin->setValue(p.defaults.datestamp.marginPx);
    {
        QString tz = QString::fromUtf8(p.defaults.timeZone);
        int tzIdx = m_timeZone->findData(tz);
        if (tzIdx >= 0) m_timeZone->setCurrentIndex(tzIdx);
        else m_timeZone->setEditText(tz);
    }

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
