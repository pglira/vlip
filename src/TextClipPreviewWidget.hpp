#pragma once

#include "Project.hpp"
#include "TextOverlayRenderer.hpp"
#include <QWidget>
#include <QImage>
#include <QString>
#include <QPointer>

namespace vlip {

// Renders a text-clip preview: background image (scaled-fit, with bars
// in the project's bgColor) or a solid bgColor, then the text overlay
// produced by ffmpeg `drawtext` — so the preview matches what ends up
// in the rendered MP4 pixel-for-pixel (modulo letterbox scaling).
class TextClipPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit TextClipPreviewWidget(TextOverlayRenderer* overlayRenderer,
                                   QWidget* parent = nullptr);

    // Update the on-screen preview. Pass empty backgroundPath for solid bg.
    void setData(const QString& text,
                 const QString& backgroundPath,
                 const TextClipStyle& style,
                 int canvasW, int canvasH);
    void clear();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    void requestOverlay();

    QPointer<TextOverlayRenderer> m_overlayRenderer;
    QImage m_background;
    QString m_loadedBgPath;
    QString m_text;
    TextClipStyle m_style;
    int m_canvasW = 1920;
    int m_canvasH = 1080;
    TextOverlayRenderer::Key m_overlayKey;
    QImage m_overlay;
};

} // namespace vlip
