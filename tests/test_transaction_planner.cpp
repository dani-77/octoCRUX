#include <QTest>
#include <QFile>
#include <QTextStream>
#include <QMap>
#include <QTemporaryDir>
#include <QDir>

#include "backend/crux_adapter.h"
#include "operations/transaction_planner.h"
#include "core/printf_parser.h"

using namespace OctoCRUX;

class MockRunnerForPlanner : public ICommandRunner {
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

class TestTransactionPlanner : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testInstallPlanSimplePort();
    void testInstallPlanDependenciesPartitioning();
    void testInstallPlanLockedUninstalledError();
    void testUpgradePlanAllOutdatedWithLocks();
    void testRemovalPlanDirectDependents();
    void testRemovalPlanRecursiveCascade();
    void testRemoveGlibcBlockedAsProtectedPackage();
    void testRemoveNonCorePackageAllowed();
    void testUpgradeCorePackageAllowed();
    void testCorePackageOverriddenByOverlayStaysProtected();
    void testNoCoreCollectionConfiguredFailsClosed();
    void testNonexistentPortHandling();
    void testFilesystemMountInspection();
    void testLockTogglePlan();

private:
    QString readFile(const QString &filePath);

    QString m_sourceDir;
    std::shared_ptr<MockRunnerForPlanner> m_runner;
    std::shared_ptr<CruxAdapter> m_adapter;
    std::unique_ptr<TransactionPlanner> m_planner;
};

QString TestTransactionPlanner::readFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream in(&file);
    return in.readAll();
}

void TestTransactionPlanner::initTestCase() {
    m_sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);
    m_runner = std::make_shared<MockRunnerForPlanner>();

    // 1. Mock prt-get printf with canonical production 11-token fixture
    const QString printfOut = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-production-11token.out"));
    QVERIFY(!printfOut.isEmpty());

    CommandResult printfRes;
    printfRes.exitCode = 0;
    printfRes.stdOut = printfOut;

    const QStringList printfArgs = {
        QStringLiteral("printf"),
        QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)
    };
    m_runner->registerResponse(QStringLiteral("prt-get"), printfArgs, printfRes);

    // 2. Mock prt-get listlocked with real locked packages
    const QString locksOut = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/prtget-listlocked.out"));
    CommandResult locksRes;
    locksRes.exitCode = 0;
    locksRes.stdOut = locksOut;
    m_runner->registerResponse(QStringLiteral("prt-get"), {QStringLiteral("listlocked")}, locksRes);

    // 3. Mock prt-get quickdep for gtk3
    const QString quickdepGtk3Out = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-quickdep.out"));
    CommandResult qdepGtk3Res;
    qdepGtk3Res.exitCode = 0;
    qdepGtk3Res.stdOut = quickdepGtk3Out;
    m_runner->registerResponse(QStringLiteral("prt-get"), {QStringLiteral("quickdep"), QStringLiteral("gtk3")}, qdepGtk3Res);

    // 4. Mock prt-get dependent for gtk3
    const QString depGtk3Out = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/cmd/inst-dependent.out"));
    CommandResult depGtk3Res;
    depGtk3Res.exitCode = 0;
    depGtk3Res.stdOut = depGtk3Out;
    m_runner->registerResponse(QStringLiteral("prt-get"), {QStringLiteral("dependent"), QStringLiteral("gtk3")}, depGtk3Res);

    // 5. Mock prt-get quickdep for a2ps
    CommandResult qdepA2psRes;
    qdepA2psRes.exitCode = 0;
    qdepA2psRes.stdOut = QStringLiteral(" boehm-gc paper a2ps \n");
    m_runner->registerResponse(QStringLiteral("prt-get"), {QStringLiteral("quickdep"), QStringLiteral("a2ps")}, qdepA2psRes);

    // 6. Mock prt-get quickdep for acl (already installed with installed dependency attr)
    CommandResult qdepAclRes;
    qdepAclRes.exitCode = 0;
    qdepAclRes.stdOut = QStringLiteral(" attr acl \n");
    m_runner->registerResponse(QStringLiteral("prt-get"), {QStringLiteral("quickdep"), QStringLiteral("acl")}, qdepAclRes);

    // 7. Mock prt-get dependent for libnotify (for cascade test)
    CommandResult depNotifyRes;
    depNotifyRes.exitCode = 0;
    depNotifyRes.stdOut = QStringLiteral("notification-daemon\n");
    m_runner->registerResponse(QStringLiteral("prt-get"), {QStringLiteral("dependent"), QStringLiteral("libnotify")}, depNotifyRes);

    // 8. Mock prt-get dependent for glibc
    CommandResult depGlibcRes;
    depGlibcRes.exitCode = 0;
    depGlibcRes.stdOut = QStringLiteral("libxcrypt\n");
    m_runner->registerResponse(QStringLiteral("prt-get"), {QStringLiteral("dependent"), QStringLiteral("glibc")}, depGlibcRes);

    // Initialize CruxAdapter with fixture prefix
    m_adapter = std::make_shared<CruxAdapter>(m_runner);
    m_adapter->setRootPrefix(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/files"));

    m_planner = std::make_unique<TransactionPlanner>(m_adapter);
}

