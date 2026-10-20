#include "mainwindow.h"
#include "core/build_log_analyzer.h"
#include "transaction_preview_dialog.h"

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
#include <QProgressBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QScrollBar>
#include <QClipboard>
#include <QCloseEvent>
#include <QPushButton>

namespace OctoCRUX {

MainWindow::MainWindow(std::shared_ptr<ICruxBackend> backend, QWidget *parent)
    : QMainWindow(parent)
    , m_backend(backend ? std::move(backend) : std::make_shared<CruxAdapter>())
    , m_planner(std::make_unique<TransactionPlanner>(m_backend))
    , m_packageModel(std::make_unique<PackageModel>(this))
    , m_proxyModel(std::make_unique<PackageFilterProxyModel>(this))
{
    setWindowTitle(QStringLiteral("OctoCRUX — CRUX Linux Package Manager"));
    resize(1024, 720);

    m_proxyModel->setSourceModel(m_packageModel.get());

    setupUi();

    connect(m_backend.get(), &ICruxBackend::outputReceived,
            this, &MainWindow::onBackendOutputReceived);
    connect(m_backend.get(), &ICruxBackend::progressChanged,
            this, &MainWindow::onProgressChanged);
    connect(m_backend.get(), &ICruxBackend::operationFinished,
            this, &MainWindow::onOperationFinished);
    connect(m_backend.get(), &ICruxBackend::rejectedConfigsDetected,
            this, &MainWindow::onRejectedConfigsDetected);

    refreshCatalog();
    updateActionButtons();
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

QPlainTextEdit* MainWindow::outputLogEdit() const {
    return m_outputLogEdit;
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

    toolbar->addSeparator();

    // 5. Operations & Execution Buttons
    m_btnSync = new QToolButton(toolbar);
    m_btnSync->setText(QStringLiteral("Sync"));
    m_btnSync->setToolTip(QStringLiteral("Synchronize ports tree via 'ports -u'"));
    toolbar->addWidget(m_btnSync);
    connect(m_btnSync, &QToolButton::clicked, this, &MainWindow::onSyncPortsClicked);

    m_btnInstall = new QToolButton(toolbar);
    m_btnInstall->setText(QStringLiteral("Install"));
    m_btnInstall->setToolTip(QStringLiteral("Install selected port and dependencies"));
    toolbar->addWidget(m_btnInstall);
    connect(m_btnInstall, &QToolButton::clicked, this, &MainWindow::onInstallClicked);

    m_btnRemove = new QToolButton(toolbar);
    m_btnRemove->setText(QStringLiteral("Remove"));
    m_btnRemove->setToolTip(QStringLiteral("Remove selected package"));
    toolbar->addWidget(m_btnRemove);
    connect(m_btnRemove, &QToolButton::clicked, this, &MainWindow::onRemoveClicked);

    m_btnUpgrade = new QToolButton(toolbar);
    m_btnUpgrade->setText(QStringLiteral("Upgrade"));
    m_btnUpgrade->setToolTip(QStringLiteral("Upgrade outdated packages"));
    toolbar->addWidget(m_btnUpgrade);
    connect(m_btnUpgrade, &QToolButton::clicked, this, &MainWindow::onUpgradeClicked);

    m_btnLock = new QToolButton(toolbar);
    m_btnLock->setText(QStringLiteral("Lock"));
    m_btnLock->setToolTip(QStringLiteral("Toggle lock status for selected package"));
    toolbar->addWidget(m_btnLock);
    connect(m_btnLock, &QToolButton::clicked, this, &MainWindow::onToggleLockClicked);

    m_btnCancel = new QToolButton(toolbar);
    m_btnCancel->setText(QStringLiteral("Cancel"));
    m_btnCancel->setToolTip(QStringLiteral("Cancel currently running operation"));
    m_btnCancel->setEnabled(false);
    toolbar->addWidget(m_btnCancel);
    connect(m_btnCancel, &QToolButton::clicked, this, &MainWindow::onCancelOperationClicked);
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

    // Context menu support
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested,
            this, &MainWindow::onCustomContextMenuRequested);

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

    // 6. Output Tab (Live Transaction / Build Log)
    m_outputLogEdit = new QPlainTextEdit(m_tabWidget);
    m_outputLogEdit->setReadOnly(true);
    m_outputLogEdit->setFont(monoFont);
    m_outputLogEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_outputLogEdit->setStyleSheet(QStringLiteral("QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }"));
    m_tabWidget->addTab(m_outputLogEdit, QStringLiteral("Output"));

    clearPortDetails();
}

void MainWindow::setupStatusBar() {
    QStatusBar *bar = statusBar();

    m_statusSummaryLabel = new QLabel(bar);
    bar->addWidget(m_statusSummaryLabel, 1);

    m_operationPhaseLabel = new QLabel(bar);
    m_operationPhaseLabel->setStyleSheet(QStringLiteral("color: #1976d2; font-weight: bold; padding-right: 6px;"));
    bar->addPermanentWidget(m_operationPhaseLabel);

    m_progressBar = new QProgressBar(bar);
    m_progressBar->setVisible(false);
    m_progressBar->setMaximumHeight(16);
    m_progressBar->setMaximumWidth(140);
    bar->addPermanentWidget(m_progressBar);

    m_statusFilterLabel = new QLabel(bar);
    m_statusFilterLabel->setStyleSheet(QStringLiteral("color: gray; padding-left: 6px;"));
    bar->addWidget(m_statusFilterLabel);

    QString helperName;
    switch (m_backend->privilegeHelperType()) {
    case PrivilegeHelperType::QtSudo: helperName = QStringLiteral("qt-sudo"); break;
    case PrivilegeHelperType::Sudo: helperName = QStringLiteral("sudo (TTY)"); break;
    case PrivilegeHelperType::Doas: helperName = QStringLiteral("doas (TTY)"); break;
    case PrivilegeHelperType::Su: helperName = QStringLiteral("su (TTY)"); break;
    case PrivilegeHelperType::None: helperName = QStringLiteral("none"); break;
    }
    m_backendLabel = new QLabel(QStringLiteral("CRUX 3.8 [%1]").arg(helperName), bar);
    m_backendLabel->setStyleSheet(QStringLiteral("color: gray; padding-right: 6px;"));
    bar->addPermanentWidget(m_backendLabel);
}

void MainWindow::checkPrivilegeHelper() {
    const auto helper = m_backend->privilegeHelperType();
    if (helper == PrivilegeHelperType::Sudo || 
        helper == PrivilegeHelperType::Doas || 
        helper == PrivilegeHelperType::Su) {
        QString helperStr;
        if (helper == PrivilegeHelperType::Sudo) helperStr = QStringLiteral("sudo");
        else if (helper == PrivilegeHelperType::Doas) helperStr = QStringLiteral("doas");
        else helperStr = QStringLiteral("su");

        QMessageBox::warning(
            this,
            QStringLiteral("Privilege Helper Warning"),
            QStringLiteral("The detected privilege helper '%1' requires a terminal (TTY).\n\n"
                           "In a graphical environment without an active terminal, package operations may fail "
                           "or hang because '%1' cannot display a password prompt.\n\n"
                           "It is strongly recommended to install 'qt-sudo' on CRUX for graphical elevation.")
                .arg(helperStr)
        );
    } else if (helper == PrivilegeHelperType::None) {
        if (m_outputLogEdit) {
            m_outputLogEdit->appendPlainText(QStringLiteral("[octoCRUX] Warning: No privilege elevation helper detected (qt-sudo/sudo/doas/su). Package mutations will run unprivileged."));
        }
    }
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
        updateActionButtons();
        return;
    }

