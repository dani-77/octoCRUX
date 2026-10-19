#include "mainwindow.h"

#include <QApplication>
#include <QTableView>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QButtonGroup>
#include <QToolButton>
#include <QLabel>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QFontDatabase>
#include <QSet>
#include <QRegularExpression>

namespace OctoCRUX {

MainWindow::MainWindow(std::shared_ptr<ICruxBackend> backend, QWidget *parent)
    : QMainWindow(parent)
    , m_backend(backend ? std::move(backend) : std::make_shared<CruxAdapter>())
    , m_packageModel(std::make_unique<PackageModel>(this))
    , m_proxyModel(std::make_unique<PackageFilterProxyModel>(this))
{
    setWindowTitle(QStringLiteral("OctoCRUX — CRUX Linux Package Manager"));
    resize(1024, 720);

    m_proxyModel->setSourceModel(m_packageModel.get());

    setupUi();

    connect(m_backend.get(), &ICruxBackend::outputReceived,
            this, &MainWindow::onBackendOutputReceived);

    refreshCatalog();
}

PackageModel* MainWindow::packageModel() const {
    return m_packageModel.get();
}

PackageFilterProxyModel* MainWindow::proxyModel() const {
    return m_proxyModel.get();
}

QTableView* MainWindow::tableView() const {
    return m_tableView;
}

QTabWidget* MainWindow::detailTabs() const {
    return m_tabWidget;
}

void MainWindow::setupUi() {
    setupToolbar();
    setupCentralWidget();
    setupStatusBar();
}

void MainWindow::setupToolbar() {
    QToolBar *toolbar = addToolBar(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);

    // 1. Status Filter Buttons
    m_statusGroup = new QButtonGroup(this);
    m_statusGroup->setExclusive(true);

    auto createFilterBtn = [this, toolbar](const QString &text, int id, bool checked = false) -> QToolButton* {
        auto *btn = new QToolButton(toolbar);
        btn->setText(text);
        btn->setCheckable(true);
        btn->setChecked(checked);
        m_statusGroup->addButton(btn, id);
        toolbar->addWidget(btn);
        return btn;
    };

    m_btnFilterAll = createFilterBtn(QStringLiteral("All"), 0, true);
    m_btnFilterInstalled = createFilterBtn(QStringLiteral("Installed"), 1);
    m_btnFilterOutdated = createFilterBtn(QStringLiteral("Outdated"), 2);
    m_btnFilterLocked = createFilterBtn(QStringLiteral("Locked"), 3);
    m_btnFilterAvailable = createFilterBtn(QStringLiteral("Available"), 4);

    connect(m_statusGroup, &QButtonGroup::idClicked,
            this, &MainWindow::onStatusButtonClicked);

    toolbar->addSeparator();

    // 2. Collection Dropdown
    auto *lblCollection = new QLabel(QStringLiteral(" Collection: "), toolbar);
    toolbar->addWidget(lblCollection);

    m_comboCollection = new QComboBox(toolbar);
    m_comboCollection->setMinimumWidth(120);
    toolbar->addWidget(m_comboCollection);

    connect(m_comboCollection, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onCollectionIndexChanged);

    toolbar->addSeparator();

    // 3. Search Box
    auto *lblSearch = new QLabel(QStringLiteral(" Search: "), toolbar);
    toolbar->addWidget(lblSearch);

    m_editSearch = new QLineEdit(toolbar);
    m_editSearch->setPlaceholderText(QStringLiteral("Filter ports by name or description..."));
    m_editSearch->setClearButtonEnabled(true);
    m_editSearch->setMinimumWidth(240);
    toolbar->addWidget(m_editSearch);

    connect(m_editSearch, &QLineEdit::textChanged,
            this, &MainWindow::onSearchTextChanged);

    toolbar->addSeparator();

    // 4. Refresh Button
    m_btnRefresh = new QToolButton(toolbar);
    m_btnRefresh->setText(QStringLiteral("Refresh"));
    m_btnRefresh->setToolTip(QStringLiteral("Reload ports catalog from disk and prt-get"));
    toolbar->addWidget(m_btnRefresh);

    connect(m_btnRefresh, &QToolButton::clicked,
            this, &MainWindow::refreshCatalog);
}

void MainWindow::setupCentralWidget() {
    m_mainSplitter = new QSplitter(Qt::Vertical, this);

    // Upper pane: TableView
    m_tableView = new QTableView(m_mainSplitter);
    m_tableView->setModel(m_proxyModel.get());
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setDefaultSectionSize(26);

    // Initial column widths
    QHeaderView *header = m_tableView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(PackageModelColumn::ColumnName, QHeaderView::Interactive);
    header->setSectionResizeMode(PackageModelColumn::ColumnVersion, QHeaderView::Interactive);
    header->setSectionResizeMode(PackageModelColumn::ColumnStatus, QHeaderView::Interactive);
    header->setSectionResizeMode(PackageModelColumn::ColumnCollection, QHeaderView::Interactive);
    header->setSectionResizeMode(PackageModelColumn::ColumnDescription, QHeaderView::Stretch);

    m_tableView->setColumnWidth(PackageModelColumn::ColumnName, 170);
    m_tableView->setColumnWidth(PackageModelColumn::ColumnVersion, 120);
    m_tableView->setColumnWidth(PackageModelColumn::ColumnStatus, 140);
    m_tableView->setColumnWidth(PackageModelColumn::ColumnCollection, 100);

    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onPackageSelectionChanged);

