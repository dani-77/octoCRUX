#include <QTest>
#include <QSignalSpy>
#include "backend/crux_process_runner.h"

using namespace OctoCRUX;

class TestCruxProcessRunner : public QObject {
    Q_OBJECT

private slots:
    void testSuccessfulExecution();
    void testPhaseDetection();
    void testLineBufferedStreaming();
    void testErrorExitClassification();
    void testCancelOperation();
};

void TestCruxProcessRunner::testSuccessfulExecution() {
    CruxProcessRunner runner;
    QSignalSpy spyFinished(&runner, &CruxProcessRunner::processFinished);
    QSignalSpy spyOutput(&runner, &CruxProcessRunner::outputReceived);

    const bool started = runner.startProcess(QStringLiteral("echo"), {QStringLiteral("Test Line 1")});
    QVERIFY(started);

    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyFinished.count(), 1);

    const QList<QVariant> args = spyFinished.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0); // exitCode
    QCOMPARE(args.at(1).value<OperationErrorKind>(), OperationErrorKind::None);

    QVERIFY(!spyOutput.isEmpty());
    QCOMPARE(spyOutput.first().at(0).toString(), QStringLiteral("Test Line 1"));
    QCOMPARE(spyOutput.first().at(1).toBool(), false); // isError == false
}

void TestCruxProcessRunner::testPhaseDetection() {
    CruxProcessRunner runner;
    QSignalSpy spyPhase(&runner, &CruxProcessRunner::phaseChanged);
    QSignalSpy spyFinished(&runner, &CruxProcessRunner::processFinished);

    const QString script = QStringLiteral(
        "echo \"=======> Downloading 'test-port'\"\n"
        "echo \"=======> Building 'test-port'\"\n"
        "echo \"=======> Checking footprint\"\n"
    );

    const bool started = runner.startProcess(QStringLiteral("sh"), {QStringLiteral("-c"), script});
    QVERIFY(started);

    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyPhase.count(), 3);
    QCOMPARE(spyPhase.at(0).at(0).toString(), QStringLiteral("Downloading 'test-port'"));
    QCOMPARE(spyPhase.at(1).at(0).toString(), QStringLiteral("Building 'test-port'"));
    QCOMPARE(spyPhase.at(2).at(0).toString(), QStringLiteral("Checking footprint"));
}

void TestCruxProcessRunner::testLineBufferedStreaming() {
    CruxProcessRunner runner;
    QSignalSpy spyOutput(&runner, &CruxProcessRunner::outputReceived);
    QSignalSpy spyFinished(&runner, &CruxProcessRunner::processFinished);

    const QString script = QStringLiteral("printf 'Line Alpha\\nLine Beta\\nLine Gamma\\n'");
    const bool started = runner.startProcess(QStringLiteral("sh"), {QStringLiteral("-c"), script});
    QVERIFY(started);

    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyOutput.count(), 3);
    QCOMPARE(spyOutput.at(0).at(0).toString(), QStringLiteral("Line Alpha"));
    QCOMPARE(spyOutput.at(1).at(0).toString(), QStringLiteral("Line Beta"));
    QCOMPARE(spyOutput.at(2).at(0).toString(), QStringLiteral("Line Gamma"));
}

void TestCruxProcessRunner::testErrorExitClassification() {
    CruxProcessRunner runner;
    QSignalSpy spyFinished(&runner, &CruxProcessRunner::processFinished);

    // Simulate pkgmk exit code 6 (md5sum mismatch)
    const QString script = QStringLiteral(
        "echo \"pkgmk: md5sum mismatch for file 'foo.tar.gz'\" >&2\n"
        "exit 6\n"
    );

    const bool started = runner.startProcess(QStringLiteral("sh"), {QStringLiteral("-c"), script});
    QVERIFY(started);

    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyFinished.count(), 1);

    const QList<QVariant> args = spyFinished.takeFirst();
    QCOMPARE(args.at(0).toInt(), 6);
    QCOMPARE(args.at(1).value<OperationErrorKind>(), OperationErrorKind::ChecksumError);
}

void TestCruxProcessRunner::testCancelOperation() {
    CruxProcessRunner runner;
    QSignalSpy spyFinished(&runner, &CruxProcessRunner::processFinished);

    const bool started = runner.startProcess(QStringLiteral("sleep"), {QStringLiteral("30")});
    QVERIFY(started);
    QVERIFY(runner.isRunning());

    // Request cancellation
    runner.cancel();

    QVERIFY(spyFinished.wait(5000));
    QVERIFY(!runner.isRunning());
    QCOMPARE(spyFinished.count(), 1);

    const QList<QVariant> args = spyFinished.takeFirst();
    QCOMPARE(args.at(1).value<OperationErrorKind>(), OperationErrorKind::GeneralError);
    QVERIFY(args.at(2).toString().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
}

QTEST_MAIN(TestCruxProcessRunner)
#include "test_crux_process_runner.moc"
