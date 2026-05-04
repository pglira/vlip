#include "Renderer.hpp"
#include "DrawtextFormulas.hpp"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QStringList>
#include <QRegularExpression>
#include <QProcess>
#include <QDateTime>
#include <QTimeZone>

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

bool Renderer::isRunning() const { return m_proc && m_proc->state() != QProcess::NotRunning; }

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
}

bool Renderer::start(const Project& p, const QString& outPath) {
    QString err = validate(p);
    if (!err.isEmpty()) { emit finished(false, err); return false; }

    cleanupTempDir();
    m_outPath = outPath;
    m_logTail.clear();
    m_totalDuration = 0.0;
    m_lastProgressLogMs = 0;
    m_cancelRequested = false;

    QString runErr;
    QString cmdLine = buildAndExecute(p, outPath, &runErr);
    if (!runErr.isEmpty()) { emit finished(false, runErr); return false; }
    Q_UNUSED(cmdLine);
    return true;
}

QString Renderer::appendBackgroundMusicChain(
    QStringList& args,
    QStringList& chains,
    int& inputIndex,
    const QStringList& backgroundMusic,
    double totalDuration,
    double transition,
    int sampleRate,
    const QVector<QPair<double, double>>& videoWindows,
    const QString& currentAudioLabel)
{
    QStringList musicFiles;
    for (const QString& path : backgroundMusic) {
        if (QFileInfo::exists(path)) {
            musicFiles.append(path);
        } else {
            emit log(QString("Background-music file missing, skipping: %1").arg(path));
        }
    }
    if (musicFiles.isEmpty() || totalDuration <= 0.001) return currentAudioLabel;

    const int firstMusicIdx = inputIndex;
    for (const QString& path : musicFiles) {
        args << "-i" << path;
        ++inputIndex;
    }

    // Concat the playlist into a single stream.
    QString musicConcat;
    for (int i = 0; i < musicFiles.size(); ++i) {
        musicConcat += QString("[%1:a]").arg(firstMusicIdx + i);
    }
    musicConcat += QString("concat=n=%1:v=0:a=1[mc]").arg(musicFiles.size());
    chains << musicConcat;

    // Loop the playlist (so short music covers a long render) and trim
    // to the exact total duration.
    chains << QString("[mc]aloop=loop=-1:size=2147483647,atrim=0:%1,asetpts=PTS-STARTPTS,"
                      "aresample=%2,aformat=sample_fmts=fltp:channel_layouts=stereo[ml]")
              .arg(totalDuration, 0, 'f', 4)
              .arg(sampleRate);

    // Volume envelope: product of one factor per video clip, plus a
    // single end-of-render fade-out factor. Each per-clip factor is 1
    // outside the duck window, ramps 1→0 over `transition` seconds before
    // the clip, holds 0 during the clip, ramps 0→1 over `transition`
    // seconds after. With transition==0, a binary mute window is used.
    QString envelope = "1";
    for (const auto& vw : videoWindows) {
        const double s = vw.first, e = vw.second;
        QString factor;
        if (transition > 0.0) {
            factor = QString(
                "if(lt(t,%1),1,if(lt(t,%2),(%2-t)/%5,if(lt(t,%3),0,if(lt(t,%4),(t-%3)/%5,1))))")
                .arg(s - transition, 0, 'f', 4)
                .arg(s,               0, 'f', 4)
                .arg(e,               0, 'f', 4)
                .arg(e + transition,  0, 'f', 4)
                .arg(transition,      0, 'f', 4);
        } else {
            factor = QString("if(lt(t,%1),1,if(lt(t,%2),0,1))")
                .arg(s, 0, 'f', 4)
                .arg(e, 0, 'f', 4);
        }
        envelope += "*(" + factor + ")";
    }
    // Final fade-out so the music tails off in lockstep with the visual
    // fade on the last clip.
    if (transition > 0.0) {
        envelope += QString("*(if(lt(t,%1),1,(%2-t)/%3))")
            .arg(totalDuration - transition, 0, 'f', 4)
            .arg(totalDuration,              0, 'f', 4)
            .arg(transition,                 0, 'f', 4);
    }
    chains << QString("[ml]volume=eval=frame:volume='%1'[md]").arg(envelope);

    // Mix the ducked music with the per-clip audio concat. duration=first
    // because the per-clip audio is exactly totalDuration long.
    chains << QString("%1[md]amix=inputs=2:duration=first:dropout_transition=0[afinal]")
              .arg(currentAudioLabel);
    return "[afinal]";
}