    m_mainSplitter->addWidget(m_tableView);

    // Lower pane: Detail Tabs
    setupDetailTabs();
    m_mainSplitter->addWidget(m_tabWidget);

    // 65% table, 35% detail tabs
    m_mainSplitter->setStretchFactor(0, 65);
    m_mainSplitter->setStretchFactor(1, 35);

    setCentralWidget(m_mainSplitter);
}

void MainWindow::setupDetailTabs() {
    m_tabWidget = new QTabWidget(m_mainSplitter);
    const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // 1. Information Tab
    m_infoBrowser = new QTextBrowser(m_tabWidget);
    m_infoBrowser->setOpenExternalLinks(false);
    m_tabWidget->addTab(m_infoBrowser, QStringLiteral("Information"));

    // 2. Files Tab
    m_filesEdit = new QPlainTextEdit(m_tabWidget);
    m_filesEdit->setReadOnly(true);
    m_filesEdit->setFont(monoFont);
    m_filesEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_tabWidget->addTab(m_filesEdit, QStringLiteral("Files"));

    // 3. Pkgfile Tab
    m_pkgfileEdit = new QPlainTextEdit(m_tabWidget);
    m_pkgfileEdit->setReadOnly(true);
    m_pkgfileEdit->setFont(monoFont);
    m_pkgfileEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_tabWidget->addTab(m_pkgfileEdit, QStringLiteral("Pkgfile"));

    // 4. Footprint Tab
    m_footprintEdit = new QPlainTextEdit(m_tabWidget);
    m_footprintEdit->setReadOnly(true);
    m_footprintEdit->setFont(monoFont);
    m_footprintEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_tabWidget->addTab(m_footprintEdit, QStringLiteral("Footprint"));

    // 5. Readme Tab
    m_readmeBrowser = new QTextBrowser(m_tabWidget);
    m_readmeBrowser->setFont(monoFont);
    m_tabWidget->addTab(m_readmeBrowser, QStringLiteral("Readme"));

    clearPortDetails();
}

void MainWindow::setupStatusBar() {
    QStatusBar *bar = statusBar();

    m_statusSummaryLabel = new QLabel(bar);
    bar->addWidget(m_statusSummaryLabel, 1);

    m_statusFilterLabel = new QLabel(bar);
    bar->addPermanentWidget(m_statusFilterLabel);

    m_backendLabel = new QLabel(QStringLiteral("CRUX 3.8 Backend"), bar);
    m_backendLabel->setStyleSheet(QStringLiteral("color: gray; padding-right: 6px;"));
    bar->addPermanentWidget(m_backendLabel);
}

void MainWindow::populateCollections() {
    m_comboCollection->blockSignals(true);
    const QString currentSelected = m_comboCollection->currentData().toString();
    m_comboCollection->clear();

    m_comboCollection->addItem(QStringLiteral("All Collections"), QString());

    QSet<QString> collectionSet;
    for (const PortInfo &p : m_packageModel->ports()) {
        if (!p.collection.isEmpty()) {
            collectionSet.insert(p.collection);
        }
    }

    QStringList sorted = collectionSet.values();
    sorted.sort(Qt::CaseInsensitive);

    for (const QString &col : sorted) {
        m_comboCollection->addItem(col, col);
    }

    int idx = m_comboCollection->findData(currentSelected);
    m_comboCollection->setCurrentIndex(idx >= 0 ? idx : 0);
    m_comboCollection->blockSignals(false);
}

