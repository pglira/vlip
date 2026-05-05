#pragma once

#include <QString>
#include <QDateTime>
#include <optional>

namespace vlip {

struct ProbeResult {
    bool ok = false;
    bool isVideo = false;
    int width = 0;
    int height = 0;
    double durationSecs = 0.0;
    bool hasAudio = false;
    // True when the video stream uses an HDR transfer (HLG = arib-std-b67
    // or PQ = smpte2084). Triggers the zscale/tonemap conversion in the
    // renderer; SDR clips skip it.
    bool isHdr = false;
    std::optional<QDateTime> creationTime;   // UTC
    QString errorText;
};

// Synchronous probe via ffprobe. Returns container/stream info plus
// any creation_time tag. For images returns dimensions and EXIF
// DateTimeOriginal in creationTime when available.
class MetaProbe {
public:
    static ProbeResult probe(const QString& path);
};

} // namespace vlip
