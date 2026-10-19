#include <QTest>
#include <QFile>
#include <QTextStream>
#include <QMap>

#include "backend/crux_adapter.h"
#include "core/printf_parser.h"
#include "model/package_model.h"
#include "model/package_filter_proxy_model.h"

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

class TestPackageModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testModelStructure();
    void testDisplayDataAndThreeAxesFormatting();
    void testFilterAll();
    void testFilterOutdated();
    void testFilterLocked();
    void testFilterInstalledAndVirtual();
    void testCollectionFilter();
    void testSearchFilter();
    void testSorting();

private:
    QString readFile(const QString &filePath);

    QString m_sourceDir;
    std::shared_ptr<MockCommandRunner> m_mockRunner;
    std::unique_ptr<CruxAdapter> m_adapter;

    std::unique_ptr<PackageModel> m_model;
    std::unique_ptr<PackageFilterProxyModel> m_proxy;
};

QString TestPackageModel::readFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QTextStream(&file).readAll();
}

void TestPackageModel::initTestCase() {
    m_sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);
    m_mockRunner = std::make_shared<MockCommandRunner>();

    const QString printfOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-literaltab.out"));
    QVERIFY(!printfOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("printf"), QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)},
        {0, printfOutput, QString()}
    );

    m_adapter = std::make_unique<CruxAdapter>(m_mockRunner);
    m_adapter->setRootPrefix(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/files"));

    const QList<PortInfo> ports = m_adapter->queryAllPorts();
    QCOMPARE(ports.size(), 2307);

    m_model = std::make_unique<PackageModel>();
    m_model->setPorts(ports);

    m_proxy = std::make_unique<PackageFilterProxyModel>();
    m_proxy->setSourceModel(m_model.get());
}

void TestPackageModel::testModelStructure() {
    QCOMPARE(m_model->rowCount(), 2307);
    QCOMPARE(m_model->columnCount(), PackageModelColumn::ColumnCount);

    QCOMPARE(m_model->headerData(ColumnName, Qt::Horizontal).toString(), QStringLiteral("Name"));
    QCOMPARE(m_model->headerData(ColumnVersion, Qt::Horizontal).toString(), QStringLiteral("Version"));
    QCOMPARE(m_model->headerData(ColumnStatus, Qt::Horizontal).toString(), QStringLiteral("Status"));
    QCOMPARE(m_model->headerData(ColumnCollection, Qt::Horizontal).toString(), QStringLiteral("Collection"));
    QCOMPARE(m_model->headerData(ColumnDescription, Qt::Horizontal).toString(), QStringLiteral("Description"));

    auto gtk3 = m_model->portByName(QStringLiteral("gtk3"));
    QVERIFY(gtk3.has_value());
    QCOMPARE(gtk3->version, QStringLiteral("3.24.52"));

    auto nonExistent = m_model->portByName(QStringLiteral("nonexistent"));
    QVERIFY(!nonExistent.has_value());
}

void TestPackageModel::testDisplayDataAndThreeAxesFormatting() {
    // 1. Regular installed package (gtk3)
    auto gtk3 = m_model->portByName(QStringLiteral("gtk3"));
    QVERIFY(gtk3.has_value());
    QCOMPARE(PackageModel::formatStatus(*gtk3), QStringLiteral("Installed"));

    // 2. Outdated and locked package (llvm)
    auto llvm = m_model->portByName(QStringLiteral("llvm"));
    QVERIFY(llvm.has_value());
    QCOMPARE(PackageModel::formatStatus(*llvm), QStringLiteral("Outdated (Locked)"));

    // 3. Virtual alias package locked without source installed (rust)
    auto rust = m_model->portByName(QStringLiteral("rust"));
    QVERIFY(rust.has_value());
    QCOMPARE(PackageModel::formatStatus(*rust), QStringLiteral("Virtual (rust-bin) [Locked]"));

    // 4. Regular uninstalled package (a2ps)
    auto a2ps = m_model->portByName(QStringLiteral("a2ps"));
    QVERIFY(a2ps.has_value());
    QCOMPARE(PackageModel::formatStatus(*a2ps), QStringLiteral("Available"));
}

void TestPackageModel::testFilterAll() {
    m_proxy->setStatusFilter(StatusFilter::All);
    m_proxy->setCollectionFilter(QString());
    m_proxy->setSearchText(QString());

    QCOMPARE(m_proxy->rowCount(), 2307);
}

