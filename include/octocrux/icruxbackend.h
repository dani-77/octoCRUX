#pragma once

#include "types.h"
#include <QObject>
#include <optional>

namespace OctoCRUX {

/**
 * @brief Pure abstract interface for CRUX package and ports backend adapters.
 * Decoupled from Qt UI widgets.
 */
class ICruxBackend : public QObject {
    Q_OBJECT
public:
    explicit ICruxBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~ICruxBackend() override = default;

    // --- Read-only queries ---
    virtual QList<PortInfo> queryAllPorts() = 0;
    virtual std::optional<PortInfo> queryPort(const QString &portName) = 0;
    virtual QStringList queryOutdatedPorts() = 0;
    virtual QStringList queryLockedPorts() = 0;
    virtual QMap<QString, QString> queryAliases() = 0;

    // --- Metadata inspection ---
    virtual QString readPortFile(const QString &portName, const QString &fileName) = 0;
    virtual QStringList queryFilesForInstalledPackage(const QString &packageName) = 0;
    virtual QString findPackageOwnerOfFile(const QString &filePath) = 0;

    // --- Dependency & impact analysis ---
    virtual QStringList queryDirectInstalledDependents(const QString &portName) = 0;
    virtual QStringList queryAllDeclaredDependents(const QString &portName) = 0;
    virtual QStringList queryRecursiveInstallOrder(const QString &portName) = 0;

    // --- Core package protection ---
    virtual bool isCorePackage(const QString &portName) = 0;
    virtual std::optional<QString> coreCollectionPath() = 0;

    // --- Configuration & Paths ---
    virtual QString rootPrefix() const { return QString(); }

    // --- Privilege helper configuration ---
    virtual void setPrivilegeHelperType(PrivilegeHelperType type) { Q_UNUSED(type); }
    virtual PrivilegeHelperType privilegeHelperType() const { return PrivilegeHelperType::None; }

    // --- Mutations & Execution ---
    virtual bool executeTransaction(const TransactionPlan &plan) = 0;
    virtual bool syncPortsTree() = 0;
    virtual bool lockPort(const QString &portName) = 0;
    virtual bool unlockPort(const QString &portName) = 0;
    virtual void cancelActiveOperation() = 0;

signals:
    void outputReceived(const QString &text, bool isError);
    void progressChanged(int current, int total, const QString &phase);
    void operationFinished(int exitCode, OperationErrorKind errorKind, const QString &summary);
    void rejectedConfigsDetected(const QStringList &rejectedFiles);
};

} // namespace OctoCRUX
