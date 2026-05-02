# vlip — Slideshow Compiler: Agent Build Brief

You are tasked with building a desktop application from scratch. This brief
describes WHAT the program must do. It deliberately does NOT prescribe a
programming language, GUI toolkit, media library, project-file format, or
build system — those are your choices. Choose tools that satisfy the
requirements below well, including the harder ones (real-time A/V playback,
dockable panes).

---

## Goal

A desktop app that takes a folder of smartphone-captured photos and short
video clips and compiles them into a single video file (a slideshow / montage)
with subtitles, basic per-item edits, and strict chronological ordering.

## Target user

A non-technical user assembling a montage from a phone-export dump. Typical
session: import ~20–200 mixed images and clips, mark a subset as used, set
durations/trims/subtitles, render once.

---

## Decisions already made (do not override)

- The application must be written in a **compiled language** (single-binary
  distribution preferred).
- Output format: **MP4 with H.264 video + AAC audio**.
- Default canvas: **1920×1080 @ 30 fps**, user-configurable.
- Items in a project are imported but default to **not included** in the
  render. The user must explicitly mark each item as "used" to include it.
- Ordering of items in the timeline is always chronological by timestamp.
  The only mechanism for deviating from this is a per-item timestamp override.
- Image timestamps come from EXIF (`DateTimeOriginal`, falling back to
  `DateTimeDigitized`, then `DateTime`); fall back to file mtime when EXIF is
  absent and flag the timestamp as uncertain.
- Video timestamps come from container creation time (e.g. MP4/MOV `mvhd`);
  fall back to file mtime when missing and flag as uncertain.

Anything else is your choice.

---

## Functional requirements

### 1. Import

- Multi-file open dialog **and** drag-and-drop into the window.
- Image formats: at minimum JPEG, PNG, HEIC, WEBP, TIFF.
- Video formats: at minimum MP4, MOV (typical smartphone outputs).
- Imported items are added to the project but are **not** marked as used
  by default — the user actively ticks the ones to include.
- Imported state survives save/reload.

### 2. Timeline view

- Shows every imported item, sorted strictly chronologically.
- Per-row info: thumbnail, filename, timestamp, duration (image) or
  trim summary (video), subtitle preview, and a "use in render" checkbox.
- Visually distinguish used vs not-used rows (dim, grey out, or similar).
- Flag rows whose timestamp is uncertain or has been overridden.
- Allow removing an item from the project (separate from the used toggle —
  this is true deletion of the project entry).

### 3. Per-item editing — common to images and videos

- Toggle "use in render".
- Subtitle text (rendered as burned-in captions in the output).
- Timestamp with an override toggle. Editing the timestamp re-sorts the
  timeline immediately.

### 4. Per-item editing — images

- Individual duration in seconds.
- Bulk-set the duration of every image at once (a single action).
- Cropping: a rectangular sub-region of the source.
- Color adjustments: at minimum brightness, contrast, saturation, with a
  "reset" action.
- A **live preview pane** that reflects crop + color edits in real time.

### 5. Per-item editing — videos

- Individual start and end times within the source clip.
- A **live preview pane** that:
  - Plays the clip with **synchronized audio**, not a slideshow of frames.
  - Has a scrub slider that can seek anywhere in the source clip.
  - Has Play / Pause / jump-to-start / jump-to-end controls.
  - Has "Set as Start" and "Set as End" buttons that copy the current scrub
    position into the clip's trim points.
- Bulk actions across all videos: trim N seconds from the start of every
  clip; trim N seconds from the end of every clip.

### 6. Project defaults

A dedicated settings area (a pane or panel — see UI requirements below) for
project-wide settings:

- Canvas width × height × fps.
- Default image duration + "Apply to all images".
- Bulk video trim from start + "Apply to all videos".
- Bulk video trim from end + "Apply to all videos".

This MUST NOT live in the top toolbar. Bulk controls are first-class
configuration, not transient buttons.

### 7. Project files

- Save and load the full project state to a single file at a user-chosen
  path.
- Restored state must include: imported items (paths, timestamps, override
  flags, used flags, subtitles), per-image edits (duration, crop, colors),
  per-video edits (start/end), project defaults, and canvas settings.
- File format must be versioned so future schema changes are
  forward-compatible.
- Source media is referenced by path. On load, handle missing source files
  gracefully (warn the user, keep the project entry).

### 8. Render

- One action that renders the project to a user-chosen MP4 path.
- Render includes only items marked as used.
- Strict chronological order.
- Subtitles burned into the output (no sidecar file).
- Image segments use the per-image duration; video segments use the per-clip
  trim range.
- Audio: real audio for video clips, silence over images, with no glitching
  at boundaries. Output sample rate normalized.
