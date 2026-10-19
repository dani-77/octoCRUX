#include <QTest>
#include <QFile>
#include <QTextStream>
#include <QDir>

#include "core/aliases_parser.h"

using namespace OctoCRUX;

class TestAliasesParser : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testParseRealAliases();
    void testApplyAliasesToPorts();
    void testSyntheticEdgeCases();

private:
    QString m_fixturePath;
    QString m_fixtureContent;
};

void TestAliasesParser::initTestCase() {
    const QString sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);
    m_fixturePath = sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/files/etc/prt-get.aliases");

    QFile file(m_fixturePath);
    if (!file.exists()) {
        m_fixturePath = sourceDir + QStringLiteral("/tests/fixtures/crux3.8/files/etc/prt-get.aliases");
        file.setFileName(m_fixturePath);
    }

    QVERIFY2(file.exists(), qPrintable(QString("Aliases fixture not found at: ") + m_fixturePath));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open aliases fixture");

    QTextStream in(&file);
    m_fixtureContent = in.readAll();
    file.close();

    QVERIFY2(!m_fixtureContent.isEmpty(), "Aliases content must not be empty");
}

void TestAliasesParser::testParseRealAliases() {
    AliasesMapping aliases = AliasesParser::parse(m_fixtureContent);

    // On the CRUX 3.8 machine, /etc/prt-get.aliases defines exactly 4 aliases
    QCOMPARE(aliases.size(), 4);

    // Test provider -> virtual mappings
    QCOMPARE(aliases.findVirtualFor(QStringLiteral("rust-bin")), QStringLiteral("rust"));
    QCOMPARE(aliases.findVirtualFor(QStringLiteral("jdk8-bin")), QStringLiteral("openjdk8"));
    QCOMPARE(aliases.findVirtualFor(QStringLiteral("librsvg-compat")), QStringLiteral("librsvg"));
    QCOMPARE(aliases.findVirtualFor(QStringLiteral("mariadb")), QStringLiteral("mysql"));

    // Test virtual -> provider mappings
    QCOMPARE(aliases.findProviderFor(QStringLiteral("rust")), QStringLiteral("rust-bin"));
    QCOMPARE(aliases.findProviderFor(QStringLiteral("openjdk8")), QStringLiteral("jdk8-bin"));
    QCOMPARE(aliases.findProviderFor(QStringLiteral("librsvg")), QStringLiteral("librsvg-compat"));
    QCOMPARE(aliases.findProviderFor(QStringLiteral("mysql")), QStringLiteral("mariadb"));

    // Test query methods
    QVERIFY(aliases.hasVirtual(QStringLiteral("rust")));
    QVERIFY(aliases.hasProvider(QStringLiteral("rust-bin")));
    QVERIFY(!aliases.hasVirtual(QStringLiteral("nonexistent")));
}

void TestAliasesParser::testApplyAliasesToPorts() {
    AliasesMapping aliases = AliasesParser::parse(m_fixtureContent);

    QList<PortInfo> ports;

    PortInfo rust;
    rust.name = QStringLiteral("rust");
    rust.installStatus = InstallStatus::NotInstalled;
    ports.append(rust);

    PortInfo rustBin;
    rustBin.name = QStringLiteral("rust-bin");
    rustBin.installStatus = InstallStatus::Installed;
    ports.append(rustBin);

    PortInfo mysql;
    mysql.name = QStringLiteral("mysql");
    mysql.installStatus = InstallStatus::NotInstalled;
    ports.append(mysql);

    // Provider mariadb is NOT installed
    PortInfo mariadb;
    mariadb.name = QStringLiteral("mariadb");
    mariadb.installStatus = InstallStatus::NotInstalled;
    ports.append(mariadb);

    // Apply aliases with default onlyIfProviderInstalled = true
    AliasesParser::applyAliases(ports, aliases, true);

    // rust should be marked as provided by rust-bin because rust-bin is installed
    QCOMPARE(ports[0].name, QStringLiteral("rust"));
    QCOMPARE(ports[0].providedByAlias, QStringLiteral("rust-bin"));

    // mysql should NOT be marked because mariadb is not installed
    QCOMPARE(ports[2].name, QStringLiteral("mysql"));
    QVERIFY(ports[2].providedByAlias.isEmpty());

    // Apply with onlyIfProviderInstalled = false
    AliasesParser::applyAliases(ports, aliases, false);
    QCOMPARE(ports[2].providedByAlias, QStringLiteral("mariadb"));
}

void TestAliasesParser::testSyntheticEdgeCases() {
    const QString synthetic = QStringLiteral(
        "# This is a comment\n"
        "\n"
        "   custom-pkg-bin :   custom-virtual   \n"
        "malformed_line_without_colon\n"
        ": empty_provider\n"
        "empty_virtual :\n"
        "# Another comment\n"
        "provider2:virtual2\n"
    );

    AliasesMapping mapping = AliasesParser::parse(synthetic);
    QCOMPARE(mapping.size(), 2);
    QCOMPARE(mapping.findVirtualFor(QStringLiteral("custom-pkg-bin")), QStringLiteral("custom-virtual"));
    QCOMPARE(mapping.findProviderFor(QStringLiteral("custom-virtual")), QStringLiteral("custom-pkg-bin"));
    QCOMPARE(mapping.findVirtualFor(QStringLiteral("provider2")), QStringLiteral("virtual2"));
}

QTEST_MAIN(TestAliasesParser)
#include "test_aliases_parser.moc"
