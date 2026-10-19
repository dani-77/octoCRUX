#pragma once

#include "octocrux/icruxbackend.h"
#include "command_runner.h"

#include <memory>
#include <QMap>

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
    QString rootPrefix() const;

    void setPrtGetBinary(const QString &bin);
    QString prtGetBinary() const;

    void setPkginfoBinary(const QString &bin);
    QString pkginfoBinary() const;

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

    // --- Mutations & Execution (Stubs for Phase 1, implemented in Phase 2) ---
    bool executeTransaction(const TransactionPlan &plan) override;
    bool syncPortsTree() override;
    bool lockPort(const QString &portName) override;
    bool unlockPort(const QString &portName) override;
    void cancelActiveOperation() override;

private:
    QString resolvePath(const QString &absolutePath) const;
    QString readTextFile(const QString &resolvedFilePath) const;
    void ensurePortCache();

    std::shared_ptr<ICommandRunner> m_runner;
    QString m_rootPrefix;
    QString m_prtGetBin = QStringLiteral("prt-get");
    QString m_pkginfoBin = QStringLiteral("pkginfo");

    QList<PortInfo> m_cachedPorts;
    QMap<QString, PortInfo> m_portsByName;
    bool m_cacheLoaded = false;
};

} // namespace OctoCRUX
