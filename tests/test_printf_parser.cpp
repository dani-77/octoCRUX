#include <QTest>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QMap>

#include "core/printf_parser.h"

using namespace OctoCRUX;

class TestPrintfParser : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testParseAllCount();
    void testSamplePorts();
    void testOutdatedPorts();
    void testApplyLocksAndThreeAxes();
    void testSyntheticProduction11Token();
    void testMalformedLines();
    void testCollectionExtraction();
    void testFormatConstantsAndStrictSchema();
    void testRealProduction11TokenFixture();

private:
    QString m_fixturePath;
    QString m_fixtureContent;
    QList<PortInfo> m_parsedPorts;
};

void TestPrintfParser::initTestCase() {
    const QString sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);
    m_fixturePath = sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-literaltab.out");

    QFile file(m_fixturePath);
    QVERIFY2(file.exists(), qPrintable(QString("Fixture file not found at: ") + m_fixturePath));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open fixture file");

    QTextStream in(&file);
    m_fixtureContent = in.readAll();
    file.close();

    QVERIFY2(!m_fixtureContent.isEmpty(), "Fixture content must not be empty");

    // Parse all ports using Fixture8Token explicitly
    m_parsedPorts = PrintfParser::parseAll(m_fixtureContent, PrintfParser::Schema::Fixture8Token);
}

void TestPrintfParser::testParseAllCount() {
    // CRUX 3.8 test machine had exactly 2,307 ports across enabled collections
    QCOMPARE(m_parsedPorts.size(), 2307);
}

void TestPrintfParser::testSamplePorts() {
    QMap<QString, PortInfo> map;
    for (const PortInfo &p : m_parsedPorts) {
        map.insert(p.name, p);
    }

    // 1. Verify a2ps (standard opt port, uninstalled, no README)
    QVERIFY(map.contains("a2ps"));
    const PortInfo a2ps = map.value("a2ps");
    QCOMPARE(a2ps.name, QStringLiteral("a2ps"));
    QCOMPARE(a2ps.version, QStringLiteral("4.15.8"));
    QCOMPARE(a2ps.release, QStringLiteral("1"));
    QCOMPARE(a2ps.installStatus, InstallStatus::NotInstalled);
    QCOMPARE(a2ps.lockStatus, LockStatus::Unlocked);
    QCOMPARE(a2ps.collectionPath, QStringLiteral("/usr/ports/opt"));
    QCOMPARE(a2ps.collection, QStringLiteral("opt"));
    QCOMPARE(a2ps.fullPortDirectory(), QStringLiteral("/usr/ports/opt/a2ps"));
    QCOMPARE(a2ps.description, QStringLiteral("ASCII to Postscript converter (Prettyprint)"));
    QCOMPARE(a2ps.hasReadme, false);
    QCOMPARE(a2ps.dependencies, QStringList({QStringLiteral("boehm-gc"), QStringLiteral("paper")}));

    // 2. Verify adwaita-icon-theme (opt port, has README)
    QVERIFY(map.contains("adwaita-icon-theme"));
    const PortInfo adwaita = map.value("adwaita-icon-theme");
    QCOMPARE(adwaita.hasReadme, true);
    QCOMPARE(adwaita.dependencies, QStringList({QStringLiteral("gtk3"), QStringLiteral("hicolor-icon-theme"), QStringLiteral("librsvg")}));

    // 3. Verify acl (core port, installed)
    QVERIFY(map.contains("acl"));
    const PortInfo acl = map.value("acl");
    QCOMPARE(acl.installStatus, InstallStatus::Installed);
    QCOMPARE(acl.collection, QStringLiteral("core"));
    QCOMPARE(acl.isInstalled(), true);
    QCOMPARE(acl.dependencies, QStringList({QStringLiteral("attr")}));

    // 4. Verify abiword (overlay port from d77crux, installed)
    QVERIFY(map.contains("abiword"));
    const PortInfo abiword = map.value("abiword");
    QCOMPARE(abiword.installStatus, InstallStatus::Installed);
    QCOMPARE(abiword.collection, QStringLiteral("d77crux"));
}

