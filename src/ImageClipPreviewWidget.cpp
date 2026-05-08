#include "ImageClipPreviewWidget.hpp"
#include "CropOverlay.hpp"
#include "DrawtextFormulas.hpp"

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
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

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

// Largest axis-aligned rectangle of aspect `targetAspect` (= w/h)
// inscribed in a `srcW × srcH` rectangle rotated by `degrees`. Returned
// as a normalized rect over the rotated bbox, so it can be assigned
// straight to the crop overlay. Falls back to the full bbox at angle 0.
QRectF inscribedRectInRotated(double srcW, double srcH,
                              double degrees, double targetAspect) {
    if (srcW <= 0 || srcH <= 0) return QRectF(0, 0, 1, 1);
    const double rad = degrees * M_PI / 180.0;
    const double a = std::abs(std::cos(rad));
    const double b = std::abs(std::sin(rad));
    const double bw = srcW * a + srcH * b;
    const double bh = srcW * b + srcH * a;
    if (bw <= 0 || bh <= 0) return QRectF(0, 0, 1, 1);
    const double r = std::max(1e-6, targetAspect);
    // Centered inscribed rect with half-dimensions (X, Y) where Y = X / r.
    // Constraints from the rotated source's two pairs of parallel edges:
    //   a*X + b*Y <= w/2   and   b*X + a*Y <= h/2
    // Substitute Y = X/r and pick the tighter bound.
    const double xMax = std::min((srcW * 0.5) / (a + b / r),
                                 (srcH * 0.5) / (b + a / r));
    const double yMax = xMax / r;
    const double nx = std::max(0.0, 0.5 - xMax / bw);
    const double ny = std::max(0.0, 0.5 - yMax / bh);
    const double nw = std::min(1.0 - nx, 2.0 * xMax / bw);
    const double nh = std::min(1.0 - ny, 2.0 * yMax / bh);
    return QRectF(nx, ny, nw, nh);
}

// Render `src` rotated by `degrees` (clockwise) into a fresh QImage sized
// to its bounding box, with corners filled black so the crop overlay's
// reference rectangle stays opaque. Uses smooth (bilinear) sampling to
// match ffmpeg's `rotate` filter at default quality.
QImage rotateIntoBbox(const QImage& src, double degrees) {
    const double rad = degrees * M_PI / 180.0;
    const double a = std::abs(std::cos(rad));
    const double b = std::abs(std::sin(rad));
    const int bw = std::max(1, int(std::ceil(src.width() * a + src.height() * b)));
    const int bh = std::max(1, int(std::ceil(src.width() * b + src.height() * a)));
    QImage out(bw, bh, QImage::Format_ARGB32);
    out.fill(Qt::black);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.translate(bw * 0.5, bh * 0.5);
    p.rotate(degrees);
    p.translate(-src.width() * 0.5, -src.height() * 0.5);
    p.drawImage(0, 0, src);
    return out;
}

} // namespace

ImageClipPreviewWidget::ImageClipPreviewWidget(TextOverlayRenderer* overlayRenderer,
                                               QWidget* parent)
    : QWidget(parent), m_overlayRenderer(overlayRenderer) {
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
    // While crop mode is open: Enter applies, Esc cancels. Disabled when
    // not cropping — and a *disabled* QShortcut doesn't consume the key
    // event, so Enter / Return / Esc reach focused QLineEdits and
    // QSpinBoxes elsewhere in the window. (A guard inside the slot
    // wouldn't help: QShortcut consumes the key whenever the shortcut
    // is enabled, regardless of what the slot does.)
    auto bindCropKey = [this](Qt::Key key, std::function<void()> action) {
        auto* shortcut = new QShortcut(QKeySequence(key), this);
        shortcut->setContext(Qt::WindowShortcut);
        shortcut->setEnabled(false);
        connect(shortcut, &QShortcut::activated, this, action);
        m_cropShortcuts.append(shortcut);
    };
    bindCropKey(Qt::Key_Return, [this]() { if (m_btnApply) m_btnApply->click(); });
    bindCropKey(Qt::Key_Enter,  [this]() { if (m_btnApply) m_btnApply->click(); });
    bindCropKey(Qt::Key_Escape, [this]() { exitCropMode(); });

    if (m_overlayRenderer) {
        connect(m_overlayRenderer, &TextOverlayRenderer::overlayReady, this,
                [this](const TextOverlayRenderer::Key& k) {
            if (!m_overlayRenderer) return;
            if (k == m_subtitleKey) {
                m_subtitleOverlay = m_overlayRenderer->getOrRequest(m_subtitleKey);
                update();
            } else if (k == m_datestampKey) {
                m_datestampOverlay = m_overlayRenderer->getOrRequest(m_datestampKey);
                update();
            }
        });
    }
}

