#include "crux_adapter.h"
#include "core/printf_parser.h"
#include "core/aliases_parser.h"
#include "core/dependency_parser.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>

namespace OctoCRUX {

CruxAdapter::CruxAdapter(std::shared_ptr<ICommandRunner> runner, QObject *parent)
    : ICruxBackend(parent)
    , m_runner(runner ? std::move(runner) : std::make_shared<DefaultCommandRunner>())
{
}

void CruxAdapter::setRootPrefix(const QString &prefix) {
    m_rootPrefix = prefix.trimmed();
    while (m_rootPrefix.endsWith(QLatin1Char('/'))) {
        m_rootPrefix.chop(1);
    }
    invalidateCache();
}

QString CruxAdapter::rootPrefix() const {
    return m_rootPrefix;
}

void CruxAdapter::setPrtGetBinary(const QString &bin) {
    m_prtGetBin = bin;
    invalidateCache();
}

QString CruxAdapter::prtGetBinary() const {
    return m_prtGetBin;
}

void CruxAdapter::setPkginfoBinary(const QString &bin) {
    m_pkginfoBin = bin;
}

QString CruxAdapter::pkginfoBinary() const {
    return m_pkginfoBin;
}

void CruxAdapter::invalidateCache() {
    m_cachedPorts.clear();
    m_portsByName.clear();
    m_cacheLoaded = false;
}

QString CruxAdapter::resolvePath(const QString &absolutePath) const {
    if (m_rootPrefix.isEmpty() || !absolutePath.startsWith(QLatin1Char('/'))) {
        return absolutePath;
    }
    return m_rootPrefix + absolutePath;
}

QString CruxAdapter::readTextFile(const QString &resolvedFilePath) const {
    QFile file(resolvedFilePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream in(&file);
    return in.readAll();
}

void CruxAdapter::ensurePortCache() {
    if (m_cacheLoaded) {
        return;
    }
    queryAllPorts();
}

QList<PortInfo> CruxAdapter::queryAllPorts() {
    // 1. Execute prt-get printf with canonical production 9-token format constant
    const QStringList args = {
        QStringLiteral("printf"),
        QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)
    };

    CommandResult res = m_runner->run(m_prtGetBin, args);
    if (!res.succeeded() && res.stdOut.isEmpty()) {
        emit outputReceived(QStringLiteral("Failed to query ports: ") + res.stdErr, true);
        return {};
    }

    // 2. Parse using PrintfParser (auto-detects 8 or 9 tokens)
    QList<PortInfo> ports = PrintfParser::parseAll(res.stdOut, PrintfParser::Schema::AutoDetect);

    // 3. Apply locks from /var/lib/pkg/prt-get.locker if present on disk
    const QString lockerPath = resolvePath(QStringLiteral("/var/lib/pkg/prt-get.locker"));
    const QString lockerContent = readTextFile(lockerPath);
    if (!lockerContent.isEmpty()) {
        const QStringList lockedNames = lockerContent.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        PrintfParser::applyLocks(ports, lockedNames);
    }

    // 4. Apply virtual aliases from /etc/prt-get.aliases if present on disk
    const QString aliasesPath = resolvePath(QStringLiteral("/etc/prt-get.aliases"));
    const QString aliasesContent = readTextFile(aliasesPath);
    if (!aliasesContent.isEmpty()) {
        const AliasesMapping aliases = AliasesParser::parse(aliasesContent);
        AliasesParser::applyAliases(ports, aliases, true);
    }

    // 5. Update in-memory index
    m_cachedPorts = ports;
    m_portsByName.clear();
    for (const PortInfo &p : m_cachedPorts) {
        m_portsByName.insert(p.name, p);
    }
    m_cacheLoaded = true;

    return m_cachedPorts;
}

std::optional<PortInfo> CruxAdapter::queryPort(const QString &portName) {
    ensurePortCache();
    auto it = m_portsByName.constFind(portName);
    if (it != m_portsByName.constEnd()) {
        return it.value();
    }
    return std::nullopt;
}

QStringList CruxAdapter::queryOutdatedPorts() {
    ensurePortCache();
    QStringList outdated;
    for (const PortInfo &p : m_cachedPorts) {
        if (p.installStatus == InstallStatus::Outdated) {
            outdated.append(p.name);
        }
    }
    return outdated;
}

QStringList CruxAdapter::queryLockedPorts() {
    ensurePortCache();
    QStringList locked;
    for (const PortInfo &p : m_cachedPorts) {
        if (p.lockStatus == LockStatus::Locked) {
            locked.append(p.name);
        }
    }
    return locked;
}

QMap<QString, QString> CruxAdapter::queryAliases() {
    const QString aliasesPath = resolvePath(QStringLiteral("/etc/prt-get.aliases"));
    const QString aliasesContent = readTextFile(aliasesPath);
    if (aliasesContent.isEmpty()) {
        return {};
    }
    const AliasesMapping mapping = AliasesParser::parse(aliasesContent);
    return mapping.virtualToProvider;
}

QString CruxAdapter::readPortFile(const QString &portName, const QString &fileName) {
    ensurePortCache();
    auto it = m_portsByName.constFind(portName);
    if (it == m_portsByName.constEnd()) {
        return QString();
    }

    const PortInfo &port = it.value();
    const QString fullDir = port.fullPortDirectory();
    if (fullDir.isEmpty()) {
        return QString();
    }

    const QString filePath = fullDir + QLatin1Char('/') + fileName;
    return readTextFile(resolvePath(filePath));
}

QStringList CruxAdapter::queryFilesForInstalledPackage(const QString &packageName) {
    const QStringList args = {
        QStringLiteral("-l"),
        packageName
    };

    CommandResult res = m_runner->run(m_pkginfoBin, args);
    if (!res.succeeded()) {
        return {};
    }

    QStringList files;
    const QStringList lines = res.stdOut.split(QLatin1Char('\n'));
    files.reserve(lines.size());

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            if (!trimmed.startsWith(QLatin1Char('/'))) {
                trimmed.prepend(QLatin1Char('/'));
            }
            files.append(trimmed);
        }
    }

    return files;
}

