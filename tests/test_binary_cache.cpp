#include <QTest>
#include <QFile>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include "core/binary_cache_report.h"
#include "ui/transaction_preview_dialog.h"

using namespace OctoCRUX;

class TestBinaryCache : public QObject {
    Q_OBJECT

private slots:
    void testParsesTheRealReport();
    void testAllCategories();
    void testSnapshotDate();
    void testPreviewSection();
    void testPreviewSectionAbsentWithoutReport();
    void testProbeRunsTheToolAndKeepsTheLastGoodReport();

private:
    static QString fixture() {
        return QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/fixtures/d77crux-cache/report-n-20261112.txt");
    }
    static QString read(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    // A line as d77crux-cache's note() prints it: printf '%-16s %-28s %s\n'.
    static QString note(const QString &category, const QString &port, const QString &detail) {
        return QStringLiteral("%1 %2 %3\n").arg(category, -16).arg(port, -28).arg(detail);
    }
    static TransactionStep upgrade(const QString &port) {
        TransactionStep s;
        s.action = TransactionAction::Upgrade;
        s.portName = port;
        return s;
    }
};

void TestBinaryCache::testParsesTheRealReport() {
    const BinaryCacheReport r = BinaryCacheReport::parse(read(fixture()));
    QCOMPARE(r.entry(QStringLiteral("dash")).status, BinaryStatus::Pending);
    QCOMPARE(r.entry(QStringLiteral("dash")).detail, QStringLiteral("tree 0.5.13.6-1, release 0.5.13.5-1"));
    QCOMPARE(r.entry(QStringLiteral("xorg-server")).status, BinaryStatus::Pending);
    QCOMPARE(r.entry(QStringLiteral("git")).status, BinaryStatus::Builds);
    QCOMPARE(r.entry(QStringLiteral("git")).detail, QStringLiteral("2.56.1-1: not in the release"));
    // The counts line ("skip-absent=1 skip-version=3") and blanks are not entries.
    QCOMPARE(r.entry(QStringLiteral("skip-version=3")).status, BinaryStatus::Unknown);
    QCOMPARE(r.entry(QStringLiteral("nano")).status, BinaryStatus::Unknown);
    QCOMPARE(BinaryCacheReport::treeAndRelease(r.entry(QStringLiteral("dash")).detail),
             qMakePair(QStringLiteral("0.5.13.6-1"), QStringLiteral("0.5.13.5-1")));
}

void TestBinaryCache::testAllCategories() {
    // d77crux-cache 0.1.2's other categories, in its own line format (synthetic
    // lines: they did not occur in the 2026-11-12 capture).
    const QString out = note(QStringLiteral("present"), QStringLiteral("nano"), QStringLiteral("9.3-1"))
        + note(QStringLiteral("cached"), QStringLiteral("zlib"), QStringLiteral("1.3.1-1"))
        + note(QStringLiteral("would-cache"), QStringLiteral("curl"), QStringLiteral("8.23.0-1 (1234 bytes)"))
        + note(QStringLiteral("locked"), QStringLiteral("llvm"), QStringLiteral("21.1.5-1: binary available"))
        + note(QStringLiteral("skip-collection"), QStringLiteral("go"), QStringLiteral("/usr/ports/contrib is not official"))
        + note(QStringLiteral("skip-modified"), QStringLiteral("vim"), QStringLiteral("Pkgfile does not match the signed .signature"))
        + note(QStringLiteral("skip-foreign"), QStringLiteral("gcc"), QStringLiteral("/usr/ports/packages/gcc#... exists"))
        + note(QStringLiteral("failed"), QStringLiteral("rsync"), QStringLiteral("download"))
        + note(QStringLiteral("removed"), QStringLiteral("old"), QStringLiteral("/usr/ports/core/old/old#1-1.pkg.tar.xz"))
        + QStringLiteral("\nLocked ports with binaries available. To update them from the cache and keep them locked:\n"
                         "  prt-get unlock llvm && d77crux-cache && prt-get update llvm; prt-get lock llvm\n");
    const BinaryCacheReport r = BinaryCacheReport::parse(out);
    QCOMPARE(r.entry(QStringLiteral("nano")).status, BinaryStatus::Ready);
    QCOMPARE(r.entry(QStringLiteral("zlib")).status, BinaryStatus::Ready);
    QCOMPARE(r.entry(QStringLiteral("curl")).status, BinaryStatus::NotPlaced);
    QCOMPARE(r.entry(QStringLiteral("llvm")).status, BinaryStatus::LockedAvailable);
    for (const char *p : {"go", "vim", "gcc", "rsync"}) {
        QCOMPARE(r.entry(QString::fromLatin1(p)).status, BinaryStatus::Builds);
    }
    QCOMPARE(r.entry(QStringLiteral("old")).status, BinaryStatus::Unknown);     // "removed" is housekeeping
    QCOMPARE(r.entry(QStringLiteral("unlock")).status, BinaryStatus::Unknown);  // hint lines
}

void TestBinaryCache::testSnapshotDate() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("channel-published"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("2026-11-10T21:26:29Z\n"); // as on the T480s
    }
    const QDateTime d = BinaryCacheContext::readSnapshotPublished(path);
    QVERIFY(d.isValid());
    QCOMPARE(d.toUTC(), QDateTime(QDate(2026, 11, 10), QTime(21, 26, 29), QTimeZone::UTC));
    QVERIFY(!BinaryCacheContext::readSnapshotPublished(dir.filePath(QStringLiteral("missing"))).isValid());
}