void TestTransactionPlanner::testInstallPlanSimplePort() {
    // a2ps quickdep: boehm-gc paper a2ps
    // In our bare-metal CRUX 3.8 fixture, a2ps, boehm-gc, and paper are uninstalled (%i == "no")
    TransactionPlan plan = m_planner->planInstall({QStringLiteral("a2ps")});

    QVERIFY(plan.isValid());
    QCOMPARE(plan.requestedTargets.size(), 1);
    QCOMPARE(plan.requestedTargets.first(), QStringLiteral("a2ps"));

    // Verify all 3 uninstalled ports are scheduled for installation in topological build order
    QCOMPARE(plan.toInstall.size(), 3);
    QCOMPARE(plan.toInstall.at(0), QStringLiteral("boehm-gc"));
    QCOMPARE(plan.toInstall.at(1), QStringLiteral("paper"));
    QCOMPARE(plan.toInstall.at(2), QStringLiteral("a2ps"));
    QCOMPARE(plan.steps.size(), 3);
    QCOMPARE(plan.steps.at(0).action, TransactionAction::Install);

    // Now test acl (where acl and its dependency attr are both installed)
    TransactionPlan planAcl = m_planner->planInstall({QStringLiteral("acl")});
    QVERIFY(planAcl.isValid());
    QVERIFY(planAcl.toInstall.isEmpty());
    QVERIFY(planAcl.satisfiedDependencies.contains(QStringLiteral("acl")));
    QVERIFY(planAcl.satisfiedDependencies.contains(QStringLiteral("attr")));
}

void TestTransactionPlanner::testInstallPlanDependenciesPartitioning() {
    // gtk3 has 118 packages in quickdep
    TransactionPlan plan = m_planner->planInstall({QStringLiteral("gtk3")});

    QVERIFY(plan.isValid());
    QCOMPARE(plan.requestedTargets.first(), QStringLiteral("gtk3"));
    QCOMPARE(plan.recursiveInstallOrder.size(), 118);

    // SCOFS verification: A naive planner would queue all 118 packages for build.
    // In reality, gtk3 and most of its dependencies are already installed on this CRUX system!
    QVERIFY(plan.satisfiedDependencies.size() > 50);
    // The number of actual actions must only reflect uninstalled or outdated packages
    QVERIFY(plan.steps.size() < 118);
}

void TestTransactionPlanner::testInstallPlanLockedUninstalledError() {
    // rust is locked in uninstalled state in opt to prevent accidental multi-hour builds
    TransactionPlan plan = m_planner->planInstall({QStringLiteral("rust")});

    // Must be flagged as invalid because an explicitly locked uninstalled port cannot be built
    QVERIFY(!plan.isValid());
    QVERIFY(!plan.errors.isEmpty());
    QVERIFY(plan.errors.first().contains(QStringLiteral("locked in uninstalled state")));
}