QString CruxAdapter::escapeErePattern(const QString &text) {
    // POSIX Extended Regular Expression (ERE) metacharacters:
    // . [ \ ( ) * + ? { | ^ $
    // NOTE: Slashes ('/') and hyphens ('-') are deliberately NOT escaped. In POSIX regcomp(3)
    // they are ordinary literal characters. Standard PCRE/Qt QRegularExpression::escape() escapes
    // '/' to '\/' and '-' to '\-', which breaks or produces undefined behavior in POSIX ERE.
    QString escaped;
    escaped.reserve(text.size() * 2);
    for (const QChar &ch : text) {
        switch (ch.unicode()) {
        case '.':
        case '[':
        case '\\':
        case '(':
        case ')':
        case '*':
        case '+':
        case '?':
        case '{':
        case '|':
        case '^':
        case '$':
            escaped.append(QLatin1Char('\\'));
            escaped.append(ch);
            break;
        default:
            escaped.append(ch);
            break;
        }
    }
    return escaped;
}

QString CruxAdapter::findPackageOwnerOfFile(const QString &filePath) {
    if (filePath.trimmed().isEmpty()) {
        return QString();
    }

    // pkginfo matches paths relative to root (e.g. "usr/bin/prt-get")
    QString normalizedTarget = filePath.trimmed();
    while (normalizedTarget.startsWith(QLatin1Char('/'))) {
        normalizedTarget.remove(0, 1);
    }

    // pkginfo -o interprets pattern as POSIX Extended Regular Expression (ERE).
    // Metacharacters (such as '+' in usr/bin/g++ or '.' in usr/lib/libstdc++.so.6)
    // MUST be escaped, otherwise real paths return no owner or match unintended files.
    // Anchors (^ and $) are deliberately omitted per empirical T480s testing.
    const QString escapedPattern = escapeErePattern(normalizedTarget);

    const QStringList args = {
        QStringLiteral("-o"),
        escapedPattern
    };

    CommandResult res = m_runner->run(m_pkginfoBin, args);
    if (!res.succeeded()) {
        return QString();
    }

    // pkginfo -o output format:
    // Package  File
    // prt-get  usr/bin/prt-get
    const QStringList lines = res.stdOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2) {
        return QString();
    }

    // Iterate through all candidate lines to find exact match
    for (int i = 1; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.size() >= 2) {
            const QString pkg = tokens.at(0);
            const QString file = tokens.at(1);
            if (file == normalizedTarget) {
                return pkg;
            }
        }
    }

    return QString();
}

QStringList CruxAdapter::queryDirectInstalledDependents(const QString &portName) {
    // Avoid CRUX quirk where nonexistent ports return rc=0 with empty stdout
    ensurePortCache();
    if (!m_portsByName.contains(portName)) {
        return {};
    }

    const QStringList args = {
        QStringLiteral("dependent"),
        portName
    };

    CommandResult res = m_runner->run(m_prtGetBin, args);
    if (!res.succeeded()) {
        return {};
    }

    return DependencyParser::parseDependents(res.stdOut);
}

QStringList CruxAdapter::queryAllDeclaredDependents(const QString &portName) {
    ensurePortCache();
    if (!m_portsByName.contains(portName)) {
        return {};
    }

    const QStringList args = {
        QStringLiteral("dependent"),
        QStringLiteral("--all"),
        portName
    };

    CommandResult res = m_runner->run(m_prtGetBin, args);
    if (!res.succeeded()) {
        return {};
    }

    return DependencyParser::parseDependents(res.stdOut);
}

QStringList CruxAdapter::queryRecursiveInstallOrder(const QString &portName) {
    ensurePortCache();
    if (!m_portsByName.contains(portName)) {
        return {};
    }

    const QStringList args = {
        QStringLiteral("quickdep"),
        portName
    };

    CommandResult res = m_runner->run(m_prtGetBin, args);
    if (!res.succeeded()) {
        return {};
    }

    const QStringList rawOrder = DependencyParser::parseQuickdep(res.stdOut);

    // Resolve aliases against installed packages
    const QString aliasesPath = resolvePath(QStringLiteral("/etc/prt-get.aliases"));
    const QString aliasesContent = readTextFile(aliasesPath);
    const AliasesMapping aliases = AliasesParser::parse(aliasesContent);

    QSet<QString> installedSet;
    for (const PortInfo &p : m_cachedPorts) {
        if (p.isInstalled()) {
            installedSet.insert(p.name);
        }
    }

    return DependencyParser::resolveQuickdepWithAliases(rawOrder, aliases, installedSet);
}

// --- Mutations & Execution (Phase 1 Stubs) ---
bool CruxAdapter::executeTransaction(const TransactionPlan &plan) {
    Q_UNUSED(plan);
    emit operationFinished(-1, OperationErrorKind::None, QStringLiteral("Mutation operations not yet implemented in Phase 1"));
    return false;
}

bool CruxAdapter::syncPortsTree() {
    emit operationFinished(-1, OperationErrorKind::None, QStringLiteral("syncPortsTree not yet implemented in Phase 1"));
    return false;
}

bool CruxAdapter::lockPort(const QString &portName) {
    Q_UNUSED(portName);
    return false;
}

bool CruxAdapter::unlockPort(const QString &portName) {
    Q_UNUSED(portName);
    return false;
}

void CruxAdapter::cancelActiveOperation() {
}

} // namespace OctoCRUX
