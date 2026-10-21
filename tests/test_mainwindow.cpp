#include <QTest>
#include <QMessageBox>
#include <QApplication>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTextBlock>
#include <QLabel>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QPointer>
#include <QToolButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
#include <functional>
#include <QFile>
#include <QTextStream>
#include <QTableView>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QTextBrowser>

#include "ui/mainwindow.h"
#include "ui/transaction_preview_dialog.h"
#include "core/printf_parser.h"

using namespace OctoCRUX;

class MockCommandRunner : public ICommandRunner {
public:
    void registerResponse(const QString &program, const QStringList &arguments, const CommandResult &result) {
        m_responses.insert(program + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')), result);
    }

    CommandResult run(const QString &program, const QStringList &arguments, int timeoutMs) override {
        Q_UNUSED(timeoutMs);
        const QString key = program + QLatin1Char(' ') + arguments.join(QLatin1Char(' '));
        if (m_responses.contains(key)) {
            return m_responses.value(key);
        }
        return {127, QString(), QStringLiteral("Command not mocked: ") + key};
    }

private:
    QMap<QString, CommandResult> m_responses;
};

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testInitialPopulationAndCounts();
    void testStatusFilterButtons();
    void testCollectionFilterCombo();
    void testSearchBoxFiltering();
    void testPortSelectionUpdatesDetailTabs();
    void testLiveExecutionStreaming();
    void testStatusBarLabelsDoNotOverlap();
    void testRejectedConfigsAlertIsReadable();
    void testRejectedAlertListsOnlyNewFiles();
    void testClickInColouredLineDoesNotColourNewLines();
    void testCancelledOperationIsNotReportedAsFailure();
    void testTransientOutputReplacesLastLine();
    void testPreviewDescribe();
    void testUpgradeButtonsWhenEveryOutdatedIsLocked();
    void testRefreshDoesNotLeaveStaleButtons();
    void testUpgradePreviewGatesExecution();
    void testInstallPreviewRejectStartsNoProcess();
    void testRequestUpgradeAllFromTheNotifier();
    void testSyncConfirmRejectStartsNoProcess();
    void testSuccessfulSyncRecordsLastSyncForTheNotifier();
    void testBuildChecksAreKeptPerPortUntilARun();
    void testBuildChecksReachCommandAndResetAfterRun();
    void testLockAndUnlockStartTheHelper();
    void testFootprintRetryOfferedForNewOnlyFailures();
    void testFootprintMissingIsReportedNotOffered();
    void testCloseRefusedDuringOperation();

private:
    QString readFile(const QString &filePath);

    // Fake prt-get/ports placed first in PATH: every process start appends its
    // argv to the probe log, so "no process started" means "probe log absent".
    bool installFakeTools();
    void removeFakeTools();
    QStringList probeLines() const;
    QToolButton* toolButton(const QString &text) const;
    // Searches for name and selects the row whose Name column equals it exactly.
    bool selectPort(const QString &name);
    // Runs fn on the next modal widget (dialog or message box) once it is shown.
    // Returns the polling timer so a test that expects no modal can cancel it.
    QPointer<QTimer> onNextModal(const std::function<void(QWidget *)> &fn);

    std::unique_ptr<QTemporaryDir> m_fakeBin;
    QByteArray m_savedPath;
    QString m_probeLog;

    QString m_sourceDir;
    std::shared_ptr<MockCommandRunner> m_mockRunner;
    std::shared_ptr<CruxAdapter> m_adapter;
    std::unique_ptr<MainWindow> m_window;
};

QString TestMainWindow::readFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QTextStream(&file).readAll();
}

void TestMainWindow::initTestCase() {
    m_sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);
    m_mockRunner = std::make_shared<MockCommandRunner>();

    // 1. Production 11-token printf fixture
    const QString printfOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-production-11token.out"));
    QVERIFY(!printfOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("printf"), QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)},
        {0, printfOutput, QString()}
    );

    // 2. Mock pkginfo -l gtk3
    const QString pkginfoLOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-pkginfo-l.out"));
    QVERIFY(!pkginfoLOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("pkginfo"),
        {QStringLiteral("-l"), QStringLiteral("gtk3")},
        {0, pkginfoLOutput, QString()}
    );

    m_adapter = std::make_shared<CruxAdapter>(m_mockRunner);
    m_adapter->setRootPrefix(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/files"));

    m_window = std::make_unique<MainWindow>(m_adapter);
}

void TestMainWindow::testInitialPopulationAndCounts() {
    PackageModel *model = m_window->packageModel();
    PackageFilterProxyModel *proxy = m_window->proxyModel();
    QTableView *view = m_window->tableView();

    QVERIFY(model != nullptr);
    QVERIFY(proxy != nullptr);
    QVERIFY(view != nullptr);

    // Should contain all 2,307 ports
    QCOMPARE(model->rowCount(), 2307);
    QCOMPARE(proxy->rowCount(), 2307);
}

void TestMainWindow::testStatusFilterButtons() {
    PackageFilterProxyModel *proxy = m_window->proxyModel();

    // 1. Filter Installed
    m_window->setFilterStatus(StatusFilter::Installed);
    // At least 49 installed ports on test machine
    QVERIFY(proxy->rowCount() > 0 && proxy->rowCount() < 2307);

    // 2. Filter Outdated
    m_window->setFilterStatus(StatusFilter::Outdated);
    // Exactly 4 outdated ports (clang, compiler-rt, lld, llvm)
    QCOMPARE(proxy->rowCount(), 4);

    // 3. Filter Locked
    m_window->setFilterStatus(StatusFilter::Locked);
    // Exactly 5 locked ports (clang, compiler-rt, lld, llvm, rust)
    QCOMPARE(proxy->rowCount(), 5);

    // 4. Reset to All
    m_window->setFilterStatus(StatusFilter::All);
    QCOMPARE(proxy->rowCount(), 2307);
}

void TestMainWindow::testCollectionFilterCombo() {
    PackageFilterProxyModel *proxy = m_window->proxyModel();

    m_window->setFilterCollection(QStringLiteral("d77crux"));
    QVERIFY(proxy->rowCount() > 0 && proxy->rowCount() < 2307);

    // Reset to all collections
    m_window->setFilterCollection(QString());
    QCOMPARE(proxy->rowCount(), 2307);
}

