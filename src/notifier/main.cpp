#include "backend/crux_adapter.h"
#include "backend/privilege_helper.h"
#include "core/octocrux_settings.h"
#include "notifier_core.h"
#include "notifier_tray.h"

#include <QApplication>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>

using namespace OctoCRUX;

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("octocrux-notifier"));
    app.setOrganizationName(QStringLiteral("OctoCRUX"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setQuitOnLastWindowClosed(false);

    auto backend = std::make_shared<CruxAdapter>();
    backend->setPrivilegeHelperType(PrivilegeHelper::probeAvailableHelper());
    auto settings = std::make_shared<OctoCruxSettings>();

    NotifierCore core(backend, settings);
    NotifierTray tray(&core);

    // The tray may appear after the notifier starts (e.g. from autostart): wait
    // for it for up to a minute before giving up, instead of failing at once.
    int attempts = 0;
    QTimer waitForTray;
    waitForTray.setInterval(2000);
    QObject::connect(&waitForTray, &QTimer::timeout, [&]() {
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            waitForTray.stop();
            tray.show();
            core.refresh();
        } else if (++attempts >= 30) {
            QTextStream(stderr) << "octocrux-notifier: no system tray available, exiting\n";
            app.exit(1);
        }
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray.show();
        QTimer::singleShot(1000, &core, &NotifierCore::refresh); // first check soon after start
    } else {
        waitForTray.start();
    }

    return app.exec();
}