void TestPackageModel::testFilterOutdated() {
    m_proxy->setStatusFilter(StatusFilter::Outdated);
    m_proxy->setCollectionFilter(QString());
    m_proxy->setSearchText(QString());

    // From diff-from-printf.txt, exactly 4 outdated packages on CRUX 3.8
    QCOMPARE(m_proxy->rowCount(), 4);

    QStringList names;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        auto port = m_proxy->portAtProxyRow(r);
        QVERIFY(port.has_value());
        names.append(port->name);
    }
    names.sort();

    const QStringList expected = {
        QStringLiteral("clang"),
        QStringLiteral("compiler-rt"),
        QStringLiteral("lld"),
        QStringLiteral("llvm")
    };
    QCOMPARE(names, expected);
}

void TestPackageModel::testFilterLocked() {
    m_proxy->setStatusFilter(StatusFilter::Locked);
    m_proxy->setCollectionFilter(QString());
    m_proxy->setSearchText(QString());

    // Exactly 5 locked packages from prtget-listlocked.out
    QCOMPARE(m_proxy->rowCount(), 5);

    QStringList names;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        auto port = m_proxy->portAtProxyRow(r);
        QVERIFY(port.has_value());
        names.append(port->name);
    }
    names.sort();

    const QStringList expected = {
        QStringLiteral("clang"),
        QStringLiteral("compiler-rt"),
        QStringLiteral("lld"),
        QStringLiteral("llvm"),
        QStringLiteral("rust")
    };
    QCOMPARE(names, expected);
}

void TestPackageModel::testFilterInstalledAndVirtual() {
    m_proxy->setStatusFilter(StatusFilter::Installed);
    m_proxy->setCollectionFilter(QString());
    m_proxy->setSearchText(QString());

    QVERIFY(m_proxy->rowCount() > 0);

    // rust should be included in installed filter because it is satisfied by rust-bin
    bool foundRust = false;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        auto port = m_proxy->portAtProxyRow(r);
        if (port && port->name == QStringLiteral("rust")) {
            foundRust = true;
            break;
        }
    }
    QVERIFY2(foundRust, "Virtual package satisfied by installed provider must be considered installed");
}

void TestPackageModel::testCollectionFilter() {
    m_proxy->setStatusFilter(StatusFilter::All);
    m_proxy->setSearchText(QString());
    m_proxy->setCollectionFilter(QStringLiteral("d77crux"));

    QVERIFY(m_proxy->rowCount() > 0);
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        auto port = m_proxy->portAtProxyRow(r);
        QVERIFY(port.has_value());
        QCOMPARE(port->collection, QStringLiteral("d77crux"));
    }
}

void TestPackageModel::testSearchFilter() {
    m_proxy->setStatusFilter(StatusFilter::All);
    m_proxy->setCollectionFilter(QString());

    // 1. Search text matching name
    m_proxy->setSearchText(QStringLiteral("gtk3"));
    m_proxy->setSearchDescription(false);
    QVERIFY(m_proxy->rowCount() >= 1);

    bool hasExactGtk3 = false;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        auto port = m_proxy->portAtProxyRow(r);
        if (port && port->name == QStringLiteral("gtk3")) {
            hasExactGtk3 = true;
        }
        QVERIFY(port->name.contains(QStringLiteral("gtk3"), Qt::CaseInsensitive));
    }
    QVERIFY(hasExactGtk3);

    // 2. Search text matching description (e.g. adwaita-icon-theme depends on gtk3 or mentions it)
    m_proxy->setSearchText(QStringLiteral("Postscript converter"));
    m_proxy->setSearchDescription(true);
    QCOMPARE(m_proxy->rowCount(), 1);
    auto a2ps = m_proxy->portAtProxyRow(0);
    QVERIFY(a2ps.has_value());
    QCOMPARE(a2ps->name, QStringLiteral("a2ps"));
}

void TestPackageModel::testSorting() {
    m_proxy->setStatusFilter(StatusFilter::All);
    m_proxy->setCollectionFilter(QString());
    m_proxy->setSearchText(QString());

    // Sort ascending by Name
    m_proxy->sort(ColumnName, Qt::AscendingOrder);
    auto firstAsc = m_proxy->portAtProxyRow(0);
    QVERIFY(firstAsc.has_value());
    QCOMPARE(firstAsc->name, QStringLiteral("a2ps"));

    // Sort descending by Name
    m_proxy->sort(ColumnName, Qt::DescendingOrder);
    auto firstDesc = m_proxy->portAtProxyRow(0);
    QVERIFY(firstDesc.has_value());
    QVERIFY(firstDesc->name.compare(QStringLiteral("a2ps")) > 0);
}

QTEST_MAIN(TestPackageModel)
#include "test_package_model.moc"
