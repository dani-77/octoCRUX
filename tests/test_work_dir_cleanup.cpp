#include <QTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include "core/work_dir_cleanup.h"
#include "operations/transaction_planner.h"
#include "backend/crux_adapter.h"
#include "backend/crux_process_runner.h"

#include <QSignalSpy>

#include <unistd.h>

using namespace OctoCRUX;

class TestWorkDirCleanup : public QObject {
    Q_OBJECT

private slots:
    void init() { WorkDirCleanup::setRequiredOwnerUidForTesting(getuid()); }
    void cleanup() { WorkDirCleanup::setRequiredOwnerUidForTesting(0); }
    void testFindsOnlyPkgmkShapedChildren();
    void testRejectsPathsOutsideOrUnsafe();
    void testRequiresTheOwner();
    void testConfiguredWorkDirectoryHasNoFallback();
    void testAdapterRefusesUnsafePathsBeforeStarting();

private:
    // A work directory as pkgmk leaves it: <root>/<name>/{pkg,src}.
    static void makeWorkDir(const QString &root, const QString &name, int payloadBytes = 0) {
        QDir(root).mkpath(name + QStringLiteral("/pkg"));
        QDir(root).mkpath(name + QStringLiteral("/src"));
        if (payloadBytes > 0) {
            QFile f(QDir(root).filePath(name + QStringLiteral("/src/payload")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(payloadBytes, 'x'));
        }
    }
};

void TestWorkDirCleanup::testFindsOnlyPkgmkShapedChildren() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path();
    makeWorkDir(root, QStringLiteral("zsh"), 4096);
    makeWorkDir(root, QStringLiteral("octo-cancel-test"));
    QDir(root).mkpath(QStringLiteral("no-markers/src"));            // pkg/ missing
    QFile plain(QDir(root).filePath(QStringLiteral("a-file")));
    QVERIFY(plain.open(QIODevice::WriteOnly));
    QVERIFY(QFile::link(QDir(root).filePath(QStringLiteral("zsh")), QDir(root).filePath(QStringLiteral("linked"))));

    const QList<WorkDirCleanup::Leftover> found = WorkDirCleanup::findLeftovers(root);
    QStringList paths;
    for (const auto &l : found) paths << l.path;
    QCOMPARE(paths, (QStringList{QDir(root).filePath(QStringLiteral("octo-cancel-test")),
                                 QDir(root).filePath(QStringLiteral("zsh"))}));
    QVERIFY(found.at(1).bytes >= 4096);
}

void TestWorkDirCleanup::testRejectsPathsOutsideOrUnsafe() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = QDir(tmp.path()).filePath(QStringLiteral("work"));
    makeWorkDir(root, QStringLiteral("zsh"));
    makeWorkDir(root, QStringLiteral("zsh/nested"));
    makeWorkDir(tmp.path(), QStringLiteral("outside"));
    makeWorkDir(root, QStringLiteral(".hidden"));
    QVERIFY(QFile::link(QDir(root).filePath(QStringLiteral("zsh")), QDir(root).filePath(QStringLiteral("linked"))));

    QVERIFY(WorkDirCleanup::isRemovableWorkDir(root, QDir(root).filePath(QStringLiteral("zsh"))));
    QVERIFY(WorkDirCleanup::isRemovableWorkDir(root + QStringLiteral("/"), root + QStringLiteral("/./zsh/")));
    // Not a direct child, outside the root, through "..", a symlink, a hidden name.
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(root, QDir(root).filePath(QStringLiteral("zsh/nested"))));
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(root, QDir(tmp.path()).filePath(QStringLiteral("outside"))));
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(root, root + QStringLiteral("/../outside")));
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(root, QDir(root).filePath(QStringLiteral("linked"))));
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(root, QDir(root).filePath(QStringLiteral(".hidden"))));
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(root, root));
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(root, QString()));
    // Unusable roots: "/", relative, missing.
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(QStringLiteral("/"), QStringLiteral("/usr")));
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(QStringLiteral("work"), QStringLiteral("work/zsh")));
    QVERIFY(!WorkDirCleanup::isRemovableWorkDir(QString(), QDir(root).filePath(QStringLiteral("zsh"))));
    QVERIFY(WorkDirCleanup::findLeftovers(QStringLiteral("/")).isEmpty());
    QVERIFY(WorkDirCleanup::findLeftovers(QDir(tmp.path()).filePath(QStringLiteral("missing"))).isEmpty());
}

