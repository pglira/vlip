#include "PropertiesPane.hpp"
#include "MainWindow.hpp"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QFileDialog>
#include <QFileInfo>

namespace vlip {

PropertiesPane::PropertiesPane(MainWindow* mw, QWidget* parent)
    : QWidget(parent), m_mw(mw) {
    buildUi();
    refresh();
}

void PropertiesPane::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);

    // Common section: filename + timestamp (read-only).
    // Subtitle lives on the Preview pane, not here.
    auto* commonForm = new QFormLayout;
    m_filename = new QLabel("—", this);
    m_filename->setWordWrap(true);
    m_timestamp = new QLabel("—", this);

    commonForm->addRow(tr("File:"), m_filename);
    commonForm->addRow(tr("Timestamp:"), m_timestamp);
    outer->addLayout(commonForm);

    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack, 1);

    // ---- Image-clip group ----
    m_imageClipGroup = new QGroupBox(tr("Image clip"), this);
    auto* iLay = new QFormLayout(m_imageClipGroup);
    m_imageClipDuration = new QDoubleSpinBox(m_imageClipGroup);
    m_imageClipDuration->setDecimals(2);
    m_imageClipDuration->setRange(0.1, 600.0);
    m_imageClipDuration->setSuffix(" s");
    iLay->addRow(tr("Duration:"), m_imageClipDuration);

    auto* cropRow = new QHBoxLayout;
    m_btnCrop = new QPushButton(tr("Crop…"), m_imageClipGroup);
    m_btnCrop->setToolTip(tr("Open the interactive crop tool  (Ctrl+Shift+C)"));
    m_btnClearCrop = new QPushButton(tr("Clear"), m_imageClipGroup);
    m_cropLabel = new QLabel(tr("(none)"), m_imageClipGroup);
    cropRow->addWidget(m_btnCrop);
    cropRow->addWidget(m_btnClearCrop);
    cropRow->addWidget(m_cropLabel, 1);
    iLay->addRow(tr("Crop:"), cropRow);
    m_stack->addWidget(m_imageClipGroup);

    // ---- Video-clip group ----
    m_videoClipGroup = new QGroupBox(tr("Video clip"), this);
    auto* vLay = new QFormLayout(m_videoClipGroup);
    m_videoClipStart = new QDoubleSpinBox(m_videoClipGroup);
    m_videoClipStart->setDecimals(3);
    m_videoClipStart->setRange(0.0, 100000.0);
    m_videoClipStart->setSuffix(" s");
    m_videoClipEnd = new QDoubleSpinBox(m_videoClipGroup);
    m_videoClipEnd->setDecimals(3);
    m_videoClipEnd->setRange(0.0, 100000.0);
    m_videoClipEnd->setSuffix(" s");
    m_videoClipInfo = new QLabel(tr("—"), m_videoClipGroup);
    vLay->addRow(tr("Source duration:"), m_videoClipInfo);
    vLay->addRow(tr("Trim start:"), m_videoClipStart);
    vLay->addRow(tr("Trim end:"), m_videoClipEnd);
    m_stack->addWidget(m_videoClipGroup);

    // ---- Text-clip group ----
    m_textClipGroup = new QGroupBox(tr("Text clip"), this);
    auto* tLay = new QFormLayout(m_textClipGroup);
    m_textClipDuration = new QDoubleSpinBox(m_textClipGroup);
    m_textClipDuration->setDecimals(2);
    m_textClipDuration->setRange(0.1, 600.0);
    m_textClipDuration->setSuffix(" s");
    tLay->addRow(tr("Duration:"), m_textClipDuration);

    auto* bgRow = new QHBoxLayout;
    m_textClipBgPath = new QLineEdit(m_textClipGroup);
    m_textClipBgPath->setReadOnly(true);
    m_textClipBgPath->setPlaceholderText(tr("(no background image — solid colour)"));
    m_textClipBrowseBg = new QPushButton(tr("Browse…"), m_textClipGroup);
    m_textClipClearBg  = new QPushButton(tr("Clear"),    m_textClipGroup);
    bgRow->addWidget(m_textClipBgPath, 1);
    bgRow->addWidget(m_textClipBrowseBg);
    bgRow->addWidget(m_textClipClearBg);
    tLay->addRow(tr("Background:"), bgRow);
    m_stack->addWidget(m_textClipGroup);

    // ---- Empty placeholder ----
    auto* placeholder = new QLabel(tr("No item selected."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(placeholder);

    connect(m_imageClipDuration, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        if (m_suspend) return;
        m_mw->setImageClipDuration(m_id, v);
    });

    connect(m_btnCrop, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        m_mw->beginImageClipCrop();
    });
    connect(m_btnClearCrop, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        m_mw->setImageClipCrop(m_id, std::nullopt);
    });

    auto pushTrim = [this]() {
        if (m_suspend) return;
        m_mw->setVideoClipTrim(m_id, m_videoClipStart->value(), m_videoClipEnd->value());
    };
    connect(m_videoClipStart, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, pushTrim);
    connect(m_videoClipEnd, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, pushTrim);

    connect(m_textClipDuration, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        if (m_suspend || m_id.isNull()) return;
        m_mw->setTextClipDuration(m_id, v);
    });
    connect(m_textClipBrowseBg, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        QString path = QFileDialog::getOpenFileName(
            this, tr("Choose background image"),
            QString(),
            tr("Images (*.jpg *.jpeg *.png *.heic *.heif *.webp *.tif *.tiff *.bmp);;All files (*.*)"));
        if (path.isEmpty()) return;
        m_mw->setTextClipBackground(m_id, path);
    });
    connect(m_textClipClearBg, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        m_mw->setTextClipBackground(m_id, QString());
    });
}

