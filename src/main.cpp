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
    return app.exec();
}
