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
    void testCancelStalledWarnsWithoutKill();
    void testCarriageReturnProgressIsTransient();
    void testCancelledAuthenticationIsReported();
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
    QCOMPARE(args.at(1).value<OperationErrorKind>(), OperationErrorKind::Cancelled);
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

void TestCruxProcessRunner::testCancelStalledWarnsWithoutKill() {
    CruxProcessRunner runner;
    runner.setCancelGracePeriodMs(500);
    QSignalSpy spyFinished(&runner, &CruxProcessRunner::processFinished);
    QSignalSpy spyStalled(&runner, &CruxProcessRunner::cancellationStalled);
    QSignalSpy spyOutput(&runner, &CruxProcessRunner::outputReceived);

    const QString script = QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/scripts/fake_stubborn_helper.sh");
    QVERIFY(runner.startProcess(QStringLiteral("sh"), {script}));

    auto sawLine = [&spyOutput](const QString &needle) {
        for (const auto &args : spyOutput) {
            if (args.at(0).toString().contains(needle)) {
                return true;
            }
        }
        return false;
    };
    // Wait until the trap is installed before signalling.
    QTRY_VERIFY_WITH_TIMEOUT(sawLine(QStringLiteral("fake helper: ready")), 5000);

    runner.cancel();

    // The first SIGTERM is ignored: after the grace period the runner warns, keeps
    // the process alive (no SIGKILL) and stays cancellable.
    QVERIFY2(spyStalled.wait(5000), "no cancellationStalled after the grace period");
    QCOMPARE(spyStalled.first().at(1).toInt(), 500);
    QVERIFY(runner.isRunning());
    QVERIFY(spyFinished.isEmpty());
    QVERIFY(sawLine(QStringLiteral("fake helper: SIGTERM ignored")));
    QVERIFY(sawLine(QStringLiteral("still running")));

    // A second cancel resends SIGTERM, which this helper honours.
    runner.cancel();
    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyFinished.first().at(0).toInt(), 143);
    QVERIFY(spyFinished.first().at(2).toString().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    QVERIFY(sawLine(QStringLiteral("fake helper: second SIGTERM, exiting")));
    QCOMPARE(spyStalled.count(), 1);
}

void TestCruxProcessRunner::testCarriageReturnProgressIsTransient() {
    // curl's progress meter (seen in the 2026-10-20 linux-firmware log) redraws one
    // terminal line with '\r'. Both '\r'-before and '\r'-after styles are covered,
    // plus CRLF line endings, which must not be treated as progress.
    CruxProcessRunner runner;
    QSignalSpy spyFinished(&runner, &CruxProcessRunner::processFinished);
    QSignalSpy spyOutput(&runner, &CruxProcessRunner::outputReceived);
    QSignalSpy spyTransient(&runner, &CruxProcessRunner::transientOutput);

    const QString script = QStringLiteral(
        "printf 'p1\\r'; sleep 0.3; printf 'p2\\r'; sleep 0.3; printf 'done\\n'\n"
        "printf '\\r q1'; sleep 0.3; printf '\\r q2'; sleep 0.3; printf '\\r q3\\n'\n"
        "printf 'crlf\\r\\n'\n");
    QVERIFY(runner.startProcess(QStringLiteral("sh"), {QStringLiteral("-c"), script}));
    QVERIFY(spyFinished.wait(5000));

    QStringList lines;
    for (const auto &args : spyOutput) lines << args.at(0).toString();
    QCOMPARE(lines, (QStringList{QStringLiteral("done"), QStringLiteral(" q3"), QStringLiteral("crlf")}));
    QCOMPARE(runner.capturedLog(), QStringLiteral("done\n q3\ncrlf\n"));

    QStringList transient;
    for (const auto &args : spyTransient) transient << args.at(0).toString();
    QVERIFY2(transient.contains(QStringLiteral("p1")) && transient.contains(QStringLiteral("p2")),
             qPrintable(transient.join(QLatin1Char('|'))));
    QVERIFY2(transient.contains(QStringLiteral(" q1")) && transient.contains(QStringLiteral(" q2")),
             qPrintable(transient.join(QLatin1Char('|'))));
    QVERIFY(!transient.contains(QStringLiteral("crlf")));

    // Output cut off in the middle of a redraw (as when Cancel stops curl): the
    // flushed remainder is its visible state, without '\r' or superseded updates.
    CruxProcessRunner cut;
    QSignalSpy spyCutFinished(&cut, &CruxProcessRunner::processFinished);
    QSignalSpy spyCutOutput(&cut, &CruxProcessRunner::outputReceived);
    QVERIFY(cut.startProcess(QStringLiteral("sh"), {QStringLiteral("-c"),
        QStringLiteral("printf '\\r 26 548.0M'; sleep 0.3; printf '\\r 27 548.0M'")}));
    QVERIFY(spyCutFinished.wait(5000));
    QVERIFY(!spyCutOutput.isEmpty());
    QCOMPARE(spyCutOutput.last().at(0).toString(), QStringLiteral(" 27 548.0M"));
    QVERIFY(!cut.capturedLog().contains(QLatin1Char('\r')));
}

void TestCruxProcessRunner::testCancelledAuthenticationIsReported() {
    // The helper's own lines from the owner's 2026-10-21 run (password dialog
    // cancelled), replayed on stderr with qt-sudo's exit code 1.
    CruxProcessRunner runner;
    QSignalSpy spyFinished(&runner, &CruxProcessRunner::processFinished);
    QVERIFY(runner.startProcess(QStringLiteral("sh"), {QStringLiteral("-c"),
        QStringLiteral("printf 'Password: \\nsudo: a password is required\\n' >&2; exit 1")}));
    QVERIFY(spyFinished.wait(5000));
    const QList<QVariant> args = spyFinished.takeFirst();
    QCOMPARE(args.at(0).toInt(), 1);
    QCOMPARE(args.at(1).value<OperationErrorKind>(), OperationErrorKind::AuthenticationError);
    QVERIFY(args.at(2).toString().contains(QStringLiteral("nothing was run as root")));
}

QTEST_MAIN(TestCruxProcessRunner)
#include "test_crux_process_runner.moc"
