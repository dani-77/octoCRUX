#include "mainwindow.h"
#include "core/build_log_analyzer.h"
#include "transaction_preview_dialog.h"
#include "settings_dialog.h"
#include "core/work_dir_cleanup.h"
#include "backend/privilege_helper.h"

#include <QApplication>
#include <QLocale>
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
#include <QTimer>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
#include <QButtonGroup>

namespace OctoCRUX {

namespace {

// QPlainTextEdit appends with the character format at the widget's text cursor.
// After a click inside a coloured line (e.g. the orange rejected-config alert)
// every later line inherited that colour (owner, 2026-10-21). Reset the format
// before each append so new output never depends on where the user clicked.
void appendOutputPlain(QPlainTextEdit *edit, const QString &text) {
    edit->setCurrentCharFormat(QTextCharFormat());
    edit->appendPlainText(text);
}

void appendOutputHtml(QPlainTextEdit *edit, const QString &html) {
    edit->setCurrentCharFormat(QTextCharFormat());
    edit->appendHtml(html);
}

// Replaces the text of the last block of the Output log, keeping the block.
void replaceLastLine(QPlainTextEdit *edit, const QString &text) {
    QTextCursor cursor(edit->document());
    cursor.movePosition(QTextCursor::End);
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.insertText(text, QTextCharFormat());
}

} // namespace

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
    connect(m_backend.get(), &ICruxBackend::transientOutputReceived,
            this, &MainWindow::onBackendTransientOutput);
    connect(m_backend.get(), &ICruxBackend::progressChanged,
            this, &MainWindow::onProgressChanged);
    connect(m_backend.get(), &ICruxBackend::operationFinished,
            this, &MainWindow::onOperationFinished);
    connect(m_backend.get(), &ICruxBackend::rejectedConfigsDetected,
            this, &MainWindow::onRejectedConfigsDetected);
    connect(m_backend.get(), &ICruxBackend::waitingChanged,
            this, &MainWindow::onWaitingChanged);

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
    m_btnUpgrade->setToolTip(QStringLiteral("Upgrade the selected package (outdated and unlocked)"));
    toolbar->addWidget(m_btnUpgrade);
    connect(m_btnUpgrade, &QToolButton::clicked, this, &MainWindow::onUpgradeClicked);

    m_btnUpgradeAll = new QToolButton(toolbar);
    m_btnUpgradeAll->setText(QStringLiteral("Upgrade All"));
    m_btnUpgradeAll->setToolTip(QStringLiteral("Upgrade every outdated, unlocked package (locked ones are skipped)"));
    toolbar->addWidget(m_btnUpgradeAll);
    connect(m_btnUpgradeAll, &QToolButton::clicked, this, &MainWindow::onUpgradeAllClicked);

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

    toolbar->addSeparator();

    m_btnSettings = new QToolButton(toolbar);
    m_btnSettings->setText(QStringLiteral("Settings..."));
    m_btnSettings->setToolTip(QStringLiteral("Privilege helper and cancel grace period"));
    toolbar->addWidget(m_btnSettings);
    connect(m_btnSettings, &QToolButton::clicked, this, &MainWindow::onSettingsClicked);
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

    // 1. Information Tab: port metadata, plus the build checks for its next run
    auto *infoPage = new QWidget(m_tabWidget);
    auto *infoLayout = new QVBoxLayout(infoPage);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    m_infoBrowser = new QTextBrowser(infoPage);
    m_infoBrowser->setOpenExternalLinks(false);
    infoLayout->addWidget(m_infoBrowser, 1);

