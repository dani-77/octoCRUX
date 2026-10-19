#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <optional>
#include <cstdint>

namespace OctoCRUX {

/**
 * @brief Axis 1: Installation status derived from prt-get %i token.
 */
enum class InstallStatus {
    NotInstalled, // %i == "no"
    Installed,    // %i == "yes"
    Outdated      // %i == "diff"
};

/**
 * @brief Axis 2: Lock status derived from prt-get %l token or /var/lib/pkg/prt-get.locker.
 */
enum class LockStatus {
    Unlocked,     // %l == "no"
    Locked        // %l == "yes"
};

/**
 * @brief Privilege escalation helper classification.
 */
enum class PrivilegeHelperType {
    None,     // Direct unprivileged execution (or already root)
    QtSudo,   // qt-sudo -d command args... (GUI frontend)
    Sudo,     // sudo command args...
    Doas,     // doas command args...
    Su        // su -c 'command args...'
};

/**
 * @brief Categorized error kinds mirroring pkgmk(8) exit codes and CRUX domain errors.
 * Note: prt-get returns generic exit codes (1/255); exact classification under prt-get
 * is derived by inspecting /var/log/pkgbuild/<port>.log.
 */
enum class OperationErrorKind {
    None = 0,
    GeneralError = 1,     // pkgmk code 1: General error
    PkgfileError = 2,     // pkgmk code 2: The Pkgfile is invalid
    SourceDirError = 3,   // pkgmk code 3: Source/build dir missing or lacking r/w permissions
    DownloadError = 4,    // pkgmk code 4: Error downloading source files
    UnpackError = 5,      // pkgmk code 5: Error unpacking source files
    ChecksumError = 6,    // pkgmk code 6: md5sum mismatch
    FootprintError = 7,   // pkgmk code 7: Footprint mismatch
    CompilationError = 8, // pkgmk code 8: Error running build() function
    InstallError = 9,     // pkgmk code 9: Error installing package via pkgadd
    SignatureError = 10,  // signify digital signature or key verification failure
    DiskSpaceError = 11,  // ENOSPC on work directory mount
    ProtectedPackage = 12 // Core package removal blocked by protection policy
};

/**
 * @brief Canonical representation of a CRUX port across all three domain axes.
 */
struct PortInfo {
    QString name;
    QString version;
    QString release;

    // The three orthogonal axes:
    InstallStatus installStatus = InstallStatus::NotInstalled;
    LockStatus lockStatus = LockStatus::Unlocked;
    QString providedByAlias; // Populated if satisfied via /etc/prt-get.aliases

    QString collectionPath;  // %p: Collection directory (e.g. /usr/ports/core)
    QString collection;      // Deduced from collectionPath (e.g. "core", "opt", "d77crux")
    QString description;     // %d
    QStringList dependencies;// Direct dependencies parsed from %e (comma-separated)

    bool hasReadme = false;      // %R
    bool hasPreInstall = false;  // %E
    bool hasPostInstall = false; // %O

    // Convenience helpers
    QString fullPortDirectory() const {
        if (collectionPath.isEmpty() || name.isEmpty()) return QString();
        return collectionPath + QLatin1Char('/') + name;
    }

    bool isInstalled() const {
        return installStatus == InstallStatus::Installed || installStatus == InstallStatus::Outdated;
    }

    bool isOutdatedEligible() const {
        return installStatus == InstallStatus::Outdated && lockStatus == LockStatus::Unlocked;
    }

    bool isOutdatedLocked() const {
        return installStatus == InstallStatus::Outdated && lockStatus == LockStatus::Locked;
    }

    bool isLockedUninstalled() const {
        return installStatus == InstallStatus::NotInstalled && lockStatus == LockStatus::Locked;
    }
};

/**
 * @brief Package details from pkginfo inspection.
 */
struct InstalledPackageDetails {
    QString name;
    QString versionRelease;
    QStringList installedFiles;
};

/**
 * @brief Discrete action in a transaction step.
 */
enum class TransactionAction {
    Install,
    Upgrade,
    Reinstall,
    Remove,
    Lock,
    Unlock
};

/**
 * @brief Discrete step in an executable transaction plan.
 */
struct TransactionStep {
    TransactionAction action = TransactionAction::Install;
    QString portName;
    QString collection;
    QString currentVersion;
    QString targetVersion;
    bool hasPreInstall = false;
    bool hasPostInstall = false;
    bool isLocked = false;
    QStringList directDependents; // Affected installed packages for removals
};

/**
 * @brief Empirical filesystem capacity and mount metadata for build directory.
 */
struct FilesystemInfo {
    QString targetPath;
    QString mountPoint;
    QString fsType;
    quint64 availableBytes = 0;
    quint64 totalBytes = 0;
    bool isTmpfs = false;
};

/**
 * @brief Comprehensive transaction plan formulated by Operations layer before committing mutations.
 */
struct TransactionPlan {
    QList<TransactionStep> steps;

    // Requested inputs
    QStringList requestedTargets;

    // Partitioned dependency analysis
    QStringList toInstall;                 // Ports to be compiled and installed
    QStringList toUpdate;                  // Ports to be updated/rebuilt
    QStringList toRemove;                  // Ports to be removed
    QStringList satisfiedDependencies;     // Dependencies already installed and up to date
    QStringList lockedSkipped;             // Outdated or requested ports skipped because locked

    // Dependency tracking
    QStringList recursiveInstallOrder;     // Full quickdep topological DAG
    QStringList reverseDependentsAffected; // Direct installed dependents affected (backwards-compat)
    QStringList reverseDependentsDirect;   // Direct installed dependents
    QStringList reverseDependentsCascade;  // Complete recursive cascade of affected installed ports

    // Safety and diagnostics
    QStringList criticalWarnings;          // Non-blocking critical warnings
    QStringList protectedPackagesBlocked;  // Core packages requested for removal but blocked
    QStringList errors;                    // Blocking errors preventing plan execution (e.g. missing port, protected package)

    // Filesystem inspection
    FilesystemInfo workMountInfo;
    quint64 estimatedWorkSpaceAvailable = 0; // Bytes free on work mount
    bool useDiskWorkRootOverride = false;    // If user enabled PKGMK_WORK_ROOT override
    QString diskWorkRootPath;                // Path on disk with >= 50 GiB headroom

    // Convenience helpers
    bool isValid() const { return errors.isEmpty(); }
    bool hasCriticalWarnings() const { return !criticalWarnings.isEmpty(); }
    bool hasProtectedPackages() const { return !protectedPackagesBlocked.isEmpty(); }
    int stepCount() const { return steps.size(); }
};

} // namespace OctoCRUX
