#include "backend/crux_adapter.h"
#include "backend/privilege_helper.h"
#include "core/octocrux_settings.h"
#include "notifier_core.h"
#include "notifier_tray.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QNetworkInterface>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>

using namespace OctoCRUX;

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("octocrux-notifier"));
    app.setOrganizationName(QStringLiteral("OctoCRUX"));
    app.setApplicationVersion(QStringLiteral(OCTOCRUX_VERSION));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "OctoCRUX tray notifier: shows available updates (read-only); syncs the ports tree "
        "only when asked or on an opt-in schedule."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    auto backend = std::make_shared<CruxAdapter>();
    auto settings = std::make_shared<OctoCruxSettings>();
    // The helper chosen in OctoCRUX Settings (another process may have changed
    // it, hence the re-read), or automatic detection. Scheduled syncs do not use
    // it: they go through pkexec or sudo -n only (UnattendedSync).
    auto currentHelper = [backend, settings]() {
        settings->sync();
        const PrivilegeHelperType type = PrivilegeHelper::resolve(settings->preferredHelper()).type;
        backend->setPrivilegeHelperType(type);
        return type;
    };
    currentHelper();

    NotifierCore core(backend, settings);
    // Same test as the OctoXBPS notifier: an interface that is up, not loopback,
    // and has an address. It does not prove the mirrors are reachable.
    core.setNetworkCheck([]() {
        for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
            const auto flags = iface.flags();
            if (flags.testFlag(QNetworkInterface::IsUp) && !flags.testFlag(QNetworkInterface::IsLoopBack)
                && !iface.addressEntries().isEmpty()) {
                return true;
            }
        }
        return false;
    });
    NotifierTray tray(&core, settings, currentHelper);

    // The tray may appear after the notifier starts (e.g. from autostart): wait
    // for it for up to a minute before giving up, instead of failing at once.
    int attempts = 0;
    QTimer waitForTray;
    waitForTray.setInterval(2000);
    QObject::connect(&waitForTray, &QTimer::timeout, [&]() {
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            waitForTray.stop();
            tray.show();
            core.tick();
        } else if (++attempts >= 30) {
            QTextStream(stderr) << "octocrux-notifier: no system tray available, exiting\n";
            app.exit(1);
        }
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray.show();
        QTimer::singleShot(1000, &core, &NotifierCore::tick); // first check soon after start
    } else {
        waitForTray.start();
    }

    return app.exec();
}
