// Save/load roundtrip test: import test data, mutate state,
// save to JSON, reload, verify the reloaded project matches.

#include "Project.h"
#include "ProjectIO.h"
#include "Importer.h"

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
    p.defaults.imageDuration = 3.5;

    auto entries = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& fi : entries) {
        auto r = vlip::Importer::importPath(fi.absoluteFilePath(), p.defaults.imageDuration);
        if (!r.ok) continue;
        if (r.item.kind == vlip::ItemKind::Image) {
            r.item.image.durationSecs = 5.0;
        }
        r.item.common().subtitle = "test:" + fi.fileName();
        r.item.common().used = true;
        p.items.append(r.item);
    }
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
    CHECK(reload.defaults.imageDuration == p.defaults.imageDuration, "imageDuration mismatch");
    CHECK(reload.items.size() == p.items.size(), "items count mismatch");

    for (int i = 0; i < p.items.size(); ++i) {
        const auto& a = p.items[i];
        const auto& b = reload.items[i];
        CHECK(a.kind == b.kind, "kind mismatch");
        CHECK(a.common().id == b.common().id, "id mismatch");
        CHECK(a.common().used == b.common().used, "used mismatch");
        CHECK(a.common().subtitle == b.common().subtitle, "subtitle mismatch");
        if (a.kind == vlip::ItemKind::Image) {
            CHECK(qFuzzyCompare(a.image.durationSecs, b.image.durationSecs),
                  "image durationSecs mismatch");
        } else {
            CHECK(qFuzzyCompare(a.video.startSecs + 1, b.video.startSecs + 1), "start mismatch");
            CHECK(a.video.hasAudio == b.video.hasAudio, "hasAudio mismatch");
        }
    }
    qInfo("OK: save/load roundtrip preserved %d items", int(p.items.size()));
    return 0;
}
