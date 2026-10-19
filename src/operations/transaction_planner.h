#pragma once

#include "octocrux/types.h"
#include "octocrux/icruxbackend.h"

#include <memory>
#include <QSet>
#include <QStringList>

namespace OctoCRUX {

/**
 * @brief Operations layer class that computes transaction plans for installs, upgrades,
 * removals, and lock toggles, while enforcing CRUX invariants and SCOFS safety guards.
 */
class TransactionPlanner {
public:
    explicit TransactionPlanner(std::shared_ptr<ICruxBackend> backend);
    ~TransactionPlanner() = default;

    /**
     * @brief Computes a full installation transaction for the requested ports.
     * Resolves topological dependency order via quickdep, checks installed status,
     * partitions into uninstalled (toInstall), outdated (toUpdate), and satisfied,
     * guards against locked uninstalled ports, and inspects filesystem capacity.
     */
    TransactionPlan planInstall(const QStringList &portNames);

    /**
     * @brief Computes an upgrade transaction for outdated ports.
     * If portNames is empty, queries all outdated ports.
     * Identifies locked packages (e.g. llvm, clang) and flags them as
     * lockedSkipped rather than blindly queueing an update.
     */
    TransactionPlan planUpgrade(const QStringList &portNames = {});

    /**
     * @brief Computes a removal transaction for the specified ports.
     * Calculates direct dependents and full recursive cascade of affected installed packages.
     * Alerts with critical warnings if essential system packages are targeted.
     */
    TransactionPlan planRemove(const QStringList &portNames);

    /**
     * @brief Creates a plan to lock or unlock a port.
     */
    TransactionPlan planLockToggle(const QString &portName, bool lock);

    /**
     * @brief Discovers the empirical work directory from PKGMK_WORK_DIR in pkgmk.conf,
     * falling back to /var/tmp then /tmp.
     */
    static QString discoverWorkDirectory(const QString &rootPrefix = QString());

    /**
     * @brief Inspects empirical filesystem capacity and mount metadata for a path.
     */
    static FilesystemInfo inspectMount(const QString &targetPath = QStringLiteral("/tmp"));

private:
    void populateMountInfo(TransactionPlan &plan, const QString &targetPath = QString());
    QStringList calculateRecursiveDependentsCascade(const QStringList &directDependents);

    std::shared_ptr<ICruxBackend> m_backend;
};

} // namespace OctoCRUX
