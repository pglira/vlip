#pragma once

#include "Project.hpp"
#include "TextOverlayRenderer.hpp"

#include <QWidget>
#include <QImage>
#include <QString>
#include <QRectF>
#include <QPointer>
#include <QVector>
#include <optional>

class QFrame;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QShortcut;
class QSlider;

namespace vlip {

class CropOverlay;

class ImageClipPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit ImageClipPreviewWidget(TextOverlayRenderer* overlayRenderer,
                                    QWidget* parent = nullptr);
    void setImage(const QString& path);
    // Persisted crop applied to the displayed image, normalized 0..1 over
    // the *rotated* bbox.
    void setCrop(const std::optional<QRectF>& crop);
    // Persisted rotation in degrees, clockwise. Re-applied to the source
    // before crop/letterbox. Outside crop mode this drives the displayed
    // image directly; in crop mode, the staged value from the rotation
    // slider supersedes it until the user Applies.
    void setRotation(double degrees);
    // Project canvas dimensions — used to label and apply the "Project"
    // aspect-ratio preset in crop mode. Safe to call any time.
    void setProjectCanvas(int width, int height);
    // Subtitle text + style for the WYSIWYG overlay. Pass empty text
    // (or any text whose trim is empty) to hide the overlay.
    void setSubtitle(const QString& text, const SubtitleStyle& style);
    // Pre-formatted date-stamp text + style for the burned-in corner
    // overlay. Pass empty text or an inactive style to hide it.
    void setDatestamp(const QString& text, const DatestampStyle& style);
    void clear();

    // Enter the interactive crop UI. Initial selection = current crop or
    // the full image; aspect mode resets to Free.
    void enterCropMode();
    bool inCropMode() const { return m_cropping; }

signals:
    // Emitted when the user clicks Apply or Reset. Both crop and rotation
    // values reflect the user's final pick — receivers should persist
    // both, not just crop.
    void cropApplied(const std::optional<QRectF>& normalized);
    void rotationApplied(double degrees);
    void cropModeExited();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    QRect imagePaintRect() const;
    QRect canvasFitRect() const;
    void buildCropUi();
    void positionCropUi();
    void exitCropMode();
    void requestSubtitleOverlay();
    void requestDatestampOverlay();
    // Active rotation in degrees: staged value while in crop mode, the
    // persisted value otherwise. Drives both display and the crop
    // overlay's bbox.
    double activeRotation() const;
    // Source image rotated by activeRotation(), painted into its bbox.
    // Empty / passthrough at rotation == 0. Rebuilt lazily when the angle
    // changes; centralises caching for paintEvent and imagePaintRect.
    const QImage& effectiveImage() const;
    void invalidateRotatedCache();
    // Push current slider/spinbox value into staged rotation, refresh
    // overlay geometry, repaint. No-op outside crop mode.
    void onStagedRotationChanged(double degrees);
    // Auto-fit: shrink the crop overlay rect to the largest axis-aligned
    // rectangle inscribed in the rotated source, honouring the current
    // aspect lock.
    void autoFitCrop();

    QPointer<TextOverlayRenderer> m_overlayRenderer;
    QString m_path;
    QImage m_orig;
    std::optional<QRectF> m_crop;
    int m_projectW = 1920;
    int m_projectH = 1080;
    double m_rotationDegrees = 0.0;
    // Staged rotation while in crop mode; ignored otherwise.
    double m_stagedRotation = 0.0;

    // Rotated-source cache. Keyed implicitly on (m_orig.cacheKey(),
    // m_rotatedAngle); rebuilt on demand from effectiveImage().
    mutable QImage m_rotated;
    mutable double m_rotatedAngle = 0.0;
    mutable qint64 m_rotatedSourceKey = 0;

    bool m_cropping = false;

    // Crop UI (built once, lazily).
    CropOverlay* m_overlay = nullptr;
    QFrame* m_cropBar = nullptr;
    QComboBox* m_aspect = nullptr;
    QSlider* m_rotSlider = nullptr;
    QDoubleSpinBox* m_rotSpin = nullptr;
    QPushButton* m_rotMinus90 = nullptr;
    QPushButton* m_rotPlus90 = nullptr;
    QPushButton* m_rotZero = nullptr;
    QPushButton* m_btnAutoFit = nullptr;
    QPushButton* m_btnApply = nullptr;
    QPushButton* m_btnReset = nullptr;
    QPushButton* m_btnCancel = nullptr;
    // Window-scope shortcuts (Enter, Return, Escape) that drive crop
    // mode. They're disabled outside crop mode so they don't intercept
    // those keys from focused QLineEdits / QSpinBoxes elsewhere in the
    // window — disabled QShortcuts don't consume the key event.
    QVector<QShortcut*> m_cropShortcuts;

    // Subtitle overlay
    QString m_subtitleText;
    SubtitleStyle m_subtitleStyle;
    TextOverlayRenderer::Key m_subtitleKey;
    QImage m_subtitleOverlay;

    // Date-stamp overlay
    QString m_datestampText;
    DatestampStyle m_datestampStyle;
    TextOverlayRenderer::Key m_datestampKey;
    QImage m_datestampOverlay;
};

} // namespace vlip
