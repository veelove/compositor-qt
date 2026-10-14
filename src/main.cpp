#include "MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Compositor Qt");
    QApplication::setOrganizationName("Compositor Qt");
    QApplication::setApplicationVersion("0.1.0");

    MainWindow window;
    window.show();

    return app.exec();
}
