#include "PropertiesPane.h"
#include "MainWindow.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QStackedWidget>
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

    // ---- Empty placeholder ----
    auto* placeholder = new QLabel(tr("No item selected."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(placeholder);

    connect(m_imgDuration, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        if (m_suspend) return;
        m_mw->setImageDuration(m_id, v);
    });

    auto pushTrim = [this]() {
        if (m_suspend) return;
        m_mw->setVideoTrim(m_id, m_vidStart->value(), m_vidEnd->value());
    };
    connect(m_vidStart, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, pushTrim);
    connect(m_vidEnd, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, pushTrim);
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

void PropertiesPane::refresh() {
    m_suspend = true;
    Item* it = current();
    if (!it) {
        m_filename->setText("—");
        m_timestamp->setText("—");
        m_stack->setCurrentIndex(2);
        m_suspend = false;
        return;
    }
    m_filename->setText(QFileInfo(it->common().sourcePath).fileName()
                        + (it->common().sourceMissing ? "  [missing]" : ""));
    QString tsText = it->common().timestamp.toString("yyyy-MM-dd HH:mm:ss");
    if (it->common().timestampUncertain) tsText += "  ?";
    m_timestamp->setText(tsText);

    if (it->kind == ItemKind::Image) {
        m_stack->setCurrentIndex(0);
        m_imgDuration->setValue(it->image.durationSecs);
    } else {
        m_stack->setCurrentIndex(1);
        m_vidInfo->setText(QString("%1 s, %2×%3, audio: %4")
            .arg(it->video.sourceDurationSecs, 0, 'f', 2)
            .arg(it->video.sourceWidth).arg(it->video.sourceHeight)
            .arg(it->video.hasAudio ? tr("yes") : tr("no")));
        m_vidStart->setRange(0.0, std::max(0.001, it->video.sourceDurationSecs));
        m_vidEnd->setRange(0.0, std::max(0.001, it->video.sourceDurationSecs));
        m_vidStart->setValue(it->video.startSecs);
        m_vidEnd->setValue(it->video.endSecs > 0 ? it->video.endSecs : it->video.sourceDurationSecs);
    }
    m_suspend = false;
}

} // namespace vlip