void TestWorkDirCleanup::testRequiresTheOwner() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    makeWorkDir(tmp.path(), QStringLiteral("zsh"));
    QCOMPARE(WorkDirCleanup::findLeftovers(tmp.path()).size(), 1);
    // In production the owner must be root; these directories belong to the test user.
    WorkDirCleanup::setRequiredOwnerUidForTesting(0);
    if (getuid() != 0) {
        QVERIFY(WorkDirCleanup::findLeftovers(tmp.path()).isEmpty());
        QVERIFY(!WorkDirCleanup::isRemovableWorkDir(tmp.path(), QDir(tmp.path()).filePath(QStringLiteral("zsh"))));
    }
}

void TestWorkDirCleanup::testConfiguredWorkDirectoryHasNoFallback() {
    QTemporaryDir prefix;
    QVERIFY(prefix.isValid());
    QDir(prefix.path()).mkpath(QStringLiteral("etc"));
    const QString conf = QDir(prefix.path()).filePath(QStringLiteral("etc/pkgmk.conf"));
    {
        QFile f(conf);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("# the owner's line\nPKGMK_WORK_DIR=\"${PKGMK_WORK_ROOT:-/usr/ports/work}/$name\"\n");
    }
    const QByteArray savedRoot = qgetenv("PKGMK_WORK_ROOT");
    auto restore = qScopeGuard([&]() { qputenv("PKGMK_WORK_ROOT", savedRoot); });
    qunsetenv("PKGMK_WORK_ROOT");
    QCOMPARE(TransactionPlanner::configuredWorkDirectory(prefix.path()), std::optional<QString>(QStringLiteral("/usr/ports/work")));
    {
        // pkgmk's own default ($PWD/work) is not in pkgmk.conf: nothing to clean, no guess.
        QFile f(conf);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        f.write("export CFLAGS=\"-O2\"\n#PKGMK_WORK_DIR=\"/tmp/$name\"\n");
    }
    QVERIFY(!TransactionPlanner::configuredWorkDirectory(prefix.path()).has_value());
    QVERIFY(!TransactionPlanner::discoverWorkDirectory(prefix.path()).isEmpty()); // the preview still falls back
}

void TestWorkDirCleanup::testAdapterRefusesUnsafePathsBeforeStarting() {
    // The UI only offers validated paths; the backend checks again and refuses the
    // whole request if any path fails, before any process (or helper) starts.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = QDir(tmp.path()).filePath(QStringLiteral("work"));
    makeWorkDir(root, QStringLiteral("zsh"));
    makeWorkDir(tmp.path(), QStringLiteral("outside"));
    CruxAdapter adapter;
    adapter.setPrivilegeHelperType(PrivilegeHelperType::None);
    QSignalSpy spyOutput(&adapter, &ICruxBackend::outputReceived);
    QSignalSpy spyFinished(&adapter, &ICruxBackend::operationFinished);

    QVERIFY(!adapter.removeWorkDirectories(root, {QDir(root).filePath(QStringLiteral("zsh")),
                                                  QDir(tmp.path()).filePath(QStringLiteral("outside"))}));
    QVERIFY(!adapter.processRunner()->isRunning());
    QVERIFY(spyOutput.last().at(0).toString().contains(QStringLiteral("refusing to remove")));
    QVERIFY(QFileInfo::exists(QDir(tmp.path()).filePath(QStringLiteral("outside"))));
    QVERIFY(QFileInfo::exists(QDir(root).filePath(QStringLiteral("zsh"))));
    QVERIFY(!adapter.removeWorkDirectories(QStringLiteral("/"), {QStringLiteral("/usr")}));
    QVERIFY(!adapter.removeWorkDirectories(root, {}));
    QCOMPARE(spyFinished.count(), 0);
    QCOMPARE(adapter.removeWorkDirectoriesCommand({QStringLiteral("/usr/ports/work/zsh")}),
             (QStringList{QStringLiteral("rm"), QStringLiteral("-rf"), QStringLiteral("--"), QStringLiteral("/usr/ports/work/zsh")}));
}

QTEST_GUILESS_MAIN(TestWorkDirCleanup)
#include "test_work_dir_cleanup.moc"