    m_buildChecksBox = new QGroupBox(QStringLiteral("Build checks for the next run of this package"), infoPage);
    m_buildChecksBox->setToolTip(QStringLiteral(
        "Unchecking a check affects only the next Install/Upgrade that builds this package. "
        "Afterwards every check is on again. Nothing runs until you confirm the preview."));
    auto *checksLayout = new QVBoxLayout(m_buildChecksBox);
    m_chkFootprint = new QCheckBox(QStringLiteral("Verify footprint"), m_buildChecksBox);
    m_rbFootprintIgnoreNew = new QRadioButton(QStringLiteral("Ignore new files only (-in, recommended)"), m_buildChecksBox);
    m_rbFootprintIgnoreAll = new QRadioButton(QStringLiteral("Ignore all differences (-if)"), m_buildChecksBox);
    auto *footprintMode = new QButtonGroup(m_buildChecksBox);
    footprintMode->addButton(m_rbFootprintIgnoreNew);
    footprintMode->addButton(m_rbFootprintIgnoreAll);
    m_chkMd5 = new QCheckBox(QStringLiteral("Verify md5sum"), m_buildChecksBox);
    m_chkSignature = new QCheckBox(QStringLiteral("Verify signature"), m_buildChecksBox);
    m_lblSignatureRisk = new QLabel(QStringLiteral(
        "Signature check off: the port files and sources cannot be authenticated."), m_buildChecksBox);
    m_lblSignatureRisk->setObjectName(QStringLiteral("signatureRiskLabel"));
    m_lblSignatureRisk->setStyleSheet(QStringLiteral("color: #c62828; font-weight: bold;"));
    m_lblSignatureRisk->setWordWrap(true);

    checksLayout->addWidget(m_chkFootprint);
    for (QRadioButton *rb : {m_rbFootprintIgnoreNew, m_rbFootprintIgnoreAll}) {
        auto *row = new QHBoxLayout();
        row->addSpacing(24);
        row->addWidget(rb);
        checksLayout->addLayout(row);
    }
    checksLayout->addWidget(m_chkMd5);
    checksLayout->addWidget(m_chkSignature);
    checksLayout->addWidget(m_lblSignatureRisk);
    infoLayout->addWidget(m_buildChecksBox);

    for (QAbstractButton *b : std::initializer_list<QAbstractButton*>{
             m_chkFootprint, m_rbFootprintIgnoreNew, m_rbFootprintIgnoreAll, m_chkMd5, m_chkSignature}) {
        connect(b, &QAbstractButton::toggled, this, &MainWindow::onBuildChecksChanged);
    }
    m_tabWidget->addTab(infoPage, QStringLiteral("Information"));

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

    m_backendLabel = new QLabel(bar);
    m_backendLabel->setStyleSheet(QStringLiteral("color: gray; padding-right: 6px;"));
    bar->addPermanentWidget(m_backendLabel);
    updateBackendLabel();
}

void MainWindow::updateBackendLabel() {
    if (!m_backendLabel) return;
    QString helperName;
    switch (m_backend->privilegeHelperType()) {
    case PrivilegeHelperType::QtSudo: helperName = QStringLiteral("qt-sudo"); break;
    case PrivilegeHelperType::Sudo: helperName = QStringLiteral("sudo (TTY)"); break;
    case PrivilegeHelperType::Doas: helperName = QStringLiteral("doas (TTY)"); break;
    case PrivilegeHelperType::Su: helperName = QStringLiteral("su (TTY)"); break;
    case PrivilegeHelperType::None: helperName = QStringLiteral("none"); break;
    }
    m_backendLabel->setText(QStringLiteral("CRUX 3.8 [%1]").arg(helperName));
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
            appendOutputPlain(m_outputLogEdit, QStringLiteral("[octoCRUX] Warning: No privilege elevation helper detected (qt-sudo/sudo/doas/su). Package mutations will run unprivileged."));
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
    // Reloading the model clears the selection without emitting selectionChanged,
    // so the buttons must be re-evaluated here or they keep the old port's state.
    updateActionButtons();
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
        if (m_lastOutputLineTransient) {
            // The final state of a redrawn line takes the place of its progress updates.
            replaceLastLine(m_outputLogEdit, text);
            m_lastOutputLineTransient = false;
        } else if (isError) {
            appendOutputHtml(m_outputLogEdit, QStringLiteral("<span style='color:#ef5350;'>%1</span>").arg(text.toHtmlEscaped()));
        } else {
            appendOutputPlain(m_outputLogEdit, text);
        }
        m_outputLogEdit->verticalScrollBar()->setValue(m_outputLogEdit->verticalScrollBar()->maximum());
    }

    if (isError) {
        statusBar()->showMessage(QStringLiteral("Backend error: ") + text, 5000);
    } else {
        statusBar()->showMessage(text, 3000);
    }
}