    const QModelIndex proxyIndex = selected.indexes().first();
    auto portOpt = m_proxyModel->portAtProxyRow(proxyIndex.row());
    if (portOpt.has_value()) {
        loadPortDetails(portOpt.value());
    } else {
        clearPortDetails();
    }
    updateActionButtons();
}

void MainWindow::onBackendOutputReceived(const QString &text, bool isError) {
    if (m_outputLogEdit) {
        if (isError) {
            m_outputLogEdit->appendHtml(QStringLiteral("<span style='color:#ef5350;'>%1</span>").arg(text.toHtmlEscaped()));
        } else {
            m_outputLogEdit->appendPlainText(text);
        }
        m_outputLogEdit->verticalScrollBar()->setValue(m_outputLogEdit->verticalScrollBar()->maximum());
    }

    if (isError) {
        statusBar()->showMessage(QStringLiteral("Backend error: ") + text, 5000);
    } else {
        statusBar()->showMessage(text, 3000);
    }
}

void MainWindow::onProgressChanged(int current, int total, const QString &phase) {
    m_operationInProgress = true;
    updateActionButtons();

    if (m_progressBar) {
        m_progressBar->setVisible(true);
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(current);
    }
    if (m_operationPhaseLabel) {
        m_operationPhaseLabel->setText(phase);
    }
}

