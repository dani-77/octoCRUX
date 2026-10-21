#pragma once

#include "backend/crux_adapter.h"
#include "core/octocrux_settings.h"
#include "core/update_summary.h"
#include "unattended_sync.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

namespace OctoCRUX {

/**
 * @brief Tray-independent logic of octocrux-notifier.
 *
 * Reads only: the outdated count comes from the backend's `prt-get printf`
 * query, refreshed on a timer and whenever /var/lib/pkg changes (pkgadd's
 * database). The only privileged action is syncNow(), started by the user,
 * which runs the backend's `ports -u` through the configured privilege
 * helper and records the time of a successful sync in the shared settings.
 * It never installs, upgrades or removes anything.
 */
class NotifierCore : public QObject {
    Q_OBJECT
public:
    NotifierCore(std::shared_ptr<CruxAdapter> backend,
                 std::shared_ptr<OctoCruxSettings> settings,
                 QObject *parent = nullptr);

    UpdateSummary summary() const { return m_summary; }
    bool isSyncing() const { return m_syncing; }
    QDateTime lastSync() const;

    /** One-line state, e.g. "7 updates available (4 locked skipped)". */
    QString statusText() const;
    /** e.g. "Ports tree last synced 3 days ago" / "...never synced by OctoCRUX". */
    QString syncAgeText() const;

    /** Seconds between periodic refreshes (5 minutes, as in OctoXBPS). */
    static constexpr int kRefreshIntervalMs = 5 * 60 * 1000;
    /** Delay that coalesces bursts of /var/lib/pkg changes into one refresh. */
    static constexpr int kWatchDebounceMs = 2000;

    /** Clock used for sync ages and scheduling; replaceable in tests. */
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    /** "Is there a network?" for scheduled syncs (default: always). */
    void setNetworkCheck(std::function<bool()> check) { m_networkUp = std::move(check); }
    /** "Is a package tool running?" for scheduled syncs (default: scan /proc). */
    void setBusyCheck(std::function<bool()> check) { m_toolBusy = std::move(check); }

    /** The last scheduled sync could not run without a password. */
    bool automaticSyncNeedsAuthorization() const { return m_autoNeedsAuth; }

    /** True if prt-get, ports, pkgmk, pkgadd or pkgrm is running (from /proc). */
    static bool isPackageToolRunning();

public slots:
    /** Re-reads the catalog and updates the summary. Never elevates. */
    void refresh();
    /** Runs `ports -u` through the privilege helper. Returns false if it did not start. */
    bool syncNow();
    /**
     * Periodic tick: refresh, then, if the schedule says a sync is due, the
     * network is up and no package tool is running, start an unattended sync.
     * A failed attempt postpones the next one by one schedule period.
     */
    void tick();

signals:
    void stateChanged();
    /** The number of upgradable packages grew: the tray shows a notification. */
    void newUpdatesFound(int upgradable);
    void syncFinished(bool success, const QString &summary);
    /** A scheduled sync found no password-free way to run (emitted once per streak). */
    void automaticSyncNeedsAuthorizationChanged(bool needed);

private slots:
    void onOperationFinished(int exitCode, OperationErrorKind kind, const QString &summary);
    void onPkgDbChanged();
    void onUnattendedFinished(UnattendedSync::Result result, UnattendedSync::Method method, const QString &summary);

private:
    void watchPkgDb();

    std::shared_ptr<CruxAdapter> m_backend;
    std::shared_ptr<OctoCruxSettings> m_settings;
    std::function<QDateTime()> m_clock;
    UpdateSummary m_summary;
    bool m_hasSummary = false;
    bool m_syncing = false;
    bool m_autoNeedsAuth = false;
    QDateTime m_lastAttempt;            // last scheduled attempt (success or not), this session
    std::function<bool()> m_networkUp;
    std::function<bool()> m_toolBusy;
    UnattendedSync m_unattended;
    QTimer m_refreshTimer;
    QTimer m_debounce;
    QFileSystemWatcher m_watcher;
};

} // namespace OctoCRUX
