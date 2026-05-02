#pragma once

#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QVector>
#include <QSharedPointer>
#include <QVariantMap>

namespace vlip {

enum class ItemKind { Image, Video };

struct Common {
    QUuid id;
    QString sourcePath;
    QDateTime timestamp;            // UTC
    bool timestampUncertain = false;
    QString subtitle;
    bool used = false;              // import-default: not used
    QString thumbPath;              // cached thumbnail path

    // For warnings on load (missing source).
    bool sourceMissing = false;
};

struct ImageItem {
    Common common;
    double durationSecs = 4.0;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

struct VideoItem {
    Common common;
    double startSecs = 0.0;
    double endSecs = 0.0;           // 0 means "to end"
    double sourceDurationSecs = 0.0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    bool hasAudio = true;
};

struct Item {
    ItemKind kind;
    ImageItem image;
    VideoItem video;
    Common& common() { return kind == ItemKind::Image ? image.common : video.common; }
    const Common& common() const { return kind == ItemKind::Image ? image.common : video.common; }

    static Item makeImage(const ImageItem& i) { Item it; it.kind = ItemKind::Image; it.image = i; return it; }
    static Item makeVideo(const VideoItem& v) { Item it; it.kind = ItemKind::Video; it.video = v; return it; }

    double effectiveDuration() const;
};

struct Canvas {
    int width = 1920;
    int height = 1080;
    int fps = 30;
};

struct Defaults {
    double imageDuration = 4.0;
    double videoTrimStart = 0.0;    // last bulk values (informational)
    double videoTrimEnd = 0.0;
    double transitionSecs = 0.0;    // fade-out/fade-in duration between clips; 0 disables
};

struct Project {
    int schemaVersion = 1;
    Canvas canvas;
    Defaults defaults;
    QVector<Item> items;

    void sortChronologically();
    int indexOfId(const QUuid& id) const;
    void applyImageDurationAll(double secs);
    void trimVideoStartAll(double secs);
    void trimVideoEndAll(double secs);
};

} // namespace vlip