void TestBinaryCache::testPreviewSection() {
    BinaryCacheContext ctx;
    ctx.available = true;
    ctx.report = BinaryCacheReport::parse(read(fixture())
        + note(QStringLiteral("present"), QStringLiteral("nano"), QStringLiteral("9.3-1"))
        + note(QStringLiteral("would-cache"), QStringLiteral("curl"), QStringLiteral("8.23.0-1 (1 bytes)"))
        + note(QStringLiteral("skip-version"), QStringLiteral("llvm"), QStringLiteral("tree 21.1.6-1, release 21.1.5-1")));
    ctx.report.checkedAt = QDateTime(QDate(2026, 11, 12), QTime(17, 30));
    ctx.snapshotPublished = QDateTime(QDate(2026, 11, 10), QTime(21, 26, 29), QTimeZone::UTC);
    ctx.now = QDateTime(QDate(2026, 11, 12), QTime(17, 31));
    ctx.largePorts = {QStringLiteral("llvm")};

    TransactionPlan plan;
    for (const char *p : {"dash", "git", "nano", "curl", "llvm", "xorg-server", "notinreport"}) {
        plan.steps << upgrade(QString::fromLatin1(p));
    }
    TransactionStep lock;
    lock.action = TransactionAction::Lock;
    lock.portName = QStringLiteral("desktop-file-utils"); // in the report, but not built here
    plan.steps << lock;

    QStringList warnings;
    const QString text = TransactionPreviewDialog::describeBinaries(plan, ctx, warnings).join(QLatin1Char('\n'));
    QVERIFY2(text.contains(QStringLiteral("report from 17:30")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("binary snapshot published 2 days ago; new ones usually come every 7-10 days")));
    QVERIFY(text.contains(QStringLiteral("Binary ready, installs without compiling (1): nano")));
    QVERIFY(text.contains(QStringLiteral("not placed yet (run d77crux-cache as root before Apply, or it compiles) (1): curl")));
    QVERIFY(text.contains(QStringLiteral("compiles now (3): dash (port 0.5.13.6-1, binary 0.5.13.5-1), "
                                         "llvm [large] (port 21.1.6-1, binary 21.1.5-1), xorg-server (port 21.1.26-1, binary 21.1.25-1)")));
    QVERIFY(text.contains(QStringLiteral("Compiles from source (1): git (2.56.1-1: not in the release)")));
    QVERIFY(!text.contains(QStringLiteral("notinreport")));
    QVERIFY(!text.contains(QStringLiteral("desktop-file-utils")));
    QCOMPARE(warnings.size(), 1);
    QVERIFY(warnings.first().startsWith(QStringLiteral("llvm take(s) long to compile, and a binary should come")));

    // An old snapshot says a new one may be due.
    ctx.now = QDateTime(QDate(2026, 11, 22), QTime(9, 0));
    warnings.clear();
    QVERIFY(TransactionPreviewDialog::describeBinaries(plan, ctx, warnings).join(QLatin1Char('\n'))
                .contains(QStringLiteral("published 12 days ago; new ones usually come every 7-10 days, so one may be due")));

    // The whole preview carries the section and the warning.
    const QString full = TransactionPreviewDialog::describe(plan, {{QStringLiteral("prt-get"), QStringLiteral("update")}},
                                                            PrivilegeHelperType::None, ctx);
    QVERIFY(full.contains(QStringLiteral("Binary packages (d77crux-cache;")));
    QVERIFY(full.contains(QStringLiteral("  - llvm take(s) long to compile")));
}

