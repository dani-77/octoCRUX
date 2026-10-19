#pragma once

#include "backend/crux_adapter.h"
#include "model/package_model.h"
#include "model/package_filter_proxy_model.h"
#include "operations/transaction_planner.h"

#include <QMainWindow>
#include <QItemSelection>
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

public slots:
    void refreshCatalog();
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
    void onProgressChanged(int current, int total, const QString &phase);
    void onOperationFinished(int exitCode, OperationErrorKind errorKind, const QString &summary);
    void onRejectedConfigsDetected(const QStringList &rejectedFiles);

    // Action buttons & context menu slots
    void onSyncPortsClicked();
    void onInstallClicked();
    void onRemoveClicked();
    void onUpgradeClicked();
    void onToggleLockClicked();
    void onCancelOperationClicked();
    void onCustomContextMenuRequested(const QPoint &pos);

private:
    void setupUi();
    void setupToolbar();
    void setupCentralWidget();
    void setupDetailTabs();
    void setupStatusBar();
    void populateCollections();
    void updateStatusBar();
    void updateActionButtons();
    void loadPortDetails(const PortInfo &port);
    void clearPortDetails();
    std::optional<PortInfo> currentSelectedPort() const;

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
    QToolButton *m_btnLock = nullptr;
    QToolButton *m_btnCancel = nullptr;

    // Main layout
    QSplitter *m_mainSplitter = nullptr;
    QTableView *m_tableView = nullptr;
    QTabWidget *m_tabWidget = nullptr;

    // Detail tab widgets
    QTextBrowser *m_infoBrowser = nullptr;
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
};

} // namespace OctoCRUX
