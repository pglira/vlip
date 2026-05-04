#pragma once

#include "Project.hpp"
#include <QWidget>
#include <QImage>
#include <QString>

namespace vlip {

// Renders a text-clip preview: background image (scaled-fit, with bars
// in the project's bgColor) or a solid bgColor, then the text overlay
// using the project's text-clip style. Layout matches the renderer
// (horizontally centered, vertical alignment from the project).
class TextClipPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit TextClipPreviewWidget(QWidget* parent = nullptr);

    // Update the on-screen preview. Pass empty backgroundPath for solid bg.
    void setData(const QString& text,
                 const QString& backgroundPath,
                 const TextClipStyle& style,
                 int canvasW, int canvasH);
    void clear();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QImage m_background;
    QString m_loadedBgPath;
    QString m_text;
    TextClipStyle m_style;
    int m_canvasW = 1920;
    int m_canvasH = 1080;
};

} // namespace vlip
