#include "ProjectIO.hpp"
#include "ThumbnailCache.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>

namespace vlip {

namespace {

// Read `font_size_px` from a style JSON blob. Legacy projects stored 0
// to mean "auto, canvas-relative" — the auto path is gone, so a missing
// or zero entry falls back to the struct's default size.
template <typename Style>
int loadFontSizePx(const QJsonObject& o) {
    const int defaultPx = Style{}.fontSizePx;
    const int loaded = o.value("font_size_px").toInt(defaultPx);
    return loaded > 0 ? loaded : defaultPx;
}

QJsonObject toJson(const QRectF& r) {
    QJsonObject o;
    o["x"] = r.x();
    o["y"] = r.y();
    o["w"] = r.width();
    o["h"] = r.height();
    return o;
}

QRectF rectFromJson(const QJsonObject& o) {
    return QRectF(o.value("x").toDouble(),
                  o.value("y").toDouble(),
                  o.value("w").toDouble(),
                  o.value("h").toDouble());
}

QJsonObject toJsonCommon(const Common& c) {
    QJsonObject o;
    o["id"] = c.id.toString(QUuid::WithoutBraces);
    o["source_path"] = c.sourcePath;
    o["timestamp"] = c.timestamp.toString(Qt::ISODateWithMs);
    o["timestamp_uncertain"] = c.timestampUncertain;
    o["subtitle"] = c.subtitle;
    o["used"] = c.used;
    return o;
}

Common commonFromJson(const QJsonObject& o) {
    Common c;
    c.id = QUuid::fromString(o.value("id").toString());
    if (c.id.isNull()) c.id = QUuid::createUuid();
    c.sourcePath = o.value("source_path").toString();
    c.timestamp = QDateTime::fromString(o.value("timestamp").toString(), Qt::ISODateWithMs);
    if (!c.timestamp.isValid()) {
        c.timestamp = QDateTime::fromString(o.value("timestamp").toString(), Qt::ISODate);
    }
    c.timestamp.setTimeSpec(Qt::UTC);
    c.timestampUncertain = o.value("timestamp_uncertain").toBool();
    c.subtitle = o.value("subtitle").toString();
    c.used = o.value("used").toBool();
    return c;
}

QJsonObject toJson(const Item& it) {
    QJsonObject o;
    o["common"] = toJsonCommon(it.common());
    switch (it.kind) {
        case ItemKind::ImageClip:
            o["kind"] = "image_clip";
            o["duration_secs"] = it.imageClip.durationSecs;
            if (it.imageClip.crop) o["crop"] = toJson(*it.imageClip.crop);
            o["src_w"] = it.imageClip.sourceWidth;
            o["src_h"] = it.imageClip.sourceHeight;
            break;
        case ItemKind::VideoClip:
            o["kind"] = "video_clip";
            o["start_secs"] = it.videoClip.startSecs;
            o["end_secs"] = it.videoClip.endSecs;
            o["source_duration_secs"] = it.videoClip.sourceDurationSecs;
            o["src_w"] = it.videoClip.sourceWidth;
            o["src_h"] = it.videoClip.sourceHeight;
            o["has_audio"] = it.videoClip.hasAudio;
            o["is_hdr"] = it.videoClip.isHdr;
            break;
        case ItemKind::TextClip:
            o["kind"] = "text_clip";
            o["text"] = it.textClip.text;
            o["background_path"] = it.textClip.backgroundPath;
            o["duration_secs"] = it.textClip.durationSecs;
            break;
    }
    return o;
}

Item itemFromJson(const QJsonObject& o) {
    Item it;
    QString kind = o.value("kind").toString();
    if (kind == "image_clip") {
        it.kind = ItemKind::ImageClip;
        it.imageClip.common = commonFromJson(o.value("common").toObject());
        it.imageClip.durationSecs = o.value("duration_secs").toDouble(4.0);
        if (o.contains("crop")) it.imageClip.crop = rectFromJson(o.value("crop").toObject());
        it.imageClip.sourceWidth = o.value("src_w").toInt();
        it.imageClip.sourceHeight = o.value("src_h").toInt();
    } else if (kind == "text_clip") {
        it.kind = ItemKind::TextClip;
        it.textClip.common = commonFromJson(o.value("common").toObject());
        it.textClip.text = o.value("text").toString();
        it.textClip.backgroundPath = o.value("background_path").toString();
        it.textClip.durationSecs = o.value("duration_secs").toDouble(5.0);
    } else {
        it.kind = ItemKind::VideoClip;
        it.videoClip.common = commonFromJson(o.value("common").toObject());
        it.videoClip.startSecs = o.value("start_secs").toDouble(0.0);
        it.videoClip.endSecs = o.value("end_secs").toDouble(0.0);
        it.videoClip.sourceDurationSecs = o.value("source_duration_secs").toDouble(0.0);
        it.videoClip.sourceWidth = o.value("src_w").toInt();
        it.videoClip.sourceHeight = o.value("src_h").toInt();
        it.videoClip.hasAudio = o.value("has_audio").toBool(true);
        it.videoClip.isHdr = o.value("is_hdr").toBool(false);
    }
    return it;
}

QString colorToString(const QColor& c) {
    // QColor parses 8-digit hex strings as #AARRGGBB (alpha first), so
    // we serialize in the same order — otherwise round-tripping a
    // non-opaque colour scrambles the channels on load.
    return QString("#%1%2%3%4")
        .arg(c.alpha(), 2, 16, QChar('0'))
        .arg(c.red(),   2, 16, QChar('0'))
        .arg(c.green(), 2, 16, QChar('0'))
        .arg(c.blue(),  2, 16, QChar('0'));
}

QColor colorFromString(const QString& s, const QColor& fallback) {
    if (s.isEmpty()) return fallback;
    QColor c(s);
    return c.isValid() ? c : fallback;
}

const char* positionToString(SubtitlePosition p) {
    switch (p) {
        case SubtitlePosition::Top:    return "top";
        case SubtitlePosition::Middle: return "middle";
        case SubtitlePosition::Bottom: return "bottom";
    }
    return "bottom";
}

SubtitlePosition positionFromString(const QString& s) {
    if (s == "top") return SubtitlePosition::Top;
    if (s == "middle") return SubtitlePosition::Middle;
    return SubtitlePosition::Bottom;
}

QJsonObject toJson(const SubtitleStyle& s) {
    QJsonObject o;
    o["font_family"] = s.fontFamily;
    o["font_size_px"] = s.fontSizePx;
    o["font_color"] = colorToString(s.fontColor);
    o["bg_color"] = colorToString(s.bgColor);
    o["outline_color"] = colorToString(s.outlineColor);
    o["outline_width_px"] = s.outlineWidthPx;
    o["position"] = positionToString(s.position);
    o["margin_px"] = s.marginPx;
    o["visible_secs"] = s.visibleSecs;
    return o;
}

SubtitleStyle subtitleFromJson(const QJsonObject& o) {
    SubtitleStyle s;
    s.fontFamily = o.value("font_family").toString();
    s.fontSizePx = loadFontSizePx<SubtitleStyle>(o);
    s.fontColor = colorFromString(o.value("font_color").toString(), s.fontColor);
    s.bgColor   = colorFromString(o.value("bg_color").toString(), s.bgColor);
    s.outlineColor   = colorFromString(o.value("outline_color").toString(), s.outlineColor);
    s.outlineWidthPx = o.value("outline_width_px").toInt(0);
    s.position  = positionFromString(o.value("position").toString("bottom"));
    s.marginPx  = o.value("margin_px").toInt(s.marginPx);
    s.visibleSecs = o.value("visible_secs").toDouble(0.0);
    return s;
}

const char* vAlignToString(VerticalAlign a) {
    switch (a) {
        case VerticalAlign::Top:    return "top";
        case VerticalAlign::Middle: return "middle";
        case VerticalAlign::Bottom: return "bottom";
    }
    return "middle";
}

VerticalAlign vAlignFromString(const QString& s) {
    if (s == "top") return VerticalAlign::Top;
    if (s == "bottom") return VerticalAlign::Bottom;
    return VerticalAlign::Middle;
}

QJsonObject toJson(const TextClipStyle& s) {
    QJsonObject o;
    o["font_family"] = s.fontFamily;
    o["font_size_px"] = s.fontSizePx;
    o["font_color"] = colorToString(s.fontColor);
    o["outline_color"] = colorToString(s.outlineColor);
    o["outline_width_px"] = s.outlineWidthPx;
    o["vertical_align"] = vAlignToString(s.verticalAlign);
    o["default_duration"] = s.defaultDuration;
    return o;
}

TextClipStyle textClipFromJson(const QJsonObject& o) {
    TextClipStyle s;
    s.fontFamily = o.value("font_family").toString();
    s.fontSizePx = loadFontSizePx<TextClipStyle>(o);
    s.fontColor = colorFromString(o.value("font_color").toString(), s.fontColor);
    s.outlineColor   = colorFromString(o.value("outline_color").toString(), s.outlineColor);
    s.outlineWidthPx = o.value("outline_width_px").toInt(0);
    s.verticalAlign = vAlignFromString(o.value("vertical_align").toString("middle"));
    s.defaultDuration = o.value("default_duration").toDouble(5.0);
    return s;
}

const char* cornerToString(Corner c) {
    switch (c) {
        case Corner::TopLeft:     return "top_left";
        case Corner::TopRight:    return "top_right";
        case Corner::BottomLeft:  return "bottom_left";
        case Corner::BottomRight: return "bottom_right";
    }
    return "bottom_right";
}

Corner cornerFromString(const QString& s) {
    if (s == "top_left")     return Corner::TopLeft;
    if (s == "top_right")    return Corner::TopRight;
    if (s == "bottom_left")  return Corner::BottomLeft;
    return Corner::BottomRight;
}

QJsonObject toJson(const DatestampStyle& d) {
    QJsonObject o;
    o["active"] = d.active;
    o["font_family"] = d.fontFamily;
    o["font_size_px"] = d.fontSizePx;
    o["corner"] = cornerToString(d.corner);
    o["margin_px"] = d.marginPx;
    o["format"] = d.format;
    return o;
}

DatestampStyle datestampFromJson(const QJsonObject& o) {
    DatestampStyle d;
    d.active = o.value("active").toBool(true);
    d.fontFamily = o.value("font_family").toString();
    d.fontSizePx = loadFontSizePx<DatestampStyle>(o);
    d.corner = cornerFromString(o.value("corner").toString("bottom_right"));
    d.marginPx = o.value("margin_px").toInt(20);
    d.format = o.value("format").toString(d.format);
    return d;
}

QJsonObject toJson(const AudioLevelling& a) {
    QJsonObject o;
    o["active"] = a.active;
    return o;
}

AudioLevelling audioLevellingFromJson(const QJsonObject& o) {
    AudioLevelling a;
    a.active = o.value("active").toBool(a.active);
    return a;
}

} // namespace

bool ProjectIO::save(const Project& p, const QString& path, QString* err) {
    QJsonObject root;
    root["schema_version"] = p.schemaVersion;
    root["app"] = "vlip";

    QJsonObject canvas;
    canvas["width"] = p.canvas.width;
    canvas["height"] = p.canvas.height;
    canvas["fps"] = p.canvas.fps;
    root["canvas"] = canvas;

    QJsonObject defaults;
    defaults["image_clip_duration"] = p.defaults.imageClipDuration;
    defaults["transition_secs"] = p.defaults.transitionSecs;
    defaults["subtitle"] = toJson(p.defaults.subtitle);
    defaults["text_clip"] = toJson(p.defaults.textClip);
    defaults["datestamp"] = toJson(p.defaults.datestamp);
    defaults["audio_levelling"] = toJson(p.defaults.audioLevelling);
    defaults["time_zone"] = QString::fromUtf8(p.defaults.timeZone);
    root["defaults"] = defaults;

    QJsonArray items;
    for (const auto& it : p.items) items.append(toJson(it));
    root["items"] = items;

    QJsonArray bgMusic;
    for (const auto& path : p.backgroundMusic) bgMusic.append(path);
    root["background_music"] = bgMusic;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = QStringLiteral("Cannot open for writing: %1").arg(f.errorString());
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (err) *err = QStringLiteral("Commit failed: %1").arg(f.errorString());
        return false;
    }
    return true;
}