void ImageClipPreviewWidget::setImage(const QString& path) {
    if (path == m_path) return;
    m_path = path;
    m_orig = loadImage(path);
    invalidateRotatedCache();
    if (m_cropping) exitCropMode();
    update();
}

void ImageClipPreviewWidget::setCrop(const std::optional<QRectF>& crop) {
    m_crop = crop;
    update();
}

void ImageClipPreviewWidget::setRotation(double degrees) {
    if (std::abs(degrees - m_rotationDegrees) < 1e-6) return;
    m_rotationDegrees = degrees;
    if (!m_cropping) invalidateRotatedCache();
    update();
}

double ImageClipPreviewWidget::activeRotation() const {
    return m_cropping ? m_stagedRotation : m_rotationDegrees;
}

void ImageClipPreviewWidget::invalidateRotatedCache() {
    m_rotated = QImage();
    m_rotatedAngle = 0.0;
    m_rotatedSourceKey = 0;
}

const QImage& ImageClipPreviewWidget::effectiveImage() const {
    if (m_orig.isNull()) return m_orig;
    const double deg = activeRotation();
    if (std::abs(deg) < 1e-4) return m_orig;
    if (!m_rotated.isNull()
        && m_rotatedSourceKey == m_orig.cacheKey()
        && std::abs(m_rotatedAngle - deg) < 1e-6) {
        return m_rotated;
    }
    m_rotated = rotateIntoBbox(m_orig, deg);
    m_rotatedAngle = deg;
    m_rotatedSourceKey = m_orig.cacheKey();
    return m_rotated;
}

void ImageClipPreviewWidget::setProjectCanvas(int w, int h) {
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
    requestSubtitleOverlay();
    requestDatestampOverlay();
    update();
}

void ImageClipPreviewWidget::clear() {
    m_path.clear();
    m_orig = QImage();
    m_crop.reset();
    m_rotationDegrees = 0.0;
    m_stagedRotation = 0.0;
    invalidateRotatedCache();
    m_subtitleText.clear();
    m_subtitleKey = {};
    m_subtitleOverlay = QImage();
    m_datestampText.clear();
    m_datestampKey = {};
    m_datestampOverlay = QImage();
    if (m_cropping) exitCropMode();
    update();
}

void ImageClipPreviewWidget::setSubtitle(const QString& text, const SubtitleStyle& style) {
    m_subtitleText = text;
    m_subtitleStyle = style;
    requestSubtitleOverlay();
    update();
}

void ImageClipPreviewWidget::setDatestamp(const QString& text, const DatestampStyle& style) {
    m_datestampText = text;
    m_datestampStyle = style;
    requestDatestampOverlay();
    update();
}

void ImageClipPreviewWidget::requestSubtitleOverlay() {
    m_subtitleOverlay = QImage();
    m_subtitleKey = {};
    if (!m_overlayRenderer || m_subtitleText.trimmed().isEmpty()) return;
    QString expr = subtitleDrawText(m_subtitleText, m_subtitleStyle,
                                    0.0, false, previewTextfileDir());
    if (expr.isEmpty()) return;
    m_subtitleKey = TextOverlayRenderer::Key{expr, m_projectW, m_projectH};
    m_subtitleOverlay = m_overlayRenderer->getOrRequest(m_subtitleKey);
}

