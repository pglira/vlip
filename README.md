# vlip — Slideshow Compiler

Turn a folder of photos and short video clips into a single MP4
slideshow — sorted chronologically, with subtitles, title cards
between clips, and per-item adjustments.

## Workflow

1. Drop your photos and clips onto the timeline (or use **Import…**).
2. Tick the items you want in the final video.
3. Tweak each item: trim videos, set image durations, crop, add a
   subtitle, drop in a text-clip title card.
4. Set project-wide preferences: canvas size, frame rate, subtitle
   styling, optional date stamp.
5. **Render** to MP4.

## What it does

- Imports a mix of JPEG, PNG, HEIC, WebP, TIFF, MP4, MOV, and similar.
- Sorts items strictly by their timestamp; items with an uncertain
  timestamp are flagged.
- Shows a live preview of the selected item. A coloured border tells
  you at a glance whether it will be included in the render
  (green = used, grey = skipped, red = source file missing).
- Frame-accurate video trimming with audio playback.
- Interactive image cropping, with an optional aspect-ratio lock to
  match the project canvas.
- Subtitles burned into the output, with adjustable font, size,
  colour, position, and on-screen duration.
- Title cards ("text clips") between items, with optional background
  image.
- Optional date / time stamp burned into a chosen corner of every
  image and video, in a configurable time zone.
- Bulk actions to apply a duration to every image or every text clip
  at once.
- Canvas presets for Full HD and 4K at 30 or 60 fps; custom values too.
- Dockable, resizable panes; layouts and column widths are remembered
  across sessions.
- Save and reopen projects; missing source files are flagged on load
  rather than silently dropped.

## Keyboard shortcuts

| Shortcut            | Action                           |
| ------------------- | -------------------------------- |
| `Ctrl+↓` / `Ctrl+J` | Next item                        |
| `Ctrl+↑` / `Ctrl+K` | Previous item                    |
| `Ctrl+Home`         | First item                       |
| `Ctrl+End`          | Last item                        |
| `Ctrl+Space`        | Toggle "used" on selected item   |
| `Delete`            | Remove selected item             |
| `Ctrl+T`            | Insert text clip after selected  |
| `Ctrl+Shift+T`      | Insert text clip before selected |
| `Ctrl+H`            | Toggle "Hide unused"             |
| `F2`                | Edit subtitle / text-clip text   |
| `Ctrl+Shift+C`      | Open crop tool (image preview)   |
| `Ctrl+P`            | Toggle project settings          |
| `Space`             | Play / pause (video preview)     |
| `Enter`             | Commit subtitle / text-clip edit |
| `Ctrl+I`            | Import media…                    |
| `Ctrl+R`            | Render…                          |
| `Ctrl+N`            | New project                      |
| `Ctrl+O` / `Ctrl+S` | Open / save project              |
| `Ctrl+Shift+S`      | Save project as…                 |

## Install and run

On Debian / Ubuntu:

```sh
sudo apt install qt6-base-dev qt6-multimedia-dev libheif-dev ffmpeg
cmake -B build && cmake --build build -j
./build/vlip
```

`ffmpeg` must be installed on your system; vlip uses it to render the
final video.

## License

MIT — see [LICENSE](LICENSE).
