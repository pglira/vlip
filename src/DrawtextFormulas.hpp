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

// Render a UTC timestamp using the supplied QDateTime format pattern
// in the project's time-zone (or system local if `tzId` is empty /
// invalid). Returns empty if the input is invalid or the pattern is
// blank.
QString formatDatestamp(const QDateTime& utc, const QByteArray& tzId,
                        const QString& pattern);

// Returns the writable directory the drawtext helpers below should
// use to materialise `textfile` payloads for live-preview rendering.
// The path is created on first call. Stable across runs so cached
// preview overlays keep their cache keys.
QString previewTextfileDir();

// All three drawtext expression builders below write the user text
// to a small file under `textWorkDir` and embed `textfile='…'` in the
// expression. This avoids every layer of ffmpeg's quote/escape
// handling for the text body — apostrophes, colons, percent signs,
// newlines and arbitrary unicode all pass through verbatim. The path
// chosen for each file is the SHA-1 hex of its contents, so identical
// text across calls maps to a stable filename (and hence a stable
// cache key for the preview's TextOverlayRenderer).
//
// `textWorkDir` must already exist or be writable (mkpath is called);
// any I/O failure causes the function to return "" — treated as "no
// overlay" by callers.

// Drawtext expression for a text clip. Returns "" for blank text or
// I/O failure.
QString textClipDrawText(const QString& text,
                         const TextClipStyle& style,
                         const QString& textWorkDir);

// Drawtext expression for an image / video clip subtitle. The
// renderer passes the segment's duration so the visibility window
// (`visibleSecs`) can be applied; the preview passes 0 (or any
// value) and `includeVisibilityWindow=false` for an always-on
// overlay.
QString subtitleDrawText(const QString& subtitle,
                         const SubtitleStyle& style,
                         double segmentDur,
                         bool includeVisibilityWindow,
                         const QString& textWorkDir);

// Drawtext expression for the corner date stamp. Returns "" if the
// style is disabled, the text is empty, or the textfile can't be
// written.
QString datestampDrawText(const QString& text,
                          const DatestampStyle& s,
                          const QString& textWorkDir);

} // namespace vlip
