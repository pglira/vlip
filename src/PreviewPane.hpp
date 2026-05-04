#pragma once

#include <QWidget>
#include <QUuid>

class QStackedWidget;
class QPlainTextEdit;
class QFrame;
class QSplitter;

namespace vlip {

class MainWindow;
class ImageClipPreviewWidget;
class VideoClipPreviewWidget;
class TextClipPreviewWidget;
class TextOverlayRenderer;
enum class ItemKind;

class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(MainWindow* mw, QWidget* parent = nullptr);

public slots:
    void onSelectionChanged(const QUuid& id);
    void refresh();
    // Refresh only when the changed item is the one we're showing.
    void onItemChanged(const QUuid& id);

    // Activates the interactive crop tool on the currently selected image clip.
    void beginImageClipCrop();
    // Move keyboard focus to the subtitle / text-clip text editor and
    // pre-select its content for fast overwrite.
    void focusSubtitleEditor();

protected:
    // Catches focus-out and Ctrl+Enter on m_textInput so the editor can
    // accept Enter for newlines while still committing on intent.
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    // Coloured border around the preview stack, signalling whether the
    // displayed item is used (will be in the render) or not.
    void updateUsedFrame(bool present, bool used, bool missing);
    // Push the editor's current contents to the model (subtitle for
    // image / video clips, text for text clips). No-op while a refresh
    // is in flight or nothing is selected.
    void commitTextInput();

    MainWindow* m_mw;
    QUuid m_id;
    bool m_isTextClip = false;            // governs how m_textInput commits
    QPlainTextEdit* m_textInput;          // dual-purpose: subtitle / textclip text
    QSplitter* m_splitter;                // user-resizable preview / editor split
    QFrame* m_previewFrame;               // hosts the stack; styled by used state
    QStackedWidget* m_stack;
    TextOverlayRenderer* m_overlayRenderer;
    ImageClipPreviewWidget* m_imageClip;
    VideoClipPreviewWidget* m_videoClip;
    TextClipPreviewWidget* m_textClip;
    bool m_suspend = false;
};

} // namespace vlip
