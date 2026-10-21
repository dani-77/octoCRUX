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
    /** How long cancelActiveOperation() waits before warning that the operation still runs. */
    virtual void setCancelGracePeriodMs(int ms) { Q_UNUSED(ms); }
    virtual int cancelGracePeriodMs() const { return 0; }

    // --- Mutations & Execution ---
    /**
     * @brief The exact command lines executeTransaction(plan) would run, in order,
     * each already wrapped by the privilege helper. Pure: starts no process.
     * Used to show the user what will run before any elevation happens.
     */
    virtual QList<QStringList> transactionCommands(const TransactionPlan &plan) const = 0;
    /** The exact (wrapped) command line syncPortsTree() would run. */
    virtual QStringList syncPortsCommand() const = 0;
    virtual bool executeTransaction(const TransactionPlan &plan) = 0;
    virtual bool syncPortsTree() = 0;
    virtual bool lockPort(const QString &portName) = 0;
    virtual bool unlockPort(const QString &portName) = 0;
    virtual void cancelActiveOperation() = 0;

signals:
    void outputReceived(const QString &text, bool isError);
    /** The current state of a line being redrawn with '\r' (e.g. a download progress meter). */
    void transientOutputReceived(const QString &text);
    void progressChanged(int current, int total, const QString &phase);
    void operationFinished(int exitCode, OperationErrorKind errorKind, const QString &summary);
    void rejectedConfigsDetected(const QStringList &rejectedFiles);
    /**
     * The running operation is waiting for the user, e.g. "Waiting for
     * authentication...". An empty reason means it is no longer waiting.
     */
    void waitingChanged(const QString &reason);
};

} // namespace OctoCRUX
