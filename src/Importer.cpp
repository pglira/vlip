#include "Importer.h"
#include "MetaProbe.h"
#include "ThumbnailCache.h"

#include <QFileInfo>

namespace vlip {

QStringList Importer::imageExtensions() {
    return {"jpg", "jpeg", "png", "heic", "heif", "webp", "tif", "tiff"};
}
QStringList Importer::videoExtensions() {
    return {"mp4", "mov", "m4v", "mkv", "webm", "avi"};
}
bool Importer::looksLikeImage(const QString& path) {
    QString s = QFileInfo(path).suffix().toLower();
    return imageExtensions().contains(s);
}
bool Importer::looksLikeVideo(const QString& path) {
    QString s = QFileInfo(path).suffix().toLower();
    return videoExtensions().contains(s);
}

ImportResult Importer::importPath(const QString& path, double defaultImageDuration) {
    ImportResult r;
    QFileInfo fi(path);
    if (!fi.exists()) {
        r.error = QStringLiteral("File not found: %1").arg(path);
        return r;
    }

    auto probe = MetaProbe::probe(path);
    bool isVideo = probe.ok && probe.isVideo;
    if (!probe.ok) {
        // ffprobe failed entirely — try extension hint as last resort.
        if (looksLikeImage(path)) isVideo = false;
        else if (looksLikeVideo(path)) isVideo = true;
        else {
            r.error = probe.errorText.isEmpty() ? QStringLiteral("Unknown format") : probe.errorText;
            return r;
        }
    }

    QDateTime ts;
    bool uncertain = false;
    if (probe.creationTime) {
        ts = *probe.creationTime;
    } else {
        ts = fi.lastModified().toUTC();
        uncertain = true;
    }

    Common c;
    c.id = QUuid::createUuid();
    c.sourcePath = fi.absoluteFilePath();
    c.timestamp = ts;
    c.timestampUncertain = uncertain;
    c.used = false;
    c.thumbPath = ThumbnailCache::getOrCreate(c.sourcePath, isVideo, 256);

    if (isVideo) {
        VideoItem v;
        v.common = c;
        v.sourceWidth = probe.width;
        v.sourceHeight = probe.height;
        v.sourceDurationSecs = probe.durationSecs;
        v.startSecs = 0.0;
        v.endSecs = 0.0; // 0 -> to end
        v.hasAudio = probe.hasAudio;
        r.item = Item::makeVideo(v);
    } else {
        ImageItem im;
        im.common = c;
        im.sourceWidth = probe.width;
        im.sourceHeight = probe.height;
        im.durationSecs = defaultImageDuration;
        r.item = Item::makeImage(im);
    }
    r.ok = true;
    if (uncertain) r.warning = QStringLiteral("Timestamp from file mtime (no metadata).");
    return r;
}

} // namespace vlip