void TestMainWindow::testSearchBoxFiltering() {
    PackageFilterProxyModel *proxy = m_window->proxyModel();

    // Search for a2ps by exact name
    m_window->setSearchText(QStringLiteral("a2ps"));
    QVERIFY(proxy->rowCount() >= 1);

    // Search for non-existent package
    m_window->setSearchText(QStringLiteral("nonexistent_package_xyz_999"));
    QCOMPARE(proxy->rowCount(), 0);

    // Clear search
    m_window->setSearchText(QString());
    QCOMPARE(proxy->rowCount(), 2307);
}

void TestMainWindow::testPortSelectionUpdatesDetailTabs() {
    PackageFilterProxyModel *proxy = m_window->proxyModel();
    QTableView *view = m_window->tableView();
    QTabWidget *tabs = m_window->detailTabs();

    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 6);

    // Filter to find "a2ps"
    m_window->setSearchText(QStringLiteral("a2ps"));
    QVERIFY(proxy->rowCount() >= 1);

    // Select row 0 in table view
    view->selectRow(0);

    // Get detail widgets from tabs
    auto *infoBrowser = tabs->widget(0)->findChild<QTextBrowser*>();
    auto *pkgfileEdit = qobject_cast<QPlainTextEdit*>(tabs->widget(2));
    auto *footprintEdit = qobject_cast<QPlainTextEdit*>(tabs->widget(3));

    QVERIFY(infoBrowser != nullptr);
    QVERIFY(pkgfileEdit != nullptr);
    QVERIFY(footprintEdit != nullptr);

    // Verify info browser contains a2ps metadata
    QVERIFY(infoBrowser->toHtml().contains(QStringLiteral("a2ps")));
    QVERIFY(infoBrowser->toHtml().contains(QStringLiteral("ASCII to Postscript converter")));

    // Verify Pkgfile tab loaded the real Pkgfile
    QVERIFY(pkgfileEdit->toPlainText().contains(QStringLiteral("name=a2ps")));

    // Verify Footprint tab loaded the real .footprint
    QVERIFY(footprintEdit->toPlainText().contains(QStringLiteral("usr/bin/a2ps")));

    // Verify Output tab exists
    auto *outputEdit = m_window->outputLogEdit();
    QVERIFY(outputEdit != nullptr);
    QCOMPARE(tabs->tabText(5), QStringLiteral("Output"));

    // Clear search
    m_window->setSearchText(QString());
}

void TestMainWindow::testLiveExecutionStreaming() {
    QPlainTextEdit *logEdit = m_window->outputLogEdit();
    QVERIFY(logEdit != nullptr);

    const int initialLength = logEdit->toPlainText().length();

    // Emit live backend output
    emit m_adapter->outputReceived(QStringLiteral("======> Building 'test-stream'"), false);
    emit m_adapter->outputReceived(QStringLiteral("gcc -O2 -pipe main.c -o main"), false);
    emit m_adapter->outputReceived(QStringLiteral("pkgmk: warning: unused variable"), true);

    QVERIFY(logEdit->toPlainText().length() > initialLength);
    QVERIFY(logEdit->toPlainText().contains(QStringLiteral("Building 'test-stream'")));
    QVERIFY(logEdit->toPlainText().contains(QStringLiteral("gcc -O2 -pipe main.c -o main")));
    QVERIFY(logEdit->toPlainText().contains(QStringLiteral("pkgmk: warning: unused variable")));
}

void TestMainWindow::testCloseRefusedDuringOperation() {
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));

    // An operation starts: closing must be refused with an explanation.
    emit m_adapter->progressChanged(1, 1, QStringLiteral("Installing test-close"));
    QVERIFY(!m_window->close());
    QVERIFY(m_window->isVisible());
    QVERIFY(m_window->closeRefusedMessage() != nullptr);
    QTRY_VERIFY(m_window->closeRefusedMessage()->isVisible());
    QVERIFY(m_window->closeRefusedMessage()->text().contains(QStringLiteral("SIGKILL")));
    m_window->closeRefusedMessage()->accept();

    // Once the operation has finished, the window closes normally.
    emit m_adapter->operationFinished(1, OperationErrorKind::GeneralError, QStringLiteral("test-close done"));
    QVERIFY(m_window->close());
    QVERIFY(!m_window->isVisible());
}

bool TestMainWindow::installFakeTools() {
    m_savedPath = qgetenv("PATH");
    m_fakeBin = std::make_unique<QTemporaryDir>();
    if (!m_fakeBin->isValid()) {
        return false;
    }
    const QString script = m_sourceDir + QStringLiteral("/tests/scripts/fake_slow_prtget.sh");
    for (const QString &name : {QStringLiteral("prt-get"), QStringLiteral("ports")}) {
        if (!QFile::link(script, m_fakeBin->filePath(name))) {
            return false;
        }
    }
    m_probeLog = m_fakeBin->filePath(QStringLiteral("probe.log"));
    qputenv("PATH", m_fakeBin->path().toLocal8Bit() + ':' + m_savedPath);
    qputenv("OCTOCRUX_PROBE_LOG", m_probeLog.toLocal8Bit());
    qputenv("FAKE_PRTGET_DURATION", "0");
    m_adapter->setPrivilegeHelperType(PrivilegeHelperType::None);

    // Safety guard: never click a mutating button unless the fakes win the lookup.
    return QStandardPaths::findExecutable(QStringLiteral("prt-get")) == m_fakeBin->filePath(QStringLiteral("prt-get"))
        && QStandardPaths::findExecutable(QStringLiteral("ports")) == m_fakeBin->filePath(QStringLiteral("ports"));
}

void TestMainWindow::removeFakeTools() {
    if (!m_fakeBin) {
        return;
    }
    qputenv("PATH", m_savedPath);
    qunsetenv("OCTOCRUX_PROBE_LOG");
    qunsetenv("FAKE_PRTGET_DURATION");
    m_fakeBin.reset();
}

