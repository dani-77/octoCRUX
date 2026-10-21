#pragma once

#include "octocrux/icruxbackend.h"
#include "command_runner.h"
#include "privilege_helper.h"
#include "crux_process_runner.h"

#include <memory>
#include <QMap>
#include <QQueue>

namespace OctoCRUX {

/**
 * @brief Concrete CRUX backend adapter implementing ICruxBackend.
 * Bridges native tools (prt-get, pkginfo) with Qt models via pure parsers.
 */
class CruxAdapter : public ICruxBackend {
    Q_OBJECT
public:
    explicit CruxAdapter(std::shared_ptr<ICommandRunner> runner = nullptr, QObject *parent = nullptr);
    ~CruxAdapter() override = default;

    // --- Configuration and path routing ---
    void setRootPrefix(const QString &prefix);
    QString rootPrefix() const override;

    void setPrtGetBinary(const QString &bin);
    QString prtGetBinary() const;

    void setPkginfoBinary(const QString &bin);
    QString pkginfoBinary() const;

    void setPortsBinary(const QString &bin);
    QString portsBinary() const;

    CruxProcessRunner* processRunner() const;

    // Invalidate port catalog cache
    void invalidateCache();

    // --- Read-only queries ---
    QList<PortInfo> queryAllPorts() override;
    std::optional<PortInfo> queryPort(const QString &portName) override;
    QStringList queryOutdatedPorts() override;
    QStringList queryLockedPorts() override;
    QMap<QString, QString> queryAliases() override;

    // --- Metadata inspection ---
    QString readPortFile(const QString &portName, const QString &fileName) override;
    QStringList queryFilesForInstalledPackage(const QString &packageName) override;
    QString findPackageOwnerOfFile(const QString &filePath) override;
    static QString escapeErePattern(const QString &text);

    // --- Dependency & impact analysis ---
    QStringList queryDirectInstalledDependents(const QString &portName) override;
    QStringList queryAllDeclaredDependents(const QString &portName) override;
    QStringList queryRecursiveInstallOrder(const QString &portName) override;

    // --- Core package protection ---
    bool isCorePackage(const QString &portName) override;
    std::optional<QString> coreCollectionPath() override;

    // --- Privilege helper configuration ---
    void setPrivilegeHelperType(PrivilegeHelperType type) override;
    PrivilegeHelperType privilegeHelperType() const override;
    void setCancelGracePeriodMs(int ms) override;
    int cancelGracePeriodMs() const override;

    // --- Mutations & Execution ---
    QList<QStringList> transactionCommands(const TransactionPlan &plan) const override;
    QStringList syncPortsCommand() const override;
    bool executeTransaction(const TransactionPlan &plan) override;
    bool syncPortsTree() override;
    bool lockPort(const QString &portName) override;
    bool unlockPort(const QString &portName) override;
    void cancelActiveOperation() override;
    QStringList removeWorkDirectoriesCommand(const QStringList &paths) const override;
    bool removeWorkDirectories(const QString &workRoot, const QStringList &paths) override;

private:
    struct BatchedOperation {
        QString program;
        QStringList arguments;
        QString description;
        int currentStep = 1;
        int totalSteps = 1;
    };

    // Single source of truth for how a plan becomes prt-get invocations; used by
    // both transactionCommands() (preview) and executeTransaction() (execution).
    QList<BatchedOperation> buildBatchedOperations(const TransactionPlan &plan) const;

    QString resolvePath(const QString &absolutePath) const;
    QString readTextFile(const QString &resolvedFilePath) const;
    void ensurePortCache();
    void checkRejectedConfigs();
    void startNextBatchedOperation();
    void onProcessRunnerFinished(int exitCode, OperationErrorKind errorKind, const QString &summary);

    std::shared_ptr<ICommandRunner> m_runner;
    PrivilegeHelper m_privilegeHelper;
    std::unique_ptr<CruxProcessRunner> m_processRunner;
    QQueue<BatchedOperation> m_pendingOperations;
    bool m_cancellationRequested = false;

    QString m_rootPrefix;
    QString m_prtGetBin = QStringLiteral("prt-get");
    QString m_pkginfoBin = QStringLiteral("pkginfo");
    QString m_portsBin = QStringLiteral("ports");

    QList<PortInfo> m_cachedPorts;
    QMap<QString, PortInfo> m_portsByName;
    bool m_cacheLoaded = false;
};

} // namespace OctoCRUX
