#include <QTest>
#include <QFile>
#include <QTextStream>
#include <QMap>

#include "backend/crux_adapter.h"
#include "core/printf_parser.h"

using namespace OctoCRUX;

class MockCommandRunner : public ICommandRunner {
public:
    void registerResponse(const QString &program, const QStringList &arguments, const CommandResult &result) {
        m_responses.insert(makeKey(program, arguments), result);
    }

    CommandResult run(const QString &program, const QStringList &arguments, int timeoutMs) override {
        Q_UNUSED(timeoutMs);
        const QString key = makeKey(program, arguments);
        if (m_responses.contains(key)) {
            return m_responses.value(key);
        }

        // Return failed command by default
        CommandResult failure;
        failure.exitCode = 127;
        failure.stdErr = QStringLiteral("Command not mocked: ") + key;
        return failure;
    }

private:
    static QString makeKey(const QString &program, const QStringList &arguments) {
        return program + QLatin1Char(' ') + arguments.join(QLatin1Char(' '));
    }

    QMap<QString, CommandResult> m_responses;
};

class TestCruxAdapter : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testQueryAllPortsAndThreeAxes();
    void testQueryPort();
    void testQueryOutdatedAndLockedPorts();
    void testQueryAliases();
    void testDependents();
    void testQuickdepWithAliasesResolution();
    void testReadPortFile();
    void testPkginfoFilesAndOwner();
    void testExecutionAndMutations();
    void testCorePackageDetection();

private:
    QString readFile(const QString &filePath);

    QString m_sourceDir;
    std::shared_ptr<MockCommandRunner> m_mockRunner;
    std::unique_ptr<CruxAdapter> m_adapter;
};

QString TestCruxAdapter::readFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream in(&file);
    return in.readAll();
}

void TestCruxAdapter::initTestCase() {
    m_sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);
    m_mockRunner = std::make_shared<MockCommandRunner>();

    // 1. Mock prt-get printf with real bare-metal CRUX 3.8 fixture
    const QString printfCmd = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-production-11token.cmd"));
    QVERIFY(!printfCmd.isEmpty());
    // Empirical provenance check: ensure the captured .cmd argv matches PrintfParser::PRODUCTION_FORMAT verbatim
    const QString expectedFormat = QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT);
    QVERIFY2(printfCmd.contains(expectedFormat), "Captured .cmd argv must match PrintfParser::PRODUCTION_FORMAT");

    const QString printfOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-production-11token.out"));
    QVERIFY(!printfOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("printf"), expectedFormat},
        {0, printfOutput, QString()}
    );

    // 2. Mock prt-get dependent gtk3 (direct installed)
    const QString dependentOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-dependent.out"));
    QVERIFY(!dependentOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("dependent"), QStringLiteral("gtk3")},
        {0, dependentOutput, QString()}
    );

    // 3. Mock prt-get dependent --all gtk3
    const QString dependentAllOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-dependent-all.out"));
    QVERIFY(!dependentAllOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("dependent"), QStringLiteral("--all"), QStringLiteral("gtk3")},
        {0, dependentAllOutput, QString()}
    );

    // 4. Mock prt-get quickdep gtk3
    const QString quickdepOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-quickdep.out"));
    QVERIFY(!quickdepOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("quickdep"), QStringLiteral("gtk3")},
        {0, quickdepOutput, QString()}
    );

    // 5. Mock pkginfo -l gtk3
    const QString pkginfoLOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-pkginfo-l.out"));
    QVERIFY(!pkginfoLOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("pkginfo"),
        {QStringLiteral("-l"), QStringLiteral("gtk3")},
        {0, pkginfoLOutput, QString()}
    );

    // 6. Mock pkginfo -o usr/bin/prt-get (relative path, no metacharacters needing escape)
    const QString pkginfoOOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/pkginfo-o-prtget-abs.out"));
    QVERIFY(!pkginfoOOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("pkginfo"),
        {QStringLiteral("-o"), QStringLiteral("usr/bin/prt-get")},
        {0, pkginfoOOutput, QString()}
    );

    // 7. Mock pkginfo -o 'usr/bin/g\+\+' (metacharacters '+' escaped via QRegularExpression::escape)
    // Fixture captured directly on T480s (pkginfo-o-disc-regex-escaped.out)
    const QString gxxOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/pkginfo-o-disc-regex-escaped.out"));
    QVERIFY(!gxxOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("pkginfo"),
        {QStringLiteral("-o"), QStringLiteral("usr/bin/g\\+\\+")},
        {0, gxxOutput, QString()}
    );

    // 8. Mock pkginfo -o 'usr/lib/libstdc\+\+\.so\.6' (tests post-filtering of multiple candidates returned by substring match)
    // Fixture from T480s manual run: returns 3 records from gcc (.so.6, .so.6.0.33, -gdb.py)
    const QString stdcxxOutput = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/pkginfo-o-stdcxx-escaped.out"));
    QVERIFY(!stdcxxOutput.isEmpty());
    m_mockRunner->registerResponse(
        QStringLiteral("pkginfo"),
        {QStringLiteral("-o"), QStringLiteral("usr/lib/libstdc\\+\\+\\.so\\.6")},
        {0, stdcxxOutput, QString()}
    );

    // Initialize CruxAdapter and point root prefix to fixtures directory
    m_adapter = std::make_unique<CruxAdapter>(m_mockRunner);
    m_adapter->setRootPrefix(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/files"));
}

