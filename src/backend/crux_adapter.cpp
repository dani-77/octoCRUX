#include "crux_adapter.h"
#include "core/printf_parser.h"
#include "core/aliases_parser.h"
#include "core/dependency_parser.h"
#include "core/build_log_analyzer.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>

namespace OctoCRUX {

CruxAdapter::CruxAdapter(std::shared_ptr<ICommandRunner> runner, QObject *parent)
    : ICruxBackend(parent)
    , m_runner(runner ? std::move(runner) : std::make_shared<DefaultCommandRunner>())
    , m_processRunner(std::make_unique<CruxProcessRunner>(this))
{
    connect(m_processRunner.get(), &CruxProcessRunner::outputReceived,
            this, &CruxAdapter::outputReceived);
    connect(m_processRunner.get(), &CruxProcessRunner::phaseChanged,
            this, [this](const QString &phase) {
                emit progressChanged(1, 1, phase);
            });
    connect(m_processRunner.get(), &CruxProcessRunner::processFinished,
            this, &CruxAdapter::onProcessRunnerFinished);
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

void CruxAdapter::setPortsBinary(const QString &bin) {
    m_portsBin = bin;
}

QString CruxAdapter::portsBinary() const {
    return m_portsBin;
}

CruxProcessRunner* CruxAdapter::processRunner() const {
    return m_processRunner.get();
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
    // 1. Execute prt-get printf with canonical production 11-token format constant
    const QStringList args = {
        QStringLiteral("printf"),
        QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)
    };

    CommandResult res = m_runner->run(m_prtGetBin, args);
    if (!res.succeeded()) {
        emit outputReceived(QStringLiteral("Failed to query ports catalog: ") + res.stdErr, true);
        return {};
    }

    // 2. Parse using PrintfParser (auto-detects schema)
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

// --- Core package protection ---
std::optional<QString> CruxAdapter::coreCollectionPath() {
    const QString confPath = resolvePath(QStringLiteral("/etc/prt-get.conf"));
    const QString confContent = readTextFile(confPath);
    if (confContent.isEmpty()) {
        return std::nullopt;
    }

    const QStringList lines = confContent.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.size() >= 2 && tokens.at(0) == QStringLiteral("prtdir")) {
            const QString dirPath = tokens.at(1).trimmed();
            const QString cleaned = QDir::cleanPath(dirPath);
            const QString collectionName = QFileInfo(cleaned).fileName();
            if (collectionName.compare(QStringLiteral("core"), Qt::CaseInsensitive) == 0) {
                return resolvePath(cleaned);
            }
        }
    }

    return std::nullopt;
}

bool CruxAdapter::isCorePackage(const QString &portName) {
    if (portName.trimmed().isEmpty()) {
        return false;
    }

    const auto coreDirOpt = coreCollectionPath();
    if (!coreDirOpt.has_value()) {
        return false;
    }

    const QString candidateDir = coreDirOpt.value() + QLatin1Char('/') + portName.trimmed();
    return QDir(candidateDir).exists();
}

// --- Privilege helper configuration ---
void CruxAdapter::setPrivilegeHelperType(PrivilegeHelperType type) {
    m_privilegeHelper.setHelperType(type);
}

PrivilegeHelperType CruxAdapter::privilegeHelperType() const {
    return m_privilegeHelper.helperType();
}