QStringList TestMainWindow::probeLines() const {
    QFile f(m_probeLog);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QToolButton* TestMainWindow::toolButton(const QString &text) const {
    for (QToolButton *b : m_window->findChildren<QToolButton*>()) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}

bool TestMainWindow::selectPort(const QString &name) {
    m_window->setSearchText(name);
    QAbstractItemModel *proxy = m_window->tableView()->model();
    for (int row = 0; row < proxy->rowCount(); ++row) {
        if (proxy->index(row, 0).data().toString() == name) {
            m_window->tableView()->selectRow(row);
            return true;
        }
    }
    return false;
}

QPointer<QTimer> TestMainWindow::onNextModal(const std::function<void(QWidget *)> &fn) {
    auto *timer = new QTimer(this);
    auto deadline = std::make_shared<int>(500); // 500 x 10 ms
    connect(timer, &QTimer::timeout, this, [timer, fn, deadline]() {
        QWidget *modal = QApplication::activeModalWidget();
        if (modal && modal->isVisible()) {
            timer->stop();
            timer->deleteLater();
            fn(modal);
        } else if (--(*deadline) <= 0) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(10);
    return timer;
}

void TestMainWindow::testStatusBarLabelsDoNotOverlap() {
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    m_window->setSearchText(QString());

    QLabel *summary = nullptr;
    QLabel *filter = nullptr;
    for (QLabel *l : m_window->statusBar()->findChildren<QLabel*>()) {
        if (l->text().startsWith(QStringLiteral("Total: "))) summary = l;
        if (l->text().startsWith(QStringLiteral("Showing all "))) filter = l;
    }
    QVERIFY(summary && filter);
    QTRY_VERIFY(summary->isVisible() && filter->isVisible());
    // Both labels must be laid out by the status bar, side by side.
    QVERIFY2(!summary->geometry().intersects(filter->geometry()),
             qPrintable(QStringLiteral("summary %1,%2 %3x%4 overlaps filter %5,%6 %7x%8")
                 .arg(summary->x()).arg(summary->y()).arg(summary->width()).arg(summary->height())
                 .arg(filter->x()).arg(filter->y()).arg(filter->width()).arg(filter->height())));
}

void TestMainWindow::testRejectedConfigsAlertIsReadable() {
    QPlainTextEdit *log = m_window->outputLogEdit();
    const int before = log->toPlainText().size();
    emit m_adapter->rejectedConfigsDetected({QStringLiteral("/var/lib/pkg/rejected/etc/passwd"),
                                             QStringLiteral("/var/lib/pkg/rejected/etc/a&b<c>")});
    const QString added = log->toPlainText().mid(before);
    QVERIFY2(!added.contains(QStringLiteral("<br/>")), qPrintable(added));
    QVERIFY(added.contains(QStringLiteral("/var/lib/pkg/rejected/etc/passwd")));
    // File names are shown literally, not interpreted as HTML.
    QVERIFY2(added.contains(QStringLiteral("/var/lib/pkg/rejected/etc/a&b<c>")), qPrintable(added));
}

void TestMainWindow::testRejectedAlertListsOnlyNewFiles() {
    QPlainTextEdit *log = m_window->outputLogEdit();
    const QString r1 = QStringLiteral("/var/lib/pkg/rejected/etc/passwd");
    const QString r2 = QStringLiteral("/var/lib/pkg/rejected/etc/fstab");
    const QString r3 = QStringLiteral("/var/lib/pkg/rejected/etc/shadow");
    auto addedBy = [&](const QStringList &files) {
        const int before = log->toPlainText().size();
        emit m_adapter->rejectedConfigsDetected(files);
        return log->toPlainText().mid(before);
    };

    addedBy({r1, r2});                       // establishes the pending set
    // Same files again (the owner saw the same 17 after every operation): one line.
    QString added = addedBy({r1, r2});
    QVERIFY2(!added.contains(QStringLiteral("ALERT")), qPrintable(added));
    QVERIFY(added.contains(QStringLiteral("2 rejected configuration file(s) still pending")));
    QVERIFY(!added.contains(r1));
    // One new file: the alert lists only it.
    added = addedBy({r1, r2, r3});
    QVERIFY(added.contains(QStringLiteral("[octoCRUX ALERT]")));
    QVERIFY(added.contains(r3));
    QVERIFY2(!added.contains(r1) && !added.contains(r2), qPrintable(added));
    QVERIFY(added.contains(QStringLiteral("(plus 2 already pending)")));

    // An operation that ran and reported nothing: the directory is empty now, so
    // a later rejection of the same file is new again.
    emit m_adapter->progressChanged(1, 1, QStringLiteral("Locking criu"));
    emit m_adapter->operationFinished(0, OperationErrorKind::None, QStringLiteral("ok"));
    added = addedBy({r1});
    QVERIFY(added.contains(QStringLiteral("[octoCRUX ALERT]")));
    QVERIFY(added.contains(r1));
}

void TestMainWindow::testClickInColouredLineDoesNotColourNewLines() {
    // Owner, 2026-10-21: after clicking inside the orange rejected-config alert, every
    // new plain line came out orange and bold. Reproduced by placing the text cursor
    // inside the alert, as the click does.
    QPlainTextEdit *log = m_window->outputLogEdit();
    emit m_adapter->rejectedConfigsDetected({QStringLiteral("/var/lib/pkg/rejected/etc/grub.d/30_uefi-firmware")});
    const int at = log->toPlainText().lastIndexOf(QStringLiteral("30_uefi-firmware"));
    QVERIFY(at > 0);
    QTextCursor inside(log->document());
    inside.setPosition(at + 3);
    log->setTextCursor(inside);
    QCOMPARE(log->textCursor().charFormat().foreground().color(), QColor(QStringLiteral("#ffa726")));

    auto lastLineFormat = [log]() {
        const QTextBlock block = log->document()->lastBlock();
        return block.begin().fragment().charFormat();
    };
    emit m_adapter->outputReceived(QStringLiteral("[octoCRUX] Locking zstd"), false);
    QCOMPARE(log->document()->lastBlock().text(), QStringLiteral("[octoCRUX] Locking zstd"));
    QTextCharFormat f = lastLineFormat();
    QVERIFY2(f.foreground().color() != QColor(QStringLiteral("#ffa726")), qPrintable(f.foreground().color().name()));
    QVERIFY(f.fontWeight() != QFont::Bold);

    // The other plain-text paths: the operation summary and a '\r' progress line.
    emit m_adapter->progressChanged(1, 1, QStringLiteral("Locking zstd"));
    emit m_adapter->operationFinished(0, OperationErrorKind::None, QStringLiteral("ok"));
    QTextBlock b = log->document()->lastBlock();
    while (b.isValid() && b.text().isEmpty()) b = b.previous();
    QVERIFY(b.text().contains(QStringLiteral("finished successfully")));
    QVERIFY(b.begin().fragment().charFormat().foreground().color() != QColor(QStringLiteral("#ffa726")));
    log->setTextCursor(inside);
    emit m_adapter->transientOutputReceived(QStringLiteral(" 10 548.0M"));
    QVERIFY(lastLineFormat().foreground().color() != QColor(QStringLiteral("#ffa726")));
    emit m_adapter->outputReceived(QStringLiteral("100 548.0M"), false);
    QVERIFY(lastLineFormat().foreground().color() != QColor(QStringLiteral("#ffa726")));
}

void TestMainWindow::testCancelledOperationIsNotReportedAsFailure() {
    QPlainTextEdit *log = m_window->outputLogEdit();
    const int before = log->toPlainText().size();
    // As observed on 2026-10-20: qt-sudo ended by SIGTERM after Cancel.
    emit m_adapter->operationFinished(15, OperationErrorKind::Cancelled,
        QStringLiteral("Operation was cancelled by user (helper terminated by a signal)"));
    const QString added = log->toPlainText().mid(before);
    QVERIFY2(added.contains(QStringLiteral("[octoCRUX] Operation cancelled: Operation was cancelled by user")), qPrintable(added));
    QVERIFY2(!added.contains(QStringLiteral("failed")), qPrintable(added));
    QVERIFY(!added.contains(QStringLiteral("General error")));
}

void TestMainWindow::testTransientOutputReplacesLastLine() {
    QPlainTextEdit *log = m_window->outputLogEdit();
    emit m_adapter->outputReceived(QStringLiteral("=======> Downloading 'x'."), false);
    const int blocks = log->blockCount();
    emit m_adapter->transientOutputReceived(QStringLiteral(" 10 548.0M"));
    emit m_adapter->transientOutputReceived(QStringLiteral(" 20 548.0M"));
    QCOMPARE(log->blockCount(), blocks + 1);
    QCOMPARE(log->document()->lastBlock().text(), QStringLiteral(" 20 548.0M"));
    // The completed line replaces the progress line in place.
    emit m_adapter->outputReceived(QStringLiteral("100 548.0M"), true);
    QCOMPARE(log->blockCount(), blocks + 1);
    QCOMPARE(log->document()->lastBlock().text(), QStringLiteral("100 548.0M"));
    // The next line is appended normally.
    emit m_adapter->outputReceived(QStringLiteral("next"), false);
    QCOMPARE(log->blockCount(), blocks + 2);
    QVERIFY(!log->toPlainText().contains(QStringLiteral(" 10 548.0M")));
}

void TestMainWindow::testPreviewDescribe() {
    TransactionPlan plan;
    TransactionStep up;
    up.action = TransactionAction::Upgrade;
    up.portName = QStringLiteral("elfutils");
    TransactionStep in;
    in.action = TransactionAction::Install;
    in.portName = QStringLiteral("a2ps");
    in.hasPostInstall = true;
    plan.steps = {up, in};
    plan.lockedSkipped = {QStringLiteral("llvm")};
    plan.workMountInfo.mountPoint = QStringLiteral("/usr/ports/work");
    plan.workMountInfo.isTmpfs = true;
    plan.workMountInfo.availableBytes = 10ull * 1024 * 1024 * 1024;
    plan.workMountInfo.totalBytes = 10ull * 1024 * 1024 * 1024;

    const QList<QStringList> commands = {
        {QStringLiteral("qt-sudo"), QStringLiteral("-d"), QStringLiteral("prt-get"), QStringLiteral("update"), QStringLiteral("elfutils")},
        {QStringLiteral("qt-sudo"), QStringLiteral("-d"), QStringLiteral("prt-get"), QStringLiteral("depinst"), QStringLiteral("a2ps")},
    };
    const QString text = TransactionPreviewDialog::describe(plan, commands, PrivilegeHelperType::QtSudo);
    QVERIFY(text.contains(QStringLiteral("1. qt-sudo -d prt-get update elfutils")));
    QVERIFY(text.contains(QStringLiteral("2. qt-sudo -d prt-get depinst a2ps")));
    QVERIFY(text.contains(QStringLiteral("2 elevated invocation(s) through qt-sudo")));
    QVERIFY(text.contains(QStringLiteral("To install (1):\n  a2ps")));
    QVERIFY(text.contains(QStringLiteral("To update (1):\n  elfutils")));
    QVERIFY(text.contains(QStringLiteral("Skipped because locked (1):\n  llvm")));
    QVERIFY(text.contains(QStringLiteral("a2ps (post-install)")));
    QVERIFY(text.contains(QStringLiteral("tmpfs (/usr/ports/work): 10.0 GiB free")));

    QCOMPARE(TransactionPreviewDialog::formatCommand({QStringLiteral("su"), QStringLiteral("-c"), QStringLiteral("a b'c")}),
             QStringLiteral("su -c 'a b'\\''c'"));

    TransactionPreviewDialog dialog(QStringLiteral("t"), plan, commands, PrivilegeHelperType::QtSudo);
    QVERIFY(dialog.cancelButton()->isDefault());
    QVERIFY(!dialog.applyButton()->isDefault());
    QVERIFY(dialog.signatureWarning() == nullptr);
    QVERIFY(!text.contains(QStringLiteral("Build check exceptions")));

    // Exceptions are listed per package; disabling the signature check adds the red banner.
    plan.steps[0].overrides.footprint = FootprintCheck::IgnoreNew;
    plan.steps[1].overrides.ignoreSignature = true;
    const QString withExceptions = TransactionPreviewDialog::describe(plan, commands, PrivilegeHelperType::QtSudo);
    QVERIFY2(withExceptions.contains(QStringLiteral("Build check exceptions for this run (2):")), qPrintable(withExceptions));
    QVERIFY(withExceptions.contains(QStringLiteral("elfutils: ignore new footprint entries (-in)")));
    QVERIFY(withExceptions.contains(QStringLiteral("a2ps: skip signature check (-is)")));
    QVERIFY(withExceptions.contains(QStringLiteral("SIGNATURE VERIFICATION DISABLED for a2ps")));
    TransactionPreviewDialog warned(QStringLiteral("t"), plan, commands, PrivilegeHelperType::QtSudo);
    QVERIFY(warned.signatureWarning() != nullptr);
    QVERIFY(warned.signatureWarning()->text().contains(QStringLiteral("a2ps")));
    QVERIFY(warned.signatureWarning()->styleSheet().contains(QStringLiteral("#c62828")));
    QVERIFY(warned.cancelButton()->isDefault());
}

void TestMainWindow::testUpgradeButtonsWhenEveryOutdatedIsLocked() {
    // Real fixture: every outdated port (clang, compiler-rt, lld, llvm) is locked,
    // so there is nothing to upgrade and neither button may start anything.
    QVERIFY2(installFakeTools(), "fake prt-get/ports not first in PATH; refusing to click");
    auto restore = qScopeGuard([this]() { removeFakeTools(); });
    m_window->setSearchText(QString());
    m_window->tableView()->clearSelection();

    QToolButton *upgrade = toolButton(QStringLiteral("Upgrade"));
    QToolButton *upgradeAll = toolButton(QStringLiteral("Upgrade All"));
    QVERIFY(upgrade && upgradeAll);
    QVERIFY(!upgrade->isEnabled());      // no selection
    QVERIFY(!upgradeAll->isEnabled());   // nothing outdated and unlocked

    // Selecting the outdated but locked llvm does not enable Upgrade either.
    QVERIFY(selectPort(QStringLiteral("llvm")));
    QVERIFY(!upgrade->isEnabled());
    QVERIFY(!upgradeAll->isEnabled());
    m_window->setSearchText(QString());
    QVERIFY(probeLines().isEmpty());
}

void TestMainWindow::testUpgradePreviewGatesExecution() {
    // Same real fixture, with gtk3 (installed, unlocked) marked outdated in memory
    // (column %i: yes -> diff) so that Upgrade has something to run. Only that one
    // field differs from the capture. (Locks also come from the fixture's
    // prt-get.locker, so unlocking llvm via %l alone would not work.)
    const QString printfPath = m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-production-11token.out");
    const QString original = readFile(printfPath);
    QStringList lines = original.split(QLatin1Char('\n'));
    bool changed = false;
    for (QString &line : lines) {
        QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() == 11 && f.at(0) == QLatin1String("gtk3") && f.at(3) == QLatin1String("yes")) {
            f[3] = QStringLiteral("diff");
            line = f.join(QLatin1Char('\t'));
            changed = true;
        }
    }
    QVERIFY(changed);
    const QStringList printfArgs = {QStringLiteral("printf"), QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)};
    m_mockRunner->registerResponse(QStringLiteral("prt-get"), printfArgs, {0, lines.join(QLatin1Char('\n')), QString()});
    m_adapter->invalidateCache();
    m_window->refreshCatalog();
    auto restore = qScopeGuard([&]() {
        removeFakeTools();
        m_mockRunner->registerResponse(QStringLiteral("prt-get"), printfArgs, {0, original, QString()});
        m_adapter->invalidateCache();
        m_window->refreshCatalog();
    });

    QVERIFY2(installFakeTools(), "fake prt-get/ports not first in PATH; refusing to click");
    m_window->setSearchText(QString());
    m_window->tableView()->clearSelection();
    QSignalSpy spyFinished(m_adapter.get(), &ICruxBackend::operationFinished);
    QToolButton *upgrade = toolButton(QStringLiteral("Upgrade All"));
    QVERIFY(upgrade && upgrade->isEnabled());
    QVERIFY(!toolButton(QStringLiteral("Upgrade"))->isEnabled()); // nothing selected

    // 1. Reject: the preview is shown and no process starts.
    QString summary;
    onNextModal([&](QWidget *w) {
        if (auto *dlg = qobject_cast<TransactionPreviewDialog*>(w)) {
            summary = dlg->summaryText();
            dlg->reject();
        } else if (auto *d = qobject_cast<QDialog*>(w)) {
            d->reject();
        }
    });
    upgrade->click();
    QVERIFY2(summary.contains(QStringLiteral("1. prt-get update gtk3")), qPrintable(summary));
    QVERIFY(summary.contains(QStringLiteral("Skipped because locked (4)")));
    QTest::qWait(200);
    QVERIFY(probeLines().isEmpty());
    QCOMPARE(spyFinished.count(), 0);

    // 2. Apply: exactly the previewed command runs, once.
    onNextModal([&](QWidget *w) {
        if (auto *dlg = qobject_cast<TransactionPreviewDialog*>(w)) {
            dlg->applyButton()->click();
        } else if (auto *d = qobject_cast<QDialog*>(w)) {
            d->reject();
        }
    });
    upgrade->click();
    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyFinished.first().at(0).toInt(), 0);
    QCOMPARE(probeLines(), QStringList{QStringLiteral("update gtk3")});
    QTRY_VERIFY(upgrade->isEnabled()); // operation finished, buttons re-evaluated

    // 3. Upgrade of the selected package only: enabled for gtk3 (outdated,
    //    unlocked), disabled for glibc (up to date); Upgrade All stays enabled.
    QToolButton *upgradeOne = toolButton(QStringLiteral("Upgrade"));
    QVERIFY(selectPort(QStringLiteral("glibc")));
    QVERIFY(!upgradeOne->isEnabled());
    QVERIFY(upgrade->isEnabled());
    QVERIFY(selectPort(QStringLiteral("gtk3")));
    QVERIFY(upgradeOne->isEnabled());
    QString single;
    onNextModal([&](QWidget *w) {
        if (auto *dlg = qobject_cast<TransactionPreviewDialog*>(w)) {
            single = dlg->summaryText();
            dlg->reject();
        } else if (auto *d = qobject_cast<QDialog*>(w)) {
            d->reject();
        }
    });
    upgradeOne->click();
    QVERIFY2(single.contains(QStringLiteral("1. prt-get update gtk3")), qPrintable(single));
    QVERIFY(!single.contains(QStringLiteral("Skipped because locked")));
    m_window->setSearchText(QString());
    QCOMPARE(probeLines().size(), 1);
}

