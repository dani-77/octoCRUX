#include "transaction_planner.h"

#include <sys/statvfs.h>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QQueue>

namespace OctoCRUX {

TransactionPlanner::TransactionPlanner(std::shared_ptr<ICruxBackend> backend)
    : m_backend(std::move(backend))
{
}

FilesystemInfo TransactionPlanner::inspectMount(const QString &targetPath) {
    FilesystemInfo info;
    info.targetPath = targetPath;

    struct statvfs stat;
    if (statvfs(targetPath.toLocal8Bit().constData(), &stat) == 0) {
        info.availableBytes = static_cast<quint64>(stat.f_bavail) * static_cast<quint64>(stat.f_frsize);
        info.totalBytes = static_cast<quint64>(stat.f_blocks) * static_cast<quint64>(stat.f_frsize);
    }

    // Parse /proc/self/mountinfo to detect mountPoint and fsType.
    // Note: Pseudo-files in /proc report st_size == 0, causing QTextStream/QFile::atEnd()
    // to return true before reading. We must use file.readAll() directly.
    QFile file(QStringLiteral("/proc/self/mountinfo"));
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray content = file.readAll();
        const QString fullText = QString::fromUtf8(content);
        const QStringList lines = fullText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

        QString bestMountPoint;
        QString bestFsType;

        const QString canonicalTarget = QFileInfo(targetPath).canonicalFilePath();
        const QString targetToMatch = canonicalTarget.isEmpty() ? targetPath : canonicalTarget;

        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;

            const QStringList parts = trimmed.split(QStringLiteral(" - "));
            if (parts.size() < 2) continue;

            const QStringList leftTokens = parts.at(0).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            const QStringList rightTokens = parts.at(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);

            if (leftTokens.size() >= 5 && !rightTokens.isEmpty()) {
                const QString mountPoint = leftTokens.at(4);
                const QString fsType = rightTokens.at(0);

                if (targetToMatch == mountPoint || 
                    targetToMatch.startsWith(mountPoint == QLatin1String("/") ? mountPoint : mountPoint + QLatin1Char('/'))) {
                    if (mountPoint.length() > bestMountPoint.length()) {
                        bestMountPoint = mountPoint;
                        bestFsType = fsType;
                    }
                }
            }
        }

        info.mountPoint = bestMountPoint;
        info.fsType = bestFsType;
        info.isTmpfs = (bestFsType == QLatin1String("tmpfs"));
    }

    return info;
}

void TransactionPlanner::populateMountInfo(TransactionPlan &plan, const QString &targetPath) {
    plan.workMountInfo = inspectMount(targetPath);
    plan.estimatedWorkSpaceAvailable = plan.workMountInfo.availableBytes;
}

