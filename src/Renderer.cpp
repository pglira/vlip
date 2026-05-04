#include "Renderer.h"

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

namespace {

QString escapeDrawText(const QString& text) {
    QString s = text;
    s.replace("\\", "\\\\");
    s.replace(":", "\\:");
    s.replace("'", "\\'");
    s.replace("%", "\\%");
    return s;
}

QString fallbackFontFile() {
    static const QStringList candidates = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/Library/Fonts/Helvetica.ttc",
        "C:/Windows/Fonts/arial.ttf",
    };
    for (const auto& p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

QString resolveFontFile(const QString& family) {
    if (!family.isEmpty()) {
        // Use fontconfig if available — works for any installed family.
        QProcess pr;
        pr.start("fc-match", {"-f", "%{file}", family});
        if (pr.waitForStarted(500) && pr.waitForFinished(1500)) {
            QString p = QString::fromUtf8(pr.readAllStandardOutput()).trimmed();
            if (!p.isEmpty() && QFileInfo::exists(p)) return p;
        }
    }
    return fallbackFontFile();
}

QString colorToDrawtext(const QColor& c) {
    return QString("0x%1%2%3@%4")
        .arg(c.red(),   2, 16, QChar('0'))
        .arg(c.green(), 2, 16, QChar('0'))
        .arg(c.blue(),  2, 16, QChar('0'))
        .arg(c.alphaF(), 0, 'f', 3);
}

// Format a stored UTC timestamp using the project's time-zone (or system
// local if the tz id is empty / invalid). Returns empty for an invalid
// QDateTime so the caller can skip drawing.
QString formatDatestamp(const QDateTime& utc, const QByteArray& tzId) {
    if (!utc.isValid()) return {};
    QDateTime t = utc;
    t.setTimeSpec(Qt::UTC);
    if (tzId.isEmpty()) {
        t = t.toLocalTime();
    } else {
        QTimeZone z(tzId);
        if (z.isValid()) t = t.toTimeZone(z);
        else             t = t.toLocalTime();
    }
    return t.toString("dd.MM.yyyy HH:mm");
}

// drawtext filter expression for the corner date stamp. `text` is the
// pre-formatted DD.MM.YYYY HH:MM string. Returns empty if the style is
// disabled or text is empty. Operates on canvas coords (post scale+pad)
// so it lives in a fixed corner of the final video.
QString datestampDrawText(const QString& text, int canvasH,
                          const DatestampStyle& s) {
    if (!s.active || text.isEmpty()) return {};
    QString font = resolveFontFile(s.fontFamily);
    int fontSize = s.fontSizePx > 0 ? s.fontSizePx : qMax(14, canvasH / 36);
    QString chain = "drawtext=";
    if (!font.isEmpty()) {
        chain += QString("fontfile='%1':").arg(font);
    }
    chain += QString("text='%1'").arg(escapeDrawText(text));
    chain += ":fontcolor=white";
    chain += QString(":fontsize=%1").arg(fontSize);
    int m = std::max(0, s.marginPx);
    switch (s.corner) {
        case Corner::TopLeft:
            chain += QString(":x=%1:y=%1").arg(m); break;
        case Corner::TopRight:
            chain += QString(":x=w-text_w-%1:y=%1").arg(m); break;
        case Corner::BottomLeft:
            chain += QString(":x=%1:y=h-text_h-%1").arg(m); break;
        case Corner::BottomRight:
            chain += QString(":x=w-text_w-%1:y=h-text_h-%1").arg(m); break;
    }
    return chain;
}

QString textClipDrawText(const QString& text, int canvasH,
                         const TextClipStyle& style) {
    if (text.trimmed().isEmpty()) return {};
    QString font = resolveFontFile(style.fontFamily);
    int fontSize = style.fontSizePx > 0 ? style.fontSizePx
                                        : qMax(20, canvasH / 12);
    QString chain = "drawtext=";
    if (!font.isEmpty()) {
        chain += QString("fontfile='%1':").arg(font);
    }
    chain += QString("text='%1'").arg(escapeDrawText(text));
    chain += QString(":fontcolor=%1").arg(colorToDrawtext(style.fontColor));
    chain += QString(":fontsize=%1").arg(fontSize);
    chain += ":x=(w-text_w)/2";
    switch (style.verticalAlign) {
        case VerticalAlign::Top:    chain += ":y=h/12"; break;
        case VerticalAlign::Middle: chain += ":y=(h-text_h)/2"; break;
        case VerticalAlign::Bottom: chain += ":y=h-(text_h)-h/12"; break;
    }
    return chain;
}

QString drawTextChain(const QString& subtitle, int canvasH,
                      const SubtitleStyle& style, double segmentDur) {
    if (subtitle.trimmed().isEmpty()) return {};
    QString font = resolveFontFile(style.fontFamily);
    int fontSize = style.fontSizePx > 0 ? style.fontSizePx
                                        : qMax(20, canvasH / 22);
    QString chain = "drawtext=";
    if (!font.isEmpty()) {
        chain += QString("fontfile='%1':").arg(font);
    }
    chain += QString("text='%1'").arg(escapeDrawText(subtitle));
    chain += QString(":fontcolor=%1").arg(colorToDrawtext(style.fontColor));
    chain += QString(":fontsize=%1").arg(fontSize);
    chain += QString(":box=1:boxcolor=%1:boxborderw=12").arg(colorToDrawtext(style.bgColor));
    chain += ":x=(w-text_w)/2";
    switch (style.position) {
        case SubtitlePosition::Top:    chain += ":y=h/12"; break;
        case SubtitlePosition::Middle: chain += ":y=(h-text_h)/2"; break;
        case SubtitlePosition::Bottom: chain += ":y=h-(text_h)-h/12"; break;
    }
    // Visibility window. Each segment's filtergraph time starts at 0, so a
    // simple lt(t,N) limits the burn to the first N seconds. Skip when 0
    // (always-on) or when the limit covers the full segment anyway.
    if (style.visibleSecs > 0.0 && style.visibleSecs < segmentDur) {
        chain += QString(":enable='lt(t,%1)'").arg(style.visibleSecs, 0, 'f', 4);
    }
    return chain;
}

} // namespace

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