void TestCruxAdapter::testQueryAllPortsAndThreeAxes() {
    const QList<PortInfo> ports = m_adapter->queryAllPorts();
    QCOMPARE(ports.size(), 2307);

    QMap<QString, PortInfo> map;
    for (const PortInfo &p : ports) {
        map.insert(p.name, p);
    }

    // 1. Outdated and locked ports
    for (const QString &name : {QStringLiteral("clang"), QStringLiteral("compiler-rt"), QStringLiteral("lld"), QStringLiteral("llvm")}) {
        QVERIFY(map.contains(name));
        const PortInfo &p = map[name];
        QCOMPARE(p.installStatus, InstallStatus::Outdated);
        QCOMPARE(p.lockStatus, LockStatus::Locked);
        QVERIFY(p.isOutdatedLocked());
    }

    // 2. Virtual alias port: rust is satisfied by installed rust-bin
    QVERIFY(map.contains("rust"));
    const PortInfo &rust = map["rust"];
    QCOMPARE(rust.installStatus, InstallStatus::NotInstalled);
    QCOMPARE(rust.lockStatus, LockStatus::Locked);
    QCOMPARE(rust.providedByAlias, QStringLiteral("rust-bin"));
    QVERIFY(rust.isLockedUninstalled());

    // rust-bin itself is installed and unlocked
    QVERIFY(map.contains("rust-bin"));
    const PortInfo &rustBin = map["rust-bin"];
    QCOMPARE(rustBin.installStatus, InstallStatus::Installed);
    QCOMPARE(rustBin.lockStatus, LockStatus::Unlocked);
}

void TestCruxAdapter::testQueryPort() {
    // Existing port
    auto portOpt = m_adapter->queryPort(QStringLiteral("gtk3"));
    QVERIFY(portOpt.has_value());
    const PortInfo &gtk3 = portOpt.value();
    QCOMPARE(gtk3.name, QStringLiteral("gtk3"));
    QCOMPARE(gtk3.version, QStringLiteral("3.24.52"));
    QCOMPARE(gtk3.release, QStringLiteral("1"));
    QCOMPARE(gtk3.collection, QStringLiteral("opt"));
    QCOMPARE(gtk3.installStatus, InstallStatus::Installed);
    QCOMPARE(gtk3.dependencies, QStringList({QStringLiteral("at-spi2-core"), QStringLiteral("gdk-pixbuf"), QStringLiteral("libepoxy"), QStringLiteral("pango")}));

    // Nonexistent port
    auto nonExistent = m_adapter->queryPort(QStringLiteral("nonexistent-pkg-xyz"));
    QVERIFY(!nonExistent.has_value());
}

