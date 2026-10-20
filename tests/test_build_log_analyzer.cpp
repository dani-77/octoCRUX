#include <QTest>
#include <QFile>
#include <QRegularExpression>
#include "core/build_log_analyzer.h"

using namespace OctoCRUX;

/*
 * Every log used here is real pkgmk 5.40.12 output, generated on CRUX 3.8 by
 * tools/gen_pkgmk_failure_logs.sh (unprivileged pkgmk against sandbox ports) and
 * stored verbatim in tests/fixtures/crux3.8/pkgmk-failures/. No line is hand-written.
 */
class TestBuildLogAnalyzer : public QObject {
    Q_OBJECT

private slots:
    void testRealPkgmkFailures_data();
    void testRealPkgmkFailures();
    void testExitCodeValueIsIgnored();
    void testPermissionDeniedInBuildIsCompilationError();
    void testSuccessLogWithFailureExitIsGeneral();
    void testUnpackDetectionAcrossPortsAndChannels();
    void testExtractFailingPort();
    void testExtractSummary();
    void testExtractFootprintFailures();
    void testRealQtSudoAuthentication();

private:
    static QString fixture(const QString &name);
    static int fixtureRc(const QString &name);
};

QString TestBuildLogAnalyzer::fixture(const QString &name) {
    QFile f(QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/fixtures/crux3.8/pkgmk-failures/%1.log").arg(name));
    if (!f.open(QIODevice::ReadOnly)) {
        qFatal("missing fixture %s", qPrintable(f.fileName()));
    }
    return QString::fromUtf8(f.readAll());
}

int TestBuildLogAnalyzer::fixtureRc(const QString &name) {
    QFile f(QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/fixtures/crux3.8/pkgmk-failures/%1.rc").arg(name));
    if (!f.open(QIODevice::ReadOnly)) {
        qFatal("missing fixture %s", qPrintable(f.fileName()));
    }
    return QString::fromUtf8(f.readAll()).trimmed().toInt();
}

void TestBuildLogAnalyzer::testRealPkgmkFailures_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<OperationErrorKind>("expected");

    QTest::newRow("build-fail")   << "build-fail"   << OperationErrorKind::CompilationError;
    QTest::newRow("enospc")       << "enospc"       << OperationErrorKind::DiskSpaceError;
    QTest::newRow("footprint")    << "footprint"    << OperationErrorKind::FootprintError;
    QTest::newRow("footprint-new") << "footprint-new" << OperationErrorKind::FootprintError;
    QTest::newRow("md5")          << "md5"          << OperationErrorKind::ChecksumError;
    QTest::newRow("signature")    << "signature"    << OperationErrorKind::SignatureError;
    QTest::newRow("download")     << "download"     << OperationErrorKind::DownloadError;
    QTest::newRow("unpack")       << "unpack"       << OperationErrorKind::UnpackError;
    QTest::newRow("pkgfile")      << "pkgfile"      << OperationErrorKind::PkgfileError;
    QTest::newRow("dirperm")      << "dirperm"      << OperationErrorKind::SourceDirError;
    QTest::newRow("interrupt")    << "interrupt"    << OperationErrorKind::GeneralError;
}

void TestBuildLogAnalyzer::testRealPkgmkFailures() {
    QFETCH(QString, name);
    QFETCH(OperationErrorKind, expected);

    // prt-get's exit code value is unspecified; any non-zero value must give the same result.
    const QString log = fixture(name);
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), expected);
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 255), expected);
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, fixtureRc(name)), expected);
}

void TestBuildLogAnalyzer::testExitCodeValueIsIgnored() {
    // Real pkgmk exits 1 for footprint and build() failures, never 7 or 8.
    QCOMPARE(fixtureRc(QStringLiteral("footprint")), 1);
    QCOMPARE(fixtureRc(QStringLiteral("build-fail")), 1);

    // A non-zero code that happens to equal an E_* value must not override the log text.
    QCOMPARE(BuildLogAnalyzer::classifyLog(fixture(QStringLiteral("md5")), 8),
             OperationErrorKind::ChecksumError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(fixture(QStringLiteral("build-fail")), 9),
             OperationErrorKind::CompilationError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(fixture(QStringLiteral("interrupt")), 2),
             OperationErrorKind::GeneralError);
}

