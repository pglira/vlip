#include "Renderer.h"

#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QStringList>
#include <QRegularExpression>
#include <QProcess>

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

QString drawTextChain(const QString& subtitle, int canvasH,
                      const SubtitleStyle& style) {
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
    return chain;
}

} // namespace

Renderer::Renderer(QObject* parent) : QObject(parent) {}
Renderer::~Renderer() { cancel(); cleanupTempDir(); }

QString Renderer::validate(const Project& p) {
    int usedCount = 0;
    for (const auto& it : p.items) {
        if (!it.common().used) continue;
        if (it.common().sourceMissing || !QFileInfo::exists(it.common().sourcePath)) continue;
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
    m_totalDuration = 0.0;
    m_logTail.clear();

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
        if (it.common().sourceMissing || !QFileInfo::exists(it.common().sourcePath)) continue;
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

        if (it.kind == ItemKind::Image) {
            const auto& img = it.image;
            // Input #inputIndex: image (loop)
            args << "-loop" << "1" << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << img.common.sourcePath;
            int imgInIdx = inputIndex++;
            // Input #inputIndex: silence
            args << "-f" << "lavfi" << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << QString("anullsrc=channel_layout=stereo:sample_rate=%1").arg(sampleRate);
            int silInIdx = inputIndex++;

            QString chain = QString("[%1:v]").arg(imgInIdx);
            chain += QString("scale=%1:%2:force_original_aspect_ratio=decrease").arg(W).arg(H);
            chain += QString(",pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black").arg(W).arg(H);
            chain += QString(",setsar=1,fps=%1,format=yuv420p").arg(FPS);
            QString dt = drawTextChain(img.common.subtitle, H, p.defaults.subtitle);
            if (!dt.isEmpty()) chain += "," + dt;
            chain += vFade;
            chain += QString("[%1]").arg(vlabel);
            chains << chain;

            QString achain = QString("[%1:a]aresample=%2,aformat=sample_fmts=fltp:channel_layouts=stereo")
                .arg(silInIdx).arg(sampleRate);
            achain += aFade;
            achain += QString("[%1]").arg(alabel);
            chains << achain;
        } else {
            const auto& vid = it.video;
            // -ss before -i for fast seek; -t for duration after -ss.
            args << "-ss" << QString::number(vid.startSecs, 'f', 4)
                 << "-t" << QString::number(dur, 'f', 4)
                 << "-i" << vid.common.sourcePath;
            int vIdx = inputIndex++;

            QString chain = QString("[%1:v]").arg(vIdx);
            chain += QString("scale=%1:%2:force_original_aspect_ratio=decrease").arg(W).arg(H);
            chain += QString(",pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black").arg(W).arg(H);
            chain += QString(",setsar=1,fps=%1,format=yuv420p").arg(FPS);
            QString dt = drawTextChain(vid.common.subtitle, H, p.defaults.subtitle);
            if (!dt.isEmpty()) chain += "," + dt;
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
    args << "-progress" << "pipe:1";
    args << outPath;

    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
    m_proc = new QProcess(this);
    m_proc->setProgram("ffmpeg");
    m_proc->setArguments(args);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        QString chunk = QString::fromUtf8(m_proc->readAllStandardOutput());
        for (const auto& line : chunk.split('\n', Qt::SkipEmptyParts)) {
            onStdoutLine(line.trimmed());
        }
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        QString chunk = QString::fromUtf8(m_proc->readAllStandardError());
        m_logTail += chunk;
        if (m_logTail.size() > 8192) m_logTail = m_logTail.right(8192);
        for (const auto& line : chunk.split('\n', Qt::SkipEmptyParts)) {
            emit log(line);
        }
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        emit log(QString("ffmpeg error: %1").arg(int(e)));
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        bool ok = (status == QProcess::NormalExit) && code == 0;
        if (ok) {
            emit progress(1.0);
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
    emit started();
    emit log(QString("ffmpeg started: %1 input(s), filter graph length %2 chars")
             .arg(inputIndex).arg(filterComplex.size()));
    return m_proc->program() + " " + m_proc->arguments().join(' ');
}

void Renderer::onStdoutLine(const QString& line) {
    // ffmpeg -progress pipe:1 emits "key=value" lines. We use out_time_us.
    int eq = line.indexOf('=');
    if (eq < 0) return;
    QString k = line.left(eq);
    QString v = line.mid(eq + 1);
    if (k == "out_time_us" || k == "out_time_ms") {
        bool ok = false;
        long long us = v.toLongLong(&ok);
        if (k == "out_time_ms") us *= 1000;
        if (ok && m_totalDuration > 0.0) {
            double s = us / 1'000'000.0;
            double frac = std::clamp(s / m_totalDuration, 0.0, 1.0);
            emit progress(frac);
        }
    } else if (k == "progress") {
        emit log("progress: " + v);
    }
}

} // namespace vlip
