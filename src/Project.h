#pragma once

#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QRectF>
#include <QVector>
#include <QSharedPointer>
#include <QVariantMap>
#include <optional>
#include <QColor>

namespace vlip {

enum class ItemKind { Image, Video, TextClip };

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
    std::optional<QRectF> crop;     // normalized 0..1 in source pixel space
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

struct TextClipItem {
    Common common;                  // sourcePath stays empty for text clips
    QString text;
    QString backgroundPath;         // optional; empty → solid bgColor from defaults
    double durationSecs = 5.0;
};

struct Item {
    ItemKind kind;
    ImageItem image;
    VideoItem video;
    TextClipItem textClip;
    Common& common() {
        switch (kind) {
            case ItemKind::Image:    return image.common;
            case ItemKind::Video:    return video.common;
            case ItemKind::TextClip: return textClip.common;
        }
        return image.common;
    }
    const Common& common() const {
        switch (kind) {
            case ItemKind::Image:    return image.common;
            case ItemKind::Video:    return video.common;
            case ItemKind::TextClip: return textClip.common;
        }
        return image.common;
    }

    static Item makeImage(const ImageItem& i)    { Item it; it.kind = ItemKind::Image;    it.image    = i; return it; }
    static Item makeVideo(const VideoItem& v)    { Item it; it.kind = ItemKind::Video;    it.video    = v; return it; }
    static Item makeTextClip(const TextClipItem& t) { Item it; it.kind = ItemKind::TextClip; it.textClip = t; return it; }

    double effectiveDuration() const;
};

struct Canvas {
    int width = 1920;
    int height = 1080;
    int fps = 30;
};

enum class SubtitlePosition { Top, Middle, Bottom };
enum class VerticalAlign { Top, Middle, Bottom };
enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

struct SubtitleStyle {
    QString fontFamily;                  // empty → first available DejaVu / system fallback
    int fontSizePx = 0;                  // 0 → auto (canvas-relative)
    QColor fontColor   = QColor(255, 255, 255, 255);
    QColor bgColor     = QColor(0,   0,   0,   140);  // alpha controls box opacity
    SubtitlePosition position = SubtitlePosition::Bottom;
    // How long, in seconds from the start of each segment, the subtitle
    // stays on screen. 0 = visible for the whole segment.
    double visibleSecs = 0.0;
};

struct TextClipStyle {
    QString fontFamily;
    int fontSizePx = 0;                  // 0 → auto (canvas-relative)
    QColor fontColor = QColor(255, 255, 255, 255);
    // Background is always black when no background image is set —
    // this is intentional and not user-configurable.
    VerticalAlign verticalAlign = VerticalAlign::Middle;
    double defaultDuration = 5.0;
};

// Burn each item's timestamp (formatted DD.MM.YYYY HH:MM) into a corner of
// the canvas while that item is on screen. White text, no background.
// Applied only to image and video items (text clips are skipped).
struct DatestampStyle {
    bool active = true;
    QString fontFamily;
    int fontSizePx = 0;                  // 0 → auto (canvas-relative)
    Corner corner = Corner::BottomRight;
    int marginPx = 20;
};

struct Defaults {
    double imageDuration = 4.0;
    double transitionSecs = 0.8;    // fade-out/fade-in duration between clips; 0 disables
    SubtitleStyle subtitle;
    TextClipStyle textClip;
    DatestampStyle datestamp;
    // IANA time-zone id (e.g. "Europe/Vienna"). Empty → system local.
    // Used to render per-item timestamps in the date-stamp overlay.
    QByteArray timeZone;
};

struct Project {
    int schemaVersion = 1;
    Canvas canvas;
    Defaults defaults;
    QVector<Item> items;

    void sortChronologically();
    int indexOfId(const QUuid& id) const;
    void applyImageDurationAll(double secs);
    void applyTextClipDurationAll(double secs);
};

} // namespace vlip