- Show progress (percentage or elapsed/total) and surface errors with enough
  detail for the user to act on them.
- Refuse to render if zero items are marked used, with a clear message.

---

## UI requirements

### Flexible, dockable layout

The UI must be built around **rearrangeable panes**, not a fixed layout.
The user can:

- Move panes between dock locations (left, right, top, bottom, center).
- Resize panes.
- Detach a pane into a floating window.
- Hide and re-show panes.

The pane layout must persist between sessions.

The five primary panes are:

1. **Timeline** — chronological list of imported items.
2. **Properties** — fields for the currently selected item.
3. **Preview** — image or video preview of the currently selected item.
4. **Project defaults** — canvas, image defaults, video defaults.
5. **Status / progress** — log messages, render progress, import warnings.

### Selection model

- Exactly one item is "selected" at a time.
- Selecting an item updates Properties and Preview.
- Selection is independent of the used/unused toggle.

---

## Non-functional requirements

- **Performance**: video preview must play smoothly (no visible stutter, no
  audio glitches) at native frame rate for typical smartphone clips
  (1080p30 / 1080p60). Scrubbing must show a frame at the new position
  within ~200 ms of the user releasing the slider.
- **Responsiveness**: import, thumbnail generation, and rendering must not
  block the UI thread.
- **Robustness**: malformed files surface as a per-file warning, not a
  crash. Missing source paths on project reload surface as a warning, not
  silent loss.
- **Footprint**: thumbnails and other caches go under the OS user cache
  directory, never inside the project directory or alongside source media.
- **Inspectability**: project files should be human-readable enough that a
  technical user can diff two saves.

---

## Verification checklist

The following must all pass:

1. Import a folder with mixed-orientation phone photos and short clips
   (some HEIC, some MP4 with audio). All appear in the timeline,
   chronologically sorted, with correct thumbnails.
2. Mark a subset as used. Render. The output MP4:
   - Plays in a standard player (mpv / VLC / QuickTime).
   - Contains only the marked items in chronological order.
   - Has subtitles burned in at the correct items.
   - Has audio from video clips and silence over images, with no audio
     glitches at clip boundaries.
   - Matches the configured resolution and fps.
3. Set bulk image duration; verify all images update. Override one image's
   duration; render; that image alone has the overridden length.
4. In the video preview: play a clip with audio. Pause at an arbitrary
   point. Click "Set as Start". The clip's trim start now equals the paused
   timestamp. Render. The output trims correctly.
5. Save the project. Close the app. Reopen. Open the project. Every piece
   of state from before is restored, including which items were marked
   used.
6. Detach the Preview pane into a floating window, dock the Properties
   pane to the right, hide Status. Restart the app. Layout is restored.
7. Refuse-to-render with no items marked used produces a clear error
   message, not a partial output.
8. A 30-second 1080p30 clip plays in preview with synchronized audio at
   idle CPU; no dropped frames, no audio underruns.

---

## Out of scope for v1

- Transitions between clips (crossfades, wipes).
- A background music track distinct from clip audio.
- Multi-track timeline editing.
- Color grading beyond brightness/contrast/saturation.
- Automatic format conversion for unusual inputs (e.g. proprietary RAW).
- Cloud sync, collaboration, undo history beyond what the UI gives for free.

---

## Notes for the implementing agent

- The hard parts of this app are: (a) synchronized A/V playback in the
  preview, (b) dockable/rearrangeable panes, and (c) a render pipeline
  that emits a clean concatenated MP4 with mixed image/video sources and
  burned subtitles. Pick libraries and a GUI toolkit that handle these
  natively. A subprocess-based "extract a still per frame" approach to
  preview is not acceptable — it cannot deliver smooth playback or audio.
- Project state is a small, well-typed data model. Keep it pure: edits
  mutate project state only, not source files or any cache. Any caches
  must be reproducible from project state and source files.
- Sorting by timestamp must run on every mutation that could change order
  (import, timestamp override).
- The "used" flag is independent of selection and of project membership.
  These are three separate concepts; don't conflate them.
- Build the layout system first (or pick a toolkit that gives it for
  free). Retrofitting dockable panes onto a fixed layout is painful.

---

## Recommended stacks

These are concrete recommendations that satisfy every requirement above.
Pick one and run with it. Deviating is fine if you can justify it.

### Primary recommendation — C++ with Qt 6

The most pragmatic choice. Qt solves the two hardest problems (dockable
panes and synchronized A/V playback) out of the box.

- **Language**: C++17 or later.
- **Build**: CMake. Vendor Qt via your distro / official installer / aqt /
  vcpkg.
