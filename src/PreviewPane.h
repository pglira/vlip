#pragma once

#include <QWidget>
#include <QUuid>

class QStackedWidget;
class QLineEdit;

namespace vlip {

class MainWindow;
class ImagePreviewWidget;
class VideoPreviewWidget;

class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(MainWindow* mw, QWidget* parent = nullptr);

public slots:
    void onSelectionChanged(const QUuid& id);
    void refresh();

private:
    MainWindow* m_mw;
    QUuid m_id;
    QLineEdit* m_subtitle;
    QStackedWidget* m_stack;
    ImagePreviewWidget* m_image;
    VideoPreviewWidget* m_video;
    bool m_suspend = false;
};

} // namespace vlip
