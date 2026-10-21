#pragma once

#include <QList>
#include <QString>

namespace OctoCRUX {

/**
 * @brief Finds pkgmk work directories left behind by a cancelled build.
 *
 * pkgmk's interrupted() trap removes $PKGMK_WORK_DIR when PKGMK_KEEP_WORK=no,
 * but a Cancel signals prt-get (the reader of pkgmk's output) and pkgmk
 * together: prt-get dies first, the trap's first echo then gets SIGPIPE, and
 * pkgmk dies before its rm -rf (reproduced unprivileged with pkgmk 5.40.12,
 * see OPEN_QUESTIONS 2.10.3). The directory stays, on tmpfs in RAM.
 *
 * Nothing here deletes: it finds candidates and validates paths, which
 * CruxAdapter::removeWorkDirectories() checks again before running rm as root.
 */
class WorkDirCleanup {
public:
    struct Leftover {
        QString path;
        quint64 bytes = 0;  ///< readable files only, so a lower bound
    };

    /**
     * Candidates under workRoot. Each must pass isRemovableWorkDir(). Call it
     * only when no package tool runs (isPackageToolRunning()): a build started
     * from a terminal has the same shape.
     */
    static QList<Leftover> findLeftovers(const QString &workRoot);

    /**
     * True only for a real directory (not a symlink) that is a direct child of
     * workRoot, has a port-like name, belongs to requiredOwnerUid() and holds
     * pkgmk's pkg/ and src/ subdirectories. workRoot must be absolute, exist,
     * and not be "/".
     */
    static bool isRemovableWorkDir(const QString &workRoot, const QString &path);

    /** A prt-get, ports, pkgmk, pkgadd or pkgrm process exists (/proc/<pid>/comm). */
    static bool isPackageToolRunning();

    /** Owner a leftover must have: root (0). Tests set their own uid. */
    static uint requiredOwnerUid();
    static void setRequiredOwnerUidForTesting(uint uid);
};

} // namespace OctoCRUX
