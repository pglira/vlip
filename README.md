# vlip — Slideshow Compiler

Turn a folder of photos and short video clips into a single MP4
slideshow — sorted chronologically, with subtitles, title cards
between clips, and per-clip adjustments.

## Workflow

1. Drop your photos and video clips onto the timeline (or use
   **Import…**) — they become image clips and video clips.
2. Tick the clips you want in the final video.
3. Tweak each clip: trim video clips, set image-clip durations, crop,
   add a subtitle, drop in a text-clip title card.
4. Set project-wide preferences: canvas size, frame rate, subtitle
   styling, optional date stamp.
5. **Render** to MP4.

## What it does

- Imports a mix of JPEG, PNG, HEIC, WebP, TIFF, MP4, MOV, and similar
  as image clips and video clips.
- Sorts clips strictly by their timestamp; clips with an uncertain
  timestamp are flagged.
- Shows a live preview of the selected clip. A coloured border tells
  you at a glance whether it will be included in the render
  (green = used, grey = skipped, red = source file missing).
- Frame-accurate video-clip trimming with audio playback.
- Interactive image-clip cropping, with an optional aspect-ratio lock
  to match the project canvas.
- Subtitles burned into the output, with adjustable font, size,
  colour, position, and on-screen duration.
- Text-clip title cards between clips, with optional background image.
- Optional date / time stamp burned into a chosen corner of every
  image and video clip, in a configurable time zone.
- Bulk actions to apply a duration to every image clip or every text
  clip at once.
- Canvas presets for Full HD and 4K at 30 or 60 fps; custom values too.
- Dockable, resizable panes; layouts and column widths are remembered
  across sessions.
- Save and reopen projects; missing source files are flagged on load
  rather than silently dropped.

## Keyboard shortcuts

| Shortcut            | Action                              |
| ------------------- | ----------------------------------- |
| `Ctrl+↓` / `Ctrl+J` | Next clip                           |
| `Ctrl+↑` / `Ctrl+K` | Previous clip                       |
| `Ctrl+Home`         | First clip                          |
| `Ctrl+End`          | Last clip                           |
| `Ctrl+Space`        | Toggle "used" on selected clip      |
| `Delete`            | Remove selected clip                |
| `Ctrl+T`            | Insert text clip after selected     |
| `Ctrl+Shift+T`      | Insert text clip before selected    |
| `Ctrl+H`            | Toggle "Hide unused"                |
| `F2`                | Edit subtitle / text-clip text      |
| `Ctrl+Shift+C`      | Open crop tool (image-clip preview) |
| `Ctrl+P`            | Toggle project settings             |
| `Space`             | Play / pause (video-clip preview)   |
| `Enter`             | Commit subtitle / text-clip edit    |
| `Ctrl+I`            | Import media…                       |
| `Ctrl+R`            | Render…                             |
| `Ctrl+N`            | New project                         |
| `Ctrl+O` / `Ctrl+S` | Open / save project                 |
| `Ctrl+Shift+S`      | Save project as…                    |

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