void TestTransactionPlanner::testUpgradePlanAllOutdatedWithLocks() {
    // In CRUX 3.8 fixture, there are 4 outdated ports: clang, compiler-rt, lld, llvm.
    // All 4 are locked! prt-get diff hides them.
    TransactionPlan plan = m_planner->planUpgrade();

    QVERIFY(plan.isValid());
    // SCOFS verification: locked packages must NOT be queued into toUpdate!
    QVERIFY(plan.toUpdate.isEmpty());
    QCOMPARE(plan.lockedSkipped.size(), 4);
    QVERIFY(plan.lockedSkipped.contains(QStringLiteral("llvm")));
    QVERIFY(plan.lockedSkipped.contains(QStringLiteral("clang")));
    QVERIFY(plan.lockedSkipped.contains(QStringLiteral("lld")));
    QVERIFY(plan.lockedSkipped.contains(QStringLiteral("compiler-rt")));
}

void TestTransactionPlanner::testRemovalPlanDirectDependents() {
    // gtk3 removal: fixture has exactly 7 installed packages depending directly on gtk3
    TransactionPlan plan = m_planner->planRemove({QStringLiteral("gtk3")});

    QVERIFY(plan.isValid());
    QCOMPARE(plan.toRemove.size(), 1);
    QCOMPARE(plan.toRemove.first(), QStringLiteral("gtk3"));

    // Verify exactly 7 direct installed dependents from inst-dependent.out:
    // tint2, granite, firefox-bin, lxappearance, thunderbird-bin, glade, libnotify
    QCOMPARE(plan.reverseDependentsDirect.size(), 7);
    QVERIFY(plan.reverseDependentsDirect.contains(QStringLiteral("tint2")));
    QVERIFY(plan.reverseDependentsDirect.contains(QStringLiteral("granite")));
    QVERIFY(plan.reverseDependentsDirect.contains(QStringLiteral("firefox-bin")));
    QVERIFY(plan.reverseDependentsDirect.contains(QStringLiteral("lxappearance")));
    QVERIFY(plan.reverseDependentsDirect.contains(QStringLiteral("thunderbird-bin")));
    QVERIFY(plan.reverseDependentsDirect.contains(QStringLiteral("glade")));
    QVERIFY(plan.reverseDependentsDirect.contains(QStringLiteral("libnotify")));

    QCOMPARE(plan.steps.size(), 1);
    QCOMPARE(plan.steps.first().action, TransactionAction::Remove);
    QCOMPARE(plan.steps.first().directDependents.size(), 7);
}

void TestTransactionPlanner::testRemovalPlanRecursiveCascade() {
    // gtk3 has libnotify; libnotify has notification-daemon.
    // The recursive cascade must contain the 7 direct dependents PLUS notification-daemon!
    TransactionPlan plan = m_planner->planRemove({QStringLiteral("gtk3")});

    QVERIFY(plan.reverseDependentsCascade.size() >= 8);
    QVERIFY(plan.reverseDependentsCascade.contains(QStringLiteral("libnotify")));
    QVERIFY(plan.reverseDependentsCascade.contains(QStringLiteral("notification-daemon")));
}

void TestTransactionPlanner::testRemoveGlibcBlockedAsProtectedPackage() {
    // 1. Attempting to remove glibc (core collection port)
    TransactionPlan planGlibc = m_planner->planRemove({QStringLiteral("glibc")});

    // Plan must be invalid (blocked)
    QVERIFY(!planGlibc.isValid());

    // Must be recorded in protectedPackagesBlocked
    QVERIFY(planGlibc.hasProtectedPackages());
    QVERIFY(planGlibc.protectedPackagesBlocked.contains(QStringLiteral("glibc")));

    // Planner must NEVER produce a remove step/argv for a protected package
    QVERIFY(planGlibc.steps.isEmpty());
    QVERIFY(!planGlibc.toRemove.contains(QStringLiteral("glibc")));

    // User-facing message must match the policy text exactly
    const QString expectedMsg = QStringLiteral(
        "glibc belongs to the CRUX core collection. OctoCRUX does not remove core packages, "
        "because doing so can leave the system unable to run programs or boot. "
        "If you really intend to remove it, use 'prt-get remove' or 'pkgrm' from a terminal."
    );
    QVERIFY(planGlibc.errors.contains(expectedMsg));
}

