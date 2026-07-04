#include "Renderer.hpp"
#include "DrawtextFormulas.hpp"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStringList>
#include <QProcess>
#include <QDateTime>
#include <QUuid>
#include <QSet>
#include <QRegularExpression>
#include <cmath>
#include <optional>

namespace vlip {

Renderer::Renderer(QObject* parent) : QObject(parent) {}
Renderer::~Renderer() { cancel(); cleanupTempDir(); }

QString Renderer::validate(const Project& p) {
    int usedCount = 0;
    for (const auto& it : p.items) {
        if (!it.common().used) continue;
        // Text clips don't have a source-file path; everything else does.
        if (it.kind != ItemKind::TextClip
            && (it.common().sourceMissing || !QFileInfo::exists(it.common().sourcePath))) continue;
        if (it.effectiveDuration() <= 0.001) continue;
        usedCount++;
    }
    if (usedCount == 0) {
        return QStringLiteral("No items are marked as 'used'. Tick items in the timeline before rendering.");
    }
    if (p.canvas.width <= 0 || p.canvas.height <= 0 || p.canvas.fps <= 0) {
        return QStringLiteral("Invalid canvas: width=%1 height=%2 fps=%3")
            .arg(p.canvas.width).arg(p.canvas.height).arg(p.canvas.fps);
    }
    return {};
}

bool Renderer::isRunning() const {
    return m_state != State::Idle
        && m_proc && m_proc->state() != QProcess::NotRunning;
}

void Renderer::cancel() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_cancelRequested = true;
        m_proc->terminate();
        if (!m_proc->waitForFinished(2000)) m_proc->kill();
    }
}

void Renderer::cleanupTempDir() {
    if (!m_workDir.isEmpty() && QFileInfo(m_workDir).exists()) {
        QDir(m_workDir).removeRecursively();
    }
    m_workDir.clear();
    m_intermediates.clear();
}

void Renderer::abortWithFailure(const QString& msg) {
    cleanupTempDir();
    m_state = State::Idle;
    emit finished(false, msg);
}

