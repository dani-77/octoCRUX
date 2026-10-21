#pragma once

#include "notifier_core.h"

#include <QIcon>
#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace OctoCRUX {

/**
 * @brief System tray presentation of NotifierCore: icon, tooltip, menu and
 * notifications. All decisions live in NotifierCore.
 */
class NotifierTray : public QObject {
    Q_OBJECT
public:
    NotifierTray(NotifierCore *core, std::shared_ptr<OctoCruxSettings> settings,
                 PrivilegeHelperType helper, QObject *parent = nullptr);
    ~NotifierTray() override;

    void show();

    /** Icon drawn in code (no icon theme required): a coloured disc with the count. */
    static QIcon makeIcon(int upgradable, bool busy);

private slots:
    void updateFromCore();
    void onNewUpdates(int upgradable);
    void onSyncFinished(bool success, const QString &summary);
    void openOctoCrux();
    void openUpgradeAll();
    void openOptions();
    void onAutomaticSyncNeedsAuthorization(bool needed);

private:
    NotifierCore *m_core;
    std::shared_ptr<OctoCruxSettings> m_settings;
    PrivilegeHelperType m_helper;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_actStatus = nullptr;
    QAction *m_actSyncAge = nullptr;
    QAction *m_actSync = nullptr;
    QAction *m_actUpgradeAll = nullptr;
    void launchOctoCrux(const QStringList &arguments);
};

} // namespace OctoCRUX
