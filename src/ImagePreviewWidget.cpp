#include "ImagePreviewWidget.h"
#include "CropOverlay.h"

#ifdef VLIP_HAS_HEIF
#include <libheif/heif_cxx.h>
#endif

#include <QPainter>
#include <QFileInfo>
#include <QImageReader>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
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

ImagePreviewWidget::ImagePreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 150);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor(20, 20, 20));
    setPalette(p);

    // Keyboard shortcut: Ctrl+Shift+C opens crop mode while the image
    // preview is visible. Modifier-letter combo so it doesn't collide with
    // Ctrl+C (copy) or 'c' typed into a focused QLineEdit.
    auto* sc = new QShortcut(QKeySequence(tr("Ctrl+Shift+C")), this);
    sc->setContext(Qt::WindowShortcut);
    connect(sc, &QShortcut::activated, this, [this]() {
        if (!isVisible() || m_orig.isNull() || m_cropping) return;
        enterCropMode();
    });
}

void ImagePreviewWidget::setImage(const QString& path) {
    if (path == m_path) return;
    m_path = path;
    m_orig = loadImage(path);
    if (m_cropping) exitCropMode();
    update();
}

void ImagePreviewWidget::setCrop(const std::optional<QRectF>& crop) {
    m_crop = crop;
    update();
}

void ImagePreviewWidget::setProjectCanvas(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (w == m_projectW && h == m_projectH) return;
    m_projectW = w;
    m_projectH = h;
    if (m_aspect) {
        m_aspect->setItemText(1, tr("Project (%1×%2)").arg(w).arg(h));
    }
    if (m_overlay && m_cropping) {
        m_overlay->setLockedAspect(double(w) / double(h));
    }
}

void ImagePreviewWidget::clear() {
    m_path.clear();
    m_orig = QImage();
    m_crop.reset();
    if (m_cropping) exitCropMode();
    update();
}

QRect ImagePreviewWidget::imagePaintRect() const {
    if (m_orig.isNull()) return QRect();
    // Crop mode shows the *full* image so the user can pick anywhere;
    // outside crop mode the persisted crop drives what's visible.
    QSize src = (m_crop && !m_cropping)
        ? QSize(std::max(1, int(std::round(m_crop->width()  * m_orig.width()))),
                std::max(1, int(std::round(m_crop->height() * m_orig.height()))))
        : m_orig.size();
    QSize target = src.scaled(size(), Qt::KeepAspectRatio);
    QPoint topLeft((width() - target.width()) / 2, (height() - target.height()) / 2);
    return QRect(topLeft, target);
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
    QRect dst = imagePaintRect();
    if (m_crop && !m_cropping) {
        QRectF c = *m_crop;
        QRect srcRect(int(std::round(c.x() * m_orig.width())),
                      int(std::round(c.y() * m_orig.height())),
                      std::max(1, int(std::round(c.width()  * m_orig.width()))),
                      std::max(1, int(std::round(c.height() * m_orig.height()))));
        p.drawImage(dst, m_orig, srcRect);
    } else {
        p.drawImage(dst, m_orig);
    }
}

void ImagePreviewWidget::resizeEvent(QResizeEvent*) {
    if (m_cropping) positionCropUi();
}

void ImagePreviewWidget::buildCropUi() {
    if (m_overlay) return;
    m_overlay = new CropOverlay(this);
    m_overlay->hide();

    m_cropBar = new QFrame(this);
    m_cropBar->setObjectName("CropBar");
    m_cropBar->setFrameShape(QFrame::StyledPanel);
    m_cropBar->setStyleSheet(
        "QFrame#CropBar { background: rgba(25,25,25,220); border: 1px solid #555;"
        " border-radius: 4px; }"
        "QFrame#CropBar QLabel, QFrame#CropBar QComboBox, QFrame#CropBar QPushButton { color: white; }"
    );
    auto* lay = new QHBoxLayout(m_cropBar);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(6);

    auto* lbl = new QLabel(tr("Aspect:"), m_cropBar);
    m_aspect = new QComboBox(m_cropBar);
    m_aspect->addItem(tr("Free"),    int(CropOverlay::AspectMode::Free));
    m_aspect->addItem(tr("Project"), int(CropOverlay::AspectMode::Locked));
    m_btnApply  = new QPushButton(tr("Apply"),  m_cropBar);
    m_btnReset  = new QPushButton(tr("Reset"),  m_cropBar);
    m_btnCancel = new QPushButton(tr("Cancel"), m_cropBar);
    m_btnApply->setDefault(true);

    lay->addWidget(lbl);
    lay->addWidget(m_aspect);
    lay->addStretch(1);
    lay->addWidget(m_btnApply);
    lay->addWidget(m_btnReset);
    lay->addWidget(m_btnCancel);
    m_cropBar->hide();

    connect(m_aspect, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int idx) {
            if (!m_cropping) return;
            m_overlay->setAspectMode(CropOverlay::AspectMode(m_aspect->itemData(idx).toInt()));
        });
    connect(m_btnApply, &QPushButton::clicked, this, [this]() {
        QRectF n = m_overlay->cropNormalized();
        std::optional<QRectF> result;
        // Treat "full image" as no crop. Allow tiny float drift.
        const double eps = 1e-4;
        bool isFull = n.x() < eps && n.y() < eps
                   && std::abs(n.width()  - 1.0) < eps
                   && std::abs(n.height() - 1.0) < eps;
        if (!isFull) result = n;
        emit cropApplied(result);
        exitCropMode();
    });
    connect(m_btnReset, &QPushButton::clicked, this, [this]() {
        emit cropApplied(std::nullopt);
        exitCropMode();
    });
    connect(m_btnCancel, &QPushButton::clicked, this, [this]() {
        exitCropMode();
    });
}

void ImagePreviewWidget::enterCropMode() {
    if (m_orig.isNull()) return;
    buildCropUi();

    // Order matters: switch into crop mode FIRST so imagePaintRect()
    // reports the full-image rect (not the cropped sub-rect), then
    // size the overlay against that, then push the saved crop.
    m_cropping = true;
    update();
    positionCropUi();
    {
        QSignalBlocker block(m_aspect);
        m_aspect->setCurrentIndex(0);
        m_aspect->setItemText(1, tr("Project (%1×%2)").arg(m_projectW).arg(m_projectH));
    }
    m_overlay->setLockedAspect(double(m_projectW) / double(m_projectH));
    m_overlay->setAspectMode(CropOverlay::AspectMode::Free);
    m_overlay->setCropNormalized(m_crop);
    m_overlay->show();
    m_overlay->raise();
    m_overlay->setFocus(Qt::MouseFocusReason);
    m_cropBar->show();
    m_cropBar->raise();
}

void ImagePreviewWidget::exitCropMode() {
    if (!m_cropping) return;
    m_cropping = false;
    if (m_overlay) m_overlay->hide();
    if (m_cropBar) m_cropBar->hide();
    update();
    emit cropModeExited();
}

void ImagePreviewWidget::positionCropUi() {
    if (!m_overlay) return;
    m_overlay->setImageRect(imagePaintRect());
    QSize hint = m_cropBar->sizeHint();
    int w = std::min(hint.width(), std::max(120, width() - 16));
    int x = (width() - w) / 2;
    m_cropBar->setGeometry(x, 6, w, hint.height());
}

} // namespace vlip