void TestTransactionPlanner::testRemoveNonCorePackageAllowed() {
    // Attempting to remove an installed non-core package (gtk3 in opt)
    TransactionPlan plan = m_planner->planRemove({QStringLiteral("gtk3")});

    QVERIFY(plan.isValid());
    QVERIFY(!plan.hasProtectedPackages());
    QCOMPARE(plan.toRemove.size(), 1);
    QCOMPARE(plan.toRemove.first(), QStringLiteral("gtk3"));
    QCOMPARE(plan.steps.size(), 1);
    QCOMPARE(plan.steps.first().action, TransactionAction::Remove);
    QCOMPARE(plan.steps.first().portName, QStringLiteral("gtk3"));
}

void TestTransactionPlanner::testUpgradeCorePackageAllowed() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString etcDir = tempDir.path() + QStringLiteral("/etc");
    QDir().mkpath(etcDir);
    const QString corePortsDir = tempDir.path() + QStringLiteral("/usr/ports/core");
    QDir().mkpath(corePortsDir + QStringLiteral("/glibc"));

    QFile confFile(etcDir + QStringLiteral("/prt-get.conf"));
    QVERIFY(confFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&confFile);
    out << "prtdir /usr/ports/core\n";
    confFile.close();

    auto tempRunner = std::make_shared<MockRunnerForPlanner>();
    // Synthetic core port glibc with %i="diff" -> InstallStatus::Outdated
    const QString glibcOutdatedLine = QStringLiteral(
        "glibc\t2.41\t1\tdiff\tno\t/usr/ports/core/glibc\tcore\tno\tyes\t\tGNU C Library\n"
    );
    tempRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("printf"), QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)},
        {0, glibcOutdatedLine, QString()}
    );

    auto tempAdapter = std::make_shared<CruxAdapter>(tempRunner);
    tempAdapter->setRootPrefix(tempDir.path());

    TransactionPlanner tempPlanner(tempAdapter);
    TransactionPlan plan = tempPlanner.planUpgrade({QStringLiteral("glibc")});

    // Updating a core package must be allowed and planned normally
    QVERIFY(plan.isValid());
    QVERIFY(!plan.hasProtectedPackages());
    QCOMPARE(plan.steps.size(), 1);
    QCOMPARE(plan.steps.first().action, TransactionAction::Upgrade);
    QCOMPARE(plan.steps.first().portName, QStringLiteral("glibc"));
}

void TestTransactionPlanner::testCorePackageOverriddenByOverlayStaysProtected() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString etcDir = tempDir.path() + QStringLiteral("/etc");
    QDir().mkpath(etcDir);

    const QString overlayPortsDir = tempDir.path() + QStringLiteral("/usr/ports/d77crux");
    const QString corePortsDir = tempDir.path() + QStringLiteral("/usr/ports/core");
    QDir().mkpath(overlayPortsDir + QStringLiteral("/glibc"));
    QDir().mkpath(corePortsDir + QStringLiteral("/glibc"));

    // Write prt-get.conf with canonical paths: overlay before core
    QFile confFile(etcDir + QStringLiteral("/prt-get.conf"));
    QVERIFY(confFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&confFile);
    out << "prtdir /usr/ports/d77crux\n";
    out << "prtdir /usr/ports/core\n";
    confFile.close();

    auto tempRunner = std::make_shared<MockRunnerForPlanner>();
    // Mock printf where winning port reports collection "d77crux"
    const QString glibcOverlayLine = QStringLiteral(
        "glibc\t2.40\t1\tyes\tno\t/usr/ports/d77crux/glibc\td77crux\tno\tyes\t\tGNU C Library\n"
    );
    tempRunner->registerResponse(
        QStringLiteral("prt-get"),
        {QStringLiteral("printf"), QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)},
        {0, glibcOverlayLine, QString()}
    );

    auto tempAdapter = std::make_shared<CruxAdapter>(tempRunner);
    tempAdapter->setRootPrefix(tempDir.path());

    // isCorePackage must check disk presence in core collection, ignoring overlay winning collection
    QVERIFY(tempAdapter->isCorePackage(QStringLiteral("glibc")));

    TransactionPlanner tempPlanner(tempAdapter);
    TransactionPlan plan = tempPlanner.planRemove({QStringLiteral("glibc")});

    // Removal must be blocked!
    QVERIFY(!plan.isValid());
    QVERIFY(plan.hasProtectedPackages());
    QVERIFY(plan.steps.isEmpty());
    QVERIFY(plan.errors.first().contains(QStringLiteral("belongs to the CRUX core collection")));
}

