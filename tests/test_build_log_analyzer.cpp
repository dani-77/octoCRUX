#include <QTest>
#include "core/build_log_analyzer.h"

using namespace OctoCRUX;

class TestBuildLogAnalyzer : public QObject {
    Q_OBJECT

private slots:
    void testDirectPkgmkExitCodes();
    void testDiskSpaceErrorClassification();
    void testFootprintMismatchClassification();
    void testMd5ChecksumMismatchClassification();
    void testSignatureFailureClassification();
    void testDownloadFailureClassification();
    void testUnpackFailureClassification();
    void testPkgfileErrorClassification();
    void testCompilationErrorClassification();
    void testInstallErrorClassification();
    void testExtractFailingPort();
    void testExtractSummary();
};

void TestBuildLogAnalyzer::testDirectPkgmkExitCodes() {
    QCOMPARE(BuildLogAnalyzer::classifyLog(QString(), 0), OperationErrorKind::None);
    QCOMPARE(BuildLogAnalyzer::classifyLog(QString(), 2), OperationErrorKind::PkgfileError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(QString(), 3), OperationErrorKind::SourceDirError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(QString(), 4), OperationErrorKind::DownloadError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(QString(), 5), OperationErrorKind::UnpackError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(QString(), 6), OperationErrorKind::ChecksumError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(QString(), 7), OperationErrorKind::FootprintError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(QString(), 8), OperationErrorKind::CompilationError);
    QCOMPARE(BuildLogAnalyzer::classifyLog(QString(), 9), OperationErrorKind::InstallError);
}

void TestBuildLogAnalyzer::testDiskSpaceErrorClassification() {
    const QString log = QStringLiteral(
        "======> Building 'llvm'\n"
        "make[2]: *** [bin/clang] Error 1\n"
        "fatal error: error writing to /tmp/ccX483j.s: No space left on device\n"
        "compilation terminated.\n"
    );
    // prt-get exits with generic 1
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::DiskSpaceError);
}

void TestBuildLogAnalyzer::testFootprintMismatchClassification() {
    const QString log = QStringLiteral(
        "======> Checking footprint\n"
        "======> Footprint differences\n"
        "NEW      usr/lib/libtest.so.2\n"
        "MISSING  usr/lib/libtest.so.1\n"
        "pkgmk: Footprint mismatch\n"
    );
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::FootprintError);
}

void TestBuildLogAnalyzer::testMd5ChecksumMismatchClassification() {
    const QString log = QStringLiteral(
        "======> Verifying checksums\n"
        "pkgmk: md5sum mismatch for file 'source.tar.gz'\n"
    );
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::ChecksumError);
}

void TestBuildLogAnalyzer::testSignatureFailureClassification() {
    const QString log = QStringLiteral(
        "======> Verifying signatures\n"
        "signify: signature verification failed\n"
    );
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::SignatureError);
}

void TestBuildLogAnalyzer::testDownloadFailureClassification() {
    const QString log = QStringLiteral(
        "curl: (22) The requested URL returned error: 404 Not Found\n"
        "pkgmk: failure downloading 'http://example.com/foo.tar.gz'\n"
    );
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::DownloadError);
}

void TestBuildLogAnalyzer::testUnpackFailureClassification() {
    const QString log = QStringLiteral(
        "tar: Error is not recoverable: exiting now\n"
        "pkgmk: error unpacking 'archive.tar.xz'\n"
    );
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::UnpackError);
}

void TestBuildLogAnalyzer::testPkgfileErrorClassification() {
    const QString log = QStringLiteral(
        "Pkgfile: line 12: syntax error near unexpected token 'fi'\n"
        "pkgmk: Pkgfile is invalid\n"
    );
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::PkgfileError);
}

void TestBuildLogAnalyzer::testCompilationErrorClassification() {
    const QString log = QStringLiteral(
        "main.cpp:45:10: fatal error: foo.h: No such file or directory\n"
        "#include <foo.h>\n"
        "make: *** [Makefile:120: main.o] Error 1\n"
    );
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::CompilationError);
}

void TestBuildLogAnalyzer::testInstallErrorClassification() {
    const QString log = QStringLiteral(
        "pkgadd: can't open /var/lib/pkg/db\n"
        "pkgmk: error while installing package via pkgadd\n"
    );
    QCOMPARE(BuildLogAnalyzer::classifyLog(log, 1), OperationErrorKind::InstallError);
}

void TestBuildLogAnalyzer::testExtractFailingPort() {
    const QString log1 = QStringLiteral(
        "======> Building 'llvm'\n"
        "======> Building 'clang'\n"
        "prt-get: failed to install 'clang'\n"
    );
    QCOMPARE(BuildLogAnalyzer::extractFailingPort(log1), QStringLiteral("clang"));

    const QString log2 = QStringLiteral(
        "======> Building 'webkit2gtk'\n"
        "compilation terminated.\n"
    );
    QCOMPARE(BuildLogAnalyzer::extractFailingPort(log2), QStringLiteral("webkit2gtk"));
}

void TestBuildLogAnalyzer::testExtractSummary() {
    QVERIFY(BuildLogAnalyzer::extractSummary(QString(), OperationErrorKind::None).contains(QStringLiteral("successfully")));
    QVERIFY(BuildLogAnalyzer::extractSummary(QString(), OperationErrorKind::DiskSpaceError).contains(QStringLiteral("ENOSPC")));
    QVERIFY(BuildLogAnalyzer::extractSummary(QString(), OperationErrorKind::FootprintError).contains(QStringLiteral("Footprint mismatch")));
    QVERIFY(BuildLogAnalyzer::extractSummary(QString(), OperationErrorKind::ChecksumError).contains(QStringLiteral("MD5 mismatch")));
}

QTEST_MAIN(TestBuildLogAnalyzer)
#include "test_build_log_analyzer.moc"
