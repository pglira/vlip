#include "DefaultsPane.hpp"
#include "MainWindow.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
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
#include <QListWidget>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>

namespace vlip {

DefaultsPane::DefaultsPane(MainWindow* mw, QWidget* parent)
    : QWidget(parent), m_mw(mw) {
    // The pane is a QTabWidget; each tab groups settings by scope (what
    // they affect: the whole canvas, all clips, only images, etc.). Each
    // tab wraps its content in a QScrollArea so tall tabs remain usable
    // when the dock is short.
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs);

    auto addTab = [tabs](const QString& title) -> QVBoxLayout* {
        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content = new QWidget;
        scroll->setWidget(content);
        auto* lay = new QVBoxLayout(content);
        lay->setContentsMargins(8, 8, 8, 8);
        tabs->addTab(scroll, title);
        return lay;
    };

    // ----- Output tab: settings that describe the rendered video as a
    // whole (canvas dimensions and cross-clip transitions).
    auto* outputTab = addTab(tr("Output"));

    // Canvas
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
    outputTab->addWidget(canvas);

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
    outputTab->addWidget(trans);
    outputTab->addStretch(1);

    connect(m_transition, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [this](double v) {
            if (m_suspend) return;
            Defaults d = m_mw->project().defaults;
            d.transitionSecs = v;
            m_mw->setDefaults(d);
        });

    // ----- Image clips tab: settings that only affect image clips -----
    auto* imgTab = addTab(tr("Image clips"));
    auto* iLay = new QFormLayout;
    m_imageClipDuration = new QDoubleSpinBox(this);
    m_imageClipDuration->setRange(0.1, 600.0);
    m_imageClipDuration->setDecimals(2);
    m_imageClipDuration->setSuffix(" s");
    iLay->addRow(tr("Default duration:"), m_imageClipDuration);
    imgTab->addLayout(iLay);
    imgTab->addStretch(1);

    connect(m_imageClipDuration, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [this](double v) {
            if (m_suspend) return;
            Defaults d = m_mw->project().defaults;
            d.imageClipDuration = v;
            m_mw->setDefaults(d);
        });

    // ----- Image && video clips tab: settings that overlay onto image and
    // video clips (subtitles and the burned-in date stamp). Text clips are
    // not affected by either.
    auto* ivTab = addTab(tr("Image && video clips"));

    // Subtitles
    auto* subs = new QGroupBox(tr("Subtitles"), this);
    auto* sLay = new QFormLayout(subs);
    m_fontFamily = new QFontComboBox(subs);
    sLay->addRow(tr("Font:"), m_fontFamily);
    m_fontSize = new QSpinBox(subs);
    m_fontSize->setRange(8, 400);
    m_fontSize->setSuffix(" px");
    sLay->addRow(tr("Font size:"), m_fontSize);
    m_fontColor = new QPushButton(tr("Pick…"), subs);
    sLay->addRow(tr("Text color:"), m_fontColor);
    m_bgColor = new QPushButton(tr("Pick…"), subs);
    m_bgColor->setToolTip(tr("Includes alpha — drag the alpha slider for a translucent box."));
    sLay->addRow(tr("Background color:"), m_bgColor);
    m_outlineColor = new QPushButton(tr("Pick…"), subs);
    sLay->addRow(tr("Outline color:"), m_outlineColor);
    m_outlineWidth = new QSpinBox(subs);
    m_outlineWidth->setRange(0, 50);
    m_outlineWidth->setSuffix(" px");
    m_outlineWidth->setSpecialValueText(tr("none"));
    sLay->addRow(tr("Outline width:"), m_outlineWidth);
    m_position = new QComboBox(subs);
    m_position->addItem(tr("Top"),    int(SubtitlePosition::Top));
    m_position->addItem(tr("Middle"), int(SubtitlePosition::Middle));
    m_position->addItem(tr("Bottom"), int(SubtitlePosition::Bottom));
    sLay->addRow(tr("Position:"), m_position);
    m_subtitleMargin = new QSpinBox(subs);
    m_subtitleMargin->setRange(0, 1000);
    m_subtitleMargin->setSuffix(" px");
    m_subtitleMargin->setToolTip(tr(
        "Distance from the canvas's top or bottom edge.\n"
        "Ignored when position is Middle."));
    sLay->addRow(tr("Margin:"), m_subtitleMargin);
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
    ivTab->addWidget(subs);