void TestPrintfParser::testOutdatedPorts() {
    // Collect all ports with installStatus == Outdated
    QStringList outdatedNames;
    for (const PortInfo &p : m_parsedPorts) {
        if (p.installStatus == InstallStatus::Outdated) {
            outdatedNames.append(p.name);
        }
    }
    outdatedNames.sort();

    // From diff-from-printf.txt, the exact 4 outdated ports on this CRUX system
    const QStringList expectedOutdated = {
        QStringLiteral("clang"),
        QStringLiteral("compiler-rt"),
        QStringLiteral("lld"),
        QStringLiteral("llvm")
    };

    QCOMPARE(outdatedNames, expectedOutdated);
}

void TestPrintfParser::testApplyLocksAndThreeAxes() {
    const QString sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);
    const QString lockedFixturePath = sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/prtget-listlocked.out");

    QFile file(lockedFixturePath);
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open prtget-listlocked.out fixture");

    QStringList lockedNames;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty()) {
            lockedNames.append(line);
        }
    }
    file.close();

    // The fixture has exactly 5 locked ports: llvm, clang, lld, compiler-rt, rust
    QCOMPARE(lockedNames.size(), 5);

    // Make a copy of parsed ports and apply locks
    QList<PortInfo> ports = m_parsedPorts;
    PrintfParser::applyLocks(ports, lockedNames);

    QMap<QString, PortInfo> map;
    int lockedCount = 0;
    for (const PortInfo &p : ports) {
        map.insert(p.name, p);
        if (p.lockStatus == LockStatus::Locked) {
            lockedCount++;
        }
    }

    QCOMPARE(lockedCount, 5);

    // Verify Three-Axis state: clang, compiler-rt, lld, llvm are Outdated AND Locked
    for (const QString &name : {QStringLiteral("clang"), QStringLiteral("compiler-rt"), QStringLiteral("lld"), QStringLiteral("llvm")}) {
        QVERIFY(map.contains(name));
        const PortInfo &p = map[name];
        QCOMPARE(p.installStatus, InstallStatus::Outdated);
        QCOMPARE(p.lockStatus, LockStatus::Locked);
        QVERIFY2(p.isOutdatedLocked(), qPrintable(name + " should be Outdated AND Locked"));
        QVERIFY2(!p.isOutdatedEligible(), qPrintable(name + " must NOT be eligible for routine update"));
    }

    // Verify rust: NotInstalled in raw ports tree, but Locked
    QVERIFY(map.contains("rust"));
    const PortInfo &rust = map["rust"];
    QCOMPARE(rust.installStatus, InstallStatus::NotInstalled);
    QCOMPARE(rust.lockStatus, LockStatus::Locked);
    QVERIFY2(rust.isLockedUninstalled(), "rust should be Locked and NotInstalled (raw axis)");

    // Verify unlocked installed port like acl
    const PortInfo &acl = map["acl"];
    QCOMPARE(acl.installStatus, InstallStatus::Installed);
    QCOMPARE(acl.lockStatus, LockStatus::Unlocked);
    QVERIFY(acl.isInstalled());
    QVERIFY(!acl.isOutdatedLocked());
}

