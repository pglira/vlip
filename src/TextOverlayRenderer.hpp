#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QPointer>

class QProcess;

namespace vlip {

// Renders a single transparent-background overlay PNG by piping a
// drawtext-only filtergraph through ffmpeg, then caches the result.
//
// One worker process is active at a time; pending requests are queued
// and de-duplicated. The cache is keyed on the exact drawtext
// expression plus canvas size, so any two callers asking for the same
// overlay share work and storage.
//
// Used by the preview widgets so what the user sees on screen matches
// the renderer's output pixel-for-pixel (modulo the letterbox scaling
// applied at paint time).
class TextOverlayRenderer : public QObject {
    Q_OBJECT
public:
    struct Key {
        // The full drawtext filter expression (post-`-vf`).
        QString filterExpr;
        int canvasW = 0;
        int canvasH = 0;
        bool operator==(const Key& o) const {
            return canvasW == o.canvasW
                && canvasH == o.canvasH
                && filterExpr == o.filterExpr;
        }
    };

    explicit TextOverlayRenderer(QObject* parent = nullptr);
    ~TextOverlayRenderer() override;

    // Returns the cached image for `k`, or a null QImage if it isn't
    // cached yet. In the latter case the request is enqueued (or a no-op
    // if it's already in flight / queued) and overlayReady(k) fires when
    // the PNG is decoded.
    QImage getOrRequest(const Key& k);

signals:
    void overlayReady(const TextOverlayRenderer::Key& key);

private:
    void startNext();
    void onProcessFinished(int code, int exitStatus);

    // LRU bound, in entries. A single 1920×1080 RGBA image is ~8 MB; we
    // keep the cap modest because typical previews land on a handful of
    // distinct overlays per session.
    static constexpr int kMaxCacheEntries = 64;

    QHash<Key, QImage> m_cache;
    QQueue<Key> m_lru;            // oldest at front
    QQueue<Key> m_queue;
    QSet<Key> m_inFlight;
    QPointer<QProcess> m_proc;
    Key m_currentKey;
    QByteArray m_pngBuf;
};

inline std::size_t qHash(const TextOverlayRenderer::Key& k, std::size_t seed = 0) noexcept {
    return qHashMulti(seed, k.filterExpr, k.canvasW, k.canvasH);
}

} // namespace vlip
