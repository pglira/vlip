#include "MetaProbe.hpp"

#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QRegularExpression>

#include <exiv2/exiv2.hpp>

namespace vlip {

namespace {

std::optional<QDateTime> parseDateGuess(const QString& s) {
    if (s.isEmpty()) return std::nullopt;
    // EXIF DateTimeOriginal: "YYYY:MM:DD HH:MM:SS" — camera-local time, no
    // timezone. We can't recover the offset here; treat as LocalTime, which
    // is the least-wrong default. The image-side path uses exiv2 directly
    // and consults OffsetTimeOriginal, so this fallback only fires for video
    // tags that happen to use the EXIF format.
    QDateTime dt = QDateTime::fromString(s, "yyyy:MM:dd HH:mm:ss");
    if (dt.isValid()) { dt.setTimeSpec(Qt::LocalTime); return dt.toUTC(); }
    // ISO with Z
    dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (dt.isValid()) return dt.toUTC();
    dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) return dt.toUTC();
    // FFmpeg's normalised form for QuickTime creation_time is UTC.
    dt = QDateTime::fromString(s, "yyyy-MM-dd HH:mm:ss");
    if (dt.isValid()) { dt.setTimeSpec(Qt::UTC); return dt; }
    return std::nullopt;
}

// Read DateTimeOriginal (+ SubSecTimeOriginal + OffsetTimeOriginal) from
// an image file via libexiv2. Returns UTC. Handles JPEG, TIFF, HEIC, PNG
// and any other format exiv2 can open.
std::optional<QDateTime> readExifCreationTime(const QString& path) {
    try {
        auto image = Exiv2::ImageFactory::open(path.toStdString());
        if (!image.get()) return std::nullopt;
        image->readMetadata();
        const auto& exif = image->exifData();
        if (exif.empty()) return std::nullopt;

        auto find = [&](const char* key) {
            return exif.findKey(Exiv2::ExifKey(key));
        };

        auto dtIt = find("Exif.Photo.DateTimeOriginal");
        if (dtIt == exif.end()) dtIt = find("Exif.Photo.DateTimeDigitized");
        if (dtIt == exif.end()) dtIt = find("Exif.Image.DateTime");
        if (dtIt == exif.end()) return std::nullopt;

        QString dtStr = QString::fromStdString(dtIt->toString()).trimmed();
        QDate date = QDate::fromString(dtStr.left(10), "yyyy:MM:dd");
        QTime time = QTime::fromString(dtStr.mid(11, 8), "HH:mm:ss");
        if (!date.isValid() || !time.isValid()) return std::nullopt;

        // SubSecTimeOriginal is the fractional digits after the decimal
        // point (e.g. "786" → .786 s = 786 ms; "5" → .5 s = 500 ms).
        int msec = 0;
        auto subIt = find("Exif.Photo.SubSecTimeOriginal");
        if (subIt == exif.end()) subIt = find("Exif.Photo.SubSecTimeDigitized");
        if (subIt == exif.end()) subIt = find("Exif.Photo.SubSecTime");
        if (subIt != exif.end()) {
            QString s = QString::fromStdString(subIt->toString()).trimmed();
            QString padded = (s + "000").left(3);  // left-justify to 3 digits
            bool ok = false;
            int v = padded.toInt(&ok);
            if (ok) msec = v;
        }
        time = time.addMSecs(msec);

        // Apply OffsetTimeOriginal ("+02:00") if present, else treat as
        // LocalTime. Newer iPhones always write the offset tag.
        auto offIt = find("Exif.Photo.OffsetTimeOriginal");
        if (offIt == exif.end()) offIt = find("Exif.Photo.OffsetTimeDigitized");
        if (offIt == exif.end()) offIt = find("Exif.Photo.OffsetTime");
        if (offIt != exif.end()) {
            QString off = QString::fromStdString(offIt->toString()).trimmed();
            QRegularExpression rx(QStringLiteral(R"(([+-])(\d{2}):(\d{2}))"));
            auto m = rx.match(off);
            if (m.hasMatch()) {
                int sign = (m.captured(1) == QLatin1String("-")) ? -1 : 1;
                int hh = m.captured(2).toInt();
                int mm = m.captured(3).toInt();
                int seconds = sign * (hh * 3600 + mm * 60);
                QDateTime dt(date, time, Qt::OffsetFromUTC, seconds);
                return dt.toUTC();
            }
        }
        QDateTime dt(date, time, Qt::LocalTime);
        return dt.toUTC();
    } catch (const std::exception&) {
        return std::nullopt;
    }
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

    // ffprobe doesn't expose EXIF for JPEG/TIFF/HEIC stills, so for a
    // still image without a creation time, read the EXIF block directly.
    if (!r.creationTime && r.ok && !r.isVideo) {
        r.creationTime = readExifCreationTime(path);
    }

    return r;
}

} // namespace vlip
