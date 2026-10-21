#pragma once

#include "backend/crux_adapter.h"
#include "model/package_model.h"
#include "model/package_filter_proxy_model.h"
#include "operations/transaction_planner.h"
#include "core/octocrux_settings.h"

#include <QMainWindow>
#include <QItemSelection>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QTableView;
class QTabWidget;
class QLineEdit;
class QComboBox;
class QButtonGroup;
class QToolButton;
class QLabel;
class QTextBrowser;
class QPlainTextEdit;
class QSplitter;
class QMessageBox;
class QCheckBox;
class QRadioButton;
class QGroupBox;
class QCloseEvent;
class QProgressBar;
QT_END_NAMESPACE

namespace OctoCRUX {

/**
 * @brief Main application window for OctoCRUX package manager.
 * Implements the Octopi/OctoXBPS desktop workflow adapted for CRUX Linux.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::shared_ptr<ICruxBackend> backend, QWidget *parent = nullptr);
    ~MainWindow() override = default;

    // Public getters for testing and automation
    PackageModel* packageModel() const;
    PackageFilterProxyModel* proxyModel() const;
    QTableView* tableView() const;
    QTabWidget* detailTabs() const;
    QPlainTextEdit* outputLogEdit() const;

    void checkPrivilegeHelper();

    /**
     * Shared settings; a successful Sync records its time there (read by the
     * notifier). Also applies the cancel grace period. The privilege helper is
     * applied only by applyPrivilegePreference().
     */
    void setSettings(std::shared_ptr<OctoCruxSettings> settings);

    /**
     * Sets the backend's privilege helper from the saved preference: the chosen
     * helper if it is in PATH, otherwise automatic detection (with a warning in
     * Output). Without settings it uses automatic detection.
     */
    void applyPrivilegePreference();

    /** Build check exceptions chosen for the next run of a package (empty = defaults). */
    BuildCheckOverrides pendingBuildOverrides(const QString &portName) const;

    /**
     * Where to look for work directories left by a cancelled build. Empty (the
     * default) reads PKGMK_WORK_DIR from pkgmk.conf; tests point it elsewhere.
     */
    void setWorkRootOverride(const QString &workRoot);

    /** Delay between checks that the cancelled tree has ended, and how many checks. */
    static constexpr int kLeftoverPollMs = 500;
    static constexpr int kLeftoverPollAttempts = 20;

    /** The message shown when a close is refused during an operation (testing). */
    QMessageBox* closeRefusedMessage() const;

public slots:
    void refreshCatalog();
    /**
     * Upgrade All as if the button had been pressed: opens the transaction
     * preview, and nothing runs until Apply. Used by `octocrux --upgrade-all`
     * (the notifier's "Upgrade All..."). With nothing to upgrade it says so.
     */
    void requestUpgradeAll();
    void setFilterStatus(StatusFilter status);
    void setFilterCollection(const QString &collection);
    void setSearchText(const QString &text);

private slots:
    void onSearchTextChanged(const QString &text);
    void onCollectionIndexChanged(int index);
    void onStatusButtonClicked(int id);
    void onPackageSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

    // Backend live execution slots
    void onBackendOutputReceived(const QString &text, bool isError);
    void onBackendTransientOutput(const QString &text);
    void onProgressChanged(int current, int total, const QString &phase);
    void onWaitingChanged(const QString &reason);
    void onOperationFinished(int exitCode, OperationErrorKind errorKind, const QString &summary);
    void onRejectedConfigsDetected(const QStringList &rejectedFiles);

    // Action buttons & context menu slots
    void onSyncPortsClicked();
    void onInstallClicked();
    void onRemoveClicked();
    void onUpgradeClicked();
    void onUpgradeAllClicked();
    void onToggleLockClicked();
    void onCancelOperationClicked();
    void onSettingsClicked();
    void onBuildChecksChanged();
    void onCustomContextMenuRequested(const QPoint &pos);

protected:
    /**
     * Refuses to close while an operation runs: tearing down the backend would end
     * the privilege helper (SIGTERM, then SIGKILL from the QProcess destructor),
     * which can leave root build processes running unsupervised.
     */
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUi();
    void setupToolbar();
    void setupCentralWidget();
    void setupDetailTabs();
    void setupStatusBar();
    void populateCollections();
    void updateStatusBar();
    void updateBackendLabel();
    void updateActionButtons();
    void loadPortDetails(const PortInfo &port);
    void clearPortDetails();
    std::optional<PortInfo> currentSelectedPort() const;
    /**
     * Shows the transaction preview and returns true only if the user pressed Apply.
     * A plan with no commands is reported as "nothing to do" and returns false.
     * No process is started here.
     */
    bool confirmTransaction(const QString &title, const TransactionPlan &plan);
    /**
     * Applies the pending build check exceptions to the plan, asks for
     * confirmation and, if accepted, executes it. Returns true if it started.
     */
    bool runTransaction(const QString &title, TransactionPlan plan);
    /** Shows the selected port's pending exceptions in the Information tab. */
    void loadBuildChecks(const QString &portName);
    /**
     * After a run whose footprint check failed with NEW entries only, offers to
     * retry those ports with -in (through the normal preview). MISSING entries
     * are only reported: -in cannot fix them.
     */
    void handleFootprintFailures(const QString &runLog);
    void offerFootprintRetry(const QStringList &ports);
    /**
     * After a Cancel: once no package tool runs (polled, see kLeftoverPollMs),
     * reports work directories the build left behind and offers to remove them.
     */
    void checkLeftoverWorkDirs(int attemptsLeft);
    QString leftoverWorkRoot() const;
    /** True if at least one package is outdated and unlocked (Upgrade All has work). */
    bool hasUpgradablePackages() const;

