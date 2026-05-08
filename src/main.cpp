#include "MainWindow.hpp"

#include <QApplication>
#include <QIcon>
#include <QImageReader>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("vlip");
    app.setOrganizationName("vlip");
    app.setWindowIcon(QIcon(":/vlip.svg"));

    // Qt 6 caps QImageReader allocations at 128 MB by default, which
    // silently rejects ~30 MP+ photos (e.g. iPhone panoramas at ~10800×3900
    // need ~161 MB as ARGB32 — `read()` just returns a null QImage). Lift
    // the cap to 4 GB so phone panoramas decode.
    QImageReader::setAllocationLimit(4096);

    vlip::MainWindow w;
    w.show();

    // Optional positional arg: open a .vlip project on launch. Anything
    // beyond the first non-flag is ignored. QApplication has already
    // consumed Qt's own switches (-style, -display, …) from app.arguments().
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i].startsWith('-')) continue;
        w.loadProject(args[i]);
        break;
    }
    return app.exec();
}