void TestBinaryCache::testPreviewSectionAbsentWithoutReport() {
    TransactionPlan plan;
    plan.steps << upgrade(QStringLiteral("dash"));
    QStringList warnings;
    BinaryCacheContext none; // d77crux-cache not installed: nothing changes
    QVERIFY(TransactionPreviewDialog::describeBinaries(plan, none, warnings).isEmpty());
    QVERIFY(!TransactionPreviewDialog::describe(plan, {}, PrivilegeHelperType::None).contains(QStringLiteral("Binary packages")));

    BinaryCacheContext unrelated; // a report, but no port of the plan in it
    unrelated.available = true;
    unrelated.report = BinaryCacheReport::parse(read(fixture()));
    plan.steps.first().portName = QStringLiteral("nano");
    QVERIFY(TransactionPreviewDialog::describeBinaries(plan, unrelated, warnings).isEmpty());
    QVERIFY(warnings.isEmpty());
}

void TestBinaryCache::testProbeRunsTheToolAndKeepsTheLastGoodReport() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log = dir.filePath(QStringLiteral("probe.log"));
    qputenv("OCTOCRUX_PROBE_LOG", log.toLocal8Bit());
    qputenv("FAKE_CACHE_REPORT", fixture().toLocal8Bit());
    auto restore = qScopeGuard([]() {
        qunsetenv("OCTOCRUX_PROBE_LOG");
        qunsetenv("FAKE_CACHE_REPORT");
        qunsetenv("FAKE_CACHE_FAIL");
    });

    BinaryCacheProbe probe;
    probe.setToolPath(QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/scripts/fake_d77crux_cache.sh"));
    QVERIFY(probe.isAvailable());
    QSignalSpy spy(&probe, &BinaryCacheProbe::reportReady);
    QVERIFY(probe.start());
    QVERIFY(!probe.start()); // one at a time
    QVERIFY(spy.wait(5000));
    QVERIFY(probe.hasReport());
    QCOMPARE(probe.lastReport().entry(QStringLiteral("dash")).status, BinaryStatus::Pending);
    QVERIFY(probe.lastReport().checkedAt.isValid());
    QCOMPARE(read(log), QStringLiteral("d77crux-cache -n\n")); // report only, never a placing run

    // No network (the tool fails): the previous report stays.
    qputenv("FAKE_CACHE_FAIL", "1");
    QVERIFY(probe.start());
    QVERIFY(spy.wait(5000));
    QCOMPARE(probe.lastReport().entry(QStringLiteral("dash")).status, BinaryStatus::Pending);

    BinaryCacheProbe disabled;
    disabled.setToolPath(QString());
    QVERIFY(!disabled.isAvailable());
    QVERIFY(!disabled.start());
}

QTEST_GUILESS_MAIN(TestBinaryCache)
#include "test_binary_cache.moc"