    auto pushSubtitle = [this]() {
        if (m_suspend) return;
        Defaults d = m_mw->project().defaults;
        d.subtitle.fontFamily = m_fontFamily->currentFont().family();
        d.subtitle.fontSizePx = m_fontSize->value();
        d.subtitle.outlineWidthPx = m_outlineWidth->value();
        d.subtitle.position   = SubtitlePosition(m_position->currentData().toInt());
        d.subtitle.marginPx   = m_subtitleMargin->value();
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
    connect(m_subtitleMargin, qOverload<int>(&QSpinBox::valueChanged), this,
            [pushSubtitle](int) { pushSubtitle(); });
    connect(m_subtitleDuration, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [pushSubtitle](double) { pushSubtitle(); });
    connect(m_outlineWidth, qOverload<int>(&QSpinBox::valueChanged), this,
            [pushSubtitle](int) { pushSubtitle(); });
    connect(m_fontColor, &QPushButton::clicked, this, [this]() {
        Defaults d = m_mw->project().defaults;
        pickColor(m_fontColor, d.subtitle.fontColor, /*alpha=*/false,
                  tr("Subtitle text color"));
        m_mw->setDefaults(d);
    });
    connect(m_bgColor, &QPushButton::clicked, this, [this]() {
        Defaults d = m_mw->project().defaults;
        pickColor(m_bgColor, d.subtitle.bgColor, /*alpha=*/true,
                  tr("Subtitle background color"));
        m_mw->setDefaults(d);
    });
    connect(m_outlineColor, &QPushButton::clicked, this, [this]() {
        Defaults d = m_mw->project().defaults;
        pickColor(m_outlineColor, d.subtitle.outlineColor, /*alpha=*/false,
                  tr("Subtitle outline color"));
        m_mw->setDefaults(d);
    });

    // ----- Text clips tab: settings that only affect text clips -----
    auto* tcTab = addTab(tr("Text clips"));
    auto* tcLay = new QFormLayout;
    m_textClipFont = new QFontComboBox(this);
    tcLay->addRow(tr("Font:"), m_textClipFont);
    m_textClipFontSize = new QSpinBox(this);
    m_textClipFontSize->setRange(8, 400);
    m_textClipFontSize->setSuffix(" px");
    tcLay->addRow(tr("Font size:"), m_textClipFontSize);
    m_textClipFontColor = new QPushButton(tr("Pick…"), this);
    tcLay->addRow(tr("Text color:"), m_textClipFontColor);
    m_textClipOutlineColor = new QPushButton(tr("Pick…"), this);
    tcLay->addRow(tr("Outline color:"), m_textClipOutlineColor);
    m_textClipOutlineWidth = new QSpinBox(this);
    m_textClipOutlineWidth->setRange(0, 50);
    m_textClipOutlineWidth->setSuffix(" px");
    m_textClipOutlineWidth->setSpecialValueText(tr("none"));
    tcLay->addRow(tr("Outline width:"), m_textClipOutlineWidth);
    m_textClipVAlign = new QComboBox(this);
    m_textClipVAlign->addItem(tr("Top"),    int(VerticalAlign::Top));
    m_textClipVAlign->addItem(tr("Middle"), int(VerticalAlign::Middle));
    m_textClipVAlign->addItem(tr("Bottom"), int(VerticalAlign::Bottom));
    tcLay->addRow(tr("Vertical align:"), m_textClipVAlign);
    m_textClipDuration = new QDoubleSpinBox(this);
    m_textClipDuration->setRange(0.1, 600.0);
    m_textClipDuration->setDecimals(2);
    m_textClipDuration->setSuffix(" s");
    tcLay->addRow(tr("Default duration:"), m_textClipDuration);
    tcTab->addLayout(tcLay);
    tcTab->addStretch(1);

    auto pushTextClip = [this]() {
        if (m_suspend) return;
        Defaults d = m_mw->project().defaults;
        d.textClip.fontFamily    = m_textClipFont->currentFont().family();
        d.textClip.fontSizePx    = m_textClipFontSize->value();
        d.textClip.outlineWidthPx = m_textClipOutlineWidth->value();
        d.textClip.verticalAlign = VerticalAlign(m_textClipVAlign->currentData().toInt());
        d.textClip.defaultDuration = m_textClipDuration->value();
        m_mw->setDefaults(d);
    };
    connect(m_textClipFont, &QFontComboBox::currentFontChanged, this,
            [pushTextClip](const QFont&) { pushTextClip(); });
    connect(m_textClipFontSize, qOverload<int>(&QSpinBox::valueChanged), this,
            [pushTextClip](int) { pushTextClip(); });
    connect(m_textClipVAlign, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [pushTextClip](int) { pushTextClip(); });
    connect(m_textClipDuration, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [pushTextClip](double) { pushTextClip(); });
    connect(m_textClipOutlineWidth, qOverload<int>(&QSpinBox::valueChanged), this,
            [pushTextClip](int) { pushTextClip(); });
    connect(m_textClipFontColor, &QPushButton::clicked, this, [this]() {
        Defaults d = m_mw->project().defaults;
        pickColor(m_textClipFontColor, d.textClip.fontColor, /*alpha=*/false,
                  tr("Text-clip text color"));
        m_mw->setDefaults(d);
    });
    connect(m_textClipOutlineColor, &QPushButton::clicked, this, [this]() {
        Defaults d = m_mw->project().defaults;
        pickColor(m_textClipOutlineColor, d.textClip.outlineColor, /*alpha=*/false,
                  tr("Text-clip outline color"));
        m_mw->setDefaults(d);
    });
    // Date stamp (per-item timestamp burned into a corner of the canvas)
    auto* ds = new QGroupBox(tr("Date stamp"), this);
    auto* dsLay = new QFormLayout(ds);
    m_dsActive = new QCheckBox(tr("Burn date/time into image and video clips"), ds);
    m_dsActive->setToolTip(tr(
        "Format: DD.MM.YYYY HH:MM. Each clip shows its own timestamp.\n"
        "Text clips are not stamped."));
    dsLay->addRow(m_dsActive);
    m_dsFont = new QFontComboBox(ds);
    dsLay->addRow(tr("Font:"), m_dsFont);
    m_dsFontSize = new QSpinBox(ds);
    m_dsFontSize->setRange(8, 400);
    m_dsFontSize->setSuffix(" px");
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
    m_dsFormat = new QLineEdit(ds);
    m_dsFormat->setPlaceholderText(QStringLiteral("dd.MM.yyyy HH:mm"));
    m_dsFormat->setToolTip(tr(
        "Qt date/time format pattern. Examples:\n"
        "  dd.MM.yyyy HH:mm   (default)\n"
        "  HH:mm              (time only)\n"
        "  dd.MM.             (day and month only)\n"
        "Codes: yyyy MM dd HH mm ss — see Qt's QDateTime::toString()."));
    dsLay->addRow(tr("Format:"), m_dsFormat);
    ivTab->addWidget(ds);
    ivTab->addStretch(1);

    auto pushDatestamp = [this]() {
        if (m_suspend) return;
        Defaults d = m_mw->project().defaults;
        d.datestamp.active     = m_dsActive->isChecked();
        d.datestamp.fontFamily = m_dsFont->currentFont().family();
        d.datestamp.fontSizePx = m_dsFontSize->value();
        d.datestamp.corner     = Corner(m_dsCorner->currentData().toInt());
        d.datestamp.marginPx   = m_dsMargin->value();
        d.datestamp.format     = m_dsFormat->text();
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
    connect(m_dsFormat, &QLineEdit::editingFinished, this, pushDatestamp);

    // ----- Audio tab: project-wide music playlist plus the automatic
    // levelling toggle. Music plays during image and text clips; the
    // playhead pauses at each video clip's start and resumes from the
    // same position at its end.
    auto* bgmTab = addTab(tr("Audio"));

    // Automatic levelling: probe each audio source's loudness at render
    // time and bias every source toward a common target so video and
    // music aren't wildly different in volume.
    m_audioLevellingActive = new QCheckBox(tr("Automatic audio levelling"), this);
    m_audioLevellingActive->setToolTip(tr(
        "When on, each video clip's audio and each music track is measured "
        "(EBU R128 integrated loudness) at render time and biased toward a "
        "common target (-16 LUFS) so they end up roughly equally loud. "
        "Adds a few seconds per source to render time."));
    bgmTab->addWidget(m_audioLevellingActive);
    connect(m_audioLevellingActive, &QCheckBox::toggled, this, [this](bool on) {
        if (m_suspend) return;
        Defaults d = m_mw->project().defaults;
        d.audioLevelling.active = on;
        m_mw->setDefaults(d);
    });

    auto* bgmHelp = new QLabel(tr(
        "Background music plays during image and text clips. The music pauses when a\n"
        "video clip starts and resumes from the same position once the clip ends —\n"
        "crossfades use the same duration as visual transitions."), this);
    bgmHelp->setWordWrap(true);
    bgmTab->addWidget(bgmHelp);

    m_musicList = new QListWidget(this);
    m_musicList->setSelectionMode(QAbstractItemView::SingleSelection);
    bgmTab->addWidget(m_musicList, 1);

    auto* bgmBtns = new QHBoxLayout;
    m_musicAdd    = new QPushButton(tr("Add…"),  this);
    m_musicRemove = new QPushButton(tr("Remove"), this);
    m_musicUp     = new QPushButton(tr("Up"),     this);
    m_musicDown   = new QPushButton(tr("Down"),   this);
    bgmBtns->addWidget(m_musicAdd);
    bgmBtns->addWidget(m_musicRemove);
    bgmBtns->addWidget(m_musicUp);
    bgmBtns->addWidget(m_musicDown);
    bgmBtns->addStretch(1);
    bgmTab->addLayout(bgmBtns);

    auto updateMusicButtons = [this]() {
        const int row = m_musicList->currentRow();
        const int n = m_musicList->count();
        m_musicRemove->setEnabled(row >= 0);
        m_musicUp->setEnabled(row > 0);
        m_musicDown->setEnabled(row >= 0 && row < n - 1);
    };
    connect(m_musicList, &QListWidget::currentRowChanged, this,
            [updateMusicButtons](int) { updateMusicButtons(); });

    connect(m_musicAdd, &QPushButton::clicked, this, [this]() {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, tr("Add background-music tracks"),
            QString(),
            tr("Audio (*.mp3 *.m4a *.aac *.ogg *.opus *.flac *.wav);;All files (*.*)"));
        for (const auto& p : paths) m_mw->addMusicTrack(p);
    });
    connect(m_musicRemove, &QPushButton::clicked, this, [this]() {
        const int row = m_musicList->currentRow();
        if (row >= 0) m_mw->removeMusicTrack(row);
    });
    connect(m_musicUp, &QPushButton::clicked, this, [this]() {
        const int row = m_musicList->currentRow();
        if (row > 0) m_mw->moveMusicTrack(row, row - 1);
    });
    connect(m_musicDown, &QPushButton::clicked, this, [this]() {
        const int row = m_musicList->currentRow();
        if (row >= 0 && row < m_musicList->count() - 1) {
            m_mw->moveMusicTrack(row, row + 1);
        }
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
    m_imageClipDuration->setValue(p.defaults.imageClipDuration);
    m_transition->setValue(p.defaults.transitionSecs);

    if (!p.defaults.subtitle.fontFamily.isEmpty()) {
        m_fontFamily->setCurrentFont(QFont(p.defaults.subtitle.fontFamily));
    }
    m_fontSize->setValue(p.defaults.subtitle.fontSizePx);
    paintSwatch(m_fontColor, p.defaults.subtitle.fontColor);
    paintSwatch(m_bgColor,   p.defaults.subtitle.bgColor);
    paintSwatch(m_outlineColor, p.defaults.subtitle.outlineColor);
    m_outlineWidth->setValue(p.defaults.subtitle.outlineWidthPx);
    int posIdx = m_position->findData(int(p.defaults.subtitle.position));
    if (posIdx >= 0) m_position->setCurrentIndex(posIdx);
    m_subtitleMargin->setValue(p.defaults.subtitle.marginPx);
    m_subtitleDuration->setValue(p.defaults.subtitle.visibleSecs);

    if (!p.defaults.textClip.fontFamily.isEmpty()) {
        m_textClipFont->setCurrentFont(QFont(p.defaults.textClip.fontFamily));
    }
    m_textClipFontSize->setValue(p.defaults.textClip.fontSizePx);
    paintSwatch(m_textClipFontColor, p.defaults.textClip.fontColor);
    paintSwatch(m_textClipOutlineColor, p.defaults.textClip.outlineColor);
    m_textClipOutlineWidth->setValue(p.defaults.textClip.outlineWidthPx);
    int vIdx = m_textClipVAlign->findData(int(p.defaults.textClip.verticalAlign));
    if (vIdx >= 0) m_textClipVAlign->setCurrentIndex(vIdx);
    m_textClipDuration->setValue(p.defaults.textClip.defaultDuration);

    m_dsActive->setChecked(p.defaults.datestamp.active);
    if (!p.defaults.datestamp.fontFamily.isEmpty()) {
        m_dsFont->setCurrentFont(QFont(p.defaults.datestamp.fontFamily));
    }
    m_dsFontSize->setValue(p.defaults.datestamp.fontSizePx);
    int cIdx = m_dsCorner->findData(int(p.defaults.datestamp.corner));
    if (cIdx >= 0) m_dsCorner->setCurrentIndex(cIdx);
    m_dsMargin->setValue(p.defaults.datestamp.marginPx);
    m_dsFormat->setText(p.defaults.datestamp.format);
    {
        QString tz = QString::fromUtf8(p.defaults.timeZone);
        int tzIdx = m_timeZone->findData(tz);
        if (tzIdx >= 0) m_timeZone->setCurrentIndex(tzIdx);
        else m_timeZone->setEditText(tz);
    }

    m_audioLevellingActive->setChecked(p.defaults.audioLevelling.active);

    // Background-music playlist. Preserve the selection across refresh
    // when possible so reorder buttons feel responsive.
    {
        const int prevRow = m_musicList->currentRow();
        QSignalBlocker block(m_musicList);
        m_musicList->clear();
        for (const QString& path : p.backgroundMusic) {
            QString display = QFileInfo(path).fileName();
            if (!QFileInfo::exists(path)) display += tr("  [missing]");
            auto* it = new QListWidgetItem(display);
            it->setToolTip(path);
            m_musicList->addItem(it);
        }
        if (prevRow >= 0 && prevRow < m_musicList->count()) {
            m_musicList->setCurrentRow(prevRow);
        }
    }
    {
        const int row = m_musicList->currentRow();
        const int n = m_musicList->count();
        m_musicRemove->setEnabled(row >= 0);
        m_musicUp->setEnabled(row > 0);
        m_musicDown->setEnabled(row >= 0 && row < n - 1);
    }

    m_suspend = false;
}

void DefaultsPane::pickColor(QPushButton* btn, QColor& target, bool withAlpha,
                             const QString& title) {
    QColorDialog::ColorDialogOptions opts;
    if (withAlpha) opts |= QColorDialog::ShowAlphaChannel;
    QColor c = QColorDialog::getColor(target, this,
                                      title.isEmpty() ? tr("Pick color") : title,
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