void TestPrintfParser::testSyntheticProduction11Token() {
    // 11-token line: %n %v %r %i %l %p %R %E %O %e %d (with embedded tab in description)
    const QString line = QStringLiteral("custom-pkg\t1.2.3\t4\tyes\tyes\t/usr/ports/custom\tyes\tyes\tno\tdepA,depB\tCustom port description\twith an embedded tab");
    auto result = PrintfParser::parseLine(line, PrintfParser::Schema::Production11Token);

    QVERIFY(result.has_value());
    const PortInfo p = result.value();
    QCOMPARE(p.name, QStringLiteral("custom-pkg"));
    QCOMPARE(p.version, QStringLiteral("1.2.3"));
    QCOMPARE(p.release, QStringLiteral("4"));
    QCOMPARE(p.installStatus, InstallStatus::Installed);
    QCOMPARE(p.lockStatus, LockStatus::Locked);
    QCOMPARE(p.collectionPath, QStringLiteral("/usr/ports/custom"));
    QCOMPARE(p.collection, QStringLiteral("custom"));
    QCOMPARE(p.hasReadme, true);
    QCOMPARE(p.hasPreInstall, true);
    QCOMPARE(p.hasPostInstall, false);
    QCOMPARE(p.dependencies, QStringList({QStringLiteral("depA"), QStringLiteral("depB")}));
    // Crucial check: description with embedded tab is preserved without corrupting fields
    QCOMPARE(p.description, QStringLiteral("Custom port description\twith an embedded tab"));
}

void TestPrintfParser::testMalformedLines() {
    // Empty line
    QVERIFY(!PrintfParser::parseLine(QString()).has_value());
    QVERIFY(!PrintfParser::parseLine(QStringLiteral("   \t  \n")).has_value());

    // Line with too few tokens (e.g. 5 tokens)
    const QString shortLine = QStringLiteral("pkg\t1.0\t1\tyes\t/usr/ports/core");
    QVERIFY(!PrintfParser::parseLine(shortLine, PrintfParser::Schema::Fixture8Token).has_value());

    // Line with empty name
    const QString noNameLine = QStringLiteral("\t1.0\t1\tyes\t/usr/ports/core\tdesc\tno\t");
    QVERIFY(!PrintfParser::parseLine(noNameLine, PrintfParser::Schema::Fixture8Token).has_value());

    // Line with invalid install status (%i not yes/no/diff)
    const QString badInstallLine = QStringLiteral("pkg\t1.0\t1\tinvalid\tno\t/usr/ports/core\tyes\tno\tno\tdep1\tdesc");
    QVERIFY(!PrintfParser::parseLine(badInstallLine, PrintfParser::Schema::Production11Token).has_value());

    // Line with invalid lock status (%l not yes/no)
    const QString badLockLine = QStringLiteral("pkg\t1.0\t1\tyes\tinvalid\t/usr/ports/core\tyes\tno\tno\tdep1\tdesc");
    QVERIFY(!PrintfParser::parseLine(badLockLine, PrintfParser::Schema::Production11Token).has_value());
}

void TestPrintfParser::testCollectionExtraction() {
    QCOMPARE(PrintfParser::extractCollectionName(QStringLiteral("/usr/ports/core")), QStringLiteral("core"));
    QCOMPARE(PrintfParser::extractCollectionName(QStringLiteral("/usr/ports/d77crux/")), QStringLiteral("d77crux"));
    QCOMPARE(PrintfParser::extractCollectionName(QStringLiteral("/opt/ports/custom/")), QStringLiteral("custom"));
    QCOMPARE(PrintfParser::extractCollectionName(QStringLiteral("custom")), QStringLiteral("custom"));
    QCOMPARE(PrintfParser::extractCollectionName(QString()), QString());
}