void MainWindow::onOperationFinished(int exitCode, OperationErrorKind errorKind, const QString &summary) {
    m_operationInProgress = false;
    updateActionButtons();

    if (m_progressBar) {
        m_progressBar->setVisible(false);
    }
    if (m_operationPhaseLabel) {
        m_operationPhaseLabel->setText(summary);
    }

    if (m_outputLogEdit) {
        const QString statusMsg = exitCode == 0
            ? QStringLiteral("\n[octoCRUX] Operation finished successfully: %1\n").arg(summary)
            : QStringLiteral("\n[octoCRUX] Operation failed (exit code %1, error: %2): %3\n")
                .arg(exitCode).arg(BuildLogAnalyzer::errorKindToString(errorKind)).arg(summary);
        m_outputLogEdit->appendPlainText(statusMsg);
    }

    if (exitCode == 0) {
        refreshCatalog();
    }
}

void MainWindow::onRejectedConfigsDetected(const QStringList &rejectedFiles) {
    if (m_outputLogEdit) {
        m_outputLogEdit->appendHtml(QStringLiteral(
            "<br/><span style='color:#ffa726; font-weight:bold;'>"
            "[octoCRUX ALERT] Rejected configuration file(s) found in /var/lib/pkg/rejected:<br/>"
            "%1</span><br/>"
        ).arg(rejectedFiles.join(QStringLiteral("<br/>")).toHtmlEscaped()));
    }
    statusBar()->showMessage(
        QStringLiteral("Warning: %1 rejected config(s) found in /var/lib/pkg/rejected!").arg(rejectedFiles.size()),
        10000
    );
}

std::optional<PortInfo> MainWindow::currentSelectedPort() const {
    if (!m_tableView || !m_tableView->selectionModel()) {
        return std::nullopt;
    }
    const QModelIndexList selected = m_tableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return std::nullopt;
    }
    return m_proxyModel->portAtProxyRow(selected.first().row());
}

void MainWindow::updateActionButtons() {
    if (!m_btnSync) return;

    if (m_operationInProgress) {
        m_btnSync->setEnabled(false);
        m_btnInstall->setEnabled(false);
        m_btnRemove->setEnabled(false);
        m_btnUpgrade->setEnabled(false);
        m_btnLock->setEnabled(false);
        m_btnRefresh->setEnabled(false);
        m_btnCancel->setEnabled(true);
        return;
    }

    m_btnCancel->setEnabled(false);
    m_btnSync->setEnabled(true);
    m_btnRefresh->setEnabled(true);

    auto portOpt = currentSelectedPort();
    if (portOpt.has_value()) {
        const PortInfo &port = portOpt.value();
        const bool isInst = port.isInstalled();
        const bool isCore = m_backend->isCorePackage(port.name);
        m_btnInstall->setEnabled(!isInst);
        m_btnRemove->setEnabled(isInst && !isCore);
        if (isCore) {
            m_btnRemove->setToolTip(QStringLiteral("Core packages cannot be removed in OctoCRUX"));
        } else {
            m_btnRemove->setToolTip(QStringLiteral("Remove selected package"));
        }
        m_btnUpgrade->setEnabled(port.installStatus == InstallStatus::Outdated);
        m_btnLock->setEnabled(true);
        m_btnLock->setText(port.lockStatus == LockStatus::Locked ? QStringLiteral("Unlock") : QStringLiteral("Lock"));
    } else {
        m_btnInstall->setEnabled(false);
        m_btnRemove->setEnabled(false);
        m_btnRemove->setToolTip(QStringLiteral("Remove selected package"));
        m_btnUpgrade->setEnabled(true);
        m_btnLock->setEnabled(false);
        m_btnLock->setText(QStringLiteral("Lock"));
    }
}