void TestMainWindow::testRefreshDoesNotLeaveStaleButtons() {
    QVERIFY(selectPort(QStringLiteral("gtk3")));
    QVERIFY(toolButton(QStringLiteral("Remove"))->isEnabled()); // installed, not core
    m_window->refreshCatalog();
    // The reload cleared the selection: nothing selected, nothing to act on.
    QVERIFY(!m_window->tableView()->selectionModel()->hasSelection());
    QVERIFY(!toolButton(QStringLiteral("Remove"))->isEnabled());
    QVERIFY(!toolButton(QStringLiteral("Install"))->isEnabled());
    QVERIFY(!toolButton(QStringLiteral("Upgrade"))->isEnabled());
    QVERIFY(!toolButton(QStringLiteral("Lock"))->isEnabled());
    m_window->setSearchText(QString());
}

void TestMainWindow::testRequestUpgradeAllFromTheNotifier() {
    // `octocrux --upgrade-all` (the notifier's "Upgrade All...") calls requestUpgradeAll().
    QVERIFY2(installFakeTools(), "fake prt-get/ports not first in PATH; refusing to continue");
    auto restore = qScopeGuard([this]() { removeFakeTools(); });

    // 1. Real fixture: every outdated port is locked, so it only says so.
    QString seen;
    onNextModal([&](QWidget *w) {
        seen = w->objectName();
        if (auto *d = qobject_cast<QDialog*>(w)) d->reject();
    });
    m_window->requestUpgradeAll();
    QCOMPARE(seen, QStringLiteral("nothingToUpgradeMessage"));

    // 2. gtk3 marked outdated in memory (one field of the fixture): the preview
    //    opens on "prt-get update gtk3"; rejecting it starts nothing.
    const QString printfPath = m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-production-11token.out");
    const QString original = readFile(printfPath);
    QStringList lines = original.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() == 11 && f.at(0) == QLatin1String("gtk3")) {
            f[3] = QStringLiteral("diff");
            line = f.join(QLatin1Char('\t'));
        }
    }
    const QStringList printfArgs = {QStringLiteral("printf"), QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)};
    m_mockRunner->registerResponse(QStringLiteral("prt-get"), printfArgs, {0, lines.join(QLatin1Char('\n')), QString()});
    auto restoreCatalog = qScopeGuard([&]() {
        m_mockRunner->registerResponse(QStringLiteral("prt-get"), printfArgs, {0, original, QString()});
        m_adapter->invalidateCache();
        m_window->refreshCatalog();
    });
    m_adapter->invalidateCache();
    m_window->refreshCatalog();

    QString summary;
    onNextModal([&](QWidget *w) {
        if (auto *dlg = qobject_cast<TransactionPreviewDialog*>(w)) summary = dlg->summaryText();
        if (auto *d = qobject_cast<QDialog*>(w)) d->reject();
    });
    m_window->requestUpgradeAll();
    QVERIFY2(summary.contains(QStringLiteral("1. prt-get update gtk3")), qPrintable(summary));
    QTest::qWait(200);
    QVERIFY(probeLines().isEmpty());
}

