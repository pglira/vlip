#include "DrawtextFormulas.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTimeZone>

namespace vlip {

namespace {

// Materialise `text` as a small file under `workDir` and return the
// absolute path. The filename is sha1(content).txt so identical content
// always lands in the same file — preview renders that re-issue the
// same overlay benefit from caching, and concurrent writes of the same
// text are idempotent. Returns "" on I/O failure.
QString writeTextfile(const QString& text, const QString& workDir) {
    if (workDir.isEmpty()) return {};
    if (!QDir().mkpath(workDir)) return {};
    const QByteArray utf8 = text.toUtf8();
    const QString name =
        QString::fromLatin1(QCryptographicHash::hash(utf8, QCryptographicHash::Sha1).toHex())
        + QStringLiteral(".txt");
    const QString path = QDir(workDir).absoluteFilePath(name);
    if (!QFileInfo::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
        if (f.write(utf8) != utf8.size()) return {};
    }
    return path;
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

} // namespace

QString previewTextfileDir() {
    static const QString d = []{
        QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (base.isEmpty()) base = QDir::tempPath();
        const QString p = QDir(base).absoluteFilePath(QStringLiteral("vlip-text"));
        QDir().mkpath(p);
        return p;
    }();
    return d;
}

QString formatDatestamp(const QDateTime& utc, const QByteArray& tzId,
                        const QString& pattern) {
    if (!utc.isValid() || pattern.isEmpty()) return {};
    QDateTime t = utc;
    t.setTimeSpec(Qt::UTC);
    if (tzId.isEmpty()) {
        t = t.toLocalTime();
    } else {
        QTimeZone z(tzId);
        if (z.isValid()) t = t.toTimeZone(z);
        else             t = t.toLocalTime();
    }
    return t.toString(pattern);
}

QString textClipDrawText(const QString& text,
                         const TextClipStyle& style,
                         const QString& textWorkDir) {
    if (text.trimmed().isEmpty()) return {};
    const QString textPath = writeTextfile(text, textWorkDir);
    if (textPath.isEmpty()) return {};
    QString font = resolveFontFile(style.fontFamily);
    const int fontSize = style.fontSizePx;
    QString chain = "drawtext=";
    if (!font.isEmpty()) {
        chain += QString("fontfile='%1':").arg(font);
    }
    chain += QString("textfile='%1'").arg(textPath);
    chain += QString(":fontcolor=%1").arg(colorToDrawtext(style.fontColor));
    chain += QString(":fontsize=%1").arg(fontSize);
    if (style.outlineWidthPx > 0) {
        chain += QString(":bordercolor=%1").arg(colorToDrawtext(style.outlineColor));
        chain += QString(":borderw=%1").arg(style.outlineWidthPx);
    }
    // T+C = top-aligned within the text block, each line centered.
    chain += ":text_align=T+C";
    chain += ":x=(w-text_w)/2";
    switch (style.verticalAlign) {
        case VerticalAlign::Top:    chain += ":y=h/12"; break;
        case VerticalAlign::Middle: chain += ":y=(h-text_h)/2"; break;
        case VerticalAlign::Bottom: chain += ":y=h-(text_h)-h/12"; break;
    }
    return chain;
}

QString subtitleDrawText(const QString& subtitle,
                         const SubtitleStyle& style, double segmentDur,
                         bool includeVisibilityWindow,
                         const QString& textWorkDir) {
    if (subtitle.trimmed().isEmpty()) return {};
    const QString textPath = writeTextfile(subtitle, textWorkDir);
    if (textPath.isEmpty()) return {};
    QString font = resolveFontFile(style.fontFamily);
    const int fontSize = style.fontSizePx;
    QString chain = "drawtext=";
    if (!font.isEmpty()) {
        chain += QString("fontfile='%1':").arg(font);
    }
    chain += QString("textfile='%1'").arg(textPath);
    chain += QString(":fontcolor=%1").arg(colorToDrawtext(style.fontColor));
    chain += QString(":fontsize=%1").arg(fontSize);
    if (style.outlineWidthPx > 0) {
        chain += QString(":bordercolor=%1").arg(colorToDrawtext(style.outlineColor));
        chain += QString(":borderw=%1").arg(style.outlineWidthPx);
    }
    chain += QString(":box=1:boxcolor=%1:boxborderw=12").arg(colorToDrawtext(style.bgColor));
    // T+C = top-aligned within the text block, each line centered.
    chain += ":text_align=T+C";
    chain += ":x=(w-text_w)/2";
    const int margin = std::max(0, style.marginPx);
    switch (style.position) {
        case SubtitlePosition::Top:    chain += QString(":y=%1").arg(margin); break;
        case SubtitlePosition::Middle: chain += ":y=(h-text_h)/2"; break;
        case SubtitlePosition::Bottom: chain += QString(":y=h-(text_h)-%1").arg(margin); break;
    }
    // Visibility window: each segment's filtergraph time starts at 0, so
    // lt(t,N) limits the burn to the first N seconds. Skipped when 0
    // (always-on), when the limit covers the full segment anyway, or
    // when the caller explicitly opts out (e.g. the live preview, which
    // is static).
    if (includeVisibilityWindow
        && style.visibleSecs > 0.0
        && style.visibleSecs < segmentDur) {
        chain += QString(":enable='lt(t,%1)'").arg(style.visibleSecs, 0, 'f', 4);
    }
    return chain;
}

QString datestampDrawText(const QString& text,
                          const DatestampStyle& s,
                          const QString& textWorkDir) {
    if (!s.active || text.isEmpty()) return {};
    const QString textPath = writeTextfile(text, textWorkDir);
    if (textPath.isEmpty()) return {};
    QString font = resolveFontFile(s.fontFamily);
    const int fontSize = s.fontSizePx;
    QString chain = "drawtext=";
    if (!font.isEmpty()) {
        chain += QString("fontfile='%1':").arg(font);
    }
    chain += QString("textfile='%1'").arg(textPath);
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

} // namespace vlip