void MainWindow::onSyncPortsClicked() {
    if (m_operationInProgress) return;

    // Sync rewrites the ports tree but does not touch installed packages: a simple
    // confirmation is enough, with Cancel as the default.
    QMessageBox box(QMessageBox::Question,
                    QStringLiteral("Synchronize ports tree"),
                    QStringLiteral("This runs, with elevated privileges:\n\n  %1\n\n"
                                   "It rewrites the ports tree under /usr/ports. Installed packages "
                                   "are not changed.")
                        .arg(TransactionPreviewDialog::formatCommand(m_backend->syncPortsCommand())),
                    QMessageBox::NoButton, this);
    box.setObjectName(QStringLiteral("syncConfirmMessage"));
    QPushButton *syncButton = box.addButton(QStringLiteral("Sync"), QMessageBox::AcceptRole);
    QPushButton *cancelButton = box.addButton(QMessageBox::Cancel);
    syncButton->setAutoDefault(false);
    box.setDefaultButton(cancelButton);
    box.exec();
    if (box.clickedButton() != syncButton) {
        return;
    }

    if (m_outputLogEdit && m_tabWidget) {
        m_tabWidget->setCurrentWidget(m_outputLogEdit);
    }
    m_backend->syncPortsTree();
}

void MainWindow::onInstallClicked() {
    if (m_operationInProgress) return;
    auto portOpt = currentSelectedPort();
    if (!portOpt.has_value()) return;

    TransactionPlan plan = m_planner->planInstall({portOpt->name});
    if (!plan.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Installation Error"), plan.errors.join(QLatin1Char('\n')));
        return;
    }
    if (!confirmTransaction(QStringLiteral("Confirm installation of %1").arg(portOpt->name), plan)) {
        return;
    }

    if (m_outputLogEdit && m_tabWidget) {
        m_tabWidget->setCurrentWidget(m_outputLogEdit);
    }
    m_backend->executeTransaction(plan);
}

void MainWindow::onRemoveClicked() {
    if (m_operationInProgress) return;
    auto portOpt = currentSelectedPort();
    if (!portOpt.has_value()) return;

    TransactionPlan plan = m_planner->planRemove({portOpt->name});
    if (!plan.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Removal Error"), plan.errors.join(QLatin1Char('\n')));
        return;
    }

    // Critical warnings (e.g. installed dependents) are listed in the preview.
    if (!confirmTransaction(QStringLiteral("Confirm removal of %1").arg(portOpt->name), plan)) {
        return;
    }

    if (m_outputLogEdit && m_tabWidget) {
        m_tabWidget->setCurrentWidget(m_outputLogEdit);
    }
    m_backend->executeTransaction(plan);
}

void MainWindow::onUpgradeClicked() {
    if (m_operationInProgress) return;
    auto portOpt = currentSelectedPort();
    TransactionPlan plan;
    if (portOpt.has_value() && portOpt->installStatus == InstallStatus::Outdated) {
        plan = m_planner->planUpgrade({portOpt->name});
    } else {
        plan = m_planner->planUpgrade();
    }

    if (!plan.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Upgrade Error"), plan.errors.join(QLatin1Char('\n')));
        return;
    }
    if (!confirmTransaction(QStringLiteral("Confirm upgrade"), plan)) {
        return;
    }

    if (m_outputLogEdit && m_tabWidget) {
        m_tabWidget->setCurrentWidget(m_outputLogEdit);
    }
    m_backend->executeTransaction(plan);
}

void MainWindow::onToggleLockClicked() {
    if (m_operationInProgress) return;
    auto portOpt = currentSelectedPort();
    if (!portOpt.has_value()) return;

    if (portOpt->lockStatus == LockStatus::Locked) {
        m_backend->unlockPort(portOpt->name);
    } else {
        m_backend->lockPort(portOpt->name);
    }
    refreshCatalog();
}

