#include "CropOverlay.h"

#include <QPainter>
#include <QMouseEvent>
#include <QRegion>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

namespace vlip {

namespace {
QRect normRect(QRect r) { return r.normalized(); }
} // namespace

CropOverlay::CropOverlay(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::StrongFocus);
}

void CropOverlay::setImageRect(const QRect& r) {
    // resizeEvent re-projects m_rect through the new size so the user's
    // normalized crop survives a parent resize.
    setGeometry(r);
}

void CropOverlay::setCropNormalized(const std::optional<QRectF>& opt) {
    QRectF n = opt.value_or(QRectF(0, 0, 1, 1));
    n = n.intersected(QRectF(0, 0, 1, 1));
    if (n.width() <= 0 || n.height() <= 0) n = QRectF(0, 0, 1, 1);
    if (width() <= 0 || height() <= 0) {
        m_rect = QRect();           // will be re-projected on first resizeEvent
        update();
        return;
    }
    m_rect = QRect(int(std::round(n.x() * width())),
                   int(std::round(n.y() * height())),
                   std::max(kMinSidePx, int(std::round(n.width()  * width()))),
                   std::max(kMinSidePx, int(std::round(n.height() * height()))));
    m_rect = clampToCanvas(m_rect);
    update();
}

QRectF CropOverlay::cropNormalized() const {
    if (width() <= 0 || height() <= 0 || m_rect.isEmpty()) {
        return QRectF(0, 0, 1, 1);
    }
    return QRectF(double(m_rect.x())      / width(),
                  double(m_rect.y())      / height(),
                  double(m_rect.width())  / width(),
                  double(m_rect.height()) / height());
}

void CropOverlay::setAspectMode(AspectMode m) {
    if (m_aspect == m) return;
    m_aspect = m;
    if (m_aspect == AspectMode::Locked && !m_rect.isEmpty()) {
        // Snap to the locked aspect anchored at the top-left, then shrink
        // uniformly to fit the canvas if needed (preserves the aspect).
        m_rect = enforceAspect(Handle::BR, m_rect);
        m_rect = clampPreservingAspect(Handle::BR, m_rect);
    }
    update();
    emitChanged();
}

void CropOverlay::setLockedAspect(double ratio) {
    if (ratio <= 0.0) return;
    if (qFuzzyCompare(m_lockedAspect, ratio)) return;
    m_lockedAspect = ratio;
    if (m_aspect == AspectMode::Locked && !m_rect.isEmpty()) {
        m_rect = enforceAspect(Handle::BR, m_rect);
        m_rect = clampPreservingAspect(Handle::BR, m_rect);
        update();
        emitChanged();
    }
}

void CropOverlay::resizeEvent(QResizeEvent* e) {
    // Preserve the user's normalized crop when the overlay is resized.
    QSize old = e->oldSize();
    if (old.width() <= 0 || old.height() <= 0 || m_rect.isEmpty()) return;
    QRectF n(double(m_rect.x())      / old.width(),
             double(m_rect.y())      / old.height(),
             double(m_rect.width())  / old.width(),
             double(m_rect.height()) / old.height());
    m_rect = QRect(int(std::round(n.x() * width())),
                   int(std::round(n.y() * height())),
                   std::max(kMinSidePx, int(std::round(n.width()  * width()))),
                   std::max(kMinSidePx, int(std::round(n.height() * height()))));
    m_rect = clampToCanvas(m_rect);
}

// ---------------------------------------------------------------- handles

QRect CropOverlay::handlePixelRect(Handle h) const {
    if (m_rect.isEmpty()) return QRect();
    const int s  = kHandlePx;
    const int hs = s / 2;
    int l = m_rect.left(), t = m_rect.top();
    int r = m_rect.right(), b = m_rect.bottom();
    int cx = (l + r) / 2, cy = (t + b) / 2;
    switch (h) {
        case Handle::TL: return QRect(l - hs, t - hs, s, s);
        case Handle::T:  return QRect(cx - hs, t - hs, s, s);
        case Handle::TR: return QRect(r - hs, t - hs, s, s);
        case Handle::R:  return QRect(r - hs, cy - hs, s, s);
        case Handle::BR: return QRect(r - hs, b - hs, s, s);
        case Handle::B:  return QRect(cx - hs, b - hs, s, s);
        case Handle::BL: return QRect(l - hs, b - hs, s, s);
        case Handle::L:  return QRect(l - hs, cy - hs, s, s);
        default:         return QRect();
    }
}

