#include "PreviewPane.hpp"
#include "MainWindow.hpp"
#include "ImageClipPreviewWidget.hpp"
#include "VideoClipPreviewWidget.hpp"
#include "TextClipPreviewWidget.hpp"
#include "Project.hpp"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QFrame>

namespace vlip {

PreviewPane::PreviewPane(MainWindow* mw, QWidget* parent)
    : QWidget(parent), m_mw(mw) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // The preview stack lives inside a QFrame whose border colour reflects
    // the selected item's "used" state — green when used, dim grey when
    // unused, faint red when the source file is missing. The frame's
    // padding doubles as the visible border thickness.
    m_previewFrame = new QFrame(this);
    m_previewFrame->setObjectName("PreviewFrame");
    auto* frameLay = new QVBoxLayout(m_previewFrame);
    frameLay->setContentsMargins(0, 0, 0, 0);
    frameLay->setSpacing(0);
    v->addWidget(m_previewFrame, 1);

    m_stack = new QStackedWidget(m_previewFrame);
    frameLay->addWidget(m_stack);

    m_imageClip = new ImageClipPreviewWidget(this);
    m_videoClip = new VideoClipPreviewWidget(mw, this);
    m_textClip  = new TextClipPreviewWidget(this);
    auto* placeholder = new QLabel(tr("(select an item)"), this);
    placeholder->setAlignment(Qt::AlignCenter);

    m_stack->addWidget(m_imageClip);   // 0
    m_stack->addWidget(m_videoClip);   // 1
    m_stack->addWidget(m_textClip);    // 2
    m_stack->addWidget(placeholder);   // 3

    m_textInput = new QLineEdit(this);
    m_textInput->setPlaceholderText(tr("Subtitle"));
    m_textInput->setAlignment(Qt::AlignCenter);
    m_textInput->setEnabled(false);
    v->addWidget(m_textInput);

    // Pressing Enter commits and drops focus back to the preview pane,
    // matching the user's expectation of "I'm done editing".
    connect(m_textInput, &QLineEdit::returnPressed, m_textInput, &QWidget::clearFocus);
    connect(m_textInput, &QLineEdit::editingFinished, this, [this]() {
        if (m_suspend || m_id.isNull()) return;
        if (m_isTextClip) m_mw->setTextClipText(m_id, m_textInput->text());
        else              m_mw->setSubtitle   (m_id, m_textInput->text());
    });

    connect(m_imageClip, &ImageClipPreviewWidget::cropApplied, this,
        [this](const std::optional<QRectF>& rect) {
            if (m_id.isNull()) return;
            m_mw->setImageClipCrop(m_id, rect);
        });

    refresh();
}

void PreviewPane::focusSubtitleEditor() {
    if (!m_textInput || !m_textInput->isEnabled()) return;
    m_textInput->setFocus(Qt::ShortcutFocusReason);
    m_textInput->selectAll();
}

void PreviewPane::beginImageClipCrop() {
    if (m_id.isNull()) return;
    int idx = m_mw->project().indexOfId(m_id);
    if (idx < 0) return;
    const Item& it = m_mw->project().items[idx];
    if (it.kind != ItemKind::ImageClip) return;
    if (it.common().sourceMissing) return;
    m_stack->setCurrentIndex(0);    // make sure image-clip preview is visible
    m_imageClip->setCrop(it.imageClip.crop);
    m_imageClip->enterCropMode();
}

void PreviewPane::onSelectionChanged(const QUuid& id) {
    m_id = id;
    refresh();
}

void PreviewPane::onItemChanged(const QUuid& id) {
    if (id == m_id) refresh();
}

void PreviewPane::updateUsedFrame(bool present, bool used, bool missing) {
    // Stylesheet on object-name selector so it doesn't bleed onto child
    // widgets. 3 px border so it's noticeable peripherally without
    // crowding the preview area.
    QString colour;
    if (!present)      colour = "#3a3a3a";       // nothing selected
    else if (missing)  colour = "#c34a4a";       // source missing
    else if (used)     colour = "#5dbb63";       // green = will be rendered
    else               colour = "#7a7a7a";       // grey = skipped
    m_previewFrame->setStyleSheet(
        QString("QFrame#PreviewFrame { border: 3px solid %1; }").arg(colour));
}

void PreviewPane::refresh() {
    m_imageClip->setProjectCanvas(m_mw->project().canvas.width,
                                  m_mw->project().canvas.height);
    int idx = m_mw->project().indexOfId(m_id);
    m_suspend = true;
    if (idx < 0) {
        m_imageClip->clear();
        m_videoClip->clear();
        m_textClip->clear();
        m_isTextClip = false;
        m_textInput->clear();
        m_textInput->setPlaceholderText(tr("Subtitle"));
        m_textInput->setEnabled(false);
        m_stack->setCurrentIndex(3);
        updateUsedFrame(false, false, false);
        m_suspend = false;
        return;
    }
    const Item& it = m_mw->project().items[idx];
    const bool missing = (it.kind != ItemKind::TextClip) && it.common().sourceMissing;
    updateUsedFrame(true, it.common().used, missing);

    // The line edit doubles as: subtitle editor for image / video clips;
    // text editor for text clips. Choose binding based on selected kind.
    m_isTextClip = (it.kind == ItemKind::TextClip);
    if (m_isTextClip) {
        m_textInput->setPlaceholderText(tr("Text"));
        m_textInput->setText(it.textClip.text);
        m_textInput->setEnabled(true);
    } else {
        m_textInput->setPlaceholderText(tr("Subtitle"));
        m_textInput->setText(it.common().subtitle);
        m_textInput->setEnabled(!it.common().sourceMissing);
    }

    if (missing) {
        m_imageClip->clear();
        m_videoClip->clear();
        m_textClip->clear();
        m_stack->setCurrentIndex(3);
        m_suspend = false;
        return;
    }

    switch (it.kind) {
        case ItemKind::ImageClip:
            m_videoClip->clear();
            m_textClip->clear();
            m_imageClip->setImage(it.common().sourcePath);
            m_imageClip->setCrop(it.imageClip.crop);
            m_stack->setCurrentIndex(0);
            break;
        case ItemKind::VideoClip:
            m_imageClip->clear();
            m_textClip->clear();
            m_videoClip->setItem(it.common().id);
            m_stack->setCurrentIndex(1);
            break;
        case ItemKind::TextClip:
            m_imageClip->clear();
            m_videoClip->clear();
            m_textClip->setData(it.textClip.text,
                                it.textClip.backgroundPath,
                                m_mw->project().defaults.textClip,
                                m_mw->project().canvas.width,
                                m_mw->project().canvas.height);
            m_stack->setCurrentIndex(2);
            break;
    }
    m_suspend = false;
}

} // namespace vlip
