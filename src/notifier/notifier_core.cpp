#include "notifier_core.h"
#include "core/work_dir_cleanup.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

namespace OctoCRUX {

NotifierCore::NotifierCore(std::shared_ptr<CruxAdapter> backend,
                           std::shared_ptr<OctoCruxSettings> settings,
                           QObject *parent)
    : QObject(parent)
    , m_backend(std::move(backend))
    , m_settings(std::move(settings))
    , m_clock([]() { return QDateTime::currentDateTime(); })
    , m_networkUp([]() { return true; })
    , m_toolBusy([]() { return NotifierCore::isPackageToolRunning(); })
{
    connect(&m_unattended, &UnattendedSync::finished, this, &NotifierCore::onUnattendedFinished);
    connect(m_backend.get(), &ICruxBackend::operationFinished,
            this, &NotifierCore::onOperationFinished);

    m_refreshTimer.setInterval(kRefreshIntervalMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, &NotifierCore::tick);
    m_refreshTimer.start();

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kWatchDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &NotifierCore::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &NotifierCore::onPkgDbChanged);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &NotifierCore::onPkgDbChanged);
    watchPkgDb();
}

void NotifierCore::watchPkgDb() {
    // pkgadd/pkgrm rewrite /var/lib/pkg/db (replacing the file), and prt-get
    // rewrites prt-get.locker there: watch the directory and the db file.
    const QString prefix = m_backend->rootPrefix();
    const QString dir = QDir::cleanPath((prefix.isEmpty() ? QStringLiteral("/") : prefix)
                                        + QStringLiteral("/var/lib/pkg"));
    if (QFileInfo(dir).isDir() && !m_watcher.directories().contains(dir)) {
        m_watcher.addPath(dir);
    }
    const QString db = QDir(dir).filePath(QStringLiteral("db"));
    if (QFileInfo::exists(db) && !m_watcher.files().contains(db)) {
        m_watcher.addPath(db); // re-added after each change: a replaced file drops out
    }
}

void NotifierCore::onPkgDbChanged() {
    watchPkgDb();
    m_debounce.start();
}

QDateTime NotifierCore::lastSync() const {
    return m_settings->lastSync();
}

void NotifierCore::refresh() {
    m_backend->invalidateCache();
    const UpdateSummary previous = m_summary;
    const bool hadSummary = m_hasSummary;
    m_summary = UpdateSummary::fromPorts(m_backend->queryAllPorts());
    m_hasSummary = true;
    emit stateChanged();
    if (m_summary.upgradable() > 0 && (!hadSummary || m_summary.upgradable() > previous.upgradable())) {
        emit newUpdatesFound(m_summary.upgradable());
    }
}

bool NotifierCore::isPackageToolRunning() {
    return WorkDirCleanup::isPackageToolRunning();
}

void NotifierCore::tick() {
    refresh();
    if (m_syncing) {
        return;
    }
    const SyncSchedule schedule = m_settings->syncSchedule();
    const QDateTime now = m_clock();
    QDateTime base = lastSync();
    if (m_lastAttempt.isValid() && (!base.isValid() || m_lastAttempt > base)) {
        base = m_lastAttempt;
    }
    if (!isSyncDue(schedule, base, now) || !m_networkUp() || m_toolBusy()) {
        return;
    }
    const QString ports = QStandardPaths::findExecutable(m_backend->portsBinary());
    if (ports.isEmpty()) {
        return;
    }
    m_lastAttempt = now;
    m_syncing = true;
    emit stateChanged();
    m_unattended.start(ports);
}

void NotifierCore::onUnattendedFinished(UnattendedSync::Result result, UnattendedSync::Method method,
                                        const QString &summary) {
    Q_UNUSED(method);
    m_syncing = false;
    const bool ok = result == UnattendedSync::Result::Synced;
    if (ok) {
        m_settings->setLastSync(m_clock());
        m_settings->sync();
    }
    const bool needsAuth = result == UnattendedSync::Result::NeedsAuthentication;
    if (needsAuth != m_autoNeedsAuth) {
        m_autoNeedsAuth = needsAuth;
        emit automaticSyncNeedsAuthorizationChanged(needsAuth);
    }
    emit syncFinished(ok, summary);
    refresh();
}

QString NotifierCore::statusText() const {
    if (m_syncing) {
        return QStringLiteral("Synchronizing ports tree...");
    }
    if (!m_hasSummary) {
        return QStringLiteral("Checking for updates...");
    }
    const int n = m_summary.upgradable();
    QString text = n == 0 ? QStringLiteral("System is up to date")
                 : n == 1 ? QStringLiteral("1 update available")
                          : QStringLiteral("%1 updates available").arg(n);
    if (!m_summary.outdatedLocked.isEmpty()) {
        text += QStringLiteral(" (%1 locked skipped)").arg(m_summary.outdatedLocked.size());
    }
    return text;
}

QString NotifierCore::syncAgeText() const {
    if (m_autoNeedsAuth && m_settings->syncSchedule().mode != SyncMode::Never) {
        return QStringLiteral("Automatic sync needs authorization (see Options)");
    }
    const int days = daysSinceSync(lastSync(), m_clock());
    if (days < 0) {
        return QStringLiteral("Ports tree never synced by OctoCRUX");
    }
    if (days == 0) {
        return QStringLiteral("Ports tree synced today");
    }
    return days == 1 ? QStringLiteral("Ports tree last synced 1 day ago")
                     : QStringLiteral("Ports tree last synced %1 days ago").arg(days);
}

bool NotifierCore::syncNow() {
    if (m_syncing) {
        return false;
    }
    m_syncing = true;
    emit stateChanged();
    if (!m_backend->syncPortsTree()) {
        // syncPortsTree() reported through operationFinished, which reset m_syncing.
        return false;
    }
    return true;
}

void NotifierCore::onOperationFinished(int exitCode, OperationErrorKind kind, const QString &summary) {
    if (!m_syncing) {
        return;
    }
    m_syncing = false;
    const bool ok = exitCode == 0 && kind == OperationErrorKind::None;
    if (ok) {
        m_settings->setLastSync(m_clock());
        m_settings->sync();
    }
    emit syncFinished(ok, summary);
    refresh();
}

} // namespace OctoCRUX