void MainWindow::onBackendTransientOutput(const QString &text) {
    if (!m_outputLogEdit) {
        return;
    }
    if (m_lastOutputLineTransient) {
        replaceLastLine(m_outputLogEdit, text);
    } else {
        appendOutputPlain(m_outputLogEdit, text);
        m_lastOutputLineTransient = true;
    }
    m_outputLogEdit->verticalScrollBar()->setValue(m_outputLogEdit->verticalScrollBar()->maximum());
}

void MainWindow::onProgressChanged(int current, int total, const QString &phase) {
    if (!m_operationInProgress) {
        // First progress of an operation: remember where its output starts, and
        // count only rejected-config reports made during this operation.
        if (m_outputLogEdit) {
            m_runOutputStart = m_outputLogEdit->toPlainText().size();
        }
        m_rejectedReportedThisRun = false;
    }
    m_operationInProgress = true;
    updateActionButtons();

    if (m_progressBar) {
        m_progressBar->setVisible(true);
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(current);
    }
    m_currentPhase = phase;
    if (m_operationPhaseLabel) {
        m_operationPhaseLabel->setText(phase);
    }
}

void MainWindow::onWaitingChanged(const QString &reason) {
    if (!m_operationInProgress || !m_operationPhaseLabel) {
        return;
    }
    m_operationPhaseLabel->setText(reason.isEmpty() ? m_currentPhase : reason);
}

void MainWindow::onOperationFinished(int exitCode, OperationErrorKind errorKind, const QString &summary) {
    // The backend checks /var/lib/pkg/rejected after every operation that ran and
    // reports only when files exist: no report means the directory is now empty
    // (e.g. after rejmerge), so forget the previous list.
    if (m_operationInProgress && !m_rejectedReportedThisRun) {
        m_knownRejected.clear();
    }
    m_rejectedReportedThisRun = false;
    m_operationInProgress = false;
    if (m_syncInFlight) {
        m_syncInFlight = false;
        if (exitCode == 0 && errorKind == OperationErrorKind::None && m_settings) {
            m_settings->setLastSync(QDateTime::currentDateTime());
            m_settings->sync();
        }
    }
    // The exceptions were for this run only: success, failure or cancel, all checks
    // are on again afterwards.
    for (const QString &port : std::as_const(m_overridesInFlight)) {
        m_buildOverrides.remove(port);
    }
    m_overridesInFlight.clear();
    loadBuildChecks(m_detailPort);
    updateActionButtons();

    if (m_progressBar) {
        m_progressBar->setVisible(false);
    }
    if (m_operationPhaseLabel) {
        m_operationPhaseLabel->setText(summary);
    }

    if (m_outputLogEdit) {
        QString statusMsg;
        if (errorKind == OperationErrorKind::Cancelled) {
            statusMsg = QStringLiteral("\n[octoCRUX] Operation cancelled: %1\n").arg(summary);
        } else if (exitCode == 0) {
            statusMsg = QStringLiteral("\n[octoCRUX] Operation finished successfully: %1\n").arg(summary);
        } else {
            statusMsg = QStringLiteral("\n[octoCRUX] Operation failed (exit code %1, error: %2): %3\n")
                .arg(exitCode).arg(BuildLogAnalyzer::errorKindToString(errorKind)).arg(summary);
        }
        appendOutputPlain(m_outputLogEdit, statusMsg);
    }

    if (exitCode == 0) {
        refreshCatalog();
    }

    // Not conditioned on the exit code: what prt-get returns when one port of a
    // multi-port run fails is not verified yet.
    if (errorKind != OperationErrorKind::Cancelled && m_outputLogEdit && m_runOutputStart >= 0) {
        handleFootprintFailures(m_outputLogEdit->toPlainText().mid(m_runOutputStart));
    }
    m_runOutputStart = -1;

    if (errorKind == OperationErrorKind::Cancelled) {
        // The root tree ends about a second after the helper; look once it has.
        QTimer::singleShot(kLeftoverPollMs, this, [this]() { checkLeftoverWorkDirs(kLeftoverPollAttempts); });
    }
}

