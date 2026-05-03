#include "TextClipPreviewWidget.h"

#ifdef VLIP_HAS_HEIF
#include <libheif/heif_cxx.h>
#endif

#include <QPainter>
#include <QFileInfo>
#include <QImageReader>
#include <QFontMetricsF>
#include <cstring>

namespace vlip {

namespace {

#ifdef VLIP_HAS_HEIF
QImage decodeHeif(const QString& path) {
    try {
        heif::Context ctx;
        ctx.read_from_file(path.toStdString());
        auto handle = ctx.get_primary_image_handle();
        auto img = handle.decode_image(heif_colorspace_RGB, heif_chroma_interleaved_RGBA);
        int w = img.get_width(heif_channel_interleaved);
        int h = img.get_height(heif_channel_interleaved);
        int stride = 0;
        const uint8_t* data = img.get_plane(heif_channel_interleaved, &stride);
        QImage qi(w, h, QImage::Format_RGBA8888);
        for (int y = 0; y < h; ++y) {
            std::memcpy(qi.scanLine(y), data + y * stride, w * 4);
        }
        return qi.convertToFormat(QImage::Format_ARGB32);
    } catch (...) {
        return {};
    }
}
#endif

QImage loadImage(const QString& path) {
    QString suffix = QFileInfo(path).suffix().toLower();
#ifdef VLIP_HAS_HEIF
    if (suffix == "heic" || suffix == "heif") {
        QImage h = decodeHeif(path);
        if (!h.isNull()) return h;
    }
#endif
    QImageReader r(path);
    r.setAutoTransform(true);
    return r.read();
}

} // namespace

TextClipPreviewWidget::TextClipPreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 150);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor(20, 20, 20));
    setPalette(p);
}

void TextClipPreviewWidget::setData(const QString& text,
                                     const QString& backgroundPath,
                                     const TextClipStyle& style,
                                     int canvasW, int canvasH) {
    m_text = text;
    m_style = style;
    m_canvasW = canvasW > 0 ? canvasW : 1920;
    m_canvasH = canvasH > 0 ? canvasH : 1080;

    if (backgroundPath != m_loadedBgPath) {
        m_loadedBgPath = backgroundPath;
        m_background = backgroundPath.isEmpty() ? QImage() : loadImage(backgroundPath);
    }
    update();
}

void TextClipPreviewWidget::clear() {
    m_text.clear();
    m_loadedBgPath.clear();
    m_background = QImage();
    update();
}

void TextClipPreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // Letterbox the canvas inside the widget so the preview keeps the
    // project's aspect ratio.
    QSize canvasFit = QSize(m_canvasW, m_canvasH).scaled(size(), Qt::KeepAspectRatio);
    QRect canvasRect((width() - canvasFit.width()) / 2,
                     (height() - canvasFit.height()) / 2,
                     canvasFit.width(), canvasFit.height());

    p.fillRect(rect(), QColor(20, 20, 20));
    p.fillRect(canvasRect, Qt::black);

    // Background image, if any: scale-fit inside the canvas rect.
    if (!m_background.isNull()) {
        QSize bgFit = m_background.size().scaled(canvasRect.size(), Qt::KeepAspectRatio);
        QRect bgRect(canvasRect.x() + (canvasRect.width()  - bgFit.width())  / 2,
                     canvasRect.y() + (canvasRect.height() - bgFit.height()) / 2,
                     bgFit.width(), bgFit.height());
        p.drawImage(bgRect, m_background);
    }

    if (m_text.isEmpty()) return;

    // Pick a font size: explicit pixel size scaled to the on-screen canvas
    // size, or the renderer's auto-rule (canvasH / 12) likewise scaled.
    double scale = double(canvasRect.height()) / m_canvasH;
    int fontPx = (m_style.fontSizePx > 0)
        ? int(std::round(m_style.fontSizePx * scale))
        : std::max(12, int(std::round((m_canvasH / 12.0) * scale)));

    QFont f;
    if (!m_style.fontFamily.isEmpty()) f.setFamily(m_style.fontFamily);
    f.setPixelSize(std::max(6, fontPx));
    p.setFont(f);
    p.setPen(m_style.fontColor);

    QFontMetricsF fm(f);
    QRectF textRect = fm.boundingRect(QRectF(canvasRect),
                                      Qt::AlignHCenter | Qt::TextWordWrap, m_text);
    double y;
    switch (m_style.verticalAlign) {
        case VerticalAlign::Top:    y = canvasRect.top() + canvasRect.height() / 12.0; break;
        case VerticalAlign::Bottom: y = canvasRect.bottom() - canvasRect.height() / 12.0
                                        - textRect.height(); break;
        case VerticalAlign::Middle:
        default:                    y = canvasRect.center().y() - textRect.height() / 2.0; break;
    }
    QRectF drawRect(canvasRect.left(), y, canvasRect.width(), textRect.height());
    p.drawText(drawRect, Qt::AlignHCenter | Qt::TextWordWrap, m_text);
}

} // namespace vlip