CropOverlay::Handle CropOverlay::hitTest(QPoint p) const {
    if (m_rect.isEmpty()) return Handle::New;
    static const Handle handles[] = {
        Handle::TL, Handle::TR, Handle::BL, Handle::BR,    // corners first
        Handle::T,  Handle::B,  Handle::L,  Handle::R,
    };
    for (Handle h : handles) {
        if (handlePixelRect(h).contains(p)) return h;
    }
    if (m_rect.contains(p)) return Handle::Inside;
    return Handle::New;
}

Qt::CursorShape CropOverlay::cursorFor(Handle h) const {
    switch (h) {
        case Handle::Inside:                       return Qt::SizeAllCursor;
        case Handle::TL: case Handle::BR:          return Qt::SizeFDiagCursor;
        case Handle::TR: case Handle::BL:          return Qt::SizeBDiagCursor;
        case Handle::L:  case Handle::R:           return Qt::SizeHorCursor;
        case Handle::T:  case Handle::B:           return Qt::SizeVerCursor;
        case Handle::New:                          return Qt::CrossCursor;
        default:                                   return Qt::ArrowCursor;
    }
}

// ---------------------------------------------------------------- math

QRect CropOverlay::resizedRect(Handle h, QPoint mouse) const {
    if (h == Handle::New) {
        return QRect(m_pressPos, mouse).normalized();
    }
    const int dx = mouse.x() - m_pressPos.x();
    const int dy = mouse.y() - m_pressPos.y();
    int l = m_pressRect.left(), r = m_pressRect.right();
    int t = m_pressRect.top(),  b = m_pressRect.bottom();
    switch (h) {
        case Handle::Inside: return m_pressRect.translated(dx, dy);
        case Handle::L:  l += dx; break;
        case Handle::R:  r += dx; break;
        case Handle::T:  t += dy; break;
        case Handle::B:  b += dy; break;
        case Handle::TL: l += dx; t += dy; break;
        case Handle::TR: r += dx; t += dy; break;
        case Handle::BL: l += dx; b += dy; break;
        case Handle::BR: r += dx; b += dy; break;
        default: break;
    }
    return QRect(QPoint(l, t), QPoint(r, b)).normalized();
}

// Apply the 16:9 lock by adjusting the dimension that the *handle didn't
// move*. Anchor is the opposite side/corner so the side the user is
// dragging tracks the mouse.
QRect CropOverlay::enforceAspect(Handle h, QRect r) const {
    if (m_aspect != AspectMode::Locked) return r;
    if (h == Handle::Inside || h == Handle::None) return r;

    r = normRect(r);
    int w   = std::max(1, r.width());
    int hgt = std::max(1, r.height());
    const double curAR = double(w) / hgt;

    auto fitWidthFromHeight  = [&]() { w   = std::max(1, int(std::round(hgt * m_lockedAspect))); };
    auto fitHeightFromWidth  = [&]() { hgt = std::max(1, int(std::round(w / m_lockedAspect)));   };

    switch (h) {
        case Handle::T:
        case Handle::B: {
            // Vertical edge moved → derive width, keep horizontal center.
            fitWidthFromHeight();
            int cx = (r.left() + r.right()) / 2;
            r.setLeft(cx - w / 2);
            r.setWidth(w);
            break;
        }
        case Handle::L:
        case Handle::R: {
            // Horizontal edge moved → derive height, keep vertical center.
            fitHeightFromWidth();
            int cy = (r.top() + r.bottom()) / 2;
            r.setTop(cy - hgt / 2);
            r.setHeight(hgt);
            break;
        }
        case Handle::TL:
            // Anchor BR.
            (curAR > m_lockedAspect) ? fitWidthFromHeight() : fitHeightFromWidth();
            r.setLeft(r.right() - w + 1);
            r.setTop (r.bottom() - hgt + 1);
            break;
        case Handle::TR:
            // Anchor BL.
            (curAR > m_lockedAspect) ? fitWidthFromHeight() : fitHeightFromWidth();
            r.setRight(r.left() + w - 1);
            r.setTop  (r.bottom() - hgt + 1);
            break;
        case Handle::BL:
            // Anchor TR.
            (curAR > m_lockedAspect) ? fitWidthFromHeight() : fitHeightFromWidth();
            r.setLeft  (r.right() - w + 1);
            r.setBottom(r.top()   + hgt - 1);
            break;
        case Handle::BR:
            // Anchor TL.
            (curAR > m_lockedAspect) ? fitWidthFromHeight() : fitHeightFromWidth();
            r.setRight (r.left() + w - 1);
            r.setBottom(r.top()  + hgt - 1);
            break;
        case Handle::New:
            // Anchor at the start of the drag (top-left of the new rect
            // after normalization).
            (curAR > m_lockedAspect) ? fitWidthFromHeight() : fitHeightFromWidth();
            r.setRight (r.left() + w - 1);
            r.setBottom(r.top()  + hgt - 1);
            break;
        default: break;
    }
    return r;
}

