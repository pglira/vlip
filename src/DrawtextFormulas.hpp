#pragma once

#include "Project.hpp"
#include <QString>
#include <QDateTime>

// Shared ffmpeg `drawtext` filter expressions for text-clip text,
// subtitles, and the burned-in date stamp. The renderer assembles the
// final filtergraph from these, and the live preview uses the same
// strings to render WYSIWYG overlays — so the on-screen and rendered
// outputs match exactly.

namespace vlip {

// Escape the special characters that drawtext's `text=` option uses.
QString escapeDrawText(const QString& text);

// Render a UTC timestamp as DD.MM.YYYY HH:MM in the project's
// time-zone (or system local if `tzId` is empty / invalid). Returns
// empty if the input is invalid.
QString formatDatestamp(const QDateTime& utc, const QByteArray& tzId);

// Drawtext expression for a text clip. Returns "" for blank text.
QString textClipDrawText(const QString& text, int canvasH,
                         const TextClipStyle& style);

// Drawtext expression for an image / video clip subtitle. The
// renderer passes the segment's duration so the visibility window
// (`visibleSecs`) can be applied; the preview passes 0 (or any
// value) and `includeVisibilityWindow=false` for an always-on
// overlay.
QString subtitleDrawText(const QString& subtitle, int canvasH,
                         const SubtitleStyle& style,
                         double segmentDur,
                         bool includeVisibilityWindow);

// Drawtext expression for the corner date stamp. Returns "" if the
// style is disabled or the text is empty.
QString datestampDrawText(const QString& text, int canvasH,
                          const DatestampStyle& s);

} // namespace vlip
