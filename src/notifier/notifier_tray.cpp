#include "notifier_tray.h"
#include "notifier_options_dialog.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QSystemTrayIcon>

namespace OctoCRUX {

NotifierTray::NotifierTray(NotifierCore *core, std::shared_ptr<OctoCruxSettings> settings,
                           PrivilegeHelperType helper, QObject *parent)
    : QObject(parent)
    , m_core(core)
    , m_settings(std::move(settings))
    , m_helper(helper)
{
    m_menu = new QMenu();
    m_actStatus = m_menu->addAction(QString());
    m_actStatus->setEnabled(false);
    m_actSyncAge = m_menu->addAction(QString());
    m_actSyncAge->setEnabled(false);
    m_menu->addSeparator();
    QAction *actOpen = m_menu->addAction(QStringLiteral("Open OctoCRUX"));
    connect(actOpen, &QAction::triggered, this, &NotifierTray::openOctoCrux);
    m_actSync = m_menu->addAction(QStringLiteral("Sync ports tree now (ports -u)"));
    m_actSync->setToolTip(QStringLiteral("Runs 'ports -u' through the privilege helper; it may ask for a password."));
    connect(m_actSync, &QAction::triggered, m_core, &NotifierCore::syncNow);
    QAction *actRefresh = m_menu->addAction(QStringLiteral("Check again"));
    connect(actRefresh, &QAction::triggered, m_core, &NotifierCore::refresh);
    QAction *actOptions = m_menu->addAction(QStringLiteral("Options..."));
    connect(actOptions, &QAction::triggered, this, &NotifierTray::openOptions);
    m_menu->addSeparator();
    QAction *actQuit = m_menu->addAction(QStringLiteral("Quit notifier"));
    connect(actQuit, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_tray = new QSystemTrayIcon(this);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            openOctoCrux();
        }
    });

    connect(m_core, &NotifierCore::stateChanged, this, &NotifierTray::updateFromCore);
    connect(m_core, &NotifierCore::newUpdatesFound, this, &NotifierTray::onNewUpdates);
    connect(m_core, &NotifierCore::syncFinished, this, &NotifierTray::onSyncFinished);
    connect(m_core, &NotifierCore::automaticSyncNeedsAuthorizationChanged,
            this, &NotifierTray::onAutomaticSyncNeedsAuthorization);
    updateFromCore();
}

NotifierTray::~NotifierTray() {
    delete m_menu;
}

void NotifierTray::show() {
    m_tray->show();
}

QIcon NotifierTray::makeIcon(int upgradable, bool busy) {
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor fill = busy ? QColor(0x75, 0x75, 0x75)
                          : upgradable > 0 ? QColor(0xe6, 0x51, 0x00)  // orange, as "Outdated" in the main window
                                           : QColor(0x2e, 0x7d, 0x32); // green, as "Installed"
        p.setBrush(fill);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(1, 1, size - 2, size - 2));
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(qMax(8, size * 6 / 10));
        p.setFont(f);
        const QString label = busy ? QStringLiteral("…")
                            : upgradable > 0 ? (upgradable > 99 ? QStringLiteral("99+") : QString::number(upgradable))
                                             : QStringLiteral("✓");
        p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, label);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

void NotifierTray::updateFromCore() {
    const QString status = m_core->statusText();
    const QString age = m_core->syncAgeText();
    m_actStatus->setText(status);
    m_actSyncAge->setText(age);
    m_actSync->setEnabled(!m_core->isSyncing());
    m_tray->setIcon(makeIcon(m_core->summary().upgradable(), m_core->isSyncing()));
    m_tray->setToolTip(QStringLiteral("OctoCRUX\n%1\n%2").arg(status, age));
}

void NotifierTray::onNewUpdates(int upgradable) {
    if (QSystemTrayIcon::supportsMessages()) {
        m_tray->showMessage(QStringLiteral("OctoCRUX"),
                            upgradable == 1 ? QStringLiteral("1 update available")
                                            : QStringLiteral("%1 updates available").arg(upgradable),
                            QSystemTrayIcon::Information, 10000);
    }
}

void NotifierTray::onSyncFinished(bool success, const QString &summary) {
    if (QSystemTrayIcon::supportsMessages()) {
        m_tray->showMessage(QStringLiteral("OctoCRUX"),
                            success ? QStringLiteral("Ports tree synchronized")
                                    : QStringLiteral("Ports tree sync failed: %1").arg(summary),
                            success ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning, 10000);
    }
}

void NotifierTray::openOptions() {
    NotifierOptionsDialog dialog(m_settings, m_helper);
    dialog.exec();
    updateFromCore();
}

void NotifierTray::onAutomaticSyncNeedsAuthorization(bool needed) {
    if (needed && QSystemTrayIcon::supportsMessages()) {
        m_tray->showMessage(QStringLiteral("OctoCRUX"),
            QStringLiteral("Automatic ports tree sync could not run without a password. "
                           "Use \"Sync ports tree now\", or allow unattended sync in Options."),
            QSystemTrayIcon::Warning, 15000);
    }
}

void NotifierTray::openOctoCrux() {
    // Prefer the octocrux next to this binary (build tree or same prefix), then PATH.
    QString program = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("octocrux"));
    if (!QFileInfo(program).isExecutable()) {
        program = QStandardPaths::findExecutable(QStringLiteral("octocrux"));
    }
    if (program.isEmpty() || !QProcess::startDetached(program, {})) {
        m_tray->showMessage(QStringLiteral("OctoCRUX"), QStringLiteral("Could not start octocrux"),
                            QSystemTrayIcon::Warning, 10000);
    }
}

} // namespace OctoCRUX
