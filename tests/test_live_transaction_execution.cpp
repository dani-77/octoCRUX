#include <QTest>
#include <QSignalSpy>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include "backend/crux_adapter.h"
#include "operations/transaction_planner.h"

using namespace OctoCRUX;

class TestLiveTransactionExecution : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testExecutionExceeding30Seconds();
    void testGracefulCancellation();
    void testSingleBatchedInvocation();
    void testMixedUpgradeAndInstallOrdering();

private:
    QString m_fakeScriptPath;
};

void TestLiveTransactionExecution::initTestCase() {
    // Locate the fake slow prt-get script in tests/scripts/fake_slow_prtget.sh
    const QString scriptRel = QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/scripts/fake_slow_prtget.sh");
    QFileInfo fi(scriptRel);
    QVERIFY2(fi.exists(), "fake_slow_prtget.sh must exist in tests/scripts");
    QVERIFY2(fi.isExecutable(), "fake_slow_prtget.sh must be executable");
    m_fakeScriptPath = fi.absoluteFilePath();
}

/**
 * @brief Test that transactions taking > 30 seconds are NOT terminated by the legacy
 * 30-second DefaultCommandRunner timeout, and that stdout streams live line-by-line.
 */
void TestLiveTransactionExecution::testExecutionExceeding30Seconds() {
    auto adapter = std::make_shared<CruxAdapter>();
    adapter->setPrivilegeHelperType(PrivilegeHelperType::None);
    adapter->setPrtGetBinary(m_fakeScriptPath);

    // Set duration to 34 seconds (exceeding legacy 30000ms timeout by 4 seconds)
    qputenv("FAKE_PRTGET_DURATION", "34");

    TransactionPlan plan;
    plan.requestedTargets = {QStringLiteral("longbuild-pkg")};
    TransactionStep step;
    step.portName = QStringLiteral("longbuild-pkg");
    step.action = TransactionAction::Install;
    plan.steps.append(step);

    QSignalSpy spyOutput(adapter.get(), &ICruxBackend::outputReceived);
    QSignalSpy spyFinished(adapter.get(), &ICruxBackend::operationFinished);

    const qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    // Start transaction asynchronously
    const bool started = adapter->executeTransaction(plan);
    QVERIFY(started);
    QVERIFY(adapter->processRunner()->isRunning());

    // 1. Verify live streaming: within 3 seconds, multiple lines must already have arrived
    QTRY_VERIFY_WITH_TIMEOUT(spyOutput.count() >= 3, 4000);

    const int initialOutputCount = spyOutput.count();
    QVERIFY(initialOutputCount >= 3);

    // 2. Wait for completion (must survive beyond 30 seconds!)
    // Wait up to 45 seconds
    QVERIFY2(spyFinished.wait(45000), "Operation failed to complete within 45s (killed by timeout?)");

    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - startTime;

    // Verify it genuinely took > 30 seconds
    QVERIFY2(elapsedMs >= 32000, "Operation did not run for > 30s as intended");

    // Verify successful exit code and error status
    QCOMPARE(spyFinished.count(), 1);
    const QList<QVariant> args = spyFinished.takeFirst();
    const int exitCode = args.at(0).toInt();
    const OperationErrorKind errorKind = args.at(1).value<OperationErrorKind>();

    QCOMPARE(exitCode, 0);
    QCOMPARE(errorKind, OperationErrorKind::None);

    // Verify continuous line streaming: at least 34 steps + headers received
    QVERIFY(spyOutput.count() >= 35);
}

/**
 * @brief Test that user cancellation sends SIGTERM to the process and cleanly
 * terminates execution without orphan builds or SIGKILL.
 */
void TestLiveTransactionExecution::testGracefulCancellation() {
    auto adapter = std::make_shared<CruxAdapter>();
    adapter->setPrivilegeHelperType(PrivilegeHelperType::None);
    adapter->setPrtGetBinary(m_fakeScriptPath);

    // Set duration to 30 seconds so it would keep running if not cancelled
    qputenv("FAKE_PRTGET_DURATION", "30");

    TransactionPlan plan;
    plan.requestedTargets = {QStringLiteral("cancel-pkg")};
    TransactionStep step;
    step.portName = QStringLiteral("cancel-pkg");
    step.action = TransactionAction::Install;
    plan.steps.append(step);

    QSignalSpy spyOutput(adapter.get(), &ICruxBackend::outputReceived);
    QSignalSpy spyFinished(adapter.get(), &ICruxBackend::operationFinished);

    const bool started = adapter->executeTransaction(plan);
    QVERIFY(started);
    QVERIFY(adapter->processRunner()->isRunning());

    // Wait until at least 2 lines arrive (second 2 of execution)
    QTRY_VERIFY_WITH_TIMEOUT(spyOutput.count() >= 2, 4000);

    // Request user cancellation
    adapter->cancelActiveOperation();

    // Cancellation must finish promptly (within 3000ms)
    QVERIFY2(spyFinished.wait(3000), "Process did not terminate promptly after cancellation");

    QVERIFY(!adapter->processRunner()->isRunning());
    QCOMPARE(spyFinished.count(), 1);

    const QList<QVariant> args = spyFinished.takeFirst();
    const OperationErrorKind errorKind = args.at(1).value<OperationErrorKind>();
    const QString summary = args.at(2).toString();

    QCOMPARE(errorKind, OperationErrorKind::Cancelled);
    QVERIFY(summary.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));

    // Verify the fake process trapped SIGTERM and emitted clean termination log
    bool sawSigtermLog = false;
    for (const auto &out : spyOutput) {
        if (out.at(0).toString().contains(QStringLiteral("SIGTERM"), Qt::CaseInsensitive)) {
            sawSigtermLog = true;
            break;
        }
    }
    QVERIFY2(sawSigtermLog, "Child process did not receive or log SIGTERM cancellation signal");
}