void MainWindow::setWorkRootOverride(const QString &workRoot) {
    m_workRootOverride = workRoot;
}

QString MainWindow::leftoverWorkRoot() const {
    if (!m_workRootOverride.isEmpty()) {
        return m_workRootOverride;
    }
    // Only a root that pkgmk.conf sets: the /var/tmp and /tmp fallbacks hold
    // other things, and nothing is deleted on a guess.
    return TransactionPlanner::configuredWorkDirectory(m_backend->rootPrefix()).value_or(QString());
}

void MainWindow::checkLeftoverWorkDirs(int attemptsLeft) {
    if (m_operationInProgress) {
        return; // something else started meanwhile
    }
    if (WorkDirCleanup::isPackageToolRunning()) {
        if (attemptsLeft > 0) {
            QTimer::singleShot(kLeftoverPollMs, this, [this, attemptsLeft]() { checkLeftoverWorkDirs(attemptsLeft - 1); });
        } else if (m_outputLogEdit) {
            appendOutputPlain(m_outputLogEdit, QStringLiteral(
                "[octoCRUX] A package tool (prt-get, pkgmk, ...) is still running %1 s after the cancel. "
                "Not checking for leftover work directories; check 'ps -ef'.")
                .arg(kLeftoverPollMs * kLeftoverPollAttempts / 1000));
        }
        return;
    }
    const QString root = leftoverWorkRoot();
    const QList<WorkDirCleanup::Leftover> leftovers = WorkDirCleanup::findLeftovers(root);
    if (leftovers.isEmpty()) {
        return;
    }

    const bool tmpfs = TransactionPlanner::inspectMount(root).isTmpfs;
    QStringList paths;
    QStringList lines;
    for (const WorkDirCleanup::Leftover &l : leftovers) {
        paths << l.path;
        lines << QStringLiteral("%1 (%2)").arg(l.path, QLocale::c().formattedDataSize(static_cast<qint64>(l.bytes)));
    }
    const QString where = tmpfs ? QStringLiteral(", on tmpfs: it uses RAM until removed or until reboot") : QString();
    if (m_outputLogEdit) {
        for (const QString &line : std::as_const(lines)) {
            appendOutputPlain(m_outputLogEdit, QStringLiteral(
                "[octoCRUX] A cancelled build left the work directory %1%2.").arg(line, where));
        }
    }

    QMessageBox box(QMessageBox::Question, QStringLiteral("Leftover work directories"),
        QStringLiteral("pkgmk removes its work directory when a build is interrupted, but a Cancel "
                       "stops it before it can%1. Left behind:\n\n  %2\n\n"
                       "Remove them? This runs, with elevated privileges:\n\n  %3")
            .arg(tmpfs ? QStringLiteral(" (these are on tmpfs, in RAM)") : QString(),
                 lines.join(QStringLiteral("\n  ")),
                 TransactionPreviewDialog::formatCommand(m_backend->removeWorkDirectoriesCommand(paths))),
        QMessageBox::NoButton, this);
    box.setObjectName(QStringLiteral("workDirCleanupMessage"));
    QPushButton *removeButton = box.addButton(QStringLiteral("Remove"), QMessageBox::AcceptRole);
    QPushButton *cancelButton = box.addButton(QMessageBox::Cancel);
    removeButton->setAutoDefault(false);
    box.setDefaultButton(cancelButton);
    box.exec();
    if (box.clickedButton() != removeButton) {
        if (m_outputLogEdit) {
            appendOutputPlain(m_outputLogEdit, QStringLiteral(
                "[octoCRUX] Leftover work directories kept. To remove them later: %1")
                .arg(TransactionPreviewDialog::formatCommand(m_backend->removeWorkDirectoriesCommand(paths))));
        }
        return;
    }
    if (m_outputLogEdit && m_tabWidget) {
        m_tabWidget->setCurrentWidget(m_outputLogEdit);
    }
    m_backend->removeWorkDirectories(root, paths);
}