bool Renderer::start(const Project& p, const QString& outPath) {
    if (m_state != State::Idle) {
        emit finished(false, QStringLiteral("A render is already in progress."));
        return false;
    }
    QString err = validate(p);
    if (!err.isEmpty()) { emit finished(false, err); return false; }

    cleanupTempDir();

    m_outPath = outPath;
    m_logTail.clear();
    m_lastProgressLogMs = 0;
    m_cancelRequested = false;
    m_renderProject = p;
    m_intermediates.clear();
    m_batchIndex = 0;
    m_completedBatchDuration = 0.0;
    m_videoWindows.clear();
    m_usedIndices.clear();
    m_batchDurations.clear();
    m_totalDuration = 0.0;

    // Pick an effective batch size from canvas resolution. Each segment
    // holds a decoder + filter chain + frame buffers in parallel; at 4K
    // ten of them in one ffmpeg process pushes memory past 32 GB.
    const long long basePixels = qint64(kBaseCanvasW) * kBaseCanvasH;
    const long long pixels = qint64(p.canvas.width) * p.canvas.height;
    m_batchSize = (pixels > 0)
        ? int(std::clamp<long long>(qint64(kBaseBatchSize) * basePixels / pixels,
                                    1LL, kBaseBatchSize))
        : kBaseBatchSize;

    // Single pass over the items: collect indices of items that survive
    // validation, accumulate the total + video-clip windows for music
    // ducking, and bin per-item durations into batches.
    const int FPS = p.canvas.fps;
    double currentBatchDur = 0.0;
    int currentBatchCount = 0;
    for (int i = 0; i < p.items.size(); ++i) {
        const Item& it = p.items[i];
        if (!it.common().used) continue;
        if (it.kind != ItemKind::TextClip
            && (it.common().sourceMissing || !QFileInfo::exists(it.common().sourcePath))) continue;
        if (it.effectiveDuration() <= 0.001) continue;

        const double dur =
            std::max(1.0 / FPS, std::round(it.effectiveDuration() * FPS) / FPS);
        if (it.kind == ItemKind::VideoClip) {
            m_videoWindows.append({m_totalDuration, m_totalDuration + dur});
        }
        m_totalDuration += dur;

        m_usedIndices.push_back(i);
        currentBatchDur += dur;
        if (++currentBatchCount == m_batchSize) {
            m_batchDurations.append(currentBatchDur);
            currentBatchDur = 0.0;
            currentBatchCount = 0;
        }
    }
    if (currentBatchCount > 0) m_batchDurations.append(currentBatchDur);

    m_workDir = QDir::temp().absoluteFilePath(
        QString("vlip-render-%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
    if (!QDir().mkpath(m_workDir)) {
        emit finished(false, QString("Could not create temp dir: %1").arg(m_workDir));
        return false;
    }

    emit log(QString("Rendering %1 items in %2 batches of up to %3 (canvas %4×%5).")
             .arg(m_usedIndices.size())
             .arg(m_batchDurations.size())
             .arg(m_batchSize)
             .arg(p.canvas.width)
             .arg(p.canvas.height));

    // Build the loudness-probe queue: every distinct audio source the
    // render will actually use (video clips with audio + music tracks).
    // Skipped entirely when automatic levelling is off.
    m_probeQueue.clear();
    m_audioGainsDb.clear();
    m_currentProbePath.clear();
    if (p.defaults.audioLevelling.active) {
        QSet<QString> seen;
        for (int idx : m_usedIndices) {
            const Item& it = p.items[idx];
            if (it.kind != ItemKind::VideoClip) continue;
            if (!it.videoClip.hasAudio) continue;
            const QString& path = it.common().sourcePath;
            if (path.isEmpty() || seen.contains(path)) continue;
            if (!QFileInfo::exists(path)) continue;
            seen.insert(path);
            m_probeQueue.append(path);
        }
        for (const QString& path : p.backgroundMusic) {
            if (path.isEmpty() || seen.contains(path)) continue;
            if (!QFileInfo::exists(path)) continue;
            seen.insert(path);
            m_probeQueue.append(path);
        }
    }
    m_probeTotal = m_probeQueue.size();

    QString runErr;
    if (!m_probeQueue.isEmpty()) {
        m_state = State::Probing;
        emit log(QString("Measuring loudness of %1 audio source(s) (target %2 LUFS)…")
                 .arg(m_probeTotal).arg(kTargetLufs, 0, 'f', 0));
        if (!startNextProbe(&runErr)) {
            abortWithFailure(runErr);
            return false;
        }
    } else {
        m_state = State::Batches;
        if (!startNextBatch(&runErr)) {
            abortWithFailure(runErr);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Per-segment filter-chain construction. Split into video-only (used per
// batch) and audio-only (used in the final concat pass over the entire
// timeline) so audio is encoded in a single uninterrupted run — no AAC
// priming-delay slop accumulating per batch boundary.
// ---------------------------------------------------------------------------
namespace {

double segmentDuration(const Item& it, int FPS) {
    // Quantise to whole frames so each clip's audio chain (atrim=0:dur)
    // and its visual length agree exactly.
    return std::max(1.0 / FPS, std::round(it.effectiveDuration() * FPS) / FPS);
}

// Per-clip fade durations. The very first item of the whole timeline has
// no fade-in (the render opens directly on its first frame); every other
// clip fades in over `transition`. Every clip fades out at its end so
// concatenation meets at "black" between clips. Each fade is clamped so
// fade-in + fade-out don't overlap.
struct ClipFade { double fadeIn = 0.0; double fadeOut = 0.0; };

ClipFade clipFadesFor(int globalIdx, double dur, double transition) {
    ClipFade f;
    f.fadeIn  = (transition > 0.0 && globalIdx > 0) ? transition : 0.0;
    f.fadeOut = (transition > 0.0)                  ? transition : 0.0;
    const double maxEach = dur / 2.0;
    if (f.fadeIn  > maxEach) f.fadeIn  = maxEach;
    if (f.fadeOut > maxEach) f.fadeOut = maxEach;
    return f;
}

// Build a comma-prefixed fade chain (",fade=in:…,fade=out:…") suitable
// for inlining into a video filter chain. Empty when both fades are zero.
QString videoFadeChain(const ClipFade& f, double dur) {
    QString s;
    if (f.fadeIn > 0.0) {
        s += QString(",fade=in:st=0:d=%1").arg(f.fadeIn, 0, 'f', 4);
    }
    if (f.fadeOut > 0.0) {
        const double st = std::max(0.0, dur - f.fadeOut);
        s += QString(",fade=out:st=%1:d=%2").arg(st, 0, 'f', 4).arg(f.fadeOut, 0, 'f', 4);
    }
    return s;
}

// Same idea, audio side: "afade=in/out" instead of "fade=in/out".
QString audioFadeChain(const ClipFade& f, double dur) {
    QString s;
    if (f.fadeIn > 0.0) {
        s += QString(",afade=in:st=0:d=%1").arg(f.fadeIn, 0, 'f', 4);
    }
    if (f.fadeOut > 0.0) {
        const double st = std::max(0.0, dur - f.fadeOut);
        s += QString(",afade=out:st=%1:d=%2").arg(st, 0, 'f', 4).arg(f.fadeOut, 0, 'f', 4);
    }
    return s;
}

// Canvas conform filters: letterbox-scale to W×H, set 1:1 SAR, lock to
// FPS, and force yuv420p so every batch's output bitstream has identical
// codec parameters (a hard requirement for concat-demux + -c copy).
//
// in_range=auto:out_range=tv makes libswscale honour the input frame's
// color_range tag — full-range JPEG/yuvj420p sources get remapped to TV
// range explicitly instead of relying on implicit detection, so the
// encoded TV-range output isn't washed-out or clipped.
QString canvasConformFilters(int W, int H, int FPS) {
    return QString("scale=%1:%2:force_original_aspect_ratio=decrease"
                   ":in_range=auto:out_range=tv,"
                   "pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black,"
                   "setsar=1,fps=%3,format=yuv420p").arg(W).arg(H).arg(FPS);
}

// HDR → SDR conversion for HLG / PQ video clips. Linearise via zscale,
// promote to gbrpf32le so the tonemap filter sees float RGB, drop the
// primaries to BT.709, run a Hable curve to compress luminance, then
// re-encode in BT.709 transfer + matrix + tv range. Comma-prefixed so
// it inlines before canvasConformFilters in the per-clip chain.
QString hdrToSdrFilters() {
    return QStringLiteral(
        "zscale=t=linear:npl=100,"
        "format=gbrpf32le,"
        "zscale=p=bt709,"
        "tonemap=tonemap=hable:desat=0,"
        "zscale=t=bt709:m=bt709:r=tv,"
        "format=yuv420p,");
}

// Rotate + crop prefix for an image clip. Empty when neither rotation
// nor crop is in effect. Returned with a trailing comma so it slots in
// before the canvas-conform filters.
//
// Rotation runs first into an expanded bounding box (corner pixels are
// black). The crop is normalized over that bbox, so it's still valid when
// rotation is 0 (bbox = source). ffmpeg's `rotate` takes radians and
// rotates clockwise for positive angle (matches QPainter::rotate, so
// preview and render agree). `rotw(A)` / `roth(A)` are ffmpeg built-ins
// that compute the bbox at angle A.
//
// Width/height/offsets are even-aligned so the eventual yuv420p chroma
// plane stays aligned — odd-pixel crops otherwise rely on ffmpeg's
// internal rounding, which can shift the visible window by a pixel.
QString rotateAndCropFilterFor(const ImageClip& img) {
    if (img.sourceWidth <= 0 || img.sourceHeight <= 0) return {};
    const double deg = img.rotationDegrees;
    const bool rotates = std::abs(deg) > 1e-4;
    if (!rotates && !img.crop) return {};

    const double rad = deg * M_PI / 180.0;
    const double absCos = std::abs(std::cos(rad));
    const double absSin = std::abs(std::sin(rad));
    const double sw = img.sourceWidth, sh = img.sourceHeight;
    // Rotated bbox dims, matched to ffmpeg's rotw/roth ceil semantics.
    const int rw = int(std::ceil(sw * absCos + sh * absSin));
    const int rh = int(std::ceil(sw * absSin + sh * absCos));

    QString out;
    if (rotates) {
        // Pin the output size with explicit rotw/roth expressions — the
        // default `ow=iw,oh=ih` would clip the corners.
        out += QString("rotate=%1:c=black:ow=rotw(%1):oh=roth(%1),")
                  .arg(rad, 0, 'f', 6);
    }
    if (img.crop) {
        const QRectF& r = *img.crop;
        int cx = std::clamp(int(std::round(r.x() * rw)), 0, rw - 1);
        int cy = std::clamp(int(std::round(r.y() * rh)), 0, rh - 1);
        int cw = std::clamp(int(std::round(r.width() * rw)), 1, rw - cx);
        int ch = std::clamp(int(std::round(r.height() * rh)), 1, rh - cy);
        cx &= ~1; cy &= ~1;
        cw &= ~1; ch &= ~1;
        if (cw < 2) cw = 2;
        if (ch < 2) ch = 2;
        out += QString("crop=%1:%2:%3:%4,").arg(cw).arg(ch).arg(cx).arg(cy);
    }
    return out;
}

// Per-clip drawtext overlays (subtitle + datestamp for image/video,
// the text-clip body otherwise). Comma-prefixed so it inlines after the
// canvas filters; empty when the clip has nothing to draw.
QString overlayDrawtextFor(const Item& it, const Project& p, double dur,
                           const QString& textWorkDir) {
    QString out;
    if (it.kind == ItemKind::TextClip) {
        const QString dt = textClipDrawText(it.textClip.text,
                                            p.defaults.textClip, textWorkDir);
        if (!dt.isEmpty()) out += "," + dt;
        return out;
    }
    const QString dt = subtitleDrawText(it.common().subtitle,
                                        p.defaults.subtitle, dur, true, textWorkDir);
    if (!dt.isEmpty()) out += "," + dt;
    const QString ds = datestampDrawText(
        formatDatestamp(it.common().timestamp,
                        p.defaults.timeZone, p.defaults.datestamp.format),
        p.defaults.datestamp, textWorkDir);
    if (!ds.isEmpty()) out += "," + ds;
    return out;
}

// Append the `-i` block(s) for a single item's visual source. Returns
// the input index of the video stream the filter chain should reference
// as `[N:v]`. Audio is intentionally suppressed at the input layer
// (-an for video clips); the concat pass reopens audio separately.
int appendVideoInput(const Item& it, double dur, int W, int H, int FPS,
                     QStringList& args, int& inputIndex)
{
    if (it.kind == ItemKind::TextClip) {
        const auto& tc = it.textClip;
        if (!tc.backgroundPath.isEmpty() && QFileInfo::exists(tc.backgroundPath)) {
            args << "-loop" << "1" << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << tc.backgroundPath;
        } else {
            args << "-f" << "lavfi"
                 << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << QString("color=c=black:size=%1x%2:rate=%3")
                            .arg(W).arg(H).arg(FPS);
        }
    } else if (it.kind == ItemKind::ImageClip) {
        args << "-loop" << "1" << "-t" << QString::number(dur, 'f', 4)
             << "-i" << it.imageClip.common.sourcePath;
    } else {
        const auto& vid = it.videoClip;
        args << "-ss" << QString::number(vid.startSecs, 'f', 4)
             << "-t"  << QString::number(dur, 'f', 4)
             << "-an"   // audio is reopened in the concat pass
             << "-i"  << vid.common.sourcePath;
    }
    return inputIndex++;
}

struct VideoBuild {
    QString concatLabels;   // "[v0][v1]…" for the batch's concat=v=1:a=0
    int nUsed = 0;
};

// Builds the per-item video chains for a slice [usedFirst, usedFirst+count)
// of `usedIndices`. The slice's starting index doubles as the global
// timeline index of its first item (used to suppress fade-in on item 0).
// Appends the corresponding visual `-i` inputs to `args`. Audio is not
// touched here — the concat pass owns it.
VideoBuild buildVideoSegments(const Project& p, const QVector<int>& usedIndices,
                              int usedFirst, int count,
                              const QString& textWorkDir,
                              QStringList& args, QStringList& chains,
                              int& inputIndex)
{
    VideoBuild b;
    const int W = p.canvas.width, H = p.canvas.height, FPS = p.canvas.fps;
    const double transition = std::max(0.0, p.defaults.transitionSecs);

    for (int k = 0; k < count; ++k) {
        const Item& it = p.items[usedIndices[usedFirst + k]];
        const int globalIdx = usedFirst + k;
        const double dur = segmentDuration(it, FPS);
        const QString vFade = videoFadeChain(clipFadesFor(globalIdx, dur, transition), dur);
        const QString vlabel = QString("v%1").arg(b.nUsed);
        const int vIdx = appendVideoInput(it, dur, W, H, FPS, args, inputIndex);

        QString chain = QString("[%1:v]").arg(vIdx);
        if (it.kind == ItemKind::ImageClip) chain += rotateAndCropFilterFor(it.imageClip);
        if (it.kind == ItemKind::VideoClip && it.videoClip.isHdr) chain += hdrToSdrFilters();
        chain += canvasConformFilters(W, H, FPS);
        chain += overlayDrawtextFor(it, p, dur, textWorkDir);
        chain += vFade;
        chain += QString("[%1]").arg(vlabel);
        chains << chain;

        b.concatLabels += QString("[%1]").arg(vlabel);
        b.nUsed++;
    }
    return b;
}

struct AudioBuild {
    QString concatLabels;   // "[a0][a1]…" for the concat-pass concat=v=0:a=1
    int nUsed = 0;
};

// Builds the per-item audio chains for the WHOLE timeline and appends the
// corresponding audio `-i` inputs to `args`. Audio for image/text clips
// and silent video clips is anullsrc; video clips with audio reopen the
// source file with -ss/-t/-vn so only the relevant slice's audio decodes.
//
// `gainsDb` is consulted only when `audioLevelling` is active: it maps
// each video-clip source path to a pre-measured bias in dB, inserted as
// a `volume=` node so the clip lands near the EBU R128 target.
AudioBuild buildAudioSegments(const Project& p, const QVector<int>& usedIndices,
                              int sampleRate,
                              const QHash<QString, double>& gainsDb,
                              QStringList& args, QStringList& chains,
                              int& inputIndex)
{
    AudioBuild b;
    const int FPS = p.canvas.fps;
    const double transition = std::max(0.0, p.defaults.transitionSecs);
    const bool levelling = p.defaults.audioLevelling.active;

    for (int globalIdx = 0; globalIdx < usedIndices.size(); ++globalIdx) {
        const Item& it = p.items[usedIndices[globalIdx]];
        const double dur = segmentDuration(it, FPS);
        const QString aFade = audioFadeChain(clipFadesFor(globalIdx, dur, transition), dur);
        const QString alabel = QString("a%1").arg(b.nUsed);

        const bool useVideoAudio = (it.kind == ItemKind::VideoClip)
                                && it.videoClip.hasAudio;
        QString achain;
        if (useVideoAudio) {
            const auto& vid = it.videoClip;
            args << "-ss" << QString::number(vid.startSecs, 'f', 4)
                 << "-t"  << QString::number(dur, 'f', 4)
                 << "-vn"  // skip video decoding — we only need audio here
                 << "-i"  << vid.common.sourcePath;
            const int aIdx = inputIndex++;

            achain = QString("[%1:a]aresample=async=1:first_pts=0,"
                             "aformat=sample_fmts=fltp:channel_layouts=stereo,"
                             "asetpts=PTS-STARTPTS,aresample=%2,apad,atrim=0:%3,"
                             "asetpts=PTS-STARTPTS")
                .arg(aIdx).arg(sampleRate).arg(dur, 0, 'f', 6);
            if (levelling) {
                const double bias = gainsDb.value(vid.common.sourcePath, 0.0);
                if (bias != 0.0) {
                    achain += QString(",volume=%1dB").arg(bias, 0, 'f', 2);
                }
            }
        } else {
            args << "-f" << "lavfi"
                 << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << QString("anullsrc=channel_layout=stereo:sample_rate=%1").arg(sampleRate);
            const int silIdx = inputIndex++;

            achain = QString("[%1:a]aresample=%2,"
                             "aformat=sample_fmts=fltp:channel_layouts=stereo")
                .arg(silIdx).arg(sampleRate);
        }
        achain += aFade;
        achain += QString("[%1]").arg(alabel);
        chains << achain;

        b.concatLabels += QString("[%1]").arg(alabel);
        b.nUsed++;
    }
    return b;
}

} // namespace

QStringList Renderer::buildBatchArgs(int firstUsedIndex, int count,
                                     const QString& batchOutPath)
{
    const int FPS = m_renderProject.canvas.fps;

    QStringList args;
    args << "-y" << "-hide_banner" << "-nostats";

    QStringList chains;
    int inputIndex = 0;
    VideoBuild b = buildVideoSegments(m_renderProject, m_usedIndices,
                                      firstUsedIndex, count,
                                      m_workDir + "/text",
                                      args, chains, inputIndex);
    chains << (b.concatLabels +
               QString("concat=n=%1:v=1:a=0[vout]").arg(b.nUsed));

    args << "-filter_complex" << chains.join(";");
    args << "-map" << "[vout]";
    // Codec params here MUST stay identical across batches so the final
    // concat-demux pass can stream-copy without re-encoding.
    //
    // -preset medium / -crf 18 lands on the visually-lossless side of
    // the libx264 quality curve (CRF 18 is the customary "transparent"
    // threshold; veryfast/CRF 20 was visibly softer than the source on
    // high-bitrate phone HEVC). Files get larger and encode is ~3-4×
    // slower, but the auto-batch-size logic at start() already shrinks
    // batches at 4K so memory stays bounded.
    //
    // -colorspace/-color_primaries/-color_trc/-color_range write the
    // BT.709 + TV-range tags into the H.264 SPS VUI; without them the
    // encoded stream is colour-untagged and players guess (BT.601 vs
    // BT.709, full vs limited), which shifts hue and contrast on
    // QuickTime / browsers / TVs. Identical flags on every batch keep
    // SPS bits identical so concat-demux + -c copy still works.
    args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "18"
         << "-pix_fmt" << "yuv420p"
         << "-colorspace" << "bt709"
         << "-color_primaries" << "bt709"
         << "-color_trc" << "bt709"
         << "-color_range" << "tv"
         << "-r" << QString::number(FPS);
    args << "-an";                  // video-only intermediate
    args << "-f" << "matroska";     // mkv preserves PTS cleanly under concat-demux
    args << "-progress" << "pipe:1";
    args << batchOutPath;
    return args;
}

QStringList Renderer::buildConcatArgs(const QString& outPath)
{
    QStringList args;
    args << "-y" << "-hide_banner" << "-nostats";
    // Input 0: concat-demux list of video-only intermediates.
    args << "-f" << "concat" << "-safe" << "0" << "-i"
         << (m_workDir + "/concat.txt");
    int inputIndex = 1;

    // Build the per-item audio chains (silence sources or video-clip
    // audio extracts) over the WHOLE timeline. Audio encodes in a single
    // uninterrupted run — no per-batch AAC priming slop accumulating.
    QStringList chains;
    AudioBuild ab = buildAudioSegments(m_renderProject, m_usedIndices,
                                       kSampleRate, m_audioGainsDb,
                                       args, chains, inputIndex);
    chains << (ab.concatLabels +
               QString("concat=n=%1:v=0:a=1[aout]").arg(ab.nUsed));

    // Optional background-music duck/mix layered on top of [aout].
    const QString audioMapLabel =
        appendBackgroundMusicChain(args, chains, inputIndex, "[aout]");

    args << "-filter_complex" << chains.join(";");
    args << "-map" << "0:v" << "-c:v" << "copy";   // stream-copy from concat
    // Re-assert the colour tags on the muxer side so the MP4 colr atom
    // matches the SPS VUI baked into the intermediates. With -c:v copy
    // these don't re-encode anything; they only populate container-level
    // metadata that some players (Apple's, especially) prefer to the
    // bitstream's own tags.
    args << "-colorspace" << "bt709"
         << "-color_primaries" << "bt709"
         << "-color_trc" << "bt709"
         << "-color_range" << "tv";
    args << "-map" << audioMapLabel
         << "-c:a" << "aac" << "-b:a" << "256k"
         << "-ar" << QString::number(kSampleRate);
    // Clamp video to the planned frame count so both streams end at the
    // same instant. -frames:v works with -c:v copy (output stops after N
    // frames), unlike -t which is keyframe-aligned. Audio is already
    // exactly m_totalDuration; video gets a small encoder-slop tail that
    // -frames:v lops off precisely.
    const long long expectedFrames =
        std::llround(m_totalDuration * m_renderProject.canvas.fps);
    args << "-frames:v" << QString::number(expectedFrames);
    args << "-movflags" << "+faststart";
    args << "-progress" << "pipe:1";
    args << outPath;
    return args;
}

QString Renderer::appendBackgroundMusicChain(QStringList& args, QStringList& chains,
                                             int& inputIndex,
                                             const QString& currentAudioLabel)
{
    QStringList musicPaths;
    for (const QString& path : m_renderProject.backgroundMusic) {
        if (QFileInfo::exists(path)) {
            musicPaths.append(path);
        } else {
            emit log(QString("Background-music file missing, skipping: %1").arg(path));
        }
    }
    if (musicPaths.isEmpty() || m_totalDuration <= 0.001) return currentAudioLabel;

    const double transition = std::max(0.0, m_renderProject.defaults.transitionSecs);

    // Carve the timeline into alternating music / silence chunks. The
    // music's playhead pauses during every video clip and resumes at the
    // clip's end (rather than the music continuing under a ducked
    // volume). Silence chunks cover the video-clip ranges so the
    // concat'd chunks add up to m_totalDuration.
    struct Chunk {
        bool isMusic = false;
        double dur = 0.0;
        double mStart = 0.0, mEnd = 0.0;  // music time, only for isMusic
        bool fadeIn = false;              // crossfade in as the previous video ends
        bool fadeOut = false;             // crossfade out as the next video starts (or render ends)
    };
    QVector<Chunk> chunks;

    auto sortedWindows = m_videoWindows;
    std::sort(sortedWindows.begin(), sortedWindows.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    double tlCursor = 0.0;
    double cumulativeMusicTime = 0.0;
    auto pushMusic = [&](double tlEnd) {
        if (tlEnd <= tlCursor) return;
        Chunk m;
        m.isMusic = true;
        m.dur     = tlEnd - tlCursor;
        m.mStart  = cumulativeMusicTime;
        m.mEnd    = cumulativeMusicTime + m.dur;
        m.fadeIn  = (tlCursor > 0.0);  // start of timeline gets no fade-in
        m.fadeOut = true;              // either pre-video or end-of-render
        cumulativeMusicTime = m.mEnd;
        chunks.append(m);
    };
    for (const auto& vw : sortedWindows) {
        pushMusic(vw.first);
        Chunk s;
        s.isMusic = false;
        s.dur     = vw.second - vw.first;
        chunks.append(s);
        tlCursor = vw.second;
    }
    pushMusic(m_totalDuration);

    if (cumulativeMusicTime <= 0.001) {
        // Entire timeline is video clips — no music plays.
        return currentAudioLabel;
    }

    // Add music inputs. When automatic levelling is active, each track
    // gets its own volume= node *before* the playlist concat so the
    // measured per-track bias survives the later asplit/atrim slicing.
    const bool levelling = m_renderProject.defaults.audioLevelling.active;
    const int firstMusicIdx = inputIndex;
    for (const QString& path : musicPaths) {
        args << "-i" << path;
        ++inputIndex;
    }
    QString musicConcat;
    for (int i = 0; i < musicPaths.size(); ++i) {
        const int idx = firstMusicIdx + i;
        const double bias = levelling ? m_audioGainsDb.value(musicPaths[i], 0.0) : 0.0;
        if (bias != 0.0) {
            chains << QString("[%1:a]volume=%2dB[mt%3]")
                          .arg(idx).arg(bias, 0, 'f', 2).arg(i);
            musicConcat += QString("[mt%1]").arg(i);
        } else {
            musicConcat += QString("[%1:a]").arg(idx);
        }
    }
    musicConcat += QString("concat=n=%1:v=0:a=1[mc]").arg(musicPaths.size());
    chains << musicConcat;

    // Loop and trim the playlist to the *total music play time* (i.e.
    // m_totalDuration minus the video-clip durations), then convert to
    // the canonical fltp/stereo/sample-rate that concat needs.
    chains << QString("[mc]aloop=loop=-1:size=2147483647,atrim=0:%1,asetpts=PTS-STARTPTS,"
                      "aresample=%2,aformat=sample_fmts=fltp:channel_layouts=stereo[ml]")
              .arg(cumulativeMusicTime, 0, 'f', 4)
              .arg(kSampleRate);

    int musicChunkCount = 0;
    for (const auto& c : chunks) if (c.isMusic) ++musicChunkCount;

    // asplit the master music into one branch per music chunk so each
    // chunk can atrim its own slice. With a single music chunk we skip
    // the split — feed [ml] directly.
    if (musicChunkCount > 1) {
        QString split = QString("[ml]asplit=%1").arg(musicChunkCount);
        for (int i = 0; i < musicChunkCount; ++i) {
            split += QString("[mks%1]").arg(i);
        }
        chains << split;
    }

    // Per-chunk filter chains. For music chunks, atrim the right slice
    // and apply optional crossfade in/out. For silence chunks, an
    // anullsrc of the matching duration. Each chunk emits [ck<i>].
    QString concatInputs;
    int musicIdx = 0;
    for (int i = 0; i < chunks.size(); ++i) {
        const auto& c = chunks[i];
        const QString out = QString("ck%1").arg(i);
        if (c.isMusic) {
            const QString src = (musicChunkCount > 1)
                ? QString("[mks%1]").arg(musicIdx)
                : QString("[ml]");
            QString chain = src
                + QString("atrim=%1:%2,asetpts=PTS-STARTPTS")
                    .arg(c.mStart, 0, 'f', 4)
                    .arg(c.mEnd,   0, 'f', 4);
            const double maxFade = c.dur / 2.0;
            const double fIn  = (c.fadeIn  && transition > 0.0) ? std::min(transition, maxFade) : 0.0;
            const double fOut = (c.fadeOut && transition > 0.0) ? std::min(transition, maxFade) : 0.0;
            if (fIn  > 0.0) chain += QString(",afade=in:st=0:d=%1").arg(fIn, 0, 'f', 4);
            if (fOut > 0.0) chain += QString(",afade=out:st=%1:d=%2")
                                    .arg(c.dur - fOut, 0, 'f', 4)
                                    .arg(fOut,         0, 'f', 4);
            chain += QString("[%1]").arg(out);
            chains << chain;
            ++musicIdx;
        } else {
            chains << QString("anullsrc=channel_layout=stereo:sample_rate=%1:duration=%2,"
                              "aformat=sample_fmts=fltp:channel_layouts=stereo[%3]")
                      .arg(kSampleRate)
                      .arg(c.dur, 0, 'f', 4)
                      .arg(out);
        }
        concatInputs += QString("[%1]").arg(out);
    }
    chains << QString("%1concat=n=%2:v=0:a=1[mfinal]")
              .arg(concatInputs).arg(chunks.size());

    // Mix the per-clip audio with the chunked music. With normalize=0
    // the per-source biases reach the output verbatim instead of being
    // halved by amix's default per-input scaling — the levelling pass
    // already picked the right absolute gains, normalize would undo that.
    const QString amixOpts = levelling
        ? QStringLiteral("inputs=2:duration=first:dropout_transition=0:normalize=0")
        : QStringLiteral("inputs=2:duration=first:dropout_transition=0");
    chains << QString("%1[mfinal]amix=%2[afinal]").arg(currentAudioLabel, amixOpts);
    return "[afinal]";
}

// Pull the last `I: <X> LUFS` value out of an ebur128 stderr dump.
// The filter prints `I:` once per second of streaming progress and
// once more in the trailing Summary block — taking the last finite
// match yields the integrated value either way. nan / -inf (silent
// audio) returns nullopt so the caller falls back to a no-op gain.
static std::optional<double> parseIntegratedLufs(const QString& stderrText) {
    static const QRegularExpression re(
        QStringLiteral(R"(I:\s*(-?\d+(?:\.\d+)?|-inf|nan)\s+LUFS)"));
    auto it = re.globalMatch(stderrText);
    std::optional<double> last;
    while (it.hasNext()) {
        auto m = it.next();
        bool ok = false;
        const double v = m.captured(1).toDouble(&ok);
        if (ok && std::isfinite(v)) last = v;
    }
    return last;
}

bool Renderer::startNextProbe(QString* err)
{
    if (m_probeQueue.isEmpty()) {
        // All probes done — slide into the regular batch pipeline.
        m_state = State::Batches;
        return startNextBatch(err);
    }
    m_currentProbePath = m_probeQueue.takeFirst();
    const int doneIdx = m_probeTotal - m_probeQueue.size();  // 1-based
    emit log(QString("Probing loudness (%1/%2): %3")
             .arg(doneIdx).arg(m_probeTotal)
             .arg(QFileInfo(m_currentProbePath).fileName()));

    QStringList args;
    args << "-hide_banner" << "-nostats"
         << "-i" << m_currentProbePath
         << "-vn"                               // audio-only — skip video decode
         << "-af" << "ebur128"
         << "-f" << "null" << "-";
    return spawnFfmpeg(args, err);
}

void Renderer::onProbeFinished(int code, int exitStatus)
{
    if (m_cancelRequested) { emitCancelled(); return; }
    const bool ok = (exitStatus == int(QProcess::NormalExit)) && code == 0;

    if (ok) {
        if (auto lufs = parseIntegratedLufs(m_logTail)) {
            const double bias = std::clamp(kTargetLufs - *lufs,
                                           -kMaxGainDb, kMaxGainDb);
            m_audioGainsDb.insert(m_currentProbePath, bias);
            emit log(QString("  measured %1 LUFS → %2%3 dB")
                     .arg(*lufs, 0, 'f', 1)
                     .arg(bias >= 0 ? "+" : "")
                     .arg(bias, 0, 'f', 1));
        } else {
            // Silent / unparseable — render at native level.
            emit log(QString("  no integrated loudness reading; leaving at 0 dB"));
        }
    } else {
        // Don't fail the whole render over a probe — fall back to 0 dB.
        emit log(QString("  ebur128 probe failed (exit %1); leaving at 0 dB").arg(code));
    }
    m_currentProbePath.clear();

    QString err;
    if (!startNextProbe(&err)) abortWithFailure(err);
}

bool Renderer::startNextBatch(QString* err)
{
    const int firstUsedIndex = m_batchIndex * m_batchSize;
    const int count = std::min<int>(m_batchSize, m_usedIndices.size() - firstUsedIndex);
    const QString outPath =
        m_workDir + QString("/batch_%1.mkv").arg(m_batchIndex, 4, 10, QChar('0'));

    emit log(QString("Rendering batch %1/%2 (%3 items)…")
             .arg(m_batchIndex + 1)
             .arg(m_batchDurations.size())
             .arg(count));

    if (!spawnFfmpeg(buildBatchArgs(firstUsedIndex, count, outPath), err)) return false;
    m_intermediates.append(outPath);
    return true;
}

bool Renderer::startConcatPass(QString* err)
{
    m_state = State::Concat;

    // Write the concat list. Paths are relative to m_workDir to keep the
    // file portable, and -safe 0 is set to allow absolute-or-relative.
    QFile listFile(m_workDir + "/concat.txt");
    if (!listFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QString("Could not write concat list: %1").arg(listFile.errorString());
        return false;
    }
    for (const QString& p : m_intermediates) {
        QString line = QString("file '%1'\n").arg(QFileInfo(p).fileName());
        listFile.write(line.toUtf8());
    }
    listFile.close();

    emit log("Concatenating segments…");

    QStringList args = buildConcatArgs(m_outPath);
    return spawnFfmpeg(args, err);
}

bool Renderer::spawnFfmpeg(const QStringList& args, QString* err)
{
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
    m_proc = new QProcess(this);
    m_proc->setProgram("ffmpeg");
    m_proc->setArguments(args);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    m_logTail.clear();

    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        m_logTail += QString::fromUtf8(m_proc->readAllStandardError());
        if (m_logTail.size() > 8192) m_logTail = m_logTail.right(8192);
    });
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        QString chunk = QString::fromUtf8(m_proc->readAllStandardOutput());
        for (const auto& line : chunk.split('\n', Qt::SkipEmptyParts)) {
            onProgressLine(line.trimmed());
        }
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        emit log(QString("ffmpeg process error: %1").arg(int(e)));
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        if (m_state == State::Probing) onProbeFinished(code, int(status));
        else if (m_state == State::Batches) onBatchFinished(code, int(status));
        else if (m_state == State::Concat) onConcatFinished(code, int(status));
    });

    m_proc->start();
    if (!m_proc->waitForStarted(5000)) {
        if (err) *err = "ffmpeg failed to start (is it installed?)";
        return false;
    }
    return true;
}

void Renderer::emitCancelled()
{
    // Delete any half-written final output (only the concat pass writes
    // there; batch intermediates live in the temp dir which we drop too).
    if (!m_outPath.isEmpty()) QFile::remove(m_outPath);
    m_cancelRequested = false;
    cleanupTempDir();
    m_state = State::Idle;
    emit cancelled();
}

void Renderer::onBatchFinished(int code, int exitStatus)
{
    if (m_cancelRequested) { emitCancelled(); return; }
    const bool ok = (exitStatus == int(QProcess::NormalExit)) && code == 0;
    if (!ok) {
        abortWithFailure(QString("ffmpeg (batch %1) exited with code %2.\n%3")
                         .arg(m_batchIndex + 1).arg(code).arg(m_logTail));
        return;
    }

    m_completedBatchDuration += m_batchDurations[m_batchIndex];
    m_batchIndex++;

    QString err;
    const bool moreBatches = (m_batchIndex < m_batchDurations.size());
    const bool started = moreBatches ? startNextBatch(&err) : startConcatPass(&err);
    if (!started) abortWithFailure(err);
}

void Renderer::onConcatFinished(int code, int exitStatus)
{
    if (m_cancelRequested) { emitCancelled(); return; }
    const bool ok = (exitStatus == int(QProcess::NormalExit)) && code == 0;
    if (ok) {
        cleanupTempDir();
        m_state = State::Idle;
        emit finished(true, m_outPath);
    } else {
        abortWithFailure(QString("ffmpeg (concat) exited with code %1.\n%2")
                         .arg(code).arg(m_logTail));
    }
}

void Renderer::onProgressLine(const QString& line) {
    // Concat is the cheap final pass — no need to clutter the log with its
    // own % counter (which would otherwise reset to 0 after batches have
    // already ticked up to ~100%). The single "Concatenating segments…"
    // line emitted in startConcatPass is enough.
    if (m_state != State::Batches) return;

    int eq = line.indexOf('=');
    if (eq < 0) return;
    QString k = line.left(eq);
    QString v = line.mid(eq + 1);
    if (k != "out_time_us" && k != "out_time_ms") return;

    bool ok = false;
    long long us = v.toLongLong(&ok);
    if (!ok) return;
    if (k == "out_time_ms") us *= 1000;
    double curSecs = us / 1'000'000.0;
    if (curSecs < 0) return;

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - m_lastProgressLogMs < 3000) return;
    m_lastProgressLogMs = nowMs;

    // Per-process out_time resets to 0 each batch — offset by cumulative
    // finished-batch duration so the user sees one monotonic counter.
    double globalSecs = curSecs + m_completedBatchDuration;

    if (m_totalDuration > 0.0) {
        double pct = std::clamp(globalSecs / m_totalDuration, 0.0, 1.0) * 100.0;
        emit log(QString("Rendering: %1% (%2 s / %3 s)")
                 .arg(pct, 0, 'f', 0)
                 .arg(globalSecs, 0, 'f', 1)
                 .arg(m_totalDuration, 0, 'f', 1));
    } else {
        emit log(QString("Rendering: %1 s").arg(globalSecs, 0, 'f', 1));
    }
}

} // namespace vlip
