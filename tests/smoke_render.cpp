// Headless smoke test: builds a Project from tests/data/, marks all
// items as used, renders to MP4, and verifies the output is non-empty.
//
// Build is wired via the CMakeLists.txt option BUILD_TESTS=ON.

#include "Project.h"
#include "ProjectIO.h"
#include "Importer.h"
#include "Renderer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QTimer>
#include <QDebug>

#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString dataDir = QString::fromUtf8(
        argc > 1 ? argv[1] : "tests/data");
    const QString out = QString::fromUtf8(
        argc > 2 ? argv[2] : "/tmp/vlip-smoke.mp4");

    QDir d(dataDir);
    if (!d.exists()) {
        qCritical("data dir not found: %s", qPrintable(dataDir));
        return 2;
    }

    vlip::Project p;
    p.canvas.width = 640;
    p.canvas.height = 360;
    p.canvas.fps = 30;

    auto entries = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& fi : entries) {
        auto r = vlip::Importer::importPath(fi.absoluteFilePath(), 1.0);
        if (!r.ok) {
            qWarning("skip %s: %s", qPrintable(fi.fileName()), qPrintable(r.error));
            continue;
        }
        // For testing keep durations small.
        if (r.item.kind == vlip::ItemKind::Image) {
            r.item.image.durationSecs = 0.5;
            r.item.image.common.subtitle = "img: " + fi.fileName();
        } else {
            r.item.video.startSecs = 0.0;
            r.item.video.endSecs = std::min(1.0, r.item.video.sourceDurationSecs);
            r.item.video.common.subtitle = "vid: " + fi.fileName();
        }
        r.item.common().used = true;
        p.items.append(r.item);
    }
    // Inject a text clip ahead of the first imported item so the smoke
    // render exercises that filter branch as well.
    if (!p.items.isEmpty()) {
        vlip::TextClipItem t;
        t.common.id = QUuid::createUuid();
        t.common.used = true;
        t.common.timestamp = p.items.first().common().timestamp.addMSecs(-1);
        t.text = "Smoke";
        t.durationSecs = 0.5;
        p.items.append(vlip::Item::makeTextClip(t));
    }

    p.sortChronologically();

    if (p.items.isEmpty()) {
        qCritical("no items imported from %s", qPrintable(dataDir));
        return 3;
    }
    qInfo("imported %d items, rendering -> %s", int(p.items.size()), qPrintable(out));

    auto* renderer = new vlip::Renderer(&app);
    int exitCode = -1;
    QObject::connect(renderer, &vlip::Renderer::log, [](const QString& s) {
        std::fprintf(stderr, "[renderer] %s\n", qPrintable(s));
    });
    QObject::connect(renderer, &vlip::Renderer::finished, &app,
        [&app, &exitCode, out](bool ok, const QString& msg) {
            std::fprintf(stderr, "\nrender finished: ok=%d msg=%s\n", ok, qPrintable(msg));
            if (ok) {
                QFileInfo fi(out);
                if (!fi.exists() || fi.size() == 0) {
                    qCritical("output file missing or empty");
                    exitCode = 5;
                } else {
                    qInfo("OK: %lld bytes at %s", (long long)fi.size(), qPrintable(out));
                    exitCode = 0;
                }
            } else {
                exitCode = 4;
            }
            app.quit();
        });

    QTimer::singleShot(0, [&]() {
        renderer->start(p, out);
    });
    app.exec();
    return exitCode;
}
