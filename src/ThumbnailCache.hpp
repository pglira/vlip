#pragma once

#include <QString>
#include <QImage>

namespace vlip {

// Thumbnails are stored under QStandardPaths::CacheLocation/thumbs/
// keyed by a hash of (absolute path, file mtime, file size, target size).
class ThumbnailCache {
public:
    static QString cacheDir();
    // Returns the cached path on success (QString may be empty on fail).
    static QString getOrCreate(const QString& sourcePath, bool isVideo, int targetSize = 256);
    static QString keyPath(const QString& sourcePath, int targetSize);
};

} // namespace vlip