QRect CropOverlay::clampToCanvas(QRect r) const {
    QRect canvas(0, 0, width(), height());
    if (canvas.isEmpty()) return QRect();
    r = normRect(r);

    // Cap dimensions to canvas size first.
    if (r.width()  > canvas.width())  r.setWidth(canvas.width());
    if (r.height() > canvas.height()) r.setHeight(canvas.height());
    if (r.width()  < kMinSidePx) r.setWidth(std::min(kMinSidePx, canvas.width()));
    if (r.height() < kMinSidePx) r.setHeight(std::min(kMinSidePx, canvas.height()));

    // Slide back inside.
    if (r.left() < 0)                         r.moveLeft(0);
    if (r.top()  < 0)                         r.moveTop(0);
    if (r.right()  >= canvas.width())         r.moveRight(canvas.width() - 1);
    if (r.bottom() >= canvas.height())        r.moveBottom(canvas.height() - 1);
    return r;
}

QRect CropOverlay::clampPreservingAspect(Handle h, QRect r) const {
    QRect canvas(0, 0, width(), height());
    if (canvas.isEmpty()) return QRect();
    r = normRect(r);

    int w = std::max(1, r.width());
    int hh = std::max(1, r.height());

    // Compute the maximum width and height available in the direction the
    // handle is being dragged, given which side is anchored.
    int wMax = canvas.width();
    int hMax = canvas.height();
    switch (h) {
        case Handle::TL:                      // anchor BR
            wMax = std::min(canvas.width(),  r.right()  + 1);
            hMax = std::min(canvas.height(), r.bottom() + 1);
            break;
        case Handle::TR:                      // anchor BL
            wMax = std::min(canvas.width(),  canvas.width()  - r.left());
            hMax = std::min(canvas.height(), r.bottom() + 1);
            break;
        case Handle::BL:                      // anchor TR
            wMax = std::min(canvas.width(),  r.right() + 1);
            hMax = std::min(canvas.height(), canvas.height() - r.top());
            break;
        case Handle::BR:                      // anchor TL
        case Handle::New:                     // anchor TL of fresh rect
            wMax = std::min(canvas.width(),  canvas.width()  - r.left());
            hMax = std::min(canvas.height(), canvas.height() - r.top());
            break;
        case Handle::T: {                     // anchor bottom edge + cx
            int cx = r.center().x();
            int half = std::min(cx, canvas.width() - 1 - cx);
            wMax = std::max(2, 2 * half);
            hMax = std::min(canvas.height(), r.bottom() + 1);
            break;
        }
        case Handle::B: {                     // anchor top edge + cx
            int cx = r.center().x();
            int half = std::min(cx, canvas.width() - 1 - cx);
            wMax = std::max(2, 2 * half);
            hMax = std::min(canvas.height(), canvas.height() - r.top());
            break;
        }
        case Handle::L: {                     // anchor right edge + cy
            int cy = r.center().y();
            int half = std::min(cy, canvas.height() - 1 - cy);
            hMax = std::max(2, 2 * half);
            wMax = std::min(canvas.width(), r.right() + 1);
            break;
        }
        case Handle::R: {                     // anchor left edge + cy
            int cy = r.center().y();
            int half = std::min(cy, canvas.height() - 1 - cy);
            hMax = std::max(2, 2 * half);
            wMax = std::min(canvas.width(), canvas.width() - r.left());
            break;
        }
        default: break;
    }

    // Shrink uniformly so both dims fit. (Aspect was already enforced; we
    // scale w and h together.)
    if (wMax < 1) wMax = 1;
    if (hMax < 1) hMax = 1;
    double scale = std::max(double(w) / wMax, double(hh) / hMax);
    if (scale > 1.0) {
        w  = std::max(kMinSidePx, int(std::floor(w  / scale)));
        hh = std::max(kMinSidePx, int(std::floor(hh / scale)));
    }

    // Re-anchor the resized rect.
    int newL = r.left(), newT = r.top();
    switch (h) {
        case Handle::TL:                      // BR fixed
            newL = r.right()  - w  + 1;
            newT = r.bottom() - hh + 1;
            break;
        case Handle::TR:                      // BL fixed
            newL = r.left();
            newT = r.bottom() - hh + 1;
            break;
        case Handle::BL:                      // TR fixed
            newL = r.right() - w + 1;
            newT = r.top();
            break;
        case Handle::BR:
        case Handle::New:                     // TL fixed
            newL = r.left();
            newT = r.top();
            break;
        case Handle::T: {                     // bottom + cx fixed
            int cx = r.center().x();
            newL = cx - w / 2;
            newT = r.bottom() - hh + 1;
            break;
        }
        case Handle::B: {                     // top + cx fixed
            int cx = r.center().x();
            newL = cx - w / 2;
            newT = r.top();
            break;
        }
        case Handle::L: {                     // right + cy fixed
            int cy = r.center().y();
            newL = r.right() - w + 1;
            newT = cy - hh / 2;
            break;
        }
        case Handle::R: {                     // left + cy fixed
            int cy = r.center().y();
            newL = r.left();
            newT = cy - hh / 2;
            break;
        }
        default: break;
    }

    QRect out(newL, newT, w, hh);
    // Final integer-rounding safety: slide entirely inside without
    // resizing (uniform shrink already made it fit).
    if (out.left() < 0)                      out.moveLeft(0);
    if (out.top()  < 0)                      out.moveTop(0);
    if (out.right()  > canvas.right())       out.moveRight(canvas.right());
    if (out.bottom() > canvas.bottom())      out.moveBottom(canvas.bottom());
    return out;
}