QString Renderer::buildAndExecute(const Project& p, const QString& outPath, QString* err) {
    int W = p.canvas.width, H = p.canvas.height, FPS = p.canvas.fps;
    const int sampleRate = 48000;

    QStringList args;
    args << "-y" << "-hide_banner" << "-nostats";

    // First pass: collect the items that will actually be rendered, so we
    // can know which one is first / last for fade-in / fade-out.
    QVector<const Item*> usedItems;
    usedItems.reserve(p.items.size());
    for (const auto& it : p.items) {
        if (!it.common().used) continue;
        if (it.kind != ItemKind::TextClip
            && (it.common().sourceMissing || !QFileInfo::exists(it.common().sourcePath))) continue;
        if (it.effectiveDuration() <= 0.001) continue;
        usedItems.push_back(&it);
    }
    const double transition = std::max(0.0, p.defaults.transitionSecs);

    // Per-item inputs and filter chains.
    QStringList chains;
    QStringList concatLabels;
    int nUsed = 0;
    int inputIndex = 0;

    // Window (start, end) in the rendered timeline for every video clip
    // included in the render — used below to duck the background music
    // around each one.
    QVector<QPair<double, double>> videoWindows;

    for (const Item* itp : usedItems) {
        const Item& it = *itp;
        double dur = it.effectiveDuration();
        // Quantize to whole frames so each segment's actual rendered
        // duration matches `dur` exactly. Without this, ffmpeg's fps=FPS
        // filter's per-clip rounding (~ ±1/(2·FPS)s) accumulates across
        // hundreds of clips and would let the music-duck timing drift
        // relative to where the video clips actually appear.
        dur = std::max(1.0 / FPS, std::round(dur * FPS) / FPS);
        m_totalDuration += dur;

        // Fade lengths for this clip. No fade-in for the first clip
        // (renders open on the first frame, not a black fade), but the
        // last clip does fade out so the video ends gently. Clamp so
        // the two fades fit inside the clip.
        double fadeIn  = (transition > 0.0 && nUsed > 0) ? transition : 0.0;
        double fadeOut = (transition > 0.0)              ? transition : 0.0;
        double maxEachFade = dur / 2.0;
        if (fadeIn  > maxEachFade) fadeIn  = maxEachFade;
        if (fadeOut > maxEachFade) fadeOut = maxEachFade;

        QString vlabel = QString("v%1").arg(nUsed);
        QString alabel = QString("a%1").arg(nUsed);

        QString vFade, aFade;
        if (fadeIn > 0.0) {
            vFade += QString(",fade=in:st=0:d=%1").arg(fadeIn, 0, 'f', 4);
            aFade += QString(",afade=in:st=0:d=%1").arg(fadeIn, 0, 'f', 4);
        }
        if (fadeOut > 0.0) {
            double st = std::max(0.0, dur - fadeOut);
            vFade += QString(",fade=out:st=%1:d=%2").arg(st, 0, 'f', 4).arg(fadeOut, 0, 'f', 4);
            aFade += QString(",afade=out:st=%1:d=%2").arg(st, 0, 'f', 4).arg(fadeOut, 0, 'f', 4);
        }

        if (it.kind == ItemKind::TextClip) {
            const auto& tc = it.textClip;
            const auto& tStyle = p.defaults.textClip;

            // Video input: a still background (looped image) if a path is
            // set, otherwise a solid-colour lavfi source.
            int bgInIdx;
            if (!tc.backgroundPath.isEmpty() && QFileInfo::exists(tc.backgroundPath)) {
                args << "-loop" << "1" << "-t" << QString::number(dur, 'f', 4)
                     << "-i" << tc.backgroundPath;
            } else {
                // No background image → solid black canvas.
                args << "-f" << "lavfi"
                     << "-t" << QString::number(dur, 'f', 4)
                     << "-i" << QString("color=c=black:size=%1x%2:rate=%3")
                            .arg(W).arg(H).arg(FPS);
            }
            bgInIdx = inputIndex++;

            // Silence track for the audio side.
            args << "-f" << "lavfi" << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << QString("anullsrc=channel_layout=stereo:sample_rate=%1").arg(sampleRate);
            int silInIdx = inputIndex++;

            QString chain = QString("[%1:v]").arg(bgInIdx);
            chain += QString("scale=%1:%2:force_original_aspect_ratio=decrease").arg(W).arg(H);
            chain += QString(",pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black").arg(W).arg(H);
            chain += QString(",setsar=1,fps=%1,format=yuv420p").arg(FPS);
            QString dt = textClipDrawText(tc.text, H, tStyle);
            if (!dt.isEmpty()) chain += "," + dt;
            chain += vFade;
            chain += QString("[%1]").arg(vlabel);
            chains << chain;

            QString achain = QString("[%1:a]aresample=%2,aformat=sample_fmts=fltp:channel_layouts=stereo")
                .arg(silInIdx).arg(sampleRate);
            achain += aFade;
            achain += QString("[%1]").arg(alabel);
            chains << achain;
        } else if (it.kind == ItemKind::ImageClip) {
            const auto& img = it.imageClip;
            // Input #inputIndex: image (loop)
            args << "-loop" << "1" << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << img.common.sourcePath;
            int imgInIdx = inputIndex++;
            // Input #inputIndex: silence
            args << "-f" << "lavfi" << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << QString("anullsrc=channel_layout=stereo:sample_rate=%1").arg(sampleRate);
            int silInIdx = inputIndex++;

            QString chain = QString("[%1:v]").arg(imgInIdx);
            if (img.crop && img.sourceWidth > 0 && img.sourceHeight > 0) {
                const QRectF& r = *img.crop;
                int sw = img.sourceWidth, sh = img.sourceHeight;
                int cx = std::clamp(int(std::round(r.x() * sw)), 0, sw - 1);
                int cy = std::clamp(int(std::round(r.y() * sh)), 0, sh - 1);
                int cw = std::clamp(int(std::round(r.width() * sw)), 1, sw - cx);
                int ch = std::clamp(int(std::round(r.height() * sh)), 1, sh - cy);
                chain += QString("crop=%1:%2:%3:%4,").arg(cw).arg(ch).arg(cx).arg(cy);
            }
            chain += QString("scale=%1:%2:force_original_aspect_ratio=decrease").arg(W).arg(H);
            chain += QString(",pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black").arg(W).arg(H);
            chain += QString(",setsar=1,fps=%1,format=yuv420p").arg(FPS);
            QString dt = subtitleDrawText(img.common.subtitle, H, p.defaults.subtitle, dur, true);
            if (!dt.isEmpty()) chain += "," + dt;
            // Per-item date stamp (canvas-space corner overlay).
            QString ds = datestampDrawText(
                formatDatestamp(img.common.timestamp, p.defaults.timeZone, p.defaults.datestamp.format),
                H, p.defaults.datestamp);
            if (!ds.isEmpty()) chain += "," + ds;
            chain += vFade;
            chain += QString("[%1]").arg(vlabel);
            chains << chain;

            QString achain = QString("[%1:a]aresample=%2,aformat=sample_fmts=fltp:channel_layouts=stereo")
                .arg(silInIdx).arg(sampleRate);
            achain += aFade;
            achain += QString("[%1]").arg(alabel);
            chains << achain;
        } else {
            videoWindows.append({m_totalDuration - dur, m_totalDuration});
            const auto& vid = it.videoClip;
            // -ss before -i for fast seek; -t for duration after -ss.
            args << "-ss" << QString::number(vid.startSecs, 'f', 4)
                 << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << vid.common.sourcePath;
            int vIdx = inputIndex++;

            QString chain = QString("[%1:v]").arg(vIdx);
            chain += QString("scale=%1:%2:force_original_aspect_ratio=decrease").arg(W).arg(H);
            chain += QString(",pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black").arg(W).arg(H);
            chain += QString(",setsar=1,fps=%1,format=yuv420p").arg(FPS);
            QString dt = subtitleDrawText(vid.common.subtitle, H, p.defaults.subtitle, dur, true);
            if (!dt.isEmpty()) chain += "," + dt;
            QString ds = datestampDrawText(
                formatDatestamp(vid.common.timestamp, p.defaults.timeZone, p.defaults.datestamp.format),
                H, p.defaults.datestamp);
            if (!ds.isEmpty()) chain += "," + ds;
            chain += vFade;
            chain += QString("[%1]").arg(vlabel);
            chains << chain;

            if (vid.hasAudio) {
                // apad+atrim forces the segment to exactly `dur` seconds
                // at the output sample rate. Without it, source-audio
                // sample-alignment quirks leave each video clip's audio a
                // few ms short, accumulating into A/V drift over many
                // clips and causing the music ducks to misalign.
                QString achain = QString("[%1:a]aresample=async=1:first_pts=0,aformat=sample_fmts=fltp:channel_layouts=stereo,asetpts=PTS-STARTPTS,aresample=%2,apad,atrim=0:%3,asetpts=PTS-STARTPTS")
                    .arg(vIdx).arg(sampleRate).arg(dur, 0, 'f', 6);
                achain += aFade;
                achain += QString("[%1]").arg(alabel);
                chains << achain;
            } else {
                args << "-f" << "lavfi" << "-t" << QString::number(dur, 'f', 4)
                     << "-i" << QString("anullsrc=channel_layout=stereo:sample_rate=%1").arg(sampleRate);
                int silIdx = inputIndex++;
                QString achain = QString("[%1:a]aresample=%2,aformat=sample_fmts=fltp:channel_layouts=stereo")
                    .arg(silIdx).arg(sampleRate);
                achain += aFade;
                achain += QString("[%1]").arg(alabel);
                chains << achain;
            }
        }

        concatLabels << QString("[%1][%2]").arg(vlabel).arg(alabel);
        nUsed++;
    }

    QString concat = concatLabels.join("") +
        QString("concat=n=%1:v=1:a=1[vout][aout]").arg(nUsed);
    chains << concat;

    // Optional background-music track — appended only if the project has
    // any music files. Returns the audio label the encoder should map.
    const QString audioMapLabel = appendBackgroundMusicChain(
        args, chains, inputIndex,
        p.backgroundMusic, m_totalDuration, transition, sampleRate,
        videoWindows, "[aout]");

    QString filterComplex = chains.join(";");

    args << "-filter_complex" << filterComplex;
    args << "-map" << "[vout]" << "-map" << audioMapLabel;
    args << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "20"
         << "-pix_fmt" << "yuv420p"
         << "-r" << QString::number(FPS);
    args << "-c:a" << "aac" << "-b:a" << "192k" << "-ar" << QString::number(sampleRate);
    args << "-movflags" << "+faststart";
    args << "-progress" << "pipe:1";       // periodic key=value lines on stdout
    args << outPath;

    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
    m_proc = new QProcess(this);
    m_proc->setProgram("ffmpeg");
    m_proc->setArguments(args);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);

    // stderr: ffmpeg's verbose banner + warnings — captured silently for
    // failure diagnosis, never forwarded to the UI log.
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        m_logTail += QString::fromUtf8(m_proc->readAllStandardError());
        if (m_logTail.size() > 8192) m_logTail = m_logTail.right(8192);
    });
    // stdout: ffmpeg's "-progress pipe:1" key=value stream — used for the
    // throttled "Rendering: X%" log lines.
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
        if (m_cancelRequested) {
            // ffmpeg's partial output is unplayable (no finalised moov) —
            // drop it so the user isn't left with a broken file.
            QFile::remove(m_outPath);
            m_cancelRequested = false;
            emit cancelled();
            return;
        }
        bool ok = (status == QProcess::NormalExit) && code == 0;
        if (ok) {
            emit finished(true, m_outPath);
        } else {
            emit finished(false, QString("ffmpeg exited with code %1.\n%2").arg(code).arg(m_logTail));
        }
    });

    m_proc->start();
    if (!m_proc->waitForStarted(5000)) {
        if (err) *err = "ffmpeg failed to start (is it installed?)";
        return {};
    }
    Q_UNUSED(filterComplex);
    Q_UNUSED(inputIndex);
    return m_proc->program() + " " + m_proc->arguments().join(' ');
}

void Renderer::onProgressLine(const QString& line) {
    // ffmpeg -progress pipe:1 emits "key=value" lines, including keys
    // like out_time_us, out_time_ms, frame, fps, speed, progress=…
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

    // Throttle: at most one log line every 3 seconds of wall time.
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - m_lastProgressLogMs < 3000) return;
    m_lastProgressLogMs = nowMs;

    if (m_totalDuration > 0.0) {
        double pct = std::clamp(curSecs / m_totalDuration, 0.0, 1.0) * 100.0;
        emit log(QString("Rendering: %1% (%2 s / %3 s)")
                 .arg(pct, 0, 'f', 0)
                 .arg(curSecs, 0, 'f', 1)
                 .arg(m_totalDuration, 0, 'f', 1));
    } else {
        emit log(QString("Rendering: %1 s").arg(curSecs, 0, 'f', 1));
    }
}

} // namespace vlip
