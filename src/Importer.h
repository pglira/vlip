#pragma once

#include "Project.h"

#include <QObject>
#include <QStringList>

namespace vlip {

struct ImportResult {
    Item item;
    bool ok = false;
    QString warning;     // warnings (e.g. mtime fallback for timestamp)
    QString error;       // fatal errors for this file
};

// Build an Item from a single source path. Reads ffprobe metadata,
// generates a thumbnail, classifies image vs video. Pure: no UI.
class Importer {
public:
    static ImportResult importPath(const QString& path, double defaultImageClipDuration);
    static QStringList imageExtensions();
    static QStringList videoExtensions();
    static bool looksLikeImage(const QString& path);
    static bool looksLikeVideo(const QString& path);
};

} // namespace vlip
