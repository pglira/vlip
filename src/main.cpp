#include "MainWindow.hpp"

#include <QApplication>
#include <QIcon>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("vlip");
    app.setOrganizationName("vlip");
    app.setWindowIcon(QIcon(":/vlip.svg"));

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
