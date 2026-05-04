#include "PreviewPane.hpp"
#include "MainWindow.hpp"
#include "ImageClipPreviewWidget.hpp"
#include "VideoClipPreviewWidget.hpp"
#include "TextClipPreviewWidget.hpp"
#include "TextOverlayRenderer.hpp"
#include "DrawtextFormulas.hpp"
#include "Project.hpp"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QFrame>
#include <QSplitter>
#include <QSettings>

namespace vlip {

PreviewPane::PreviewPane(MainWindow* mw, QWidget* parent)
    : QWidget(parent), m_mw(mw) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    v->addWidget(m_splitter);

    // The preview stack lives inside a QFrame whose border colour reflects
    // the selected item's "used" state — green when used, dim grey when
    // unused, faint red when the source file is missing. The frame's
    // padding doubles as the visible border thickness.
    m_previewFrame = new QFrame;
    m_previewFrame->setObjectName("PreviewFrame");
    auto* frameLay = new QVBoxLayout(m_previewFrame);
    frameLay->setContentsMargins(0, 0, 0, 0);
    frameLay->setSpacing(0);
    m_splitter->addWidget(m_previewFrame);

    m_stack = new QStackedWidget(m_previewFrame);
    frameLay->addWidget(m_stack);

    m_overlayRenderer = new TextOverlayRenderer(this);
    m_imageClip = new ImageClipPreviewWidget(m_overlayRenderer, this);
    m_videoClip = new VideoClipPreviewWidget(mw, m_overlayRenderer, this);
    m_textClip  = new TextClipPreviewWidget(m_overlayRenderer, this);
    auto* placeholder = new QLabel(tr("(select an item)"), this);
    placeholder->setAlignment(Qt::AlignCenter);

    m_stack->addWidget(m_imageClip);   // 0
    m_stack->addWidget(m_videoClip);   // 1
    m_stack->addWidget(m_textClip);    // 2
    m_stack->addWidget(placeholder);   // 3

    m_textInput = new QPlainTextEdit;
    m_textInput->setPlaceholderText(tr("Subtitle  (Enter to commit, Shift+Enter for new line)"));
    m_textInput->setEnabled(false);
    // Minimum height ~ one line so the user can shrink it but never collapse
    // it. Default split puts it at ~3 lines tall.
    {
        const int line = m_textInput->fontMetrics().lineSpacing();
        m_textInput->setMinimumHeight(line + 10);
        m_splitter->addWidget(m_textInput);
        m_splitter->setStretchFactor(0, 1);  // preview frame absorbs window growth
        m_splitter->setStretchFactor(1, 0);  // editor keeps its size
        m_splitter->setSizes({1000, line * 3 + 10});
    }
    m_textInput->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_textInput->setTabChangesFocus(true);
    m_textInput->installEventFilter(this);

    // Persist the user's chosen split between the preview and the
    // editor across sessions.
    {
        QSettings s("vlip", "vlip");
        const auto state = s.value("preview/splitterState").toByteArray();
        if (!state.isEmpty()) m_splitter->restoreState(state);
    }
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        QSettings("vlip", "vlip").setValue("preview/splitterState",
                                           m_splitter->saveState());
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

void PreviewPane::commitTextInput() {
    if (m_suspend || m_id.isNull() || !m_textInput) return;
    const QString text = m_textInput->toPlainText();
    if (m_isTextClip) m_mw->setTextClipText(m_id, text);
    else              m_mw->setSubtitle   (m_id, text);
}

bool PreviewPane::eventFilter(QObject* obj, QEvent* e) {
    if (obj == m_textInput) {
        if (e->type() == QEvent::FocusOut) {
            commitTextInput();
        } else if (e->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(e);
            const bool isEnter = (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter);
            if (isEnter && !ke->modifiers().testFlag(Qt::ShiftModifier)) {
                m_textInput->clearFocus();   // triggers FocusOut → commitTextInput()
                return true;                 // swallow so no newline is inserted
            }
            // Shift+Enter falls through to QPlainTextEdit's default
            // handling, which inserts a newline.
        }
    }
    return QWidget::eventFilter(obj, e);
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
    m_videoClip->setProjectCanvas(m_mw->project().canvas.width,
                                  m_mw->project().canvas.height);
    int idx = m_mw->project().indexOfId(m_id);
    m_suspend = true;
    if (idx < 0) {
        m_imageClip->clear();
        m_videoClip->clear();
        m_textClip->clear();
        m_isTextClip = false;
        m_textInput->clear();
        m_textInput->setPlaceholderText(tr("Subtitle  (Enter to commit, Shift+Enter for new line)"));
        m_textInput->setEnabled(false);
        m_stack->setCurrentIndex(3);
        updateUsedFrame(false, false, false);
        m_suspend = false;
        return;
    }
    const Item& it = m_mw->project().items[idx];
    const bool missing = (it.kind != ItemKind::TextClip) && it.common().sourceMissing;
    updateUsedFrame(true, it.common().used, missing);

    // The text editor doubles as: subtitle editor for image / video
    // clips; text editor for text clips. Choose binding based on
    // selected kind. Enter (or focus loss) commits the contents to
    // the model; Shift+Enter inserts a newline.
    m_isTextClip = (it.kind == ItemKind::TextClip);
    if (m_isTextClip) {
        m_textInput->setPlaceholderText(tr("Text  (Enter to commit, Shift+Enter for new line)"));
        m_textInput->setPlainText(it.textClip.text);
        m_textInput->setEnabled(true);
    } else {
        m_textInput->setPlaceholderText(tr("Subtitle  (Enter to commit, Shift+Enter for new line)"));
        m_textInput->setPlainText(it.common().subtitle);
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

    const QString datestampText = formatDatestamp(it.common().timestamp,
                                                  m_mw->project().defaults.timeZone);
    const auto& datestampStyle = m_mw->project().defaults.datestamp;

    switch (it.kind) {
        case ItemKind::ImageClip:
            m_videoClip->clear();
            m_textClip->clear();
            m_imageClip->setImage(it.common().sourcePath);
            m_imageClip->setCrop(it.imageClip.crop);
            m_imageClip->setSubtitle(it.common().subtitle, m_mw->project().defaults.subtitle);
            m_imageClip->setDatestamp(datestampText, datestampStyle);
            m_stack->setCurrentIndex(0);
            break;
        case ItemKind::VideoClip:
            m_imageClip->clear();
            m_textClip->clear();
            m_videoClip->setItem(it.common().id);
            m_videoClip->setSubtitle(it.common().subtitle, m_mw->project().defaults.subtitle);
            m_videoClip->setDatestamp(datestampText, datestampStyle);
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