TransactionPlan TransactionPlanner::planInstall(const QStringList &portNames) {
    TransactionPlan plan;
    populateMountInfo(plan);

    if (!m_backend) {
        plan.errors.append(QStringLiteral("Backend adapter is not available"));
        return plan;
    }

    QSet<QString> processedPorts;

    for (const QString &target : portNames) {
        if (target.trimmed().isEmpty()) continue;

        auto targetOpt = m_backend->queryPort(target);
        if (!targetOpt.has_value()) {
            plan.errors.append(QStringLiteral("Port '%1' does not exist in the ports tree").arg(target));
            continue;
        }

        plan.requestedTargets.append(target);

        // Retrieve recursive install order from backend (quickdep with alias resolution)
        const QStringList order = m_backend->queryRecursiveInstallOrder(target);
        if (order.isEmpty()) {
            // Target port with no dependencies or single port
            if (!processedPorts.contains(target)) {
                processedPorts.insert(target);
                const PortInfo &p = targetOpt.value();
                if (p.lockStatus == LockStatus::Locked && !p.isInstalled()) {
                    plan.errors.append(QStringLiteral("Port '%1' is locked in uninstalled state and cannot be installed").arg(p.name));
                } else if (!p.isInstalled()) {
                    plan.toInstall.append(p.name);
                    TransactionStep step;
                    step.action = TransactionAction::Install;
                    step.portName = p.name;
                    step.collection = p.collection;
                    step.targetVersion = p.version + QLatin1Char('-') + p.release;
                    step.hasPreInstall = p.hasPreInstall;
                    step.hasPostInstall = p.hasPostInstall;
                    plan.steps.append(step);
                } else if (p.installStatus == InstallStatus::Outdated) {
                    if (p.lockStatus == LockStatus::Locked) {
                        plan.lockedSkipped.append(p.name);
                    } else {
                        plan.toUpdate.append(p.name);
                        TransactionStep step;
                        step.action = TransactionAction::Upgrade;
                        step.portName = p.name;
                        step.collection = p.collection;
                        step.targetVersion = p.version + QLatin1Char('-') + p.release;
                        step.hasPreInstall = p.hasPreInstall;
                        step.hasPostInstall = p.hasPostInstall;
                        plan.steps.append(step);
                    }
                } else {
                    plan.satisfiedDependencies.append(p.name);
                }
            }
            continue;
        }

        // Process topological order from quickdep
        for (const QString &depName : order) {
            if (processedPorts.contains(depName)) continue;
            processedPorts.insert(depName);
            plan.recursiveInstallOrder.append(depName);

            auto depOpt = m_backend->queryPort(depName);
            if (!depOpt.has_value()) {
                plan.errors.append(QStringLiteral("Dependency '%1' required by '%2' was not found in ports tree").arg(depName, target));
                continue;
            }

            const PortInfo &p = depOpt.value();

            if (p.lockStatus == LockStatus::Locked) {
                if (!p.isInstalled()) {
                    // SCOFS Guard: Locked uninstalled port cannot be built
                    plan.errors.append(QStringLiteral("Dependency '%1' is explicitly locked in uninstalled state and cannot be compiled").arg(p.name));
                    continue;
                }
                if (p.installStatus == InstallStatus::Outdated) {
                    // SCOFS Guard: Outdated locked package is skipped
                    plan.lockedSkipped.append(p.name);
                    continue;
                }
                // Installed and locked up to date
                plan.satisfiedDependencies.append(p.name);
                continue;
            }

            if (!p.isInstalled()) {
                plan.toInstall.append(p.name);
                TransactionStep step;
                step.action = TransactionAction::Install;
                step.portName = p.name;
                step.collection = p.collection;
                step.targetVersion = p.version + QLatin1Char('-') + p.release;
                step.hasPreInstall = p.hasPreInstall;
                step.hasPostInstall = p.hasPostInstall;
                plan.steps.append(step);
            } else if (p.installStatus == InstallStatus::Outdated) {
                plan.toUpdate.append(p.name);
                TransactionStep step;
                step.action = TransactionAction::Upgrade;
                step.portName = p.name;
                step.collection = p.collection;
                step.targetVersion = p.version + QLatin1Char('-') + p.release;
                step.hasPreInstall = p.hasPreInstall;
                step.hasPostInstall = p.hasPostInstall;
                plan.steps.append(step);
            } else {
                plan.satisfiedDependencies.append(p.name);
            }
        }
    }

    return plan;
}

TransactionPlan TransactionPlanner::planUpgrade(const QStringList &portNames) {
    TransactionPlan plan;
    populateMountInfo(plan);

    if (!m_backend) {
        plan.errors.append(QStringLiteral("Backend adapter is not available"));
        return plan;
    }

    const QStringList targets = portNames.isEmpty() ? m_backend->queryOutdatedPorts() : portNames;
    plan.requestedTargets = targets;

    for (const QString &target : targets) {
        auto portOpt = m_backend->queryPort(target);
        if (!portOpt.has_value()) {
            plan.errors.append(QStringLiteral("Port '%1' does not exist in the ports tree").arg(target));
            continue;
        }

        const PortInfo &p = portOpt.value();

        // SCOFS Guard: Check lock status. prt-get diff suppresses locked packages, but OctoCRUX detects them!
        if (p.lockStatus == LockStatus::Locked) {
            plan.lockedSkipped.append(p.name);
            continue;
        }

        if (p.installStatus != InstallStatus::Outdated) {
            // Port is not outdated; record as satisfied
            plan.satisfiedDependencies.append(p.name);
            continue;
        }

        plan.toUpdate.append(p.name);
        TransactionStep step;
        step.action = TransactionAction::Upgrade;
        step.portName = p.name;
        step.collection = p.collection;
        step.targetVersion = p.version + QLatin1Char('-') + p.release;
        step.hasPreInstall = p.hasPreInstall;
        step.hasPostInstall = p.hasPostInstall;
        plan.steps.append(step);
    }

    return plan;
}

