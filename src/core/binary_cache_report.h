#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>

namespace OctoCRUX {

/**
 * @brief What d77crux-cache (the d77crux binary cache for core/opt/xorg) says
 * about one outdated port.
 *
 * d77crux-cache places verified CRUX binaries where pkgmk finds them, so
 * prt-get installs instead of compiling; prt-get stays the only authority.
 * OctoCRUX only reads its report ("d77crux-cache -n", unprivileged) to tell
 * the user which updates will compile.
 */
enum class BinaryStatus {
    Unknown,         ///< not in the report (e.g. not installed, or no report)
    Ready,           ///< present/cached: the archive is in place, prt-get installs it
    NotPlaced,       ///< would-cache: a binary exists, but d77crux-cache must run as root first
    Pending,         ///< skip-version: the port is newer than the snapshot; a binary should follow
    LockedAvailable, ///< locked: a binary exists, but the port is locked
    Builds,          ///< skip-absent/collection/modified/foreign, failed: compiles from source
};

class BinaryCacheReport {
public:
    struct Entry {
        BinaryStatus status = BinaryStatus::Unknown;
        QString category;   ///< d77crux-cache's own word, e.g. "skip-version"
        QString detail;     ///< the rest of its line
    };

    /**
     * Parses d77crux-cache's report lines ("<category> <port> <detail>", as
     * printed by its note()). Other lines (counts, hints, blanks) are ignored.
     */
    static BinaryCacheReport parse(const QString &output);

    static BinaryStatus statusForCategory(const QString &category);

    Entry entry(const QString &port) const;
    bool isEmpty() const { return m_entries.isEmpty(); }

    /** For skip-version: "tree 0.5.13.6-1, release 0.5.13.5-1" -> {"0.5.13.6-1", "0.5.13.5-1"}. */
    static QPair<QString, QString> treeAndRelease(const QString &detail);

    /** When the report was taken (set by BinaryCacheProbe). */
    QDateTime checkedAt;

private:
    QHash<QString, Entry> m_entries;
};

/** Everything the transaction preview needs to show binary availability. */
struct BinaryCacheContext {
    bool available = false;       ///< d77crux-cache is installed and a report exists
    BinaryCacheReport report;
    QDateTime snapshotPublished;  ///< invalid if unknown
    QDateTime now;
    QStringList largePorts;       ///< ports that take long to compile (Settings)

    /** /var/lib/d77crux-cache/channel-published (written by d77crux-cache); invalid if absent. */
    static QDateTime readSnapshotPublished(const QString &path = QStringLiteral("/var/lib/d77crux-cache/channel-published"));
};

/**
 * @brief Runs "d77crux-cache -n" (report only, no root needed) in the
 * background and keeps the last report.
 */
class BinaryCacheProbe : public QObject {
    Q_OBJECT
public:
    explicit BinaryCacheProbe(QObject *parent = nullptr);
    ~BinaryCacheProbe() override;

    /** d77crux-cache from PATH by default; an empty path disables the probe (tests). */
    QString toolPath() const;
    void setToolPath(const QString &path);

    bool isAvailable() const;
    bool isRunning() const;
    /** Starts a report unless one is running or the tool is missing. */
    bool start();

    const BinaryCacheReport &lastReport() const { return m_report; }
    bool hasReport() const { return m_hasReport; }

    static constexpr int kTimeoutMs = 60000;

signals:
    void reportReady();

private:
    QString m_toolPath;
    bool m_toolPathSet = false;
    std::unique_ptr<QProcess> m_process;
    QTimer m_timeout;
    BinaryCacheReport m_report;
    bool m_hasReport = false;
};

} // namespace OctoCRUX
