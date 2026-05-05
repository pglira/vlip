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

enum class ItemKind { ImageClip, VideoClip, TextClip };

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

struct ImageClip {
    Common common;
    double durationSecs = 4.0;
    std::optional<QRectF> crop;     // normalized 0..1 in source pixel space
    int sourceWidth = 0;
    int sourceHeight = 0;
};

struct VideoClip {
    Common common;
    double startSecs = 0.0;
    double endSecs = 0.0;           // 0 means "to end"
    double sourceDurationSecs = 0.0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    bool hasAudio = true;
};

struct TextClip {
    Common common;                  // sourcePath stays empty for text clips
    QString text;
    QString backgroundPath;         // optional; empty → solid bgColor from defaults
    double durationSecs = 5.0;
};

struct Item {
    ItemKind kind;
    ImageClip imageClip;
    VideoClip videoClip;
    TextClip textClip;
    Common& common() {
        switch (kind) {
            case ItemKind::ImageClip: return imageClip.common;
            case ItemKind::VideoClip: return videoClip.common;
            case ItemKind::TextClip:  return textClip.common;
        }
        return imageClip.common;
    }
    const Common& common() const {
        switch (kind) {
            case ItemKind::ImageClip: return imageClip.common;
            case ItemKind::VideoClip: return videoClip.common;
            case ItemKind::TextClip:  return textClip.common;
        }
        return imageClip.common;
    }

    static Item makeImageClip(const ImageClip& c) { Item it; it.kind = ItemKind::ImageClip; it.imageClip = c; return it; }
    static Item makeVideoClip(const VideoClip& c) { Item it; it.kind = ItemKind::VideoClip; it.videoClip = c; return it; }
    static Item makeTextClip (const TextClip&  c) { Item it; it.kind = ItemKind::TextClip;  it.textClip  = c; return it; }

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
    int fontSizePx = 50;                 // sized for 1080p; user adjusts per project
    QColor fontColor   = QColor(255, 255, 255, 255);
    QColor bgColor     = QColor(0,   0,   0,   140);  // alpha controls box opacity
    QColor outlineColor    = QColor(0, 0, 0, 255);
    int    outlineWidthPx  = 0;          // 0 = no outline
    SubtitlePosition position = SubtitlePosition::Bottom;
    int    marginPx = 90;                // distance from canvas edge for Top/Bottom; ignored for Middle
    // How long, in seconds from the start of each segment, the subtitle
    // stays on screen. 0 = visible for the whole segment.
    double visibleSecs = 0.0;
};

struct TextClipStyle {
    QString fontFamily;
    int fontSizePx = 90;                 // sized for 1080p; user adjusts per project
    QColor fontColor = QColor(255, 255, 255, 255);
    QColor outlineColor    = QColor(0, 0, 0, 255);
    int    outlineWidthPx  = 0;          // 0 = no outline
    // Background is always black when no background image is set —
    // this is intentional and not user-configurable.
    VerticalAlign verticalAlign = VerticalAlign::Middle;
    double defaultDuration = 5.0;
};

// Burn each item's timestamp (formatted DD.MM.YYYY HH:MM) into a corner of
// the canvas while that item is on screen. White text, no background.
// Applied only to image and video clips (text clips are skipped).
struct DatestampStyle {
    bool active = true;
    QString fontFamily;
    int fontSizePx = 30;                 // sized for 1080p; user adjusts per project
    Corner corner = Corner::BottomRight;
    int marginPx = 20;
    // QDateTime format pattern (yyyy / MM / dd / HH / mm / ss …). Lets
    // the user pick e.g. "HH:mm" for time-only or "dd.MM." for a
    // day-and-month-only stamp.
    QString format = "dd.MM.yyyy HH:mm";
};

// Automatic audio levelling. When active, the renderer probes each
// audio source (video clips with audio + each background-music track)
// for its EBU R128 integrated loudness and applies a single per-source
// `volume=NdB` bias so every source ends up near a common target
// (-16 LUFS — consumer/streaming-typical, suits living-room TV
// playback). The final amix also switches to normalize=0 so the
// computed gains reach the output verbatim instead of being halved by
// amix's default per-input scaling. When inactive, audio is mixed at
// its native level with amix's default scaling.
struct AudioLevelling {
    bool active = true;
};

struct Defaults {
    double imageClipDuration = 4.0;
    double transitionSecs = 0.8;    // fade-out/fade-in duration between clips; 0 disables
    SubtitleStyle subtitle;
    TextClipStyle textClip;
    DatestampStyle datestamp;
    AudioLevelling audioLevelling;
    // IANA time-zone id (e.g. "Europe/Vienna"). Empty → system local.
    // Used to render per-item timestamps in the date-stamp overlay.
    QByteArray timeZone;
};

struct Project {
    int schemaVersion = 1;
    Canvas canvas;
    Defaults defaults;
    QVector<Item> items;
    // Project-wide background-music playlist. Plays during image and
    // text clips; the playhead pauses at each video clip's start and
    // resumes at its end.
    QStringList backgroundMusic;

    void sortChronologically();
    int indexOfId(const QUuid& id) const;
    void applyImageClipDurationAll(double secs);
    void applyTextClipDurationAll(double secs);
};

} // namespace vlip
