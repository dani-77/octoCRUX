#include "backend/crux_adapter.h"
#include "ui/mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("octoCRUX"));
    app.setApplicationDisplayName(QStringLiteral("OctoCRUX — CRUX Linux Package Manager"));
    app.setOrganizationName(QStringLiteral("OctoCRUX"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    auto backend = std::make_shared<OctoCRUX::CruxAdapter>();
    OctoCRUX::MainWindow window(backend);
    window.show();

    return app.exec();
}
