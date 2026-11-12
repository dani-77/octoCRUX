#include "binary_cache_report.h"

#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

namespace OctoCRUX {

BinaryStatus BinaryCacheReport::statusForCategory(const QString &category) {
    // d77crux-cache 0.1.2 note() categories.
    static const QHash<QString, BinaryStatus> map = {
        {QStringLiteral("present"), BinaryStatus::Ready},
        {QStringLiteral("cached"), BinaryStatus::Ready},
        {QStringLiteral("would-cache"), BinaryStatus::NotPlaced},
        {QStringLiteral("skip-version"), BinaryStatus::Pending},
        {QStringLiteral("locked"), BinaryStatus::LockedAvailable},
        {QStringLiteral("skip-absent"), BinaryStatus::Builds},
        {QStringLiteral("skip-collection"), BinaryStatus::Builds},
        {QStringLiteral("skip-modified"), BinaryStatus::Builds},
        {QStringLiteral("skip-foreign"), BinaryStatus::Builds},
        {QStringLiteral("failed"), BinaryStatus::Builds},
    };
    return map.value(category, BinaryStatus::Unknown);
}

BinaryCacheReport BinaryCacheReport::parse(const QString &output) {
    BinaryCacheReport report;
    static const QRegularExpression lineRe(QStringLiteral("^([a-z][a-z-]*)\\s+(\\S+)\\s*(.*)$"));
    for (const QString &raw : output.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = lineRe.match(raw.trimmed());
        if (!m.hasMatch()) {
            continue;
        }
        const BinaryStatus status = statusForCategory(m.captured(1));
        if (status == BinaryStatus::Unknown) {
            continue; // "removed", or not a report line at all
        }
        report.m_entries.insert(m.captured(2), Entry{status, m.captured(1), m.captured(3).trimmed()});
    }
    return report;
}

BinaryCacheReport::Entry BinaryCacheReport::entry(const QString &port) const {
    return m_entries.value(port);
}

QPair<QString, QString> BinaryCacheReport::treeAndRelease(const QString &detail) {
    static const QRegularExpression re(QStringLiteral("^tree (\\S+), release (\\S+)$"));
    const QRegularExpressionMatch m = re.match(detail);
    return m.hasMatch() ? qMakePair(m.captured(1), m.captured(2)) : qMakePair(QString(), QString());
}

QDateTime BinaryCacheContext::readSnapshotPublished(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QDateTime();
    }
    return QDateTime::fromString(QString::fromUtf8(f.readAll()).trimmed(), Qt::ISODate);
}

BinaryCacheProbe::BinaryCacheProbe(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_process) m_process->kill(); // unprivileged, report only
    });
}

BinaryCacheProbe::~BinaryCacheProbe() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QString BinaryCacheProbe::toolPath() const {
    return m_toolPathSet ? m_toolPath : QStandardPaths::findExecutable(QStringLiteral("d77crux-cache"));
}

void BinaryCacheProbe::setToolPath(const QString &path) {
    m_toolPath = path;
    m_toolPathSet = true;
}

bool BinaryCacheProbe::isAvailable() const {
    return !toolPath().isEmpty();
}

bool BinaryCacheProbe::isRunning() const {
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool BinaryCacheProbe::start() {
    const QString tool = toolPath();
    if (tool.isEmpty() || isRunning()) {
        return false;
    }
    m_process = std::make_unique<QProcess>();
    QProcess *p = m_process.get();
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, p](int exitCode, QProcess::ExitStatus status) {
        m_timeout.stop();
        // A failed run (no network, bad signature) leaves the previous report.
        if (status == QProcess::NormalExit && exitCode == 0) {
            m_report = BinaryCacheReport::parse(QString::fromUtf8(p->readAllStandardOutput()));
            m_report.checkedAt = QDateTime::currentDateTime();
            m_hasReport = true;
        }
        emit reportReady();
    });
    p->start(tool, {QStringLiteral("-n")});
    p->closeWriteChannel();
    m_timeout.start();
    return true;
}

} // namespace OctoCRUX