- **GUI**: Qt 6 Widgets. `QMainWindow` + `QDockWidget` is the gold-standard
  dockable-pane system: move, resize, dock, float, hide, all built in.
  Persist layout with `QMainWindow::saveState()` / `restoreState()` via
  `QSettings`.
- **A/V preview**: Qt Multimedia. `QMediaPlayer` plus `QVideoWidget`
  (simplest) or `QVideoSink` (for custom rendering) and `QAudioOutput`.
  Synchronized audio is automatic — that's exactly what `QMediaPlayer` is
  for. Scrubbing via `setPosition()`, frame-accurate trim points by reading
  `QMediaPlayer::position()` at pause.
- **Render pipeline**: shell out to `ffmpeg` via `QProcess`. Build the
  filtergraph in C++; parse `-progress pipe:1` output for the progress UI.
  (libav\* directly is also viable but much more code.)
- **EXIF**: libexiv2.
- **Container metadata**: `ffprobe` via `QProcess`, parsed with
  `QJsonDocument`.
- **Image edits**: `QImage` for crop and pixel-level brightness / contrast /
  saturation.
- **Project file**: JSON via `QJsonDocument`, with a `version` field.
- **Thumbnails / caches**: under `QStandardPaths::CacheLocation`.

Trade-offs: Qt is a large dependency, but it's the only stack here where
docking AND A/V playback are first-class. Cross-platform from one codebase.

### Alternative — Rust with egui + GStreamer

Use this if you want a single-language Rust stack and don't need Qt-grade
docking polish.

- **Language**: Rust (stable).
- **GUI**: `eframe` + `egui`, with
  [`egui_dock`](https://crates.io/crates/egui_dock) for dockable, floatable,
  hideable panes. Serialize the dock state with `serde` and persist via
  `eframe`'s built-in storage.
- **A/V preview**: `gstreamer` + `gstreamer-rs`. Build a pipeline with
  `playbin` for the audio side and `decodebin` → `appsink` to pull decoded
  video frames into an egui texture. Use `autoaudiosink` (or `pulsesink` /
  `pipewiresink`) for audio. GStreamer handles A/V sync natively. Seeking
  via `gst::SeekFlags::FLUSH | KEY_UNIT` for scrub, `ACCURATE` for "Set as
  Start/End".
- **Render pipeline**: shell out to `ffmpeg` via `std::process::Command`.
- **EXIF**: `kamadak-exif`.
- **Container metadata**: `mp4` crate, or shell out to `ffprobe`.
- **Image edits**: `image` crate for decode + pixel ops.
- **Project file**: `serde` + `serde_json`.
- **Concurrency**: `std::thread` + `mpsc` is enough.

Trade-offs: GStreamer is a heavy runtime dependency (especially on Windows)
and `egui_dock` is not as polished as Qt's docking. Pure Rust everywhere
else.

### Alternative — C++ with Dear ImGui (docking branch) + libmpv

Use this if you want a thin, GPU-accelerated UI without Qt's footprint.

- **Language**: C++17 or later.
- **Build**: CMake with vcpkg or Conan.
- **GUI**: Dear ImGui on the **docking** branch + GLFW or SDL2 + an OpenGL3
  / Vulkan backend. The docking branch gives you docked, floatable, and
  detached-into-OS-windows panes. ImGui auto-persists layout to `imgui.ini`.
- **A/V preview**: **libmpv** embedded as a render context.
  `mpv_render_context` renders into a shared GL FBO; you present that
  texture inside an ImGui image widget. mpv handles decoding, A/V sync, and
  audio output internally — you just send commands (`loadfile`, `seek`,
  `pause`).
- **Render pipeline**: shell out to `ffmpeg`.
- **EXIF**: libexiv2.
- **Image decode**: `stb_image` for reading + hand-rolled pixel ops, or
  OpenCV if you want filters cheaply.
- **Project file**: `nlohmann/json` with a `version` field.

Trade-offs: leaner than Qt, native docking, libmpv is the most
battle-tested player you can embed. Downside is more glue code, especially
the GL context sharing between ImGui's renderer and mpv's render context.

### Stacks to avoid for this project

- **Web frontends (Tauri, Electron)** — fail the compiled-language spirit
  and make A/V preview harder, not easier.
- **Fyne, Iced, Slint** — none have a strong dockable-pane story; you'd
  spend more time building docking than building the rest of the app.
- **Pure-Rust GUI without `egui_dock` or equivalent** — the docking
  requirement will eat your schedule.
- **Subprocess-per-frame approaches to video preview** — cannot meet the
  smoothness or audio-sync requirements. Use a real media library
  (Qt Multimedia, GStreamer, or libmpv).