// --- Mutations & Execution ---
bool CruxAdapter::executeTransaction(const TransactionPlan &plan) {
    m_cancellationRequested = false;
    m_pendingOperations.clear();

    if (!plan.isValid()) {
        const QString err = plan.errors.join(QStringLiteral("; "));
        emit outputReceived(QStringLiteral("[octoCRUX] Error: %1").arg(err), true);
        const OperationErrorKind kind = plan.hasProtectedPackages()
            ? OperationErrorKind::ProtectedPackage
            : OperationErrorKind::GeneralError;
        emit operationFinished(-1, kind, err);
        return false;
    }

    if (plan.steps.isEmpty()) {
        emit outputReceived(QStringLiteral("[octoCRUX] No executable actions in transaction plan."), false);
        emit operationFinished(0, OperationErrorKind::None, QStringLiteral("No executable actions in transaction plan"));
        return true;
    }

    if (m_processRunner->isRunning()) {
        emit outputReceived(QStringLiteral("[octoCRUX] Error: Another process is already running"), true);
        emit operationFinished(-1, OperationErrorKind::GeneralError, QStringLiteral("Process already running"));
        return false;
    }

    // Subordinate execution strictly to plan.steps (the sole vetted authority).
    // Partition targets by action, enforcing execution order:
    // 1. Upgrade steps first ('prt-get update <upgradeTargets>')
    // 2. Install steps second ('prt-get depinst <installTargets>')
    // 3. Reinstall steps ('prt-get update -fr <reinstallTargets>')
    // 4. Remove steps ('prt-get remove <removeTargets>')
    // 5. Lock steps ('prt-get lock <lockTargets>')
    // 6. Unlock steps ('prt-get unlock <unlockTargets>')

    QStringList upgradeTargets;
    QStringList installTargets;
    QStringList reinstallTargets;
    QStringList removeTargets;
    QStringList lockTargets;
    QStringList unlockTargets;

    for (const auto &step : plan.steps) {
        switch (step.action) {
        case TransactionAction::Upgrade:
            if (!upgradeTargets.contains(step.portName)) {
                upgradeTargets.append(step.portName);
            }
            break;
        case TransactionAction::Install:
            if (!installTargets.contains(step.portName)) {
                installTargets.append(step.portName);
            }
            break;
        case TransactionAction::Reinstall:
            if (!reinstallTargets.contains(step.portName)) {
                reinstallTargets.append(step.portName);
            }
            break;
        case TransactionAction::Remove:
            if (!removeTargets.contains(step.portName)) {
                removeTargets.append(step.portName);
            }
            break;
        case TransactionAction::Lock:
            if (!lockTargets.contains(step.portName)) {
                lockTargets.append(step.portName);
            }
            break;
        case TransactionAction::Unlock:
            if (!unlockTargets.contains(step.portName)) {
                unlockTargets.append(step.portName);
            }
            break;
        }
    }

    // 1. Upgrades first
    if (!upgradeTargets.isEmpty()) {
        BatchedOperation op;
        op.program = m_prtGetBin;
        op.arguments = QStringList{QStringLiteral("update")} + upgradeTargets;
        op.description = QStringLiteral("Updating %1").arg(upgradeTargets.join(QStringLiteral(", ")));
        m_pendingOperations.enqueue(op);
    }

    // 2. Installs second
    if (!installTargets.isEmpty()) {
        BatchedOperation op;
        op.program = m_prtGetBin;
        op.arguments = QStringList{QStringLiteral("depinst")} + installTargets;
        op.description = QStringLiteral("Installing %1").arg(installTargets.join(QStringLiteral(", ")));
        m_pendingOperations.enqueue(op);
    }

    // 3. Reinstalls
    if (!reinstallTargets.isEmpty()) {
        BatchedOperation op;
        op.program = m_prtGetBin;
        op.arguments = QStringList{QStringLiteral("update"), QStringLiteral("-fr")} + reinstallTargets;
        op.description = QStringLiteral("Reinstalling %1").arg(reinstallTargets.join(QStringLiteral(", ")));
        m_pendingOperations.enqueue(op);
    }

    // 4. Removals
    if (!removeTargets.isEmpty()) {
        BatchedOperation op;
        op.program = m_prtGetBin;
        op.arguments = QStringList{QStringLiteral("remove")} + removeTargets;
        op.description = QStringLiteral("Removing %1").arg(removeTargets.join(QStringLiteral(", ")));
        m_pendingOperations.enqueue(op);
    }

    // 5. Locks
    if (!lockTargets.isEmpty()) {
        BatchedOperation op;
        op.program = m_prtGetBin;
        op.arguments = QStringList{QStringLiteral("lock")} + lockTargets;
        op.description = QStringLiteral("Locking %1").arg(lockTargets.join(QStringLiteral(", ")));
        m_pendingOperations.enqueue(op);
    }

    // 6. Unlocks
    if (!unlockTargets.isEmpty()) {
        BatchedOperation op;
        op.program = m_prtGetBin;
        op.arguments = QStringList{QStringLiteral("unlock")} + unlockTargets;
        op.description = QStringLiteral("Unlocking %1").arg(unlockTargets.join(QStringLiteral(", ")));
        m_pendingOperations.enqueue(op);
    }

    if (m_pendingOperations.isEmpty()) {
        emit outputReceived(QStringLiteral("[octoCRUX] No executable actions in transaction plan."), false);
        emit operationFinished(0, OperationErrorKind::None, QStringLiteral("No operations in transaction plan"));
        return true;
    }

    const int totalOps = m_pendingOperations.size();
    for (int i = 0; i < totalOps; ++i) {
        m_pendingOperations[i].currentStep = i + 1;
        m_pendingOperations[i].totalSteps = totalOps;
    }

    startNextBatchedOperation();
    return true;
}