void TestCruxAdapter::testQueryOutdatedAndLockedPorts() {
    QStringList outdated = m_adapter->queryOutdatedPorts();
    outdated.sort();
    const QStringList expectedOutdated = {
        QStringLiteral("clang"),
        QStringLiteral("compiler-rt"),
        QStringLiteral("lld"),
        QStringLiteral("llvm")
    };
    QCOMPARE(outdated, expectedOutdated);

    QStringList locked = m_adapter->queryLockedPorts();
    locked.sort();
    const QStringList expectedLocked = {
        QStringLiteral("clang"),
        QStringLiteral("compiler-rt"),
        QStringLiteral("lld"),
        QStringLiteral("llvm"),
        QStringLiteral("rust")
    };
    QCOMPARE(locked, expectedLocked);
}

void TestCruxAdapter::testQueryAliases() {
    const QMap<QString, QString> aliases = m_adapter->queryAliases();
    QCOMPARE(aliases.size(), 4);
    QCOMPARE(aliases.value(QStringLiteral("rust")), QStringLiteral("rust-bin"));
    QCOMPARE(aliases.value(QStringLiteral("openjdk8")), QStringLiteral("jdk8-bin"));
    QCOMPARE(aliases.value(QStringLiteral("librsvg")), QStringLiteral("librsvg-compat"));
    QCOMPARE(aliases.value(QStringLiteral("mysql")), QStringLiteral("mariadb"));
}

void TestCruxAdapter::testDependents() {
    // Direct installed dependents
    const QStringList direct = m_adapter->queryDirectInstalledDependents(QStringLiteral("gtk3"));
    QCOMPARE(direct.size(), 7);
    QCOMPARE(direct.first(), QStringLiteral("tint2"));
    QCOMPARE(direct.last(), QStringLiteral("libnotify"));

    // All declared dependents
    const QStringList all = m_adapter->queryAllDeclaredDependents(QStringLiteral("gtk3"));
    QCOMPARE(all.size(), 38);

    // Nonexistent port safely returns empty without invoking prt-get
    const QStringList nonExistent = m_adapter->queryDirectInstalledDependents(QStringLiteral("nonexistent-pkg-xyz"));
    QVERIFY(nonExistent.isEmpty());
}

void TestCruxAdapter::testQuickdepWithAliasesResolution() {
    const QStringList order = m_adapter->queryRecursiveInstallOrder(QStringLiteral("gtk3"));
    QCOMPARE(order.size(), 118);
    QCOMPARE(order.first(), QStringLiteral("expat"));
    QCOMPARE(order.last(), QStringLiteral("gtk3"));

    // "rust" must be replaced with "rust-bin" in the resolved order
    QVERIFY(!order.contains(QStringLiteral("rust")));
    QVERIFY(order.contains(QStringLiteral("rust-bin")));
}

void TestCruxAdapter::testReadPortFile() {
    // Read Pkgfile for a2ps from fixture directory
    const QString pkgfileContent = m_adapter->readPortFile(QStringLiteral("a2ps"), QStringLiteral("Pkgfile"));
    QVERIFY(!pkgfileContent.isEmpty());
    QVERIFY(pkgfileContent.contains(QStringLiteral("Description: ASCII to Postscript converter (Prettyprint)")));
    QVERIFY(pkgfileContent.contains(QStringLiteral("name=a2ps")));

    // Read .footprint for a2ps
    const QString footprintContent = m_adapter->readPortFile(QStringLiteral("a2ps"), QStringLiteral(".footprint"));
    QVERIFY(!footprintContent.isEmpty());
    QVERIFY(footprintContent.contains(QStringLiteral("usr/bin/a2ps")));

    // Nonexistent port or file returns empty string
    QVERIFY(m_adapter->readPortFile(QStringLiteral("nonexistent"), QStringLiteral("Pkgfile")).isEmpty());
    QVERIFY(m_adapter->readPortFile(QStringLiteral("a2ps"), QStringLiteral("nonexistent_file.txt")).isEmpty());
}

