#include "work_dir_cleanup.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace OctoCRUX {

namespace {
uint g_requiredOwnerUid = 0;

QString canonicalRoot(const QString &workRoot) {
    if (workRoot.isEmpty() || !QDir::isAbsolutePath(workRoot)) {
        return QString();
    }
    const QFileInfo root(workRoot);
    const QString canonical = root.canonicalFilePath();
    if (!root.isDir() || canonical.isEmpty() || canonical == QLatin1String("/")) {
        return QString();
    }
    return canonical;
}

bool isRealDir(const QString &path) {
    const QFileInfo fi(path);
    return fi.isDir() && !fi.isSymLink();
}

quint64 directorySize(const QString &path) {
    quint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (!fi.isSymLink()) {
            total += static_cast<quint64>(fi.size());
        }
    }
    return total;
}
} // namespace

uint WorkDirCleanup::requiredOwnerUid() {
    return g_requiredOwnerUid;
}

void WorkDirCleanup::setRequiredOwnerUidForTesting(uint uid) {
    g_requiredOwnerUid = uid;
}

bool WorkDirCleanup::isRemovableWorkDir(const QString &workRoot, const QString &path) {
    const QString root = canonicalRoot(workRoot);
    if (root.isEmpty() || path.isEmpty()) {
        return false;
    }
    // A port name as pkgmk uses it for $name; no "/" and no leading dot.
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]*$"));
    const QFileInfo fi(QDir::cleanPath(path));
    if (!nameRe.match(fi.fileName()).hasMatch() || !isRealDir(fi.filePath())) {
        return false;
    }
    // A direct child of the work root, also after resolving any symlinked ancestor.
    if (QFileInfo(fi.canonicalFilePath()).path() != root) {
        return false;
    }
    if (fi.ownerId() != g_requiredOwnerUid) {
        return false;
    }
    const QDir dir(fi.filePath());
    return isRealDir(dir.filePath(QStringLiteral("pkg"))) && isRealDir(dir.filePath(QStringLiteral("src")));
}

QList<WorkDirCleanup::Leftover> WorkDirCleanup::findLeftovers(const QString &workRoot) {
    QList<Leftover> found;
    const QString root = canonicalRoot(workRoot);
    if (root.isEmpty()) {
        return found;
    }
    const QStringList names = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QString &name : names) {
        const QString path = QDir(root).filePath(name);
        if (isRemovableWorkDir(root, path)) {
            found.append({path, directorySize(path)});
        }
    }
    return found;
}

bool WorkDirCleanup::isPackageToolRunning() {
    static const QSet<QString> tools = {QStringLiteral("prt-get"), QStringLiteral("ports"),
                                        QStringLiteral("pkgmk"), QStringLiteral("pkgadd"),
                                        QStringLiteral("pkgrm")};
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        if (pid.isEmpty() || !pid.at(0).isDigit()) {
            continue;
        }
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        if (comm.open(QIODevice::ReadOnly)
            && tools.contains(QString::fromUtf8(comm.readAll()).trimmed())) {
            return true;
        }
    }
    return false;
}

} // namespace OctoCRUX
