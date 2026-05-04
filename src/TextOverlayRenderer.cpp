#include "TextOverlayRenderer.hpp"

#include <QProcess>
#include <QStringList>

namespace vlip {

TextOverlayRenderer::TextOverlayRenderer(QObject* parent) : QObject(parent) {}

TextOverlayRenderer::~TextOverlayRenderer() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
}

QImage TextOverlayRenderer::getOrRequest(const Key& k) {
    if (k.filterExpr.isEmpty() || k.canvasW <= 0 || k.canvasH <= 0) {
        return {};
    }
    auto it = m_cache.constFind(k);
    if (it != m_cache.constEnd()) {
        // Touch the LRU so this entry survives the next eviction.
        m_lru.removeOne(k);
        m_lru.enqueue(k);
        return it.value();
    }
    // Avoid duplicate work — if the same overlay is already queued or
    // running, just wait for the existing request to finish.
    if (m_inFlight.contains(k)) return {};
    m_inFlight.insert(k);
    m_queue.enqueue(k);
    if (!m_proc || m_proc->state() == QProcess::NotRunning) {
        startNext();
    }
    return {};
}

void TextOverlayRenderer::startNext() {
    if (m_queue.isEmpty()) return;
    m_currentKey = m_queue.dequeue();
    m_pngBuf.clear();

    if (!m_proc) {
        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::SeparateChannels);
        connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
            if (m_proc) m_pngBuf += m_proc->readAllStandardOutput();
        });
        connect(m_proc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus s) {
            onProcessFinished(code, int(s));
        });
    }

    // Notes on the source: the lavfi `color` source outputs an opaque
    // canvas even when an alpha is specified in the colour string —
    // `format=rgba` alone keeps every pixel at alpha=255. We have to
    // explicitly zero the alpha channel with `colorchannelmixer=aa=0`
    // before drawtext runs so the non-text pixels stay transparent.
    QStringList args = {
        "-hide_banner", "-loglevel", "error",
        "-f", "lavfi",
        "-i", QString("color=c=black:s=%1x%2:d=0.04")
                  .arg(m_currentKey.canvasW).arg(m_currentKey.canvasH),
        "-vf", QString("format=rgba,colorchannelmixer=aa=0,%1").arg(m_currentKey.filterExpr),
        "-frames:v", "1",
        "-f", "image2pipe",
        "-vcodec", "png",
        "pipe:1",
    };
    m_proc->start("ffmpeg", args);
}

void TextOverlayRenderer::onProcessFinished(int code, int exitStatus) {
    QImage img;
    const bool ok = (exitStatus == int(QProcess::NormalExit) && code == 0);
    if (ok) img.loadFromData(m_pngBuf, "PNG");
    m_pngBuf.clear();

    // Cache (even null images, so we don't re-spawn ffmpeg for a known-
    // failing input — e.g. a font that fc-match can't resolve).
    m_cache.insert(m_currentKey, img);
    m_lru.enqueue(m_currentKey);
    while (m_lru.size() > kMaxCacheEntries) {
        Key evict = m_lru.dequeue();
        m_cache.remove(evict);
    }
    m_inFlight.remove(m_currentKey);

    Key finished = m_currentKey;
    m_currentKey = {};
    emit overlayReady(finished);

    if (!m_queue.isEmpty()) startNext();
}

} // namespace vlip
