#pragma once

#include "backend/crux_adapter.h"
#include "model/package_model.h"
#include "model/package_filter_proxy_model.h"

#include <QMainWindow>
#include <QItemSelection>
#include <memory>

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
    void onBackendOutputReceived(const QString &text, bool isError);

private:
    void setupUi();
    void setupToolbar();
    void setupCentralWidget();
    void setupDetailTabs();
    void setupStatusBar();
    void populateCollections();
    void updateStatusBar();
    void loadPortDetails(const PortInfo &port);
    void clearPortDetails();

    std::shared_ptr<ICruxBackend> m_backend;
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

    // Status bar labels
    QLabel *m_statusSummaryLabel = nullptr;
    QLabel *m_statusFilterLabel = nullptr;
    QLabel *m_backendLabel = nullptr;
};

} // namespace OctoCRUX