void TestTransactionPlanner::testNoCoreCollectionConfiguredFailsClosed() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString etcDir = tempDir.path() + QStringLiteral("/etc");
    QDir().mkpath(etcDir);

    // Write prt-get.conf with NO core collection
    QFile confFile(etcDir + QStringLiteral("/prt-get.conf"));
    QVERIFY(confFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&confFile);
    out << "prtdir /usr/ports/opt\n";
    confFile.close();

    auto tempRunner = std::make_shared<MockRunnerForPlanner>();
    auto tempAdapter = std::make_shared<CruxAdapter>(tempRunner);
    tempAdapter->setRootPrefix(tempDir.path());

    TransactionPlanner tempPlanner(tempAdapter);
    TransactionPlan plan = tempPlanner.planRemove({QStringLiteral("gtk3")});

    // Fails closed: removals blocked because no core collection is configured
    QVERIFY(!plan.isValid());
    QVERIFY(plan.steps.isEmpty());
    QVERIFY(plan.errors.first().contains(QStringLiteral("No 'core' collection is configured in /etc/prt-get.conf")));
}

void TestTransactionPlanner::testNonexistentPortHandling() {
    TransactionPlan plan = m_planner->planInstall({QStringLiteral("nonexistent-pkg-999")});
    QVERIFY(!plan.isValid());
    QVERIFY(plan.errors.first().contains(QStringLiteral("does not exist in the ports tree")));

    TransactionPlan planRem = m_planner->planRemove({QStringLiteral("nonexistent-pkg-999")});
    QVERIFY(!planRem.isValid());
    QVERIFY(planRem.errors.first().contains(QStringLiteral("does not exist in the ports tree")));
}

void TestTransactionPlanner::testFilesystemMountInspection() {
    FilesystemInfo tmpInfo = TransactionPlanner::inspectMount(QStringLiteral("/tmp"));
    QVERIFY(tmpInfo.availableBytes > 0);
    QVERIFY(tmpInfo.totalBytes > 0);
    QVERIFY(!tmpInfo.mountPoint.isEmpty());
    QVERIFY(!tmpInfo.fsType.isEmpty());

    FilesystemInfo rootInfo = TransactionPlanner::inspectMount(QStringLiteral("/"));
    QCOMPARE(rootInfo.mountPoint, QStringLiteral("/"));
    QVERIFY(rootInfo.availableBytes > 0);
    QVERIFY(rootInfo.totalBytes > 0);
}

void TestTransactionPlanner::testLockTogglePlan() {
    TransactionPlan lockPlan = m_planner->planLockToggle(QStringLiteral("a2ps"), true);
    QVERIFY(lockPlan.isValid());
    QCOMPARE(lockPlan.steps.size(), 1);
    QCOMPARE(lockPlan.steps.first().action, TransactionAction::Lock);
    QVERIFY(lockPlan.steps.first().isLocked);

    TransactionPlan unlockPlan = m_planner->planLockToggle(QStringLiteral("a2ps"), false);
    QVERIFY(unlockPlan.isValid());
    QCOMPARE(unlockPlan.steps.size(), 1);
    QCOMPARE(unlockPlan.steps.first().action, TransactionAction::Unlock);
    QVERIFY(!unlockPlan.steps.first().isLocked);
}

QTEST_MAIN(TestTransactionPlanner)
#include "test_transaction_planner.moc"
