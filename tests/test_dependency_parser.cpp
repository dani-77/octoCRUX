#include <QTest>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QSet>

#include "core/dependency_parser.h"
#include "core/aliases_parser.h"

using namespace OctoCRUX;

class TestDependencyParser : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testParseDirectInstalledDependents();
    void testParseAllDeclaredDependents();
    void testParseQuickdepOrder();
    void testResolveQuickdepWithAliases();
    void testEmptyAndEdgeCases();

private:
    QString m_sourceDir;
    QString m_directDependentsContent;
    QString m_allDependentsContent;
    QString m_quickdepContent;
    QString m_aliasesContent;
};

void TestDependencyParser::initTestCase() {
    m_sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);

    // 1. Direct installed dependents fixture (prt-get dependent gtk3)
    const QString directPath = m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-dependent.out");
    QFile directFile(directPath);
    QVERIFY2(directFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open inst-dependent.out");
    m_directDependentsContent = QTextStream(&directFile).readAll();
    directFile.close();

    // 2. All declared dependents fixture (prt-get dependent --all gtk3)
    const QString allPath = m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-dependent-all.out");
    QFile allFile(allPath);
    QVERIFY2(allFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open inst-dependent-all.out");
    m_allDependentsContent = QTextStream(&allFile).readAll();
    allFile.close();

    // 3. Quickdep order fixture (prt-get quickdep gtk3)
    const QString quickdepPath = m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-quickdep.out");
    QFile quickdepFile(quickdepPath);
    QVERIFY2(quickdepFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open inst-quickdep.out");
    m_quickdepContent = QTextStream(&quickdepFile).readAll();
    quickdepFile.close();

    // 4. Aliases fixture
    const QString aliasesPath = m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/files/etc/prt-get.aliases");
    QFile aliasesFile(aliasesPath);
    QVERIFY2(aliasesFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open prt-get.aliases");
    m_aliasesContent = QTextStream(&aliasesFile).readAll();
    aliasesFile.close();
}

void TestDependencyParser::testParseDirectInstalledDependents() {
    const QStringList dependents = DependencyParser::parseDependents(m_directDependentsContent);

    // CRUX 3.8 test run has exactly 7 direct installed dependents for gtk3
    QCOMPARE(dependents.size(), 7);

    const QStringList expected = {
        QStringLiteral("tint2"),
        QStringLiteral("granite"),
        QStringLiteral("firefox-bin"),
        QStringLiteral("lxappearance"),
        QStringLiteral("thunderbird-bin"),
        QStringLiteral("glade"),
        QStringLiteral("libnotify")
    };

    QCOMPARE(dependents, expected);
}

void TestDependencyParser::testParseAllDeclaredDependents() {
    const QStringList allDependents = DependencyParser::parseDependents(m_allDependentsContent);

    // CRUX 3.8 test run has exactly 38 total declared dependents across the ports tree
    QCOMPARE(allDependents.size(), 38);

    // Verify key dependents are present
    QVERIFY(allDependents.contains(QStringLiteral("tint2")));
    QVERIFY(allDependents.contains(QStringLiteral("gimp")));
    QVERIFY(allDependents.contains(QStringLiteral("webkitgtk40")));
    QVERIFY(allDependents.contains(QStringLiteral("nvidia")));
}

void TestDependencyParser::testParseQuickdepOrder() {
    const QStringList quickdep = DependencyParser::parseQuickdep(m_quickdepContent);

    // CRUX 3.8 quickdep gtk3 produces exactly 118 recursive dependencies
    QCOMPARE(quickdep.size(), 118);

    // The first item should be a base dependency like expat
    QCOMPARE(quickdep.first(), QStringLiteral("expat"));

    // The target port itself must always be at the end of the quickdep sequence
    QCOMPARE(DependencyParser::extractTargetPort(quickdep), QStringLiteral("gtk3"));
    QCOMPARE(quickdep.last(), QStringLiteral("gtk3"));
}

void TestDependencyParser::testResolveQuickdepWithAliases() {
    const QStringList rawQuickdep = DependencyParser::parseQuickdep(m_quickdepContent);
    const AliasesMapping aliases = AliasesParser::parse(m_aliasesContent);

    // Raw quickdep has "rust", but NOT "rust-bin"
    QVERIFY(rawQuickdep.contains(QStringLiteral("rust")));
    QVERIFY(!rawQuickdep.contains(QStringLiteral("rust-bin")));

    // Simulate installed packages set that contains rust-bin
    QSet<QString> installed = {
        QStringLiteral("rust-bin"),
        QStringLiteral("glibc"),
        QStringLiteral("zlib")
    };

    const QStringList resolved = DependencyParser::resolveQuickdepWithAliases(rawQuickdep, aliases, installed);

    // The size of the dependency sequence must remain unchanged
    QCOMPARE(resolved.size(), 118);

    // "rust" must be replaced by "rust-bin" because rust-bin is installed
    QVERIFY(!resolved.contains(QStringLiteral("rust")));
    QVERIFY(resolved.contains(QStringLiteral("rust-bin")));

    // The target port must remain gtk3
    QCOMPARE(resolved.last(), QStringLiteral("gtk3"));
}

void TestDependencyParser::testEmptyAndEdgeCases() {
    QVERIFY(DependencyParser::parseDependents(QString()).isEmpty());
    QVERIFY(DependencyParser::parseDependents(QStringLiteral("   \n\n\r\n  ")).isEmpty());

    QVERIFY(DependencyParser::parseQuickdep(QString()).isEmpty());
    QVERIFY(DependencyParser::parseQuickdep(QStringLiteral("   \n\t  ")).isEmpty());

    QVERIFY(DependencyParser::extractTargetPort(QStringList()).isEmpty());
}

QTEST_MAIN(TestDependencyParser)
#include "test_dependency_parser.moc"