QStringList TransactionPlanner::calculateRecursiveDependentsCascade(const QStringList &directDependents) {
    QStringList cascade;
    QSet<QString> visited;
    QQueue<QString> queue;

    for (const QString &d : directDependents) {
        queue.enqueue(d);
    }

    while (!queue.isEmpty()) {
        const QString curr = queue.dequeue();
        if (visited.contains(curr)) continue;
        visited.insert(curr);
        cascade.append(curr);

        const QStringList subDeps = m_backend->queryDirectInstalledDependents(curr);
        for (const QString &s : subDeps) {
            if (!visited.contains(s)) {
                queue.enqueue(s);
            }
        }
    }

    return cascade;
}

TransactionPlan TransactionPlanner::planRemove(const QStringList &portNames) {
    TransactionPlan plan;
    populateMountInfo(plan);

    if (!m_backend) {
        plan.errors.append(QStringLiteral("Backend adapter is not available"));
        return plan;
    }

    // SCOFS Policy: Core collection discovery & fail-closed protection
    const auto coreDirOpt = m_backend->coreCollectionPath();
    if (!coreDirOpt.has_value()) {
        plan.errors.append(QStringLiteral("No 'core' collection is configured in /etc/prt-get.conf. Removals are blocked to prevent accidental removal of core system packages."));
        return plan;
    }

    plan.requestedTargets = portNames;

    for (const QString &target : portNames) {
        if (target.trimmed().isEmpty()) continue;

        auto portOpt = m_backend->queryPort(target);
        if (!portOpt.has_value()) {
            plan.errors.append(QStringLiteral("Port '%1' does not exist in the ports tree").arg(target));
            continue;
        }

        const PortInfo &p = portOpt.value();
        if (!p.isInstalled()) {
            plan.errors.append(QStringLiteral("Port '%1' is not currently installed").arg(target));
            continue;
        }

        // SCOFS Policy: Core package protection rule
        // Check membership in the core collection directly on disk, not winning %p.
        // Removals of core packages are strictly blocked in OctoCRUX.
        if (m_backend->isCorePackage(target)) {
            plan.protectedPackagesBlocked.append(target);
            plan.errors.append(
                QStringLiteral("%1 belongs to the CRUX core collection. OctoCRUX does not remove core packages, because doing so can leave the system unable to run programs or boot. If you really intend to remove it, use 'prt-get remove' or 'pkgrm' from a terminal.")
                .arg(target)
            );
            continue; // Never produce a remove step/argv for a protected package!
        }

        plan.toRemove.append(target);

        // Direct installed dependents detection
        const QStringList direct = m_backend->queryDirectInstalledDependents(target);
        for (const QString &d : direct) {
            if (!plan.reverseDependentsDirect.contains(d)) {
                plan.reverseDependentsDirect.append(d);
            }
        }

        TransactionStep step;
        step.action = TransactionAction::Remove;
        step.portName = target;
        step.collection = p.collection;
        step.directDependents = direct;
        plan.steps.append(step);
    }

    plan.reverseDependentsAffected = plan.reverseDependentsDirect;

    // SCOFS Guard 3: Calculate full multi-tier cascade of reverse dependents
    plan.reverseDependentsCascade = calculateRecursiveDependentsCascade(plan.reverseDependentsDirect);

    return plan;
}

TransactionPlan TransactionPlanner::planLockToggle(const QString &portName, bool lock) {
    TransactionPlan plan;
    populateMountInfo(plan);

    if (!m_backend) {
        plan.errors.append(QStringLiteral("Backend adapter is not available"));
        return plan;
    }

    auto portOpt = m_backend->queryPort(portName);
    if (!portOpt.has_value()) {
        plan.errors.append(QStringLiteral("Port '%1' does not exist in the ports tree").arg(portName));
        return plan;
    }

    const PortInfo &p = portOpt.value();
    plan.requestedTargets.append(portName);

    TransactionStep step;
    step.action = lock ? TransactionAction::Lock : TransactionAction::Unlock;
    step.portName = p.name;
    step.collection = p.collection;
    step.isLocked = lock;
    plan.steps.append(step);

    return plan;
}

} // namespace OctoCRUX
