#include "ProjectIO.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>

namespace vlip {

namespace {

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
    if (it.kind == ItemKind::Image) {
        o["kind"] = "image";
        o["duration_secs"] = it.image.durationSecs;
        o["src_w"] = it.image.sourceWidth;
        o["src_h"] = it.image.sourceHeight;
    } else {
        o["kind"] = "video";
        o["start_secs"] = it.video.startSecs;
        o["end_secs"] = it.video.endSecs;
        o["source_duration_secs"] = it.video.sourceDurationSecs;
        o["src_w"] = it.video.sourceWidth;
        o["src_h"] = it.video.sourceHeight;
        o["has_audio"] = it.video.hasAudio;
    }
    return o;
}

Item itemFromJson(const QJsonObject& o) {
    Item it;
    QString kind = o.value("kind").toString();
    if (kind == "image") {
        it.kind = ItemKind::Image;
        it.image.common = commonFromJson(o.value("common").toObject());
        it.image.durationSecs = o.value("duration_secs").toDouble(4.0);
        it.image.sourceWidth = o.value("src_w").toInt();
        it.image.sourceHeight = o.value("src_h").toInt();
    } else {
        it.kind = ItemKind::Video;
        it.video.common = commonFromJson(o.value("common").toObject());
        it.video.startSecs = o.value("start_secs").toDouble(0.0);
        it.video.endSecs = o.value("end_secs").toDouble(0.0);
        it.video.sourceDurationSecs = o.value("source_duration_secs").toDouble(0.0);
        it.video.sourceWidth = o.value("src_w").toInt();
        it.video.sourceHeight = o.value("src_h").toInt();
        it.video.hasAudio = o.value("has_audio").toBool(true);
    }
    return it;
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
    defaults["image_duration"] = p.defaults.imageDuration;
    defaults["video_trim_start"] = p.defaults.videoTrimStart;
    defaults["video_trim_end"] = p.defaults.videoTrimEnd;
    defaults["transition_secs"] = p.defaults.transitionSecs;
    root["defaults"] = defaults;

    QJsonArray items;
    for (const auto& it : p.items) items.append(toJson(it));
    root["items"] = items;

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
    p->defaults.imageDuration = defaults.value("image_duration").toDouble(4.0);
    p->defaults.videoTrimStart = defaults.value("video_trim_start").toDouble(0.0);
    p->defaults.videoTrimEnd = defaults.value("video_trim_end").toDouble(0.0);
    p->defaults.transitionSecs = defaults.value("transition_secs").toDouble(0.0);

    p->items.clear();
    for (auto v : root.value("items").toArray()) {
        Item it = itemFromJson(v.toObject());
        if (!QFileInfo::exists(it.common().sourcePath)) {
            it.common().sourceMissing = true;
            if (warnings) {
                warnings->append(QStringLiteral("Source file missing: %1").arg(it.common().sourcePath));
            }
        }
        p->items.append(it);
    }
    p->sortChronologically();
    return true;
}

} // namespace vlip