void TestMainWindow::testInstallPreviewRejectStartsNoProcess() {
    auto restore = qScopeGuard([this]() { removeFakeTools(); });
    QVERIFY2(installFakeTools(), "fake prt-get/ports not first in PATH; refusing to click");
    m_window->setSearchText(QStringLiteral("a2ps"));
    QVERIFY(m_window->proxyModel()->rowCount() >= 1);
    m_window->tableView()->selectRow(0);
    QSignalSpy spyFinished(m_adapter.get(), &ICruxBackend::operationFinished);

    QString summary;
    onNextModal([&](QWidget *w) {
        if (auto *dlg = qobject_cast<TransactionPreviewDialog*>(w)) {
            summary = dlg->summaryText();
            dlg->cancelButton()->click();
        } else if (auto *d = qobject_cast<QDialog*>(w)) {
            d->reject();
        }
    });
    QToolButton *install = toolButton(QStringLiteral("Install"));
    QVERIFY(install && install->isEnabled());
    install->click();

    QVERIFY2(summary.contains(QStringLiteral("prt-get depinst")), qPrintable(summary));
    QVERIFY(summary.contains(QStringLiteral("a2ps")));
    QTest::qWait(200);
    QVERIFY(probeLines().isEmpty());
    QCOMPARE(spyFinished.count(), 0);
    m_window->setSearchText(QString());
}