void MainWindow::handleFootprintFailures(const QString &runLog) {
    QStringList newOnly;
    QStringList withMissing;
    for (const auto &f : BuildLogAnalyzer::extractFootprintFailures(runLog)) {
        QStringList &list = f.onlyNewEntries ? newOnly : withMissing;
        if (!list.contains(f.port)) {
            list << f.port;
        }
    }
    if (!withMissing.isEmpty() && m_outputLogEdit) {
        appendOutputPlain(m_outputLogEdit, QStringLiteral(
            "[octoCRUX] Footprint mismatch with MISSING files in: %1. Ignoring only new files (-in) "
            "cannot fix this. Review the build log; if the package is still correct, untick "
            "\"Verify footprint\" and choose \"Ignore all differences (-if)\" in its Information tab.")
            .arg(withMissing.join(QStringLiteral(", "))));
    }
    if (!newOnly.isEmpty()) {
        // After this slot returns: never start a new transaction from inside the
        // backend's finished signal.
        QTimer::singleShot(0, this, [this, newOnly]() { offerFootprintRetry(newOnly); });
    }
}

void MainWindow::offerFootprintRetry(const QStringList &ports) {
    if (m_operationInProgress) {
        return;
    }
    QMessageBox box(QMessageBox::Question,
                    QStringLiteral("Footprint check failed"),
                    QStringLiteral("The build of %1 failed the footprint check with new files only.\n\n"
                                   "Retry now, ignoring new files (pkgmk -in)? The commands are shown "
                                   "for confirmation before anything runs.")
                        .arg(ports.join(QStringLiteral(", "))),
                    QMessageBox::NoButton, this);
    box.setObjectName(QStringLiteral("footprintRetryMessage"));
    QPushButton *retry = box.addButton(QStringLiteral("Retry with -in"), QMessageBox::AcceptRole);
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() != retry) {
        return;
    }

    // Exceptions for these ports' next run (visible in their Information tab).
    QStringList toUpgrade;
    QStringList toInstall;
    for (const QString &port : ports) {
        BuildCheckOverrides o = m_buildOverrides.value(port);
        o.footprint = FootprintCheck::IgnoreNew;
        m_buildOverrides.insert(port, o);
        const auto info = m_backend->queryPort(port);
        if (info.has_value() && info->isInstalled()) {
            toUpgrade << port;   // a failed upgrade leaves the old version installed
        } else {
            toInstall << port;
        }
    }
    loadBuildChecks(m_detailPort);

    TransactionPlan plan = toUpgrade.isEmpty() ? TransactionPlan() : m_planner->planUpgrade(toUpgrade);
    if (!toInstall.isEmpty()) {
        const TransactionPlan installPlan = m_planner->planInstall(toInstall);
        if (toUpgrade.isEmpty()) {
            plan = installPlan;
        } else {
            plan.steps += installPlan.steps;
            plan.toInstall += installPlan.toInstall;
            plan.lockedSkipped += installPlan.lockedSkipped;
            plan.errors += installPlan.errors;
            plan.criticalWarnings += installPlan.criticalWarnings;
        }
    }
    if (!plan.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Retry Error"), plan.errors.join(QLatin1Char('\n')));
        return;
    }
    runTransaction(QStringLiteral("Retry %1 ignoring new files").arg(ports.join(QStringLiteral(", "))), plan);
}