void MainWindow::onCancelOperationClicked() {
    m_backend->cancelActiveOperation();
}

bool MainWindow::confirmTransaction(const QString &title, const TransactionPlan &plan) {
    const QList<QStringList> commands = m_backend->transactionCommands(plan);
    if (commands.isEmpty()) {
        QString text = QStringLiteral("Nothing to execute: the plan contains no actions.");
        if (!plan.lockedSkipped.isEmpty()) {
            text += QStringLiteral("\n\nSkipped because locked: %1")
                        .arg(plan.lockedSkipped.join(QStringLiteral(" ")));
        }
        QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Ok, this);
        box.setObjectName(QStringLiteral("nothingToDoMessage"));
        box.exec();
        return false;
    }

    TransactionPreviewDialog dialog(title, plan, commands, m_backend->privilegeHelperType(), this);
    return dialog.exec() == QDialog::Accepted;
}

QMessageBox* MainWindow::closeRefusedMessage() const {
    return m_closeRefusedMessage;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!m_operationInProgress) {
        QMainWindow::closeEvent(event);
        return;
    }

    event->ignore();

    if (!m_closeRefusedMessage) {
        m_closeRefusedMessage = new QMessageBox(
            QMessageBox::Warning,
            QStringLiteral("Operation in progress"),
            QStringLiteral(
                "OctoCRUX cannot be closed while an operation is running.\n\n"
                "Closing now would end the privilege helper (qt-sudo/sudo) with SIGTERM and, "
                "one second later, SIGKILL. The root build processes it started (prt-get, "
                "pkgmk, make) could keep running unsupervised, with nobody to report or "
                "stop them.\n\n"
                "Press Cancel in the toolbar and wait until the operation reports that it "
                "has finished, then close the window."),
            QMessageBox::Ok,
            this);
    }
    // open(), not exec(): window-modal and non-blocking, so the event loop keeps
    // streaming build output while the message is shown.
    m_closeRefusedMessage->open();
}

void MainWindow::onCustomContextMenuRequested(const QPoint &pos) {
    QModelIndex index = m_tableView->indexAt(pos);
    if (!index.isValid()) return;

    auto portOpt = m_proxyModel->portAtProxyRow(index.row());
    if (!portOpt.has_value()) return;

    const PortInfo &port = portOpt.value();

    QMenu menu(this);
    QAction *actInstall = menu.addAction(QStringLiteral("Install '%1'").arg(port.name));
    actInstall->setEnabled(!port.isInstalled() && !m_operationInProgress);
    connect(actInstall, &QAction::triggered, this, &MainWindow::onInstallClicked);

    const bool isCore = m_backend->isCorePackage(port.name);
    QAction *actRemove = menu.addAction(QStringLiteral("Remove '%1'").arg(port.name));
    actRemove->setEnabled(port.isInstalled() && !isCore && !m_operationInProgress);
    if (isCore) {
        actRemove->setToolTip(QStringLiteral("Core packages cannot be removed in OctoCRUX"));
    }
    connect(actRemove, &QAction::triggered, this, &MainWindow::onRemoveClicked);

    QAction *actUpgrade = menu.addAction(QStringLiteral("Upgrade '%1'").arg(port.name));
    actUpgrade->setEnabled(port.installStatus == InstallStatus::Outdated && !m_operationInProgress);
    connect(actUpgrade, &QAction::triggered, this, &MainWindow::onUpgradeClicked);

    menu.addSeparator();

    const bool isLocked = (port.lockStatus == LockStatus::Locked);
    QAction *actLock = menu.addAction(isLocked ? QStringLiteral("Unlock '%1'").arg(port.name) : QStringLiteral("Lock '%1'").arg(port.name));
    actLock->setEnabled(!m_operationInProgress);
    connect(actLock, &QAction::triggered, this, &MainWindow::onToggleLockClicked);

    menu.addSeparator();

    QAction *actCopyName = menu.addAction(QStringLiteral("Copy Port Name"));
    connect(actCopyName, &QAction::triggered, this, [&port]() {
        QGuiApplication::clipboard()->setText(port.name);
    });

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
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