void TestMainWindow::testSyncConfirmRejectStartsNoProcess() {
    auto restore = qScopeGuard([this]() { removeFakeTools(); });
    QVERIFY2(installFakeTools(), "fake prt-get/ports not first in PATH; refusing to click");
    QSignalSpy spyFinished(m_adapter.get(), &ICruxBackend::operationFinished);

    QString seenName, seenText;
    onNextModal([&](QWidget *w) {
        seenName = w->objectName();
        if (auto *box = qobject_cast<QMessageBox*>(w)) {
            seenText = box->text();
            QVERIFY(box->defaultButton() == box->button(QMessageBox::Cancel));
            box->button(QMessageBox::Cancel)->click();
        } else if (auto *d = qobject_cast<QDialog*>(w)) {
            d->reject();
        }
    });
    QToolButton *sync = toolButton(QStringLiteral("Sync"));
    QVERIFY(sync && sync->isEnabled());
    sync->click();

    QCOMPARE(seenName, QStringLiteral("syncConfirmMessage"));
    QVERIFY(seenText.contains(QStringLiteral("ports -u")));
    QTest::qWait(200);
    QVERIFY(probeLines().isEmpty());
    QCOMPARE(spyFinished.count(), 0);
}

namespace {
QCheckBox* checkBox(QWidget *root, const QString &text) {
    for (QCheckBox *c : root->findChildren<QCheckBox*>()) {
        if (c->text() == text) return c;
    }
    return nullptr;
}
QRadioButton* radio(QWidget *root, const QString &prefix) {
    for (QRadioButton *r : root->findChildren<QRadioButton*>()) {
        if (r->text().startsWith(prefix)) return r;
    }
    return nullptr;
}
} // namespace