// ---------------------------------------------------------------- events

void CropOverlay::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }

    m_active   = hitTest(e->pos());
    m_pressPos = e->pos();
    m_pressRect = m_rect;

    if (m_active == Handle::New) {
        // Start drawing a fresh selection at the cursor.
        QRect seed(e->pos(), e->pos());
        m_pressRect = seed;
        m_rect = seed;
        update();
    }
    e->accept();
}

void CropOverlay::mouseMoveEvent(QMouseEvent* e) {
    if (m_active == Handle::None) {
        setCursor(cursorFor(hitTest(e->pos())));
        return;
    }
    QRect r = resizedRect(m_active, e->pos());
    r = enforceAspect(m_active, r);
    // Aspect lock + a resize-handle drag → shrink uniformly toward the
    // anchor so the rect stays inside the canvas without breaking the
    // aspect ratio. Free mode or moving the rect uses the simple slide-
    // clamp.
    if (m_aspect == AspectMode::Locked && m_active != Handle::Inside) {
        r = clampPreservingAspect(m_active, r);
    } else {
        r = clampToCanvas(r);
    }
    if (r != m_rect) {
        m_rect = r;
        update();
        emitChanged();
    }
    e->accept();
}

void CropOverlay::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QWidget::mouseReleaseEvent(e); return; }

    // If the user clicked-without-dragging in NewSelection mode, the rect
    // is still effectively empty — restore the previous selection.
    if (m_active == Handle::New
        && (m_rect.width() < kMinSidePx || m_rect.height() < kMinSidePx)) {
        m_rect = m_pressRect.size().isEmpty() ? QRect() : m_pressRect;
    }
    m_active = Handle::None;
    setCursor(cursorFor(hitTest(e->pos())));
    update();
    e->accept();
}

void CropOverlay::emitChanged() {
    emit cropRectChanged(cropNormalized());
}

// ---------------------------------------------------------------- paint

void CropOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Dim everything outside the selection.
    QRegion outside(rect());
    if (!m_rect.isEmpty()) outside -= QRegion(m_rect);
    p.setClipRegion(outside);
    p.fillRect(rect(), QColor(0, 0, 0, 130));
    p.setClipping(false);

    if (m_rect.isEmpty()) return;

    // Selection border.
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(m_rect.adjusted(0, 0, -1, -1));

    // Rule-of-thirds gridlines (don't draw if too small to make sense).
    if (m_rect.width() > 30 && m_rect.height() > 30) {
        p.setPen(QPen(QColor(255, 255, 255, 90), 1));
        for (int i = 1; i <= 2; ++i) {
            int x = m_rect.left() + m_rect.width()  * i / 3;
            int y = m_rect.top()  + m_rect.height() * i / 3;
            p.drawLine(x, m_rect.top(), x, m_rect.bottom());
            p.drawLine(m_rect.left(), y, m_rect.right(), y);
        }
    }

    // Handles.
    static const Handle handles[] = {
        Handle::TL, Handle::T, Handle::TR,
        Handle::R, Handle::BR, Handle::B,
        Handle::BL, Handle::L
    };
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::white);
    for (Handle h : handles) {
        p.drawRect(handlePixelRect(h));
    }
}

} // namespace vlip
