#pragma once

#include <QWidget>
#include <QUuid>

class QStackedWidget;
class QLineEdit;

namespace vlip {

class MainWindow;
class ImagePreviewWidget;
class VideoPreviewWidget;
class TextClipPreviewWidget;
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

    // Activates the interactive crop tool on the currently selected image.
    void beginImageCrop();

private:
    MainWindow* m_mw;
    QUuid m_id;
    bool m_isTextClip = false;            // governs how m_textInput commits
    QLineEdit* m_textInput;               // dual-purpose: subtitle / textclip text
    QStackedWidget* m_stack;
    ImagePreviewWidget* m_image;
    VideoPreviewWidget* m_video;
    TextClipPreviewWidget* m_text;
    bool m_suspend = false;
};

} // namespace vlip
