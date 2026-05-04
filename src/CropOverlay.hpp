#pragma once

#include <QWidget>
#include <QRect>
#include <QRectF>
#include <optional>

namespace vlip {

// Transparent overlay that lets the user pick a rectangular crop on top of
// an image. Sized to match the image's painted rect inside its parent.
//
// Internal state is one widget-pixel rect (`m_rect`). Public API talks in
// normalized 0..1 source-image coords. The widget aspect equals the image
// aspect (since the parent paints the image into this exact rect), so a
// 16:9 lock applied in widget pixels is also 16:9 in source pixels.
class CropOverlay : public QWidget {
    Q_OBJECT
public:
    enum class AspectMode { Free, Locked };

    explicit CropOverlay(QWidget* parent = nullptr);

    // Set the overlay's geometry inside the parent. Re-projects the
    // current selection through resizeEvent so the user's normalized
    // crop is preserved across parent resizes.
    void setImageRect(const QRect& parentLocal);

    // Replace the current selection with this normalized 0..1 rect.
    // nullopt = full image.
    void setCropNormalized(const std::optional<QRectF>& norm);

    // Current selection in normalized 0..1 coords. Returns (0,0,1,1) if
    // the overlay has no usable size yet.
    QRectF cropNormalized() const;

    void setAspectMode(AspectMode m);
    AspectMode aspectMode() const { return m_aspect; }

    // Aspect (width / height) used when the lock is on. Default 16:9.
    void setLockedAspect(double ratio);

signals:
    // Emitted on every interactive change. Receivers should not assume the
    // user is finished — the final value is read on Apply.
    void cropRectChanged(const QRectF& normalized);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    enum class Handle {
        None,
        Inside,
        L, R, T, B,
        TL, TR, BL, BR,
        New          // dragging a fresh selection from an empty area
    };

    // Hit-test in widget-local pixel coords.
    Handle hitTest(QPoint p) const;
    Qt::CursorShape cursorFor(Handle h) const;
    QRect handlePixelRect(Handle h) const;

    // Pure: compute the new rect from the press state, the active handle,
    // and the current mouse position. Aspect lock and clamp applied
    // afterwards.
    QRect resizedRect(Handle h, QPoint mouse) const;
    QRect enforceAspect(Handle anchor, QRect r) const;
    // Slide-clamp; may shrink width/height independently. Use only when
    // there's no aspect lock to preserve (Free mode or "move").
    QRect clampToCanvas(QRect r) const;
    // Shrink uniformly toward the anchor implied by the active handle so
    // the result fits the canvas without breaking the 16:9 lock.
    QRect clampPreservingAspect(Handle h, QRect r) const;

    void emitChanged();

    // State
    QRect m_rect;                 // current selection, widget-local pixels
    AspectMode m_aspect = AspectMode::Free;
    double m_lockedAspect = 16.0 / 9.0;

    // Drag-in-progress
    Handle m_active = Handle::None;
    QPoint m_pressPos;
    QRect  m_pressRect;

    static constexpr int kHandlePx = 12;
    static constexpr int kMinSidePx = 12;
};

} // namespace vlip