void TestCruxAdapter::testPkginfoFilesAndOwner() {
    // Query files for gtk3
    const QStringList files = m_adapter->queryFilesForInstalledPackage(QStringLiteral("gtk3"));
    QVERIFY(!files.isEmpty());
    QVERIFY(files.contains(QStringLiteral("/usr/bin/gtk-builder-tool")));
    QVERIFY(files.contains(QStringLiteral("/etc/gtk-3.0/im-multipress.conf")));

    // Find owner of /usr/bin/prt-get
    const QString owner = m_adapter->findPackageOwnerOfFile(QStringLiteral("/usr/bin/prt-get"));
    QCOMPARE(owner, QStringLiteral("prt-get"));

    // Find owner of /usr/bin/g++ (tests '+' metacharacter escaping via regex)
    const QString gxxOwner = m_adapter->findPackageOwnerOfFile(QStringLiteral("/usr/bin/g++"));
    QCOMPARE(gxxOwner, QStringLiteral("gcc"));

    // Find owner of /usr/lib/libstdc++.so.6 (tests '+' escaping and post-filtering among multiple substring candidates)
    const QString stdcxxOwner = m_adapter->findPackageOwnerOfFile(QStringLiteral("/usr/lib/libstdc++.so.6"));
    QCOMPARE(stdcxxOwner, QStringLiteral("gcc"));

    // Find owner of unknown file
    const QString noOwner = m_adapter->findPackageOwnerOfFile(QStringLiteral("/unknown/file/path"));
    QVERIFY(noOwner.isEmpty());
}

void TestCruxAdapter::testExecutionAndMutations() {
    m_adapter->setPrivilegeHelperType(PrivilegeHelperType::None);

    // 1. Invalid plan fails
    TransactionPlan invalidPlan;
    invalidPlan.errors.append(QStringLiteral("Unresolvable cyclic dependency"));
    QVERIFY(!m_adapter->executeTransaction(invalidPlan));

    // 2. Empty valid plan succeeds
    TransactionPlan emptyPlan;
    QVERIFY(m_adapter->executeTransaction(emptyPlan));

    // 3. Plan with step executes via runner
    TransactionPlan installPlan;
    TransactionStep step;
    step.portName = QStringLiteral("leaf");
    step.action = TransactionAction::Install;
    installPlan.steps.append(step);

    m_mockRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("depinst"), QStringLiteral("leaf")},
        {0, QStringLiteral("======> Building 'leaf'\nSuccess\n"), QString()}
    );
    QVERIFY(m_adapter->executeTransaction(installPlan));

    // 4. Ports sync
    m_mockRunner->registerResponse(
        QStringLiteral("ports"),
        {QStringLiteral("-u")},
        {0, QStringLiteral("Synchronizing ports tree...\n"), QString()}
    );
    QVERIFY(m_adapter->syncPortsTree());

    // 5. Lock and unlock port
    m_mockRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("lock"), QStringLiteral("llvm")},
        {0, QStringLiteral("llvm locked\n"), QString()}
    );
    QVERIFY(m_adapter->lockPort(QStringLiteral("llvm")));

    m_mockRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("unlock"), QStringLiteral("llvm")},
        {0, QStringLiteral("llvm unlocked\n"), QString()}
    );
    QVERIFY(m_adapter->unlockPort(QStringLiteral("llvm")));
}

void TestCruxAdapter::testCorePackageDetection() {
    auto corePath = m_adapter->coreCollectionPath();
    QVERIFY(corePath.has_value());
    QVERIFY(corePath.value().endsWith(QStringLiteral("/usr/ports/core")));

    // glibc exists in core
    QVERIFY(m_adapter->isCorePackage(QStringLiteral("glibc")));

    // gtk3 exists in opt, not core
    QVERIFY(!m_adapter->isCorePackage(QStringLiteral("gtk3")));

    // Non-existent package
    QVERIFY(!m_adapter->isCorePackage(QStringLiteral("nonexistent_package")));
}

QTEST_MAIN(TestCruxAdapter)
#include "test_crux_adapter.moc"