void MainWindow::refreshCatalog() {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<PortInfo> ports = m_backend->queryAllPorts();
    m_packageModel->setPorts(ports);
    populateCollections();
    updateStatusBar();
    clearPortDetails();
    QApplication::restoreOverrideCursor();
}

void MainWindow::setFilterStatus(StatusFilter status) {
    int btnId = 0;
    switch (status) {
    case StatusFilter::All: btnId = 0; break;
    case StatusFilter::Installed: btnId = 1; break;
    case StatusFilter::Outdated: btnId = 2; break;
    case StatusFilter::Locked: btnId = 3; break;
    case StatusFilter::NotInstalled: btnId = 4; break;
    }
    if (auto *btn = m_statusGroup->button(btnId)) {
        btn->setChecked(true);
    }
    m_proxyModel->setStatusFilter(status);
    updateStatusBar();
}

void MainWindow::setFilterCollection(const QString &collection) {
    int idx = m_comboCollection->findData(collection);
    if (idx >= 0) {
        m_comboCollection->setCurrentIndex(idx);
    }
}

void MainWindow::setSearchText(const QString &text) {
    m_editSearch->setText(text);
}

void MainWindow::onSearchTextChanged(const QString &text) {
    m_proxyModel->setSearchText(text);
    updateStatusBar();
}

void MainWindow::onCollectionIndexChanged(int index) {
    const QString collection = m_comboCollection->itemData(index).toString();
    m_proxyModel->setCollectionFilter(collection);
    updateStatusBar();
}

void MainWindow::onStatusButtonClicked(int id) {
    switch (id) {
    case 0: m_proxyModel->setStatusFilter(StatusFilter::All); break;
    case 1: m_proxyModel->setStatusFilter(StatusFilter::Installed); break;
    case 2: m_proxyModel->setStatusFilter(StatusFilter::Outdated); break;
    case 3: m_proxyModel->setStatusFilter(StatusFilter::Locked); break;
    case 4: m_proxyModel->setStatusFilter(StatusFilter::NotInstalled); break;
    default: m_proxyModel->setStatusFilter(StatusFilter::All); break;
    }
    updateStatusBar();
}

void MainWindow::onPackageSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected) {
    Q_UNUSED(deselected);
    if (selected.indexes().isEmpty()) {
        clearPortDetails();
        return;
    }

    const QModelIndex proxyIndex = selected.indexes().first();
    auto portOpt = m_proxyModel->portAtProxyRow(proxyIndex.row());
    if (portOpt.has_value()) {
        loadPortDetails(portOpt.value());
    } else {
        clearPortDetails();
    }
}

void MainWindow::onBackendOutputReceived(const QString &text, bool isError) {
    if (isError) {
        statusBar()->showMessage(QStringLiteral("Backend error: ") + text, 5000);
    } else {
        statusBar()->showMessage(text, 3000);
    }
}

void MainWindow::updateStatusBar() {
    int total = m_packageModel->rowCount();
    int visible = m_proxyModel->rowCount();

    int installed = 0;
    int outdated = 0;
    int locked = 0;

    for (const PortInfo &p : m_packageModel->ports()) {
        if (p.isInstalled()) installed++;
        if (p.installStatus == InstallStatus::Outdated) outdated++;
        if (p.lockStatus == LockStatus::Locked) locked++;
    }

    m_statusSummaryLabel->setText(
        QStringLiteral("Total: %1 ports | Installed: %2 | Outdated: %3 | Locked: %4")
            .arg(total)
            .arg(installed)
            .arg(outdated)
            .arg(locked)
    );

    if (visible == total) {
        m_statusFilterLabel->setText(QStringLiteral("Showing all %1 ports").arg(total));
    } else {
        m_statusFilterLabel->setText(QStringLiteral("Filtered: %1 of %2 ports").arg(visible).arg(total));
    }
}