    QString runErr;
    QString cmdLine = buildAndExecute(p, outPath, &runErr);
    if (!runErr.isEmpty()) { emit finished(false, runErr); return false; }
    Q_UNUSED(cmdLine);
    return true;
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
    const int totalUsed = usedItems.size();
    const double transition = std::max(0.0, p.defaults.transitionSecs);

    // Per-item inputs and filter chains.
    QStringList chains;
    QStringList concatLabels;
    int nUsed = 0;
    int inputIndex = 0;

    for (const Item* itp : usedItems) {
        const Item& it = *itp;
        double dur = it.effectiveDuration();
        m_totalDuration += dur;

        // Fade lengths for this clip. No fade-in for the first clip,
        // no fade-out for the last clip. Clamp so the two fades fit.
        double fadeIn  = (transition > 0.0 && nUsed > 0)             ? transition : 0.0;
        double fadeOut = (transition > 0.0 && nUsed < totalUsed - 1) ? transition : 0.0;
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
            QString dt = drawTextChain(img.common.subtitle, H, p.defaults.subtitle, dur);
            if (!dt.isEmpty()) chain += "," + dt;
            // Per-item date stamp (canvas-space corner overlay).
            QString ds = datestampDrawText(
                formatDatestamp(img.common.timestamp, p.defaults.timeZone),
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
            QString dt = drawTextChain(vid.common.subtitle, H, p.defaults.subtitle, dur);
            if (!dt.isEmpty()) chain += "," + dt;
            QString ds = datestampDrawText(
                formatDatestamp(vid.common.timestamp, p.defaults.timeZone),
                H, p.defaults.datestamp);
            if (!ds.isEmpty()) chain += "," + ds;
            chain += vFade;
            chain += QString("[%1]").arg(vlabel);
            chains << chain;

            if (vid.hasAudio) {
                QString achain = QString("[%1:a]aresample=async=1:first_pts=0,aformat=sample_fmts=fltp:channel_layouts=stereo,asetpts=PTS-STARTPTS,aresample=%2")
                    .arg(vIdx).arg(sampleRate);
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

    QString filterComplex = chains.join(";");

    args << "-filter_complex" << filterComplex;
    args << "-map" << "[vout]" << "-map" << "[aout]";
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