void MainWindow::onRejectedConfigsDetected(const QStringList &rejectedFiles) {
    // The backend reports every pending file after each operation. Show the full
    // alert only for files that were not pending last time; otherwise one line.
    m_rejectedReportedThisRun = true;
    QStringList newFiles;
    for (const QString &file : rejectedFiles) {
        if (!m_knownRejected.contains(file)) {
            newFiles << file;
        }
    }
    m_knownRejected = QSet<QString>(rejectedFiles.cbegin(), rejectedFiles.cend());

    if (newFiles.isEmpty()) {
        if (m_outputLogEdit) {
            appendOutputPlain(m_outputLogEdit, QStringLiteral(
                "[octoCRUX] %1 rejected configuration file(s) still pending in /var/lib/pkg/rejected "
                "(unchanged). Review them with rejmerge.").arg(rejectedFiles.size()));
        }
        return;
    }

    // Escape each file name, then join with real line breaks: escaping after the
    // join turned the <br/> separators into visible text.
    QStringList escaped;
    for (const QString &file : std::as_const(newFiles)) {
        escaped << file.toHtmlEscaped();
    }
    const int alreadyPending = rejectedFiles.size() - newFiles.size();
    const QString tail = alreadyPending > 0
        ? QStringLiteral("<br/>(plus %1 already pending)").arg(alreadyPending)
        : QString();
    if (m_outputLogEdit) {
        appendOutputHtml(m_outputLogEdit, QStringLiteral(
            "<br/><span style='color:#ffa726; font-weight:bold;'>"
            "[octoCRUX ALERT] Rejected configuration file(s) found in /var/lib/pkg/rejected:<br/>"
            "%1%2</span><br/>"
        ).arg(escaped.join(QStringLiteral("<br/>")), tail));
    }
    statusBar()->showMessage(
        QStringLiteral("Warning: %1 new rejected config(s) in /var/lib/pkg/rejected!").arg(newFiles.size()),
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
        m_btnUpgradeAll->setEnabled(false);
        m_btnLock->setEnabled(false);
        m_btnRefresh->setEnabled(false);
        m_btnCancel->setEnabled(true);
        m_btnSettings->setEnabled(false);
        if (m_buildChecksBox) m_buildChecksBox->setEnabled(false);
        return;
    }
    m_btnSettings->setEnabled(m_settings != nullptr);
    if (m_buildChecksBox) m_buildChecksBox->setEnabled(!m_detailPort.isEmpty());

    // Independent of the selection: the system-wide upgrade.
    m_btnUpgradeAll->setEnabled(hasUpgradablePackages());

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
        m_btnUpgrade->setEnabled(port.installStatus == InstallStatus::Outdated
                                 && port.lockStatus == LockStatus::Unlocked);
        // prt-get 5.19.9 refuses to lock a package that is not installed ("Package
        // '<name>' not found", rc 255). Unlock stays available, so an older lock on an
        // uninstalled port (e.g. rust) can still be removed.
        const bool isLocked = port.lockStatus == LockStatus::Locked;
        m_btnLock->setEnabled(isLocked || isInst);
        m_btnLock->setText(isLocked ? QStringLiteral("Unlock") : QStringLiteral("Lock"));
        m_btnLock->setToolTip(isLocked || isInst
            ? QStringLiteral("Toggle lock status for selected package")
            : QStringLiteral("prt-get can only lock installed packages"));
    } else {
        m_btnInstall->setEnabled(false);
        m_btnRemove->setEnabled(false);
        m_btnRemove->setToolTip(QStringLiteral("Remove selected package"));
        m_btnUpgrade->setEnabled(false);
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
    m_syncInFlight = true;
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
    runTransaction(QStringLiteral("Confirm installation of %1").arg(portOpt->name), plan);
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
    runTransaction(QStringLiteral("Confirm removal of %1").arg(portOpt->name), plan);
}

void MainWindow::onUpgradeClicked() {
    if (m_operationInProgress) return;
    // Selected package only. The system-wide upgrade is onUpgradeAllClicked().
    auto portOpt = currentSelectedPort();
    if (!portOpt.has_value() || portOpt->installStatus != InstallStatus::Outdated) return;
    const TransactionPlan plan = m_planner->planUpgrade({portOpt->name});

    if (!plan.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Upgrade Error"), plan.errors.join(QLatin1Char('\n')));
        return;
    }
    runTransaction(QStringLiteral("Confirm upgrade of %1").arg(portOpt->name), plan);
}