void ImageClipPreviewWidget::requestDatestampOverlay() {
    m_datestampOverlay = QImage();
    m_datestampKey = {};
    if (!m_overlayRenderer || m_datestampText.isEmpty() || !m_datestampStyle.active) return;
    QString expr = datestampDrawText(m_datestampText, m_datestampStyle,
                                     previewTextfileDir());
    if (expr.isEmpty()) return;
    m_datestampKey = TextOverlayRenderer::Key{expr, m_projectW, m_projectH};
    m_datestampOverlay = m_overlayRenderer->getOrRequest(m_datestampKey);
}

QRect ImageClipPreviewWidget::canvasFitRect() const {
    // Crop mode wants the maximum interactive area; outside crop mode
    // we letterbox the project canvas inside the widget so the subtitle
    // overlay can be positioned in canvas coordinates (matching the
    // renderer's pad+drawtext compositing).
    QSize boxSize = m_cropping
        ? size()
        : QSize(m_projectW, m_projectH).scaled(size(), Qt::KeepAspectRatio);
    return QRect((width() - boxSize.width()) / 2,
                 (height() - boxSize.height()) / 2,
                 boxSize.width(), boxSize.height());
}

QRect ImageClipPreviewWidget::imagePaintRect() const {
    const QImage& base = effectiveImage();
    if (base.isNull()) return QRect();
    // Outside crop mode the persisted crop drives what's visible; in
    // crop mode the *full* rotated bbox is shown so the user can pick
    // anywhere on it.
    QSize src = (m_crop && !m_cropping)
        ? QSize(std::max(1, int(std::round(m_crop->width()  * base.width()))),
                std::max(1, int(std::round(m_crop->height() * base.height()))))
        : base.size();
    QRect host = canvasFitRect();
    QSize target = src.scaled(host.size(), Qt::KeepAspectRatio);
    return QRect(host.x() + (host.width()  - target.width())  / 2,
                 host.y() + (host.height() - target.height()) / 2,
                 target.width(), target.height());
}

void ImageClipPreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());

    // Canvas rect (only visible when not cropping). Filled black so the
    // preview looks like the rendered MP4: image centered with black
    // letterbox bars where image-aspect != canvas-aspect.
    QRect canvasRect = canvasFitRect();
    if (!m_cropping) p.fillRect(canvasRect, Qt::black);

    if (m_orig.isNull()) {
        p.setPen(Qt::lightGray);
        p.drawText(rect(), Qt::AlignCenter,
            !m_path.isEmpty() ? tr("(unable to decode image)") : tr("(no image clip)"));
        return;
    }
    const QImage& base = effectiveImage();
    QRect dst = imagePaintRect();
    if (m_crop && !m_cropping) {
        QRectF c = *m_crop;
        QRect srcRect(int(std::round(c.x() * base.width())),
                      int(std::round(c.y() * base.height())),
                      std::max(1, int(std::round(c.width()  * base.width()))),
                      std::max(1, int(std::round(c.height() * base.height()))));
        p.drawImage(dst, base, srcRect);
    } else {
        p.drawImage(dst, base);
    }

    // Subtitle and date-stamp overlays in canvas coordinates — only
    // outside crop mode (cropping shouldn't be obstructed by burn-ins).
    if (!m_cropping) {
        if (!m_subtitleOverlay.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(canvasRect, m_subtitleOverlay);
        }
        if (!m_datestampOverlay.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(canvasRect, m_datestampOverlay);
        }
    }
}

void ImageClipPreviewWidget::resizeEvent(QResizeEvent*) {
    if (m_cropping) positionCropUi();
}

