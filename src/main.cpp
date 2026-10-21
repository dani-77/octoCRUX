#include "backend/crux_adapter.h"
#include "ui/mainwindow.h"
#include "core/octocrux_settings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("octoCRUX"));
    app.setApplicationDisplayName(QStringLiteral("OctoCRUX — CRUX Linux Package Manager"));
    app.setOrganizationName(QStringLiteral("OctoCRUX"));
    app.setApplicationVersion(QStringLiteral(OCTOCRUX_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OctoCRUX, a Qt package manager for CRUX"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption upgradeAll(QStringLiteral("upgrade-all"),
        QStringLiteral("Open the Upgrade All preview at start (nothing runs until Apply)."));
    parser.addOption(upgradeAll);
    parser.process(app);

    auto backend = std::make_shared<OctoCRUX::CruxAdapter>();

    OctoCRUX::MainWindow window(backend);
    window.setSettings(std::make_shared<OctoCRUX::OctoCruxSettings>());
    // The helper chosen in Settings, or automatic detection (qt-sudo, sudo, doas, su).
    window.applyPrivilegePreference();
    window.show();
    window.checkPrivilegeHelper();
    if (parser.isSet(upgradeAll)) {
        QTimer::singleShot(0, &window, &OctoCRUX::MainWindow::requestUpgradeAll);
    }

    return app.exec();
}