Item* PropertiesPane::current() {
    int idx = m_mw->project().indexOfId(m_id);
    if (idx < 0) return nullptr;
    return const_cast<Item*>(&m_mw->project().items[idx]);
}

void PropertiesPane::onSelectionChanged(const QUuid& id) {
    m_id = id;
    refresh();
}

void PropertiesPane::onItemChanged(const QUuid& id) {
    if (id == m_id) refresh();
}

void PropertiesPane::refresh() {
    m_suspend = true;
    Item* it = current();
    if (!it) {
        m_filename->setText("—");
        m_timestamp->setText("—");
        m_stack->setCurrentIndex(3);
        m_suspend = false;
        return;
    }
    if (it->kind == ItemKind::TextClip) {
        m_filename->setText(tr("(text clip)"));
    } else {
        m_filename->setText(QFileInfo(it->common().sourcePath).fileName()
                            + (it->common().sourceMissing ? "  [missing]" : ""));
    }
    QString tsText = it->common().timestamp.toString("yyyy-MM-dd HH:mm:ss");
    if (it->common().timestampUncertain) tsText += "  ?";
    m_timestamp->setText(tsText);

    switch (it->kind) {
        case ItemKind::ImageClip:
            m_stack->setCurrentIndex(0);
            m_imageClipDuration->setValue(it->imageClip.durationSecs);
            if (it->imageClip.crop) {
                const QRectF& r = *it->imageClip.crop;
                m_cropLabel->setText(QString("x=%1 y=%2 w=%3 h=%4")
                    .arg(r.x(), 0, 'f', 3).arg(r.y(), 0, 'f', 3)
                    .arg(r.width(), 0, 'f', 3).arg(r.height(), 0, 'f', 3));
                m_btnClearCrop->setEnabled(true);
            } else {
                m_cropLabel->setText(tr("(none)"));
                m_btnClearCrop->setEnabled(false);
            }
            break;
        case ItemKind::VideoClip:
            m_stack->setCurrentIndex(1);
            m_videoClipInfo->setText(QString("%1 s, %2×%3, audio: %4")
                .arg(it->videoClip.sourceDurationSecs, 0, 'f', 2)
                .arg(it->videoClip.sourceWidth).arg(it->videoClip.sourceHeight)
                .arg(it->videoClip.hasAudio ? tr("yes") : tr("no")));
            m_videoClipStart->setRange(0.0, std::max(0.001, it->videoClip.sourceDurationSecs));
            m_videoClipEnd->setRange(0.0, std::max(0.001, it->videoClip.sourceDurationSecs));
            m_videoClipStart->setValue(it->videoClip.startSecs);
            m_videoClipEnd->setValue(it->videoClip.endSecs > 0 ? it->videoClip.endSecs : it->videoClip.sourceDurationSecs);
            break;
        case ItemKind::TextClip:
            m_stack->setCurrentIndex(2);
            m_textClipDuration->setValue(it->textClip.durationSecs);
            m_textClipBgPath->setText(it->textClip.backgroundPath);
            m_textClipClearBg->setEnabled(!it->textClip.backgroundPath.isEmpty());
            break;
    }
    m_suspend = false;
}

} // namespace vlip
