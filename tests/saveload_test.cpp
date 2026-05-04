// Save/load roundtrip test: import test data, mutate state,
// save to JSON, reload, verify the reloaded project matches.

#include "Project.hpp"
#include "ProjectIO.hpp"
#include "Importer.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QDebug>

#include <cstdio>

#define CHECK(cond, msg) do { \
    if (!(cond)) { qCritical("FAIL: %s", msg); return 1; } \
} while (0)

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString dataDir = QString::fromUtf8(argc > 1 ? argv[1] : "tests/data");
    QDir d(dataDir);
    if (!d.exists()) {
        qCritical("data dir not found: %s", qPrintable(dataDir));
        return 2;
    }

    vlip::Project p;
    p.canvas.width = 1280;
    p.canvas.height = 720;
    p.canvas.fps = 25;
    p.defaults.imageClipDuration = 3.5;

    auto entries = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& fi : entries) {
        auto r = vlip::Importer::importPath(fi.absoluteFilePath(), p.defaults.imageClipDuration);
        if (!r.ok) continue;
        if (r.item.kind == vlip::ItemKind::ImageClip) {
            r.item.imageClip.durationSecs = 5.0;
        }
        r.item.common().subtitle = "test:" + fi.fileName();
        r.item.common().used = true;
        p.items.append(r.item);
    }
    // Add a synthetic text clip to exercise that branch of the model + IO.
    {
        vlip::TextClip t;
        t.common.id = QUuid::createUuid();
        t.common.used = true;
        t.common.timestamp = QDateTime::currentDateTimeUtc().addYears(-1);
        t.text = "hello\nworld";
        t.backgroundPath = "";
        t.durationSecs = 4.25;
        p.items.append(vlip::Item::makeTextClip(t));
    }
    // Tweak text-clip defaults so we exercise the round-trip too.
    p.defaults.textClip.fontSizePx = 80;
    p.defaults.textClip.verticalAlign = vlip::VerticalAlign::Top;
    p.defaults.textClip.defaultDuration = 6.0;
    // Distinct colours per channel (including a non-trivial alpha) so we
    // catch any RGB ↔ alpha shuffling in the colour serializer.
    p.defaults.textClip.fontColor = QColor(11, 22, 33, 200);
    p.defaults.subtitle.fontColor = QColor(44, 55, 66, 255);
    p.defaults.subtitle.bgColor   = QColor(77, 88, 99, 140);
    p.defaults.datestamp.format   = "HH:mm";

    // Background-music playlist (paths don't have to exist for the
    // round-trip — a missing-file warning is expected on load and is OK).
    p.backgroundMusic = QStringList{
        "/tmp/vlip-test-music-a.mp3",
        "/tmp/vlip-test-music-b.mp3",
    };

    p.sortChronologically();
    CHECK(!p.items.isEmpty(), "no items imported");

    QString saveTo = "/tmp/vlip-saveload.vlip";
    QString err;
    CHECK(vlip::ProjectIO::save(p, saveTo, &err), qPrintable(err));

    vlip::Project reload;
    QStringList warns;
    QString err2;
    CHECK(vlip::ProjectIO::load(&reload, saveTo, &warns, &err2), qPrintable(err2));

    CHECK(reload.canvas.width == p.canvas.width, "canvas.width mismatch");
    CHECK(reload.canvas.height == p.canvas.height, "canvas.height mismatch");
    CHECK(reload.canvas.fps == p.canvas.fps, "canvas.fps mismatch");
    CHECK(reload.defaults.imageClipDuration == p.defaults.imageClipDuration,
          "imageClipDuration mismatch");
    CHECK(reload.defaults.textClip.fontSizePx == p.defaults.textClip.fontSizePx,
          "text-clip fontSize mismatch");
    CHECK(reload.defaults.textClip.verticalAlign == p.defaults.textClip.verticalAlign,
          "text-clip vAlign mismatch");
    CHECK(qFuzzyCompare(reload.defaults.textClip.defaultDuration,
                        p.defaults.textClip.defaultDuration),
          "text-clip default duration mismatch");
    CHECK(reload.defaults.textClip.fontColor == p.defaults.textClip.fontColor,
          "text-clip fontColor mismatch");
    CHECK(reload.defaults.subtitle.fontColor == p.defaults.subtitle.fontColor,
          "subtitle fontColor mismatch");
    CHECK(reload.defaults.subtitle.bgColor == p.defaults.subtitle.bgColor,
          "subtitle bgColor mismatch");
    CHECK(reload.defaults.datestamp.format == p.defaults.datestamp.format,
          "datestamp format mismatch");
    CHECK(reload.items.size() == p.items.size(), "items count mismatch");
    CHECK(reload.backgroundMusic == p.backgroundMusic, "backgroundMusic mismatch");

    for (int i = 0; i < p.items.size(); ++i) {
        const auto& a = p.items[i];
        const auto& b = reload.items[i];
        CHECK(a.kind == b.kind, "kind mismatch");
        CHECK(a.common().id == b.common().id, "id mismatch");
        CHECK(a.common().used == b.common().used, "used mismatch");
        CHECK(a.common().subtitle == b.common().subtitle, "subtitle mismatch");
        switch (a.kind) {
            case vlip::ItemKind::ImageClip:
                CHECK(qFuzzyCompare(a.imageClip.durationSecs, b.imageClip.durationSecs),
                      "image-clip durationSecs mismatch");
                break;
            case vlip::ItemKind::VideoClip:
                CHECK(qFuzzyCompare(a.videoClip.startSecs + 1, b.videoClip.startSecs + 1),
                      "start mismatch");
                CHECK(a.videoClip.hasAudio == b.videoClip.hasAudio, "hasAudio mismatch");
                break;
            case vlip::ItemKind::TextClip:
                CHECK(a.textClip.text == b.textClip.text, "text-clip text mismatch");
                CHECK(qFuzzyCompare(a.textClip.durationSecs, b.textClip.durationSecs),
                      "text-clip duration mismatch");
                CHECK(a.textClip.backgroundPath == b.textClip.backgroundPath,
                      "text-clip bg path mismatch");
                break;
        }
    }
    qInfo("OK: save/load roundtrip preserved %d items", int(p.items.size()));
    return 0;
}
