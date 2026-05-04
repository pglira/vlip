#include "MetaProbe.hpp"

#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>

namespace vlip {

namespace {

std::optional<QDateTime> parseDateGuess(const QString& s) {
    if (s.isEmpty()) return std::nullopt;
    // EXIF DateTimeOriginal: "YYYY:MM:DD HH:MM:SS"
    QDateTime dt = QDateTime::fromString(s, "yyyy:MM:dd HH:mm:ss");
    if (dt.isValid()) { dt.setTimeSpec(Qt::UTC); return dt; }
    // ISO with Z
    dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (dt.isValid()) return dt.toUTC();
    dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) return dt.toUTC();
    // FFmpeg sometimes emits "YYYY-MM-DD HH:MM:SS"
    dt = QDateTime::fromString(s, "yyyy-MM-dd HH:mm:ss");
    if (dt.isValid()) { dt.setTimeSpec(Qt::UTC); return dt; }
    return std::nullopt;
}

} // namespace

ProbeResult MetaProbe::probe(const QString& path) {
    ProbeResult r;
    QFileInfo fi(path);
    if (!fi.exists()) { r.errorText = "File not found"; return r; }

    QProcess p;
    p.start("ffprobe", {
        "-v", "error",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        path
    });
    if (!p.waitForStarted(5000)) { r.errorText = "ffprobe not available"; return r; }
    if (!p.waitForFinished(15000)) { p.kill(); r.errorText = "ffprobe timed out"; return r; }
    if (p.exitCode() != 0) {
        r.errorText = QString::fromUtf8(p.readAllStandardError());
        return r;
    }
    auto doc = QJsonDocument::fromJson(p.readAllStandardOutput());
    if (!doc.isObject()) { r.errorText = "ffprobe parse error"; return r; }
    auto root = doc.object();
    auto streams = root.value("streams").toArray();
    auto format = root.value("format").toObject();

    bool sawVideo = false;
    bool hasMotion = false; // duration > 0 with frame count > 1 implies motion video
    for (auto v : streams) {
        auto s = v.toObject();
        QString type = s.value("codec_type").toString();
        if (type == "video") {
            sawVideo = true;
            if (r.width == 0) {
                r.width = s.value("width").toInt();
                r.height = s.value("height").toInt();
            }
            int nbFrames = s.value("nb_frames").toString().toInt();
            // If avg_frame_rate is "0/0", or nb_frames is 1, this is an image.
            if (nbFrames > 1) hasMotion = true;
            QString durStr = s.value("duration").toString();
            if (durStr.isEmpty()) durStr = format.value("duration").toString();
            double d = durStr.toDouble();
            if (d > 0.04) hasMotion = true;
            // creation_time on stream tags
            auto tags = s.value("tags").toObject();
            QString ct = tags.value("creation_time").toString();
            if (!ct.isEmpty() && !r.creationTime) {
                r.creationTime = parseDateGuess(ct);
            }
            QString dto = tags.value("DateTimeOriginal").toString();
            if (!dto.isEmpty() && !r.creationTime) {
                r.creationTime = parseDateGuess(dto);
            }
        } else if (type == "audio") {
            r.hasAudio = true;
        }
    }
    // Format-level duration & creation_time
    double fmtDur = format.value("duration").toString().toDouble();
    auto fmtTags = format.value("tags").toObject();
    QString fmtCT = fmtTags.value("creation_time").toString();
    if (fmtCT.isEmpty()) fmtCT = fmtTags.value("com.apple.quicktime.creationdate").toString();
    QString fmtDTO = fmtTags.value("DateTimeOriginal").toString();
    if (fmtDTO.isEmpty()) fmtDTO = fmtTags.value("DateTime").toString();

    if (!r.creationTime && !fmtCT.isEmpty()) r.creationTime = parseDateGuess(fmtCT);
    if (!r.creationTime && !fmtDTO.isEmpty()) r.creationTime = parseDateGuess(fmtDTO);

    r.isVideo = sawVideo && hasMotion;
    if (r.isVideo) {
        r.durationSecs = fmtDur;
    }
    r.ok = sawVideo;
    if (!r.ok) r.errorText = "no decodable streams";
    return r;
}

} // namespace vlip
