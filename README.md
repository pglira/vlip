# vlip — Slideshow Compiler

A small Qt 6 desktop app that compiles a folder of phone photos and short
video clips into a single H.264/AAC MP4 — with subtitles, per-item
edits, text-clip title cards, and strict chronological ordering.

## Features

- Drag-and-drop import of mixed images (JPEG/PNG/HEIC/WebP/TIFF) and
  videos (MP4/MOV/…) — parallelised, with progress in the status pane.
- Strict chronological ordering by EXIF / container timestamp;
  uncertain timestamps are flagged.
- Per-item edits: subtitle (burned in), image duration + interactive
  crop tool with project-aspect lock, video trim with frame-accurate
  scrubbing.
- Text clips between images/videos: text + optional background image,
  inserted before/after the selected item from the timeline toolbar.
- Project-wide options: canvas presets (Full HD / 4K @ 30/60), subtitle
  styling, transitions, and a corner date-stamp overlay (per-item
  timestamps in any IANA time zone).
- Dockable, persistent UI: timeline on the left, preview / properties /
  project settings on the right, status at the bottom. Layouts and
  timeline column widths survive restart.
- One-click render via `ffmpeg`; refuses to render when nothing is
  marked used, with a clear message.

## Keyboard shortcuts

| Shortcut             | Action                              |
| -------------------- | ----------------------------------- |
| `Ctrl+↓` / `Ctrl+J`  | Next item                           |
| `Ctrl+↑` / `Ctrl+K`  | Previous item                       |
| `Ctrl+Space`         | Toggle "used" on selected item      |
| `Ctrl+Shift+C`       | Open crop tool (image preview)      |
| `Space`              | Play / pause (video preview)        |
| `Enter`              | Commit subtitle / text-clip edit    |
| `Ctrl+R`             | Render…                             |
| `Ctrl+S` / `Ctrl+O`  | Save / open project                 |

## Build

Linux, Qt 6.4+:

```sh
sudo apt install qt6-base-dev qt6-multimedia-dev libheif-dev ffmpeg
cmake -B build && cmake --build build -j
./build/vlip
```

`ffmpeg` and `ffprobe` must be on `PATH` at runtime.

## License

MIT — see [LICENSE](LICENSE).
