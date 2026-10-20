#include <QTest>
#include <QSignalSpy>
#include <QFile>
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
    void testInteractivePromptGetsEof();
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

    // Replay a real pkgmk md5sum failure log (tools/gen_pkgmk_failure_logs.sh) on stderr
    // and exit with a code unrelated to E_MD5: classification must come from the text.
    const QString fixture = QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/fixtures/crux3.8/pkgmk-failures/md5.log");
    QVERIFY(QFile::exists(fixture));

    const bool started = runner.startProcess(QStringLiteral("sh"),
        {QStringLiteral("-c"), QStringLiteral("cat \"$1\" >&2; exit 1"), QStringLiteral("sh"), fixture});
    QVERIFY(started);

    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyFinished.count(), 1);

    const QList<QVariant> args = spyFinished.takeFirst();
    QCOMPARE(args.at(0).toInt(), 1);
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

void TestCruxProcessRunner::testInteractivePromptGetsEof() {
    CruxProcessRunner runner;
    QSignalSpy spyFinished(&runner, &CruxProcessRunner::processFinished);
    QSignalSpy spyOutput(&runner, &CruxProcessRunner::outputReceived);

    const QString script = QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/scripts/fake_interactive_pkgfile.sh");
    QVERIFY(runner.startProcess(QStringLiteral("sh"), {script}));

    // Without EOF on stdin the script blocks in 'read' until killed.
    QVERIFY2(spyFinished.wait(5000), "process blocked reading stdin");
    QCOMPARE(spyFinished.first().at(0).toInt(), 1);

    QStringList lines;
    for (const auto &args : spyOutput) {
        lines << args.at(0).toString();
    }
    QVERIFY(lines.contains(QStringLiteral("Accept the license? [y/N]")));
    QVERIFY(lines.contains(QStringLiteral("stdin: EOF")));
}

QTEST_MAIN(TestCruxProcessRunner)
#include "test_crux_process_runner.moc"