void ImageClipPreviewWidget::buildCropUi() {
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

    auto* rotLabel = new QLabel(tr("Rotate:"), m_cropBar);
    m_rotMinus90 = new QPushButton(tr("−90°"), m_cropBar);
    m_rotMinus90->setToolTip(tr("Rotate 90° counter-clockwise"));
    m_rotPlus90  = new QPushButton(tr("+90°"), m_cropBar);
    m_rotPlus90->setToolTip(tr("Rotate 90° clockwise"));
    m_rotSlider = new QSlider(Qt::Horizontal, m_cropBar);
    // Slider range: ±15° in 0.1° steps so tilt fixes feel precise. The
    // spinbox accepts the full ±180° range for big-jump nudges.
    m_rotSlider->setRange(-150, 150);
    m_rotSlider->setSingleStep(1);
    m_rotSlider->setPageStep(10);
    m_rotSlider->setMinimumWidth(140);
    m_rotSpin = new QDoubleSpinBox(m_cropBar);
    m_rotSpin->setDecimals(1);
    m_rotSpin->setSingleStep(0.1);
    m_rotSpin->setRange(-180.0, 180.0);
    m_rotSpin->setSuffix(QStringLiteral(" °"));
    m_rotSpin->setKeyboardTracking(false);
    m_rotZero = new QPushButton(tr("0°"), m_cropBar);
    m_rotZero->setToolTip(tr("Reset rotation"));
    m_btnAutoFit = new QPushButton(tr("Auto-fit"), m_cropBar);
    m_btnAutoFit->setToolTip(tr(
        "Shrink crop to the largest rectangle that fits inside the rotated\n"
        "image — eliminates black corners after a tilt fix."));

    m_btnApply  = new QPushButton(tr("Apply"),  m_cropBar);
    m_btnReset  = new QPushButton(tr("Reset"),  m_cropBar);
    m_btnCancel = new QPushButton(tr("Cancel"), m_cropBar);
    m_btnApply->setDefault(true);

    lay->addWidget(lbl);
    lay->addWidget(m_aspect);
    lay->addSpacing(8);
    lay->addWidget(rotLabel);
    lay->addWidget(m_rotMinus90);
    lay->addWidget(m_rotSlider);
    lay->addWidget(m_rotSpin);
    lay->addWidget(m_rotPlus90);
    lay->addWidget(m_rotZero);
    lay->addSpacing(8);
    lay->addWidget(m_btnAutoFit);
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

    // Rotation widgets share one staged value. Slider drives a 0.1°-step
    // 10× int — translate both ways with QSignalBlocker to break loops.
    connect(m_rotSlider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_cropping) return;
        const double deg = v / 10.0;
        if (std::abs(m_rotSpin->value() - deg) > 1e-6) {
            QSignalBlocker b(m_rotSpin);
            m_rotSpin->setValue(deg);
        }
        onStagedRotationChanged(deg);
    });
    connect(m_rotSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [this](double deg) {
            if (!m_cropping) return;
            const int sliderTick = int(std::round(deg * 10.0));
            const int clamped = std::clamp(sliderTick,
                                           m_rotSlider->minimum(),
                                           m_rotSlider->maximum());
            if (m_rotSlider->value() != clamped) {
                QSignalBlocker b(m_rotSlider);
                m_rotSlider->setValue(clamped);
            }
            onStagedRotationChanged(deg);
        });
    connect(m_rotMinus90, &QPushButton::clicked, this, [this]() {
        m_rotSpin->setValue(m_stagedRotation - 90.0);  // signal handler does the rest
    });
    connect(m_rotPlus90, &QPushButton::clicked, this, [this]() {
        m_rotSpin->setValue(m_stagedRotation + 90.0);
    });
    connect(m_rotZero, &QPushButton::clicked, this, [this]() {
        m_rotSpin->setValue(0.0);
    });
    connect(m_btnAutoFit, &QPushButton::clicked, this, [this]() {
        autoFitCrop();
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
        emit rotationApplied(m_stagedRotation);
        exitCropMode();
    });
    connect(m_btnReset, &QPushButton::clicked, this, [this]() {
        // Reset clears both crop AND rotation — one click to undo a tilt fix.
        emit cropApplied(std::nullopt);
        emit rotationApplied(0.0);
        exitCropMode();
    });
    connect(m_btnCancel, &QPushButton::clicked, this, [this]() {
        exitCropMode();
    });
}