void TestPrintfParser::testFormatConstantsAndStrictSchema() {
    // 1. Verify canonical format string constants
    QCOMPARE(QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT), QStringLiteral("%n\t%v\t%r\t%i\t%l\t%p\t%R\t%E\t%O\t%e\t%d\n"));
    QCOMPARE(QString::fromLatin1(PrintfParser::FIXTURE_FORMAT), QStringLiteral("%n\t%v\t%r\t%i\t%p\t%d\t%R\t%e\n"));

    // 2. Strict schema enforcement: 8-token line MUST be rejected when Production11Token is requested
    const QString line8 = QStringLiteral("pkg\t1.0\t1\tyes\t/usr/ports/core\tdesc\tno\tdep1");
    QVERIFY(!PrintfParser::parseLine(line8, PrintfParser::Schema::Production11Token).has_value());

    // 3. Strict schema enforcement: 11-token line MUST be rejected when Fixture8Token is requested
    const QString line11 = QStringLiteral("pkg\t1.0\t1\tyes\tno\t/usr/ports/core\tyes\tno\tno\tdep1\tdesc");
    QVERIFY(!PrintfParser::parseLine(line11, PrintfParser::Schema::Fixture8Token).has_value());

    // 4. In their matching schemas, both lines must parse successfully
    QVERIFY(PrintfParser::parseLine(line8, PrintfParser::Schema::Fixture8Token).has_value());
    QVERIFY(PrintfParser::parseLine(line11, PrintfParser::Schema::Production11Token).has_value());
}

void TestPrintfParser::testRealProduction11TokenFixture() {
    const QString sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);

    // 1. Provenance test: assert captured .cmd argv contains PRODUCTION_FORMAT verbatim
    const QString cmdPath = sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-production-11token.cmd");
    QFile cmdFile(cmdPath);
    QVERIFY2(cmdFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open printf-production-11token.cmd");
    QTextStream cmdIn(&cmdFile);
    const QString cmdContent = cmdIn.readAll();
    cmdFile.close();

    const QString expectedFormat = QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT);
    QVERIFY2(cmdContent.contains(expectedFormat), "Captured .cmd argv must contain PRODUCTION_FORMAT verbatim");

    // 2. Parse real CRUX 3.8 bare-metal production fixture
    const QString outPath = sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-production-11token.out");
    QFile outFile(outPath);
    QVERIFY2(outFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open printf-production-11token.out");
    QTextStream outIn(&outFile);
    const QString outContent = outIn.readAll();
    outFile.close();

    const QList<PortInfo> ports = PrintfParser::parseAll(outContent, PrintfParser::Schema::Production11Token);
    QCOMPARE(ports.size(), 2307);

    QMap<QString, PortInfo> map;
    int preInstallCount = 0;
    int postInstallCount = 0;
    for (const PortInfo &p : ports) {
        map.insert(p.name, p);
        if (p.hasPreInstall) preInstallCount++;
        if (p.hasPostInstall) postInstallCount++;
    }

    // Outdated ports (%i == diff)
    QCOMPARE(map["clang"].installStatus, InstallStatus::Outdated);
    QCOMPARE(map["compiler-rt"].installStatus, InstallStatus::Outdated);
    QCOMPARE(map["lld"].installStatus, InstallStatus::Outdated);
    QCOMPARE(map["llvm"].installStatus, InstallStatus::Outdated);

    // Locked ports (%l == yes)
    QCOMPARE(map["clang"].lockStatus, LockStatus::Locked);
    QCOMPARE(map["compiler-rt"].lockStatus, LockStatus::Locked);
    QCOMPARE(map["lld"].lockStatus, LockStatus::Locked);
    QCOMPARE(map["llvm"].lockStatus, LockStatus::Locked);
    QCOMPARE(map["rust"].lockStatus, LockStatus::Locked);

    // Outdated locked detection
    QVERIFY(map["clang"].isOutdatedLocked());
    QVERIFY(map["compiler-rt"].isOutdatedLocked());
    QVERIFY(map["lld"].isOutdatedLocked());
    QVERIFY(map["llvm"].isOutdatedLocked());

    // Scripts detection
    QCOMPARE(preInstallCount, 39);
    QCOMPARE(postInstallCount, 76);

    // Specific port checks
    QVERIFY(map["adwaita-icon-theme"].hasPostInstall);
    QVERIFY(map["adwaita-icon-theme"].hasReadme);
    QVERIFY(map["avahi"].hasPreInstall);
}

QTEST_MAIN(TestPrintfParser)
#include "test_printf_parser.moc"