void TestBuildLogAnalyzer::testPermissionDeniedInBuildIsCompilationError() {
    // "Permission denied" from a command inside build() is a build() failure, not a
    // source/package directory problem (pkgmk reports those as "Directory '...'").
    const QString log = fixture(QStringLiteral("build-eacces"));
    QVERIFY(log.contains(QStringLiteral("Permission denied")));
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::CompilationError);
}

void TestBuildLogAnalyzer::testSuccessLogWithFailureExitIsGeneral() {
    const QString log = fixture(QStringLiteral("success"));
    QCOMPARE(fixtureRc(QStringLiteral("success")), 0);
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 0), OperationErrorKind::None);
    // No pkgmk ERROR line: a later prt-get step failed, cause unknown from this log.
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::GeneralError);
}

void TestBuildLogAnalyzer::testUnpackDetectionAcrossPortsAndChannels() {
    // A previous port's successful build() trace must not hide the next port's unpack failure.
    QCOMPARE(BuildLogAnalyzer::classifyLog(fixture(QStringLiteral("success")) + fixture(QStringLiteral("unpack")), 1),
             OperationErrorKind::UnpackError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(fixture(QStringLiteral("unpack")) + fixture(QStringLiteral("success")), 1),
             OperationErrorKind::UnpackError);

    // CruxProcessRunner interleaves stdout ("=======> Building ...") and stderr
    // ("+ build" xtrace) in arrival order. Same real lines, stderr trace first:
    // still a build() failure.
    QStringList lines = fixture(QStringLiteral("build-fail")).split(QLatin1Char('\n'));
    const int phase = lines.indexOf(QRegularExpression(QStringLiteral("^=======> Building '.*'\\.$")));
    const int trace = lines.indexOf(QStringLiteral("+ build"));
    QVERIFY(phase >= 0 && trace > phase);
    lines.move(trace, phase);
    QCOMPARE(BuildLogAnalyzer::classifyLog(lines.join(QLatin1Char('\n')), 1),
             OperationErrorKind::CompilationError);
}

void TestBuildLogAnalyzer::testExtractFailingPort() {
    QCOMPARE(BuildLogAnalyzer::extractFailingPort(fixture(QStringLiteral("build-fail"))),
             QStringLiteral("octo-build-fail"));
    QCOMPARE(BuildLogAnalyzer::extractFailingPort(fixture(QStringLiteral("signature"))),
             QStringLiteral("lz4"));
    // md5sum fails before the "Building" phase line; the "ERROR: Building ... failed." line names it.
    QCOMPARE(BuildLogAnalyzer::extractFailingPort(fixture(QStringLiteral("md5"))),
             QStringLiteral("octo-md5"));
    // Pkgfile/directory checks fail before any package target is printed.
    QCOMPARE(BuildLogAnalyzer::extractFailingPort(fixture(QStringLiteral("pkgfile"))), QString());
    QCOMPARE(BuildLogAnalyzer::extractFailingPort(fixture(QStringLiteral("dirperm"))), QString());
    // The last "Building" line wins when several ports are built in one run.
    const QString twoPorts = fixture(QStringLiteral("success")) + fixture(QStringLiteral("footprint"));
    QCOMPARE(BuildLogAnalyzer::extractFailingPort(twoPorts), QStringLiteral("octo-footprint"));
}

void TestBuildLogAnalyzer::testExtractSummary() {
    QVERIFY(BuildLogAnalyzer::extractSummary(QString(), OperationErrorKind::None).contains(QStringLiteral("successfully")));
    QVERIFY(BuildLogAnalyzer::extractSummary(QString(), OperationErrorKind::DiskSpaceError).contains(QStringLiteral("ENOSPC")));
    QVERIFY(BuildLogAnalyzer::extractSummary(QString(), OperationErrorKind::FootprintError).contains(QStringLiteral("Footprint mismatch")));
    QVERIFY(BuildLogAnalyzer::extractSummary(QString(), OperationErrorKind::ChecksumError).contains(QStringLiteral("MD5 mismatch")));
    QVERIFY(BuildLogAnalyzer::extractSummary(fixture(QStringLiteral("interrupt")), OperationErrorKind::GeneralError)
                .contains(QStringLiteral("octo-interrupt")));
}