void TestMainWindow::testSuccessfulSyncRecordsLastSyncForTheNotifier() {
    QVERIFY2(installFakeTools(), "fake prt-get/ports not first in PATH; refusing to click");
    auto restore = qScopeGuard([this]() { removeFakeTools(); m_window->setSettings(nullptr); });
    QTemporaryDir dir;
    auto settings = std::make_shared<OctoCruxSettings>(dir.filePath(QStringLiteral("octoCRUX.conf")));
    m_window->setSettings(settings);
    QSignalSpy spyFinished(m_adapter.get(), &ICruxBackend::operationFinished);

    onNextModal([&](QWidget *w) {
        if (auto *box = qobject_cast<QMessageBox*>(w)) {
            for (QAbstractButton *b : box->buttons()) {
                if (b->text() == QLatin1String("Sync")) b->click();
            }
        } else if (auto *d = qobject_cast<QDialog*>(w)) {
            d->reject();
        }
    });
    const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
    toolButton(QStringLiteral("Sync"))->click();
    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(probeLines(), QStringList{QStringLiteral("-u")});
    QVERIFY(settings->lastSync().isValid());
    QVERIFY(settings->lastSync() >= before);

    // A later non-sync operation does not touch it.
    const QDateTime recorded = settings->lastSync();
    emit m_adapter->progressChanged(1, 1, QStringLiteral("Locking x"));
    emit m_adapter->operationFinished(0, OperationErrorKind::None, QStringLiteral("ok"));
    QCOMPARE(settings->lastSync(), recorded);
}

void TestMainWindow::testBuildChecksAreKeptPerPortUntilARun() {
    QWidget *info = m_window->detailTabs()->widget(0);
    QCheckBox *footprint = checkBox(info, QStringLiteral("Verify footprint"));
    QCheckBox *md5 = checkBox(info, QStringLiteral("Verify md5sum"));
    QCheckBox *signature = checkBox(info, QStringLiteral("Verify signature"));
    QRadioButton *ignoreNew = radio(info, QStringLiteral("Ignore new files only"));
    QRadioButton *ignoreAll = radio(info, QStringLiteral("Ignore all differences"));
    QLabel *risk = info->findChild<QLabel*>(QStringLiteral("signatureRiskLabel"));
    QVERIFY(footprint && md5 && signature && ignoreNew && ignoreAll && risk);

    // Defaults: every check on; footprint mode choices only matter when it is off.
    QVERIFY(selectPort(QStringLiteral("a2ps")));
    QVERIFY(footprint->isChecked() && md5->isChecked() && signature->isChecked());
    QVERIFY(!ignoreNew->isEnabled() && !ignoreAll->isEnabled());
    QVERIFY(risk->isHidden());

    footprint->setChecked(false);
    QVERIFY(ignoreNew->isEnabled() && ignoreNew->isChecked()); // -in is the default choice
    md5->setChecked(false);
    signature->setChecked(false);
    QVERIFY(!risk->isHidden());
    BuildCheckOverrides o = m_window->pendingBuildOverrides(QStringLiteral("a2ps"));
    QCOMPARE(o.footprint, FootprintCheck::IgnoreNew);
    QVERIFY(o.ignoreMd5 && o.ignoreSignature);

    // Another port shows its own (default) checks; coming back restores a2ps's.
    QVERIFY(selectPort(QStringLiteral("gtk3")));
    QVERIFY(footprint->isChecked() && md5->isChecked() && signature->isChecked());
    QVERIFY(m_window->pendingBuildOverrides(QStringLiteral("gtk3")).isDefault());
    QVERIFY(selectPort(QStringLiteral("a2ps")));
    QVERIFY(!footprint->isChecked() && !md5->isChecked() && !signature->isChecked());

    // Re-checking everything removes the exception.
    footprint->setChecked(true);
    md5->setChecked(true);
    signature->setChecked(true);
    QVERIFY(m_window->pendingBuildOverrides(QStringLiteral("a2ps")).isDefault());
    m_window->setSearchText(QString());
}

void TestMainWindow::testBuildChecksReachCommandAndResetAfterRun() {
    QVERIFY2(installFakeTools(), "fake prt-get/ports not first in PATH; refusing to click");
    auto restore = qScopeGuard([this]() { removeFakeTools(); });
    QWidget *info = m_window->detailTabs()->widget(0);
    QSignalSpy spyFinished(m_adapter.get(), &ICruxBackend::operationFinished);

    QVERIFY(selectPort(QStringLiteral("a2ps")));
    checkBox(info, QStringLiteral("Verify footprint"))->setChecked(false);   // -> -in
    checkBox(info, QStringLiteral("Verify signature"))->setChecked(false);   // -> -is
    QToolButton *install = toolButton(QStringLiteral("Install"));
    QVERIFY(install && install->isEnabled());

    // 1. The preview shows the exact flags and the red banner; Cancel keeps the exceptions.
    QString summary;
    bool banner = false;
    onNextModal([&](QWidget *w) {
        if (auto *dlg = qobject_cast<TransactionPreviewDialog*>(w)) {
            summary = dlg->summaryText();
            banner = dlg->signatureWarning() != nullptr;
            dlg->reject();
        } else if (auto *d = qobject_cast<QDialog*>(w)) {
            d->reject();
        }
    });
    install->click();
    QVERIFY2(summary.contains(QStringLiteral("prt-get depinst -is --margs=-in a2ps")), qPrintable(summary));
    QVERIFY(banner);
    QVERIFY(probeLines().isEmpty());
    QVERIFY(!m_window->pendingBuildOverrides(QStringLiteral("a2ps")).isDefault());

    // 2. Apply runs exactly those flags; afterwards every check is on again.
    onNextModal([&](QWidget *w) {
        if (auto *dlg = qobject_cast<TransactionPreviewDialog*>(w)) {
            dlg->applyButton()->click();
        } else if (auto *d = qobject_cast<QDialog*>(w)) {
            d->reject();
        }
    });
    install->click();
    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(probeLines(), QStringList{QStringLiteral("depinst -is --margs=-in a2ps")});
    QVERIFY(m_window->pendingBuildOverrides(QStringLiteral("a2ps")).isDefault());
    QVERIFY(selectPort(QStringLiteral("a2ps")));
    QVERIFY(checkBox(info, QStringLiteral("Verify footprint"))->isChecked());
    QVERIFY(checkBox(info, QStringLiteral("Verify signature"))->isChecked());
    m_window->setSearchText(QString());
}