void MainWindow::loadPortDetails(const PortInfo &port) {
    // 1. Information Tab (Formatted HTML)
    QString statusColor = QStringLiteral("#555555");
    if (port.installStatus == InstallStatus::Installed) {
        statusColor = QStringLiteral("#2e7d32"); // Green
    } else if (port.installStatus == InstallStatus::Outdated) {
        statusColor = QStringLiteral("#e65100"); // Orange
    }

    QString html;
    html += QStringLiteral("<h2>%1 <small style='font-size:70%; color:#666;'>%2-%3</small></h2>")
                .arg(port.name.toHtmlEscaped(), port.version.toHtmlEscaped(), port.release.toHtmlEscaped());

    html += QStringLiteral("<table cellpadding='3' cellspacing='0' style='font-size: 13px;'>");
    html += QStringLiteral("<tr><td><b>Status:</b></td><td style='color:%1; font-weight:bold;'>%2</td></tr>")
                .arg(statusColor, PackageModel::formatStatus(port).toHtmlEscaped());

    html += QStringLiteral("<tr><td><b>Collection:</b></td><td>%1 (<code>%2</code>)</td></tr>")
                .arg(port.collection.toHtmlEscaped(), port.collectionPath.toHtmlEscaped());

    if (!port.providedByAlias.isEmpty()) {
        html += QStringLiteral("<tr><td><b>Virtual Alias:</b></td><td style='color:#1565c0;'>Satisfied by installed provider: <b>%1</b></td></tr>")
                    .arg(port.providedByAlias.toHtmlEscaped());
    }

    // Pre/Post-install script indicators
    QString scriptsText;
    if (port.hasPreInstall && port.hasPostInstall) {
        scriptsText = QStringLiteral("Pre-install (pre-install) and Post-install (post-install) present");
    } else if (port.hasPreInstall) {
        scriptsText = QStringLiteral("Pre-install script (pre-install) present");
    } else if (port.hasPostInstall) {
        scriptsText = QStringLiteral("Post-install script (post-install) present");
    } else {
        scriptsText = QStringLiteral("None (standard package build)");
    }
    html += QStringLiteral("<tr><td><b>Install Scripts:</b></td><td>%1</td></tr>").arg(scriptsText);

    // Dependencies
    QString depsText = port.dependencies.isEmpty() ? QStringLiteral("<i>None</i>") : port.dependencies.join(QStringLiteral(", ")).toHtmlEscaped();
    html += QStringLiteral("<tr><td><b>Dependencies:</b></td><td>%1</td></tr>").arg(depsText);

    html += QStringLiteral("<tr><td valign='top'><b>Description:</b></td><td>%1</td></tr>")
                .arg(port.description.isEmpty() ? QStringLiteral("<i>No description</i>") : port.description.toHtmlEscaped());

    html += QStringLiteral("</table>");

    m_infoBrowser->setHtml(html);

    // 2. Files Tab
    if (port.isInstalled()) {
        const QStringList files = m_backend->queryFilesForInstalledPackage(port.name);
        if (files.isEmpty()) {
            m_filesEdit->setPlainText(QStringLiteral("(No files recorded for package %1)").arg(port.name));
        } else {
            m_filesEdit->setPlainText(files.join(QLatin1Char('\n')));
        }
    } else {
        m_filesEdit->setPlainText(
            QStringLiteral("Package '%1' is not installed.\nInstalled file inventory is only available for installed packages.")
                .arg(port.name)
        );
    }

    // 3. Pkgfile Tab
    const QString pkgfileContent = m_backend->readPortFile(port.name, QStringLiteral("Pkgfile"));
    if (pkgfileContent.isEmpty()) {
        m_pkgfileEdit->setPlainText(QStringLiteral("# Pkgfile not found for port '%1'").arg(port.name));
    } else {
        m_pkgfileEdit->setPlainText(pkgfileContent);
    }

    // 4. Footprint Tab
    const QString footprintContent = m_backend->readPortFile(port.name, QStringLiteral(".footprint"));
    if (footprintContent.isEmpty()) {
        m_footprintEdit->setPlainText(QStringLiteral("# .footprint not found for port '%1'").arg(port.name));
    } else {
        m_footprintEdit->setPlainText(footprintContent);
    }

    // 5. Readme Tab
    if (port.hasReadme) {
        const QString readmeContent = m_backend->readPortFile(port.name, QStringLiteral("README"));
        if (readmeContent.isEmpty()) {
            m_readmeBrowser->setPlainText(QStringLiteral("(README declared by port metadata, but file was empty)"));
        } else {
            m_readmeBrowser->setPlainText(readmeContent);
        }
    } else {
        m_readmeBrowser->setPlainText(QStringLiteral("No README documentation declared for port '%1'.").arg(port.name));
    }
}

void MainWindow::clearPortDetails() {
    m_infoBrowser->setHtml(QStringLiteral("<p style='color:gray; font-style:italic;'>Select a port from the list above to view details.</p>"));
    m_filesEdit->setPlainText(QStringLiteral("No package selected."));
    m_pkgfileEdit->setPlainText(QStringLiteral("# No package selected."));
    m_footprintEdit->setPlainText(QStringLiteral("# No package selected."));
    m_readmeBrowser->setPlainText(QStringLiteral("No package selected."));
}

} // namespace OctoCRUX