void CruxAdapter::startNextBatchedOperation() {
    if (m_pendingOperations.isEmpty()) {
        return;
    }

    BatchedOperation op = m_pendingOperations.dequeue();
    emit progressChanged(op.currentStep, op.totalSteps, op.description);
    emit outputReceived(QStringLiteral("[octoCRUX] %1").arg(op.description), false);

    const QStringList wrappedCmd = m_privilegeHelper.wrap(op.program, op.arguments);
    const QString execProg = wrappedCmd.first();
    const QStringList execArgs = wrappedCmd.mid(1);

    const bool started = m_processRunner->startProcess(execProg, execArgs);
    if (!started) {
        const QString err = QStringLiteral("Failed to start process: %1").arg(execProg);
        emit outputReceived(QStringLiteral("[octoCRUX] Error: %1").arg(err), true);
        m_pendingOperations.clear();
        emit operationFinished(-1, OperationErrorKind::GeneralError, err);
    }
}

void CruxAdapter::onProcessRunnerFinished(int exitCode, OperationErrorKind errorKind, const QString &summary) {
    if (!m_pendingOperations.isEmpty() && exitCode == 0 && !m_cancellationRequested) {
        startNextBatchedOperation();
        return;
    }

    m_pendingOperations.clear();
    invalidateCache();
    checkRejectedConfigs();

    emit operationFinished(exitCode, errorKind, summary);
}

bool CruxAdapter::syncPortsTree() {
    if (m_processRunner->isRunning()) {
        emit outputReceived(QStringLiteral("[octoCRUX] Error: Another process is already running"), true);
        emit operationFinished(-1, OperationErrorKind::GeneralError, QStringLiteral("Process already running"));
        return false;
    }

    m_cancellationRequested = false;
    m_pendingOperations.clear();

    const QString description = QStringLiteral("Updating ports tree via 'ports -u'...");
    emit progressChanged(1, 1, description);
    emit outputReceived(QStringLiteral("[octoCRUX] %1").arg(description), false);

    const QStringList wrappedCmd = m_privilegeHelper.wrap(m_portsBin, {QStringLiteral("-u")});
    const bool started = m_processRunner->startProcess(wrappedCmd.first(), wrappedCmd.mid(1));
    if (!started) {
        const QString err = QStringLiteral("Failed to start '%1 -u'").arg(m_portsBin);
        emit outputReceived(QStringLiteral("[octoCRUX] Error: %1").arg(err), true);
        emit operationFinished(-1, OperationErrorKind::GeneralError, err);
        return false;
    }
    return true;
}

bool CruxAdapter::lockPort(const QString &portName) {
    if (m_processRunner->isRunning()) {
        emit outputReceived(QStringLiteral("[octoCRUX] Error: Another process is already running"), true);
        return false;
    }
    const QString description = QStringLiteral("Locking %1").arg(portName);
    emit progressChanged(1, 1, description);
    emit outputReceived(QStringLiteral("[octoCRUX] %1").arg(description), false);

    const QStringList wrappedCmd = m_privilegeHelper.wrap(m_prtGetBin, {QStringLiteral("lock"), portName});
    return m_processRunner->startProcess(wrappedCmd.first(), wrappedCmd.mid(1));
}

bool CruxAdapter::unlockPort(const QString &portName) {
    if (m_processRunner->isRunning()) {
        emit outputReceived(QStringLiteral("[octoCRUX] Error: Another process is already running"), true);
        return false;
    }
    const QString description = QStringLiteral("Unlocking %1").arg(portName);
    emit progressChanged(1, 1, description);
    emit outputReceived(QStringLiteral("[octoCRUX] %1").arg(description), false);

    const QStringList wrappedCmd = m_privilegeHelper.wrap(m_prtGetBin, {QStringLiteral("unlock"), portName});
    return m_processRunner->startProcess(wrappedCmd.first(), wrappedCmd.mid(1));
}

void CruxAdapter::cancelActiveOperation() {
    m_cancellationRequested = true;
    m_pendingOperations.clear();
    if (m_processRunner && m_processRunner->isRunning()) {
        m_processRunner->cancel();
    }
}

void CruxAdapter::checkRejectedConfigs() {
    const QString rejectedPath = resolvePath(QStringLiteral("/var/lib/pkg/rejected"));
    QDir dir(rejectedPath);
    if (!dir.exists()) {
        return;
    }

    QStringList rejectedFiles;
    QDirIterator it(rejectedPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        rejectedFiles.append(it.filePath());
    }

    if (!rejectedFiles.isEmpty()) {
        emit rejectedConfigsDetected(rejectedFiles);
    }
}

} // namespace OctoCRUX