namespace {
QString pkgmkFixture(const QString &sourceDir, const QString &name) {
    QFile f(sourceDir + QStringLiteral("/tests/fixtures/crux3.8/pkgmk-failures/%1.log").arg(name));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
} // namespace

void TestMainWindow::testLockAndUnlockStartTheHelper() {
    QVERIFY2(installFakeTools(), "fake prt-get/ports not first in PATH; refusing to click");
    auto restore = qScopeGuard([this]() { removeFakeTools(); });
    QSignalSpy spyFinished(m_adapter.get(), &ICruxBackend::operationFinished);

    // gtk3 is unlocked in the fixture: the button reads "Lock" and must run "prt-get lock gtk3".
    QVERIFY(selectPort(QStringLiteral("gtk3")));
    QToolButton *lock = toolButton(QStringLiteral("Lock"));
    QVERIFY2(lock && lock->isEnabled(), "Lock button missing or disabled with gtk3 selected");
    lock->click();
    QVERIFY2(spyFinished.wait(5000), "no operationFinished after Lock");
    QCOMPARE(probeLines(), QStringList{QStringLiteral("lock gtk3")});

    // Not installed (a2ps): prt-get would answer "Package 'a2ps' not found" (owner,
    // 2026-10-21), so Lock is disabled and says why.
    QVERIFY(selectPort(QStringLiteral("a2ps")));
    QVERIFY(!lock->isEnabled());
    QCOMPARE(lock->toolTip(), QStringLiteral("prt-get can only lock installed packages"));
    // Locked but not installed (rust in the fixture): Unlock stays available.
    QVERIFY(selectPort(QStringLiteral("rust")));
    QCOMPARE(lock->text(), QStringLiteral("Unlock"));
    QVERIFY(lock->isEnabled());
    QCOMPARE(probeLines().size(), 1);
    m_window->setSearchText(QString());
}

void TestMainWindow::testFootprintRetryOfferedForNewOnlyFailures() {
    QVERIFY2(installFakeTools(), "fake prt-get/ports not first in PATH; refusing to click");
    auto restore = qScopeGuard([this]() { removeFakeTools(); });

    // Real pkgmk output of a NEW-only footprint failure, with one field changed in
    // memory: the package name in the build target (octo-footprint-new -> a2ps),
    // so that the retry plans a port that exists in the catalog fixture.
    QString log = pkgmkFixture(m_sourceDir, QStringLiteral("footprint-new"));
    QVERIFY(log.contains(QStringLiteral("/octo-footprint-new#")));
    log.replace(QStringLiteral("/octo-footprint-new#"), QStringLiteral("/a2ps#"));

    QString offerText, previewText;
    bool cancelIsDefault = false;
    onNextModal([&](QWidget *w) {
        auto *box = qobject_cast<QMessageBox*>(w);
        if (!box || box->objectName() != QLatin1String("footprintRetryMessage")) {
            if (auto *d = qobject_cast<QDialog*>(w)) d->reject();
            return;
        }
        offerText = box->text();
        cancelIsDefault = box->defaultButton() == box->button(QMessageBox::Cancel);
        onNextModal([&](QWidget *w2) {
            if (auto *dlg = qobject_cast<TransactionPreviewDialog*>(w2)) {
                previewText = dlg->summaryText();
            }
            if (auto *d = qobject_cast<QDialog*>(w2)) d->reject();
        });
        for (QAbstractButton *b : box->buttons()) {
            if (b->text() == QLatin1String("Retry with -in")) b->click();
        }
    });

    emit m_adapter->progressChanged(1, 1, QStringLiteral("Installing a2ps"));
    for (const QString &line : log.split(QLatin1Char('\n'))) {
        emit m_adapter->outputReceived(line, line.startsWith(QStringLiteral("=======> ERROR")));
    }
    emit m_adapter->operationFinished(1, OperationErrorKind::FootprintError, QStringLiteral("Footprint mismatch"));

    QTRY_VERIFY_WITH_TIMEOUT(!previewText.isEmpty(), 5000);
    QVERIFY2(offerText.contains(QStringLiteral("a2ps")), qPrintable(offerText));
    QVERIFY(cancelIsDefault);
    QVERIFY2(previewText.contains(QStringLiteral("prt-get depinst --margs=-in a2ps")), qPrintable(previewText));
    QVERIFY(probeLines().isEmpty());   // preview rejected: nothing ran

    // The exception stays pending (nothing ran) and is visible in a2ps's Information tab.
    QCOMPARE(m_window->pendingBuildOverrides(QStringLiteral("a2ps")).footprint, FootprintCheck::IgnoreNew);
    QVERIFY(selectPort(QStringLiteral("a2ps")));
    QCheckBox *footprint = checkBox(m_window->detailTabs()->widget(0), QStringLiteral("Verify footprint"));
    QVERIFY(!footprint->isChecked());
    footprint->setChecked(true); // clean up for later tests
    QVERIFY(m_window->pendingBuildOverrides(QStringLiteral("a2ps")).isDefault());
    m_window->setSearchText(QString());
}

void TestMainWindow::testFootprintMissingIsReportedNotOffered() {
    const QString log = pkgmkFixture(m_sourceDir, QStringLiteral("footprint")); // NEW + MISSING
    QVERIFY(!log.isEmpty());
    bool modalSeen = false;
    QPointer<QTimer> watcher = onNextModal([&](QWidget *w) {
        modalSeen = true;
        if (auto *d = qobject_cast<QDialog*>(w)) d->reject();
    });
    // The watcher captures locals: it must not outlive this test.
    auto stopWatcher = qScopeGuard([&watcher]() { if (watcher) { watcher->stop(); watcher->deleteLater(); } });
    QPlainTextEdit *out = m_window->outputLogEdit();
    emit m_adapter->progressChanged(1, 1, QStringLiteral("Installing octo-footprint"));
    for (const QString &line : log.split(QLatin1Char('\n'))) {
        emit m_adapter->outputReceived(line, false);
    }
    emit m_adapter->operationFinished(1, OperationErrorKind::FootprintError, QStringLiteral("Footprint mismatch"));
    QTest::qWait(300);
    QVERIFY(!modalSeen);
    QVERIFY(out->toPlainText().contains(QStringLiteral("Footprint mismatch with MISSING files in: octo-footprint.")));

    // A cancelled run never triggers the offer either.
    const QString newOnly = pkgmkFixture(m_sourceDir, QStringLiteral("footprint-new"));
    emit m_adapter->progressChanged(1, 1, QStringLiteral("Installing octo-footprint-new"));
    for (const QString &line : newOnly.split(QLatin1Char('\n'))) {
        emit m_adapter->outputReceived(line, false);
    }
    emit m_adapter->operationFinished(15, OperationErrorKind::Cancelled, QStringLiteral("Operation was cancelled by user"));
    QTest::qWait(300);
    QVERIFY(!modalSeen);
}

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"