/**
 * @brief Test that a transaction with multiple targets is invoked as a SINGLE command
 * rather than multiple invocations, ensuring only ONE password elevation prompt.
 */
void TestLiveTransactionExecution::testSingleBatchedInvocation() {
    const QString probeLogPath = QStringLiteral("/tmp/octocrux_probe_batch_test.log");
    QFile::remove(probeLogPath);
    qputenv("OCTOCRUX_PROBE_LOG", probeLogPath.toLocal8Bit());
    qputenv("FAKE_PRTGET_DURATION", "1");

    auto adapter = std::make_shared<CruxAdapter>();
    adapter->setPrivilegeHelperType(PrivilegeHelperType::None);
    adapter->setPrtGetBinary(m_fakeScriptPath);

    // Plan with 3 targets
    TransactionPlan plan;
    plan.requestedTargets = {QStringLiteral("pkg1"), QStringLiteral("pkg2"), QStringLiteral("pkg3")};

    TransactionStep s1{TransactionAction::Install, QStringLiteral("pkg1"), {}, {}, {}, false, false, false, {}, {}};
    TransactionStep s2{TransactionAction::Install, QStringLiteral("pkg2"), {}, {}, {}, false, false, false, {}, {}};
    TransactionStep s3{TransactionAction::Install, QStringLiteral("pkg3"), {}, {}, {}, false, false, false, {}, {}};
    plan.steps = {s1, s2, s3};

    QSignalSpy spyFinished(adapter.get(), &ICruxBackend::operationFinished);

    const bool started = adapter->executeTransaction(plan);
    QVERIFY(started);

    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyFinished.count(), 1);

    // Inspect the probe log file to verify the exact command line executed
    QFile probeFile(probeLogPath);
    QVERIFY(probeFile.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(probeFile.readAll()).trimmed();
    probeFile.close();
    QFile::remove(probeLogPath);

    // There must be EXACTLY ONE invocation line
    const QStringList lines = content.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 1);

    // The single invocation must pass all targets to depinst: 'depinst pkg1 pkg2 pkg3'
    QCOMPARE(lines.first(), QStringLiteral("depinst pkg1 pkg2 pkg3"));

    qunsetenv("OCTOCRUX_PROBE_LOG");
}

/**
 * @brief Test that a composite transaction containing both Upgrade and Install steps
 * orders operations correctly: Upgrade first, then Install, with each batched into a
 * single command (resulting in two sequential elevations).
 */
void TestLiveTransactionExecution::testMixedUpgradeAndInstallOrdering() {
    const QString probeLogPath = QStringLiteral("/tmp/octocrux_probe_mixed_order_test.log");
    QFile::remove(probeLogPath);
    qputenv("OCTOCRUX_PROBE_LOG", probeLogPath.toLocal8Bit());
    qputenv("FAKE_PRTGET_DURATION", "1");

    auto adapter = std::make_shared<CruxAdapter>();
    adapter->setPrivilegeHelperType(PrivilegeHelperType::None);
    adapter->setPrtGetBinary(m_fakeScriptPath);

    // Plan with mixed actions: Install pkgA, Upgrade pkgB, Install pkgC, Upgrade pkgD
    TransactionPlan plan;
    plan.requestedTargets = {QStringLiteral("pkgA"), QStringLiteral("pkgB"), QStringLiteral("pkgC"), QStringLiteral("pkgD")};

    TransactionStep s1{TransactionAction::Install, QStringLiteral("pkgA"), {}, {}, {}, false, false, false, {}, {}};
    TransactionStep s2{TransactionAction::Upgrade, QStringLiteral("pkgB"), {}, {}, {}, false, false, false, {}, {}};
    TransactionStep s3{TransactionAction::Install, QStringLiteral("pkgC"), {}, {}, {}, false, false, false, {}, {}};
    TransactionStep s4{TransactionAction::Upgrade, QStringLiteral("pkgD"), {}, {}, {}, false, false, false, {}, {}};
    plan.steps = {s1, s2, s3, s4};

    QSignalSpy spyFinished(adapter.get(), &ICruxBackend::operationFinished);

    const bool started = adapter->executeTransaction(plan);
    QVERIFY(started);

    QVERIFY(spyFinished.wait(10000));
    QCOMPARE(spyFinished.count(), 1);

    // Inspect the probe log file to verify the exact command line executed and ordering
    QFile probeFile(probeLogPath);
    QVERIFY(probeFile.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(probeFile.readAll()).trimmed();
    probeFile.close();
    QFile::remove(probeLogPath);

    // There must be EXACTLY TWO invocation lines (one for updates, one for installs)
    const QStringList lines = content.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);

    // Invariant: Upgrade must run FIRST, then Install (depinst)
    QCOMPARE(lines.at(0), QStringLiteral("update pkgB pkgD"));
    QCOMPARE(lines.at(1), QStringLiteral("depinst pkgA pkgC"));

    qunsetenv("OCTOCRUX_PROBE_LOG");
}

QTEST_MAIN(TestLiveTransactionExecution)
#include "test_live_transaction_execution.moc"
