#include "PropertiesPane.h"
#include "MainWindow.h"

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

    // ---- Image group ----
    m_imgGroup = new QGroupBox(tr("Image"), this);
    auto* iLay = new QFormLayout(m_imgGroup);
    m_imgDuration = new QDoubleSpinBox(m_imgGroup);
    m_imgDuration->setDecimals(2);
    m_imgDuration->setRange(0.1, 600.0);
    m_imgDuration->setSuffix(" s");
    iLay->addRow(tr("Duration:"), m_imgDuration);

    auto* cropRow = new QHBoxLayout;
    m_btnCrop = new QPushButton(tr("Crop…"), m_imgGroup);
    m_btnCrop->setToolTip(tr("Open the interactive crop tool  (Ctrl+Shift+C)"));
    m_btnClearCrop = new QPushButton(tr("Clear"), m_imgGroup);
    m_cropLabel = new QLabel(tr("(none)"), m_imgGroup);
    cropRow->addWidget(m_btnCrop);
    cropRow->addWidget(m_btnClearCrop);
    cropRow->addWidget(m_cropLabel, 1);
    iLay->addRow(tr("Crop:"), cropRow);
    m_stack->addWidget(m_imgGroup);

    // ---- Video group ----
    m_vidGroup = new QGroupBox(tr("Video"), this);
    auto* vLay = new QFormLayout(m_vidGroup);
    m_vidStart = new QDoubleSpinBox(m_vidGroup);
    m_vidStart->setDecimals(3);
    m_vidStart->setRange(0.0, 100000.0);
    m_vidStart->setSuffix(" s");
    m_vidEnd = new QDoubleSpinBox(m_vidGroup);
    m_vidEnd->setDecimals(3);
    m_vidEnd->setRange(0.0, 100000.0);
    m_vidEnd->setSuffix(" s");
    m_vidInfo = new QLabel(tr("—"), m_vidGroup);
    vLay->addRow(tr("Source duration:"), m_vidInfo);
    vLay->addRow(tr("Trim start:"), m_vidStart);
    vLay->addRow(tr("Trim end:"), m_vidEnd);
    m_stack->addWidget(m_vidGroup);

    // ---- Text-clip group ----
    m_textGroup = new QGroupBox(tr("Text clip"), this);
    auto* tLay = new QFormLayout(m_textGroup);
    m_textDuration = new QDoubleSpinBox(m_textGroup);
    m_textDuration->setDecimals(2);
    m_textDuration->setRange(0.1, 600.0);
    m_textDuration->setSuffix(" s");
    tLay->addRow(tr("Duration:"), m_textDuration);

    auto* bgRow = new QHBoxLayout;
    m_textBgPath = new QLineEdit(m_textGroup);
    m_textBgPath->setReadOnly(true);
    m_textBgPath->setPlaceholderText(tr("(no background image — solid colour)"));
    m_textBrowseBg = new QPushButton(tr("Browse…"), m_textGroup);
    m_textClearBg  = new QPushButton(tr("Clear"),    m_textGroup);
    bgRow->addWidget(m_textBgPath, 1);
    bgRow->addWidget(m_textBrowseBg);
    bgRow->addWidget(m_textClearBg);
    tLay->addRow(tr("Background:"), bgRow);
    m_stack->addWidget(m_textGroup);

    // ---- Empty placeholder ----
    auto* placeholder = new QLabel(tr("No item selected."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(placeholder);

    connect(m_imgDuration, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        if (m_suspend) return;
        m_mw->setImageDuration(m_id, v);
    });

    connect(m_btnCrop, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        m_mw->beginImageCrop();
    });
    connect(m_btnClearCrop, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        m_mw->setImageCrop(m_id, std::nullopt);
    });

    auto pushTrim = [this]() {
        if (m_suspend) return;
        m_mw->setVideoTrim(m_id, m_vidStart->value(), m_vidEnd->value());
    };
    connect(m_vidStart, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, pushTrim);
    connect(m_vidEnd, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, pushTrim);

    connect(m_textDuration, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        if (m_suspend || m_id.isNull()) return;
        m_mw->setTextClipDuration(m_id, v);
    });
    connect(m_textBrowseBg, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        QString path = QFileDialog::getOpenFileName(
            this, tr("Choose background image"),
            QString(),
            tr("Images (*.jpg *.jpeg *.png *.heic *.heif *.webp *.tif *.tiff *.bmp);;All files (*.*)"));
        if (path.isEmpty()) return;
        m_mw->setTextClipBackground(m_id, path);
    });
    connect(m_textClearBg, &QPushButton::clicked, this, [this]() {
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
        case ItemKind::Image:
            m_stack->setCurrentIndex(0);
            m_imgDuration->setValue(it->image.durationSecs);
            if (it->image.crop) {
                const QRectF& r = *it->image.crop;
                m_cropLabel->setText(QString("x=%1 y=%2 w=%3 h=%4")
                    .arg(r.x(), 0, 'f', 3).arg(r.y(), 0, 'f', 3)
                    .arg(r.width(), 0, 'f', 3).arg(r.height(), 0, 'f', 3));
                m_btnClearCrop->setEnabled(true);
            } else {
                m_cropLabel->setText(tr("(none)"));
                m_btnClearCrop->setEnabled(false);
            }
            break;
        case ItemKind::Video:
            m_stack->setCurrentIndex(1);
            m_vidInfo->setText(QString("%1 s, %2×%3, audio: %4")
                .arg(it->video.sourceDurationSecs, 0, 'f', 2)
                .arg(it->video.sourceWidth).arg(it->video.sourceHeight)
                .arg(it->video.hasAudio ? tr("yes") : tr("no")));
            m_vidStart->setRange(0.0, std::max(0.001, it->video.sourceDurationSecs));
            m_vidEnd->setRange(0.0, std::max(0.001, it->video.sourceDurationSecs));
            m_vidStart->setValue(it->video.startSecs);
            m_vidEnd->setValue(it->video.endSecs > 0 ? it->video.endSecs : it->video.sourceDurationSecs);
            break;
        case ItemKind::TextClip:
            m_stack->setCurrentIndex(2);
            m_textDuration->setValue(it->textClip.durationSecs);
            m_textBgPath->setText(it->textClip.backgroundPath);
            m_textClearBg->setEnabled(!it->textClip.backgroundPath.isEmpty());
            break;
    }
    m_suspend = false;
}

} // namespace vlip
