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
 * @brief Categorized error kinds for CRUX build/install failures and domain errors.
 * The numeric values follow the E_* names in pkgmk(8), but they are NOT exit codes
 * OctoCRUX can rely on: OctoCRUX runs prt-get (through the privilege helper), whose
 * exit code mapping is undocumented ("to be determined"), and pkgutils 5.40.12 pkgmk exits 1
 * for footprint, build() and pkgadd failures (E_FOOTPRINT/E_BUILD/E_INSTALL are
 * defined but unused). Classification is derived from the pkgmk log text instead.
 */
enum class OperationErrorKind {
    None = 0,
    GeneralError = 1,     // Unclassified failure
    PkgfileError = 2,     // Pkgfile invalid (E_PKGFILE)
    SourceDirError = 3,   // Source/package dir missing or lacking r/w permissions (E_DIR_PERM)
    DownloadError = 4,    // Error downloading source files (E_DOWNLOAD)
    UnpackError = 5,      // Error unpacking source files (E_UNPACK)
    ChecksumError = 6,    // md5sum mismatch (E_MD5)
    FootprintError = 7,   // Footprint mismatch (E_FOOTPRINT, unused by pkgmk: exits 1)
    CompilationError = 8, // build() function failed (E_BUILD, unused by pkgmk: exits 1)
    InstallError = 9,     // pkgadd failed (E_INSTALL, unused by pkgmk: exits 1)
    SignatureError = 10,  // signify signature mismatch or verification failure (E_SIGNATURE)
    DiskSpaceError = 11,  // ENOSPC on work directory mount
    ProtectedPackage = 12, // Core package removal blocked by protection policy
    Cancelled = 13         // Stopped at the user's request (not a failure of the operation itself)
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
 * @brief How pkgmk checks the footprint of a built package.
 */
enum class FootprintCheck {
    Full,       // default: any NEW or MISSING entry fails the build
    IgnoreNew,  // pkgmk -in: tolerate NEW entries only (prt-get --margs=-in)
    Ignore      // pkgmk -if: skip the footprint check (prt-get -if)
};

/**
 * @brief Per-package, per-run exceptions to pkgmk's build checks.
 * Chosen by the user for the next run that builds the package; default = all checks on.
 */
struct BuildCheckOverrides {
    FootprintCheck footprint = FootprintCheck::Full;
    bool ignoreMd5 = false;        // pkgmk -im (prt-get -im)
    bool ignoreSignature = false;  // pkgmk -is (prt-get -is)

    bool isDefault() const {
        return footprint == FootprintCheck::Full && !ignoreMd5 && !ignoreSignature;
    }
    bool operator==(const BuildCheckOverrides &o) const {
        return footprint == o.footprint && ignoreMd5 == o.ignoreMd5 && ignoreSignature == o.ignoreSignature;
    }
    bool operator!=(const BuildCheckOverrides &o) const { return !(*this == o); }

    /** prt-get options for these exceptions, placed after the command (prt-get(8)). */
    QStringList prtGetArgs() const {
        QStringList args;
        if (footprint == FootprintCheck::Ignore) args << QStringLiteral("-if");
        if (ignoreMd5) args << QStringLiteral("-im");
        if (ignoreSignature) args << QStringLiteral("-is");
        // -in has no prt-get shortcut; --margs passes it to pkgmk.
        if (footprint == FootprintCheck::IgnoreNew) args << QStringLiteral("--margs=-in");
        return args;
    }

    /** Human-readable list, e.g. "ignore new footprint entries (-in), skip md5sum (-im)". */
    QString describe() const {
        QStringList parts;
        if (footprint == FootprintCheck::IgnoreNew) parts << QStringLiteral("ignore new footprint entries (-in)");
        if (footprint == FootprintCheck::Ignore) parts << QStringLiteral("skip footprint check (-if)");
        if (ignoreMd5) parts << QStringLiteral("skip md5sum check (-im)");
        if (ignoreSignature) parts << QStringLiteral("skip signature check (-is)");
        return parts.join(QStringLiteral(", "));
    }
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
    BuildCheckOverrides overrides; // Build check exceptions for this run (Install/Upgrade/Reinstall)
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