void MainWindow::requestUpgradeAll() {
    if (m_operationInProgress) {
        return;
    }
    if (!hasUpgradablePackages()) {
        QMessageBox box(QMessageBox::Information, QStringLiteral("Upgrade All"),
                        QStringLiteral("There are no outdated, unlocked packages to upgrade."),
                        QMessageBox::Ok, this);
        box.setObjectName(QStringLiteral("nothingToUpgradeMessage"));
        box.exec();
        return;
    }
    onUpgradeAllClicked();
}

void MainWindow::onUpgradeAllClicked() {
    if (m_operationInProgress) return;
    const TransactionPlan plan = m_planner->planUpgrade();
    if (!plan.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Upgrade Error"), plan.errors.join(QLatin1Char('\n')));
        return;
    }
    runTransaction(QStringLiteral("Confirm upgrade of all outdated packages"), plan);
}

bool MainWindow::hasUpgradablePackages() const {
    for (const PortInfo &p : m_packageModel->ports()) {
        if (p.installStatus == InstallStatus::Outdated && p.lockStatus == LockStatus::Unlocked) {
            return true;
        }
    }
    return false;
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

void MainWindow::setSettings(std::shared_ptr<OctoCruxSettings> settings) {
    m_settings = std::move(settings);
    if (m_settings) {
        m_backend->setCancelGracePeriodMs(m_settings->cancelGraceSeconds() * 1000);
    }
    updateActionButtons();
}

void MainWindow::applyPrivilegePreference() {
    const std::optional<PrivilegeHelperType> preferred =
        m_settings ? m_settings->preferredHelper() : std::nullopt;
    const PrivilegeHelper::Resolution r = PrivilegeHelper::resolve(preferred);
    m_backend->setPrivilegeHelperType(r.type);
    updateBackendLabel();
    if (r.fellBack && m_outputLogEdit) {
        const QString used = PrivilegeHelper::helperBinary(r.type);
        appendOutputPlain(m_outputLogEdit, QStringLiteral(
            "[octoCRUX] Warning: the privilege helper chosen in Settings ('%1') is not in PATH; "
            "using automatic detection ('%2').")
            .arg(PrivilegeHelper::helperBinary(*preferred), used.isEmpty() ? QStringLiteral("none") : used));
    }
}

void MainWindow::onSettingsClicked() {
    if (m_operationInProgress || !m_settings) return;

    SettingsDialog dialog(m_settings->preferredHelper(), m_settings->cancelGraceSeconds(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_settings->setPreferredHelper(dialog.preferredHelper());
    m_settings->setCancelGraceSeconds(dialog.cancelGraceSeconds());
    m_settings->sync();
    m_backend->setCancelGracePeriodMs(m_settings->cancelGraceSeconds() * 1000);

    const PrivilegeHelperType before = m_backend->privilegeHelperType();
    applyPrivilegePreference();
    if (m_backend->privilegeHelperType() != before) {
        checkPrivilegeHelper(); // the same TTY / no-helper warning as at start
    }
}

bool MainWindow::runTransaction(const QString &title, TransactionPlan plan) {
    // Attach the pending exceptions to the steps that build a package.
    QStringList consumed;
    for (TransactionStep &step : plan.steps) {
        const bool builds = step.action == TransactionAction::Install
                         || step.action == TransactionAction::Upgrade
                         || step.action == TransactionAction::Reinstall;
        const auto it = m_buildOverrides.constFind(step.portName);
        if (builds && it != m_buildOverrides.constEnd()) {
            step.overrides = it.value();
            if (!consumed.contains(step.portName)) {
                consumed << step.portName;
            }
        }
    }

    if (!confirmTransaction(title, plan)) {
        return false; // nothing ran: the exceptions stay pending
    }

    m_overridesInFlight = consumed;
    if (m_outputLogEdit && m_tabWidget) {
        m_tabWidget->setCurrentWidget(m_outputLogEdit);
    }
    m_backend->executeTransaction(plan);
    return true;
}

BuildCheckOverrides MainWindow::pendingBuildOverrides(const QString &portName) const {
    return m_buildOverrides.value(portName);
}

void MainWindow::loadBuildChecks(const QString &portName) {
    if (!m_buildChecksBox) {
        return;
    }
    const BuildCheckOverrides o = m_buildOverrides.value(portName);
    const QList<QAbstractButton*> buttons = {m_chkFootprint, m_rbFootprintIgnoreNew, m_rbFootprintIgnoreAll,
                                             m_chkMd5, m_chkSignature};
    for (QAbstractButton *b : buttons) {
        b->blockSignals(true);
    }
    m_chkFootprint->setChecked(o.footprint == FootprintCheck::Full);
    m_rbFootprintIgnoreAll->setChecked(o.footprint == FootprintCheck::Ignore);
    m_rbFootprintIgnoreNew->setChecked(o.footprint != FootprintCheck::Ignore);
    m_chkMd5->setChecked(!o.ignoreMd5);
    m_chkSignature->setChecked(!o.ignoreSignature);
    for (QAbstractButton *b : buttons) {
        b->blockSignals(false);
    }
    m_rbFootprintIgnoreNew->setEnabled(o.footprint != FootprintCheck::Full);
    m_rbFootprintIgnoreAll->setEnabled(o.footprint != FootprintCheck::Full);
    m_lblSignatureRisk->setVisible(o.ignoreSignature);
    m_buildChecksBox->setEnabled(!portName.isEmpty() && !m_operationInProgress);
}

void MainWindow::onBuildChecksChanged() {
    if (m_detailPort.isEmpty()) {
        return;
    }
    BuildCheckOverrides o;
    if (!m_chkFootprint->isChecked()) {
        o.footprint = m_rbFootprintIgnoreAll->isChecked() ? FootprintCheck::Ignore : FootprintCheck::IgnoreNew;
    }
    o.ignoreMd5 = !m_chkMd5->isChecked();
    o.ignoreSignature = !m_chkSignature->isChecked();
    if (o.isDefault()) {
        m_buildOverrides.remove(m_detailPort);
    } else {
        m_buildOverrides.insert(m_detailPort, o);
    }
    loadBuildChecks(m_detailPort); // refresh dependent widgets (radios, risk label)
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
    actUpgrade->setEnabled(port.installStatus == InstallStatus::Outdated
                           && port.lockStatus == LockStatus::Unlocked && !m_operationInProgress);
    connect(actUpgrade, &QAction::triggered, this, &MainWindow::onUpgradeClicked);

    menu.addSeparator();

    const bool isLocked = (port.lockStatus == LockStatus::Locked);
    QAction *actLock = menu.addAction(isLocked ? QStringLiteral("Unlock '%1'").arg(port.name) : QStringLiteral("Lock '%1'").arg(port.name));
    actLock->setEnabled(!m_operationInProgress && (isLocked || port.isInstalled()));
    if (!isLocked && !port.isInstalled()) {
        actLock->setToolTip(QStringLiteral("prt-get can only lock installed packages"));
    }
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
    m_detailPort = port.name;
    loadBuildChecks(port.name);

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
    m_detailPort.clear();
    loadBuildChecks(QString());
    m_infoBrowser->setHtml(QStringLiteral("<p style='color:gray; font-style:italic;'>Select a port from the list above to view details.</p>"));
    m_filesEdit->setPlainText(QStringLiteral("No package selected."));
    m_pkgfileEdit->setPlainText(QStringLiteral("# No package selected."));
    m_footprintEdit->setPlainText(QStringLiteral("# No package selected."));
    m_readmeBrowser->setPlainText(QStringLiteral("No package selected."));
}

} // namespace OctoCRUX