bool ProjectIO::load(Project* p, const QString& path, QStringList* warnings, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("Cannot open: %1").arg(f.errorString());
        return false;
    }
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (err) *err = QStringLiteral("Parse error: %1").arg(pe.errorString());
        return false;
    }
    auto root = doc.object();
    p->schemaVersion = root.value("schema_version").toInt(1);

    auto canvas = root.value("canvas").toObject();
    p->canvas.width = canvas.value("width").toInt(1920);
    p->canvas.height = canvas.value("height").toInt(1080);
    p->canvas.fps = canvas.value("fps").toInt(30);

    auto defaults = root.value("defaults").toObject();
    p->defaults.imageClipDuration = defaults.value("image_clip_duration").toDouble(4.0);
    p->defaults.transitionSecs = defaults.value("transition_secs").toDouble(0.8);
    if (defaults.contains("subtitle")) {
        p->defaults.subtitle = subtitleFromJson(defaults.value("subtitle").toObject());
    }
    if (defaults.contains("text_clip")) {
        p->defaults.textClip = textClipFromJson(defaults.value("text_clip").toObject());
    }
    if (defaults.contains("datestamp")) {
        p->defaults.datestamp = datestampFromJson(defaults.value("datestamp").toObject());
    }
    if (defaults.contains("audio_levelling")) {
        p->defaults.audioLevelling =
            audioLevellingFromJson(defaults.value("audio_levelling").toObject());
    }
    if (defaults.contains("time_zone")) {
        p->defaults.timeZone = defaults.value("time_zone").toString().toUtf8();
    }

    p->items.clear();
    for (auto v : root.value("items").toArray()) {
        Item it = itemFromJson(v.toObject());
        // Text clips have no source file path; everything else must exist.
        if (it.kind == ItemKind::TextClip) {
            const auto& bg = it.textClip.backgroundPath;
            if (!bg.isEmpty() && !QFileInfo::exists(bg)) {
                if (warnings) {
                    warnings->append(QStringLiteral(
                        "Text-clip background missing: %1").arg(bg));
                }
            }
            p->items.append(it);
            continue;
        }
        if (!QFileInfo::exists(it.common().sourcePath)) {
            it.common().sourceMissing = true;
            if (warnings) {
                warnings->append(QStringLiteral("Source file missing: %1").arg(it.common().sourcePath));
            }
        } else {
            // thumbPath is a derived path (keyed by mtime/size) — never
            // serialized. Repopulate from the cache, regenerating if needed.
            const bool isVideo = (it.kind == ItemKind::VideoClip);
            it.common().thumbPath =
                ThumbnailCache::getOrCreate(it.common().sourcePath, isVideo, 256);
        }
        p->items.append(it);
    }
    p->sortChronologically();

    p->backgroundMusic.clear();
    for (auto v : root.value("background_music").toArray()) {
        // Backwards compat: a short-lived in-development format wrapped
        // each entry in a {path, volume_db} object; older projects
        // stored a plain string. Read either.
        const QString musicPath = v.isString()
            ? v.toString()
            : v.toObject().value("path").toString();
        if (musicPath.isEmpty()) continue;
        p->backgroundMusic.append(musicPath);
        if (!QFileInfo::exists(musicPath) && warnings) {
            warnings->append(QStringLiteral("Background-music file missing: %1").arg(musicPath));
        }
    }
    return true;
}

} // namespace vlip
