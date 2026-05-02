use std::path::PathBuf;
use std::time::Duration;

use chrono::{TimeZone, Utc};
use uuid::Uuid;

use vlip::model::{ColorAdjust, Common, ImageItem, Item, Project, Rect};
use vlip::render::{self, RenderEvent};

fn ffmpeg_available() -> bool {
    std::process::Command::new("ffmpeg")
        .arg("-version")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

fn make_color_png(path: &std::path::Path, color: &str) {
    let status = std::process::Command::new("ffmpeg")
        .args([
            "-y", "-hide_banner", "-loglevel", "error",
            "-f", "lavfi", "-i", &format!("color=c={color}:size=320x240"),
            "-frames:v", "1",
        ])
        .arg(path)
        .status()
        .unwrap();
    assert!(status.success());
}

fn img_item(path: PathBuf, ts: i64, dur: f32, subtitle: Option<&str>, crop: Option<Rect>) -> Item {
    Item::Image(ImageItem {
        common: Common {
            id: Uuid::new_v4(),
            source_path: path,
            timestamp: Utc.timestamp_opt(ts, 0).unwrap(),
            timestamp_overridden: false,
            timestamp_uncertain: false,
            subtitle: subtitle.map(String::from),
            used: true,
        },
        duration_secs: dur,
        crop,
        adjust: ColorAdjust { brightness: 0.05, contrast: 1.1, saturation: 1.0 },
    })
}

#[test]
fn renders_two_image_slideshow_to_mp4() {
    if !ffmpeg_available() {
        eprintln!("ffmpeg not on PATH, skipping smoke test");
        return;
    }
    let tmp = std::env::temp_dir().join("vlip-smoke");
    std::fs::create_dir_all(&tmp).unwrap();
    let red = tmp.join("red.png");
    let blue = tmp.join("blue.png");
    make_color_png(&red, "red");
    make_color_png(&blue, "blue");

    let mut p = Project::default();
    p.canvas.width = 320;
    p.canvas.height = 240;
    p.canvas.fps = 25;
    p.items.push(img_item(red, 100, 0.5, Some("hello: world"), None));
    p.items.push(img_item(
        blue,
        200,
        0.5,
        None,
        Some(Rect { x: 0.1, y: 0.1, w: 0.8, h: 0.8 }),
    ));
    p.sort_chronologically();

    let out = tmp.join("out.mp4");
    let _ = std::fs::remove_file(&out);
    let handle = render::render_async(p, out.clone());

    let deadline = std::time::Instant::now() + Duration::from_secs(60);
    let mut finished_ok = None;
    let mut last_log = String::new();
    while std::time::Instant::now() < deadline {
        match handle.rx.recv_timeout(Duration::from_secs(1)) {
            Ok(RenderEvent::Finished(r)) => {
                if let Err(e) = &r {
                    eprintln!("render error: {e:#}\nlog tail:\n{last_log}");
                }
                finished_ok = Some(r.is_ok());
                break;
            }
            Ok(RenderEvent::Log(s)) => last_log = s,
            Ok(_) => {}
            Err(std::sync::mpsc::RecvTimeoutError::Timeout) => continue,
            Err(std::sync::mpsc::RecvTimeoutError::Disconnected) => break,
        }
    }
    assert_eq!(finished_ok, Some(true), "render did not finish successfully");
    let meta = std::fs::metadata(&out).expect("output mp4 should exist");
    assert!(meta.len() > 0, "output mp4 should be non-empty");
}