void TestBuildLogAnalyzer::testExtractFootprintFailures() {
    using F = BuildLogAnalyzer::FootprintFailure;
    // NEW and MISSING (ownership differs): -in would not help.
    QCOMPARE(BuildLogAnalyzer::extractFootprintFailures(fixture(QStringLiteral("footprint"))),
             (QList<F>{{QStringLiteral("octo-footprint"), false}}));
    // NEW only (the criu/elfutils pattern of 2026-10-20): -in accepts it.
    QCOMPARE(BuildLogAnalyzer::extractFootprintFailures(fixture(QStringLiteral("footprint-new"))),
             (QList<F>{{QStringLiteral("octo-footprint-new"), true}}));
    // Several ports in one run, as in a sysup; other failures are not footprint failures.
    const QString run = fixture(QStringLiteral("footprint-new")) + fixture(QStringLiteral("build-fail"))
                      + fixture(QStringLiteral("success")) + fixture(QStringLiteral("footprint"));
    QCOMPARE(BuildLogAnalyzer::extractFootprintFailures(run),
             (QList<F>{{QStringLiteral("octo-footprint-new"), true}, {QStringLiteral("octo-footprint"), false}}));
    QVERIFY(BuildLogAnalyzer::extractFootprintFailures(fixture(QStringLiteral("md5"))).isEmpty());
    QVERIFY(BuildLogAnalyzer::extractFootprintFailures(fixture(QStringLiteral("success"))).isEmpty());
}

void TestBuildLogAnalyzer::testRealQtSudoAuthentication() {
    // Owner's Output tab of 2026-10-21 (qt-sudo 2.4.1, sudo 1.9.17p2), split into the
    // four operations it records; see tests/fixtures/crux3.8/qtsudo-auth/PROVENANCE.txt.
    QFile f(QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/fixtures/crux3.8/qtsudo-auth/octocrux-output-20261021.txt"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString all = QString::fromUtf8(f.readAll());
    static const QRegularExpression opStart(QStringLiteral("^\\[octoCRUX\\] (?:Locking|Unlocking) criu$"),
                                            QRegularExpression::MultilineOption);
    QStringList ops;
    QRegularExpressionMatchIterator it = opStart.globalMatch(all);
    QList<qsizetype> starts;
    while (it.hasNext()) starts << it.next().capturedStart();
    for (int i = 0; i < starts.size(); ++i) {
        ops << all.mid(starts[i], (i + 1 < starts.size() ? starts[i + 1] : all.size()) - starts[i]);
    }
    QCOMPARE(ops.size(), 4);

    // 2. Password dialog cancelled: helper exit code 1.
    QVERIFY(ops[1].contains(QStringLiteral("sudo: a password is required")));
    QCOMPARE(BuildLogAnalyzer::classifyLog(ops[1], 1), OperationErrorKind::AuthenticationError);
    QVERIFY(BuildLogAnalyzer::extractSummary(ops[1], OperationErrorKind::AuthenticationError)
                .contains(QStringLiteral("no changes were made")));
    // 3. Two wrong passwords, then the right one: success; "Sorry, try again." is not an error.
    QVERIFY(ops[2].contains(QStringLiteral("Sorry, try again.")));
    QCOMPARE(BuildLogAnalyzer::classifyLog(ops[2], 0), OperationErrorKind::None);
    // 1. and 4. Plain successes.
    QCOMPARE(BuildLogAnalyzer::classifyLog(ops[0], 0), OperationErrorKind::None);
    QCOMPARE(BuildLogAnalyzer::classifyLog(ops[3], 0), OperationErrorKind::None);
    // A real build failure log is never mistaken for an authentication failure.
    QCOMPARE(BuildLogAnalyzer::classifyLog(fixture(QStringLiteral("md5")), 1), OperationErrorKind::ChecksumError);
}

QTEST_MAIN(TestBuildLogAnalyzer)
#include "test_build_log_analyzer.moc"
