#include <QTest>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QTableView>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QTextBrowser>

#include "ui/mainwindow.h"
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
    void testCloseRefusedDuringOperation();

private:
    QString readFile(const QString &filePath);

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
    auto *infoBrowser = qobject_cast<QTextBrowser*>(tabs->widget(0));
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

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"