    std::shared_ptr<ICruxBackend> m_backend;
    std::unique_ptr<TransactionPlanner> m_planner;
    std::unique_ptr<PackageModel> m_packageModel;
    std::unique_ptr<PackageFilterProxyModel> m_proxyModel;

    // Toolbar & Filter widgets
    QToolButton *m_btnFilterAll = nullptr;
    QToolButton *m_btnFilterInstalled = nullptr;
    QToolButton *m_btnFilterOutdated = nullptr;
    QToolButton *m_btnFilterLocked = nullptr;
    QToolButton *m_btnFilterAvailable = nullptr;
    QButtonGroup *m_statusGroup = nullptr;
    QComboBox *m_comboCollection = nullptr;
    QLineEdit *m_editSearch = nullptr;
    QToolButton *m_btnRefresh = nullptr;

    // Action buttons
    QToolButton *m_btnSync = nullptr;
    QToolButton *m_btnInstall = nullptr;
    QToolButton *m_btnRemove = nullptr;
    QToolButton *m_btnUpgrade = nullptr;
    QToolButton *m_btnUpgradeAll = nullptr;
    QToolButton *m_btnLock = nullptr;
    QToolButton *m_btnCancel = nullptr;
    QToolButton *m_btnSettings = nullptr;

    // Main layout
    QSplitter *m_mainSplitter = nullptr;
    QTableView *m_tableView = nullptr;
    QTabWidget *m_tabWidget = nullptr;

    // Detail tab widgets
    QTextBrowser *m_infoBrowser = nullptr;
    QGroupBox *m_buildChecksBox = nullptr;
    QCheckBox *m_chkFootprint = nullptr;
    QRadioButton *m_rbFootprintIgnoreNew = nullptr;
    QRadioButton *m_rbFootprintIgnoreAll = nullptr;
    QCheckBox *m_chkMd5 = nullptr;
    QCheckBox *m_chkSignature = nullptr;
    QLabel *m_lblSignatureRisk = nullptr;
    QPlainTextEdit *m_filesEdit = nullptr;
    QPlainTextEdit *m_pkgfileEdit = nullptr;
    QPlainTextEdit *m_footprintEdit = nullptr;
    QTextBrowser *m_readmeBrowser = nullptr;
    QPlainTextEdit *m_outputLogEdit = nullptr;

    // Status bar widgets
    QLabel *m_statusSummaryLabel = nullptr;
    QLabel *m_statusFilterLabel = nullptr;
    QLabel *m_operationPhaseLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_backendLabel = nullptr;

    bool m_operationInProgress = false;
    QString m_currentPhase;             // shown again when a waiting hint ends
    // The last Output line is a '\r' progress line, to be replaced in place.
    bool m_lastOutputLineTransient = false;
    QPointer<QMessageBox> m_closeRefusedMessage;

    // Per-package build check exceptions for the next run (session only, never saved).
    QHash<QString, BuildCheckOverrides> m_buildOverrides;
    QString m_detailPort;               // port shown in the detail tabs
    QStringList m_overridesInFlight;    // exceptions consumed by the running operation
    int m_runOutputStart = -1;          // Output text offset where the running operation began
    // /var/lib/pkg/rejected as last reported, so the alert lists only new files.
    QSet<QString> m_knownRejected;
    bool m_rejectedReportedThisRun = false;

    QString m_workRootOverride;
    std::shared_ptr<OctoCruxSettings> m_settings;
    bool m_syncInFlight = false;        // the running operation is ports -u
};

} // namespace OctoCRUX