void ImageClipPreviewWidget::enterCropMode() {
    if (m_orig.isNull()) return;
    buildCropUi();

    // Order matters: switch into crop mode FIRST so imagePaintRect()
    // reports the full-image rect (not the cropped sub-rect), then
    // size the overlay against that, then push the saved crop.
    m_cropping = true;
    m_stagedRotation = m_rotationDegrees;
    invalidateRotatedCache();   // staged value may differ from cached angle
    for (QShortcut* sc : m_cropShortcuts) sc->setEnabled(true);
    update();
    positionCropUi();
    {
        QSignalBlocker block(m_aspect);
        m_aspect->setCurrentIndex(0);
        m_aspect->setItemText(1, tr("Project (%1×%2)").arg(m_projectW).arg(m_projectH));
    }
    {
        const int tick = std::clamp(int(std::round(m_stagedRotation * 10.0)),
                                    m_rotSlider->minimum(),
                                    m_rotSlider->maximum());
        QSignalBlocker bs(m_rotSlider);
        QSignalBlocker bp(m_rotSpin);
        m_rotSlider->setValue(tick);
        m_rotSpin->setValue(m_stagedRotation);
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

void ImageClipPreviewWidget::onStagedRotationChanged(double degrees) {
    if (!m_cropping) return;
    if (std::abs(degrees - m_stagedRotation) < 1e-6) return;
    // Preserve the user's crop in normalized bbox coords as the bbox grows
    // or shrinks. We snapshot before changing rotation, then push the same
    // normalized rect back through the resized overlay below.
    const QRectF prevCrop = m_overlay ? m_overlay->cropNormalized() : QRectF(0, 0, 1, 1);
    m_stagedRotation = degrees;
    invalidateRotatedCache();
    update();           // forces effectiveImage() to rebuild at the new angle
    positionCropUi();   // bbox dims changed → image rect changed
    if (m_overlay) {
        m_overlay->setImageRect(imagePaintRect());
        m_overlay->setCropNormalized(prevCrop);
    }
}

void ImageClipPreviewWidget::autoFitCrop() {
    if (!m_cropping || m_orig.isNull() || !m_overlay) return;
    const bool locked = m_overlay->aspectMode() == CropOverlay::AspectMode::Locked;
    const double sourceAspect =
        double(m_orig.width()) / std::max(1, m_orig.height());
    const double aspect = locked
        ? double(m_projectW) / std::max(1, m_projectH)
        : sourceAspect;
    const QRectF inscribed = inscribedRectInRotated(
        m_orig.width(), m_orig.height(), m_stagedRotation, aspect);
    m_overlay->setCropNormalized(inscribed);
}

void ImageClipPreviewWidget::exitCropMode() {
    if (!m_cropping) return;
    m_cropping = false;
    for (QShortcut* sc : m_cropShortcuts) sc->setEnabled(false);
    if (m_overlay) m_overlay->hide();
    if (m_cropBar) m_cropBar->hide();
    update();
    emit cropModeExited();
}

void ImageClipPreviewWidget::positionCropUi() {
    if (!m_overlay) return;
    m_overlay->setImageRect(imagePaintRect());
    QSize hint = m_cropBar->sizeHint();
    int w = std::min(hint.width(), std::max(120, width() - 16));
    int x = (width() - w) / 2;
    m_cropBar->setGeometry(x, 6, w, hint.height());
}

} // namespace vlip
