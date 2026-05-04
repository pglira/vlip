#include "ThumbnailCache.hpp"

#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QProcess>

namespace vlip {

QString ThumbnailCache::cacheDir() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (d.isEmpty()) d = QDir::tempPath() + "/vlip-cache";
    QDir().mkpath(d + "/thumbs");
    return d + "/thumbs";
}

QString ThumbnailCache::keyPath(const QString& sourcePath, int targetSize) {
    QFileInfo fi(sourcePath);
    QByteArray key;
    key += fi.absoluteFilePath().toUtf8();
    key += "|";
    key += QByteArray::number(fi.lastModified().toMSecsSinceEpoch());
    key += "|";
    key += QByteArray::number(fi.size());
    key += "|";
    key += QByteArray::number(targetSize);
    QByteArray hex = QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex();
    return cacheDir() + "/" + QString::fromLatin1(hex) + ".jpg";
}

QString ThumbnailCache::getOrCreate(const QString& sourcePath, bool isVideo, int targetSize) {
    QString out = keyPath(sourcePath, targetSize);
    if (QFileInfo::exists(out)) return out;

    QStringList args;
    args << "-y" << "-hide_banner" << "-loglevel" << "error";
    if (isVideo) {
        // Seek a bit into the clip for a representative frame.
        args << "-ss" << "1" << "-i" << sourcePath << "-frames:v" << "1";
    } else {
        args << "-i" << sourcePath << "-frames:v" << "1";
    }
    args << "-vf" << QString("scale='min(%1,iw)':-2").arg(targetSize);
    args << "-q:v" << "5";
    args << out;

    QProcess p;
    p.start("ffmpeg", args);
    if (!p.waitForStarted(5000)) return {};
    if (!p.waitForFinished(20000)) { p.kill(); return {}; }
    if (p.exitCode() != 0) return {};
    return out;
}

} // namespace vlip
