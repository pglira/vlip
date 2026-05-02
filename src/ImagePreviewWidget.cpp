#include "ImagePreviewWidget.h"

#ifdef VLIP_HAS_HEIF
#include <libheif/heif_cxx.h>
#endif

#include <QPainter>
#include <QFileInfo>
#include <QImageReader>
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
    QImage img = r.read();
    return img;
}

} // namespace

ImagePreviewWidget::ImagePreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 150);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor(20, 20, 20));
    setPalette(p);
}

void ImagePreviewWidget::setImage(const QString& path) {
    if (path == m_path) return;
    m_path = path;
    m_orig = loadImage(path);
    update();
}

void ImagePreviewWidget::clear() {
    m_path.clear();
    m_orig = QImage();
    update();
}

void ImagePreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    if (m_orig.isNull()) {
        p.setPen(Qt::lightGray);
        p.drawText(rect(), Qt::AlignCenter,
            !m_path.isEmpty() ? tr("(unable to decode image)") : tr("(no image)"));
        return;
    }
    QSize target = m_orig.size().scaled(size(), Qt::KeepAspectRatio);
    QPoint topLeft((width() - target.width()) / 2, (height() - target.height()) / 2);
    p.drawImage(QRect(topLeft, target), m_orig);
}

} // namespace vlip
