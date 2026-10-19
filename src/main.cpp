#include "backend/crux_adapter.h"
#include "backend/privilege_helper.h"
#include "ui/mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("octoCRUX"));
    app.setApplicationDisplayName(QStringLiteral("OctoCRUX — CRUX Linux Package Manager"));
    app.setOrganizationName(QStringLiteral("OctoCRUX"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    auto backend = std::make_shared<OctoCRUX::CruxAdapter>();
    const auto helper = OctoCRUX::PrivilegeHelper::probeAvailableHelper();
    backend->setPrivilegeHelperType(helper);

    OctoCRUX::MainWindow window(backend);
    window.show();
    window.checkPrivilegeHelper();

    return app.exec();
}
