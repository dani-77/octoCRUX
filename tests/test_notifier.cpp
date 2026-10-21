#include <QTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include "backend/crux_adapter.h"
#include "core/printf_parser.h"
#include "notifier/notifier_core.h"
#include "notifier/unattended_sync.h"
#include <QCoreApplication>

using namespace OctoCRUX;

namespace {

class MockCommandRunner : public ICommandRunner {
public:
    void registerResponse(const QString &program, const QStringList &arguments, const CommandResult &result) {
        m_responses.insert(program + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')), result);
    }
    CommandResult run(const QString &program, const QStringList &arguments, int timeoutMs) override {
        Q_UNUSED(timeoutMs);
        const QString key = program + QLatin1Char(' ') + arguments.join(QLatin1Char(' '));
        return m_responses.value(key, {127, QString(), QStringLiteral("Command not mocked: ") + key});
    }
private:
    QMap<QString, CommandResult> m_responses;
};

QString readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

/*
 * NotifierCore against the real 2026-10-19 catalog fixture (printf output and
 * prt-get.locker: llvm, clang, lld, compiler-rt outdated and locked). Syncing
 * goes through a fake `ports` first in PATH, with no privilege helper.
 */
class TestNotifier : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testReadOnlySummaryOfRealFixture();
    void testNewUpdatesAreAnnouncedOnlyWhenTheCountGrows();
    void testSuccessfulSyncRecordsTimeAndRefreshes();
    void testFailedSyncRecordsNothing();
    void testPolkitRuleAndActionDefinition();
    void testScheduledSyncNeverByDefault();
    void testScheduledSyncUsesPolkitWhenAuthorized();
    void testScheduledSyncFallsBackToSudoN();
    void testScheduledSyncWithoutPasswordFreeWayOnlyReminds();
    void testScheduledSyncSkipsWithoutNetworkOrWhenBusy();

private:
    void setPrintf(const QString &text);
    bool installFakePorts(int exitCode);
    // Fake ports, pkcheck, pkexec and sudo first in PATH; false unless all four
    // resolve to the fakes (the real sudo/pkexec must never run from a test).
    bool installFakeElevation(int pkcheckExit, bool sudoNeedsPassword);
    QStringList probe() const;
    void setSchedule(SyncMode mode);

    QString m_sourceDir;
    QString m_printf;
    std::shared_ptr<MockCommandRunner> m_runner;
    std::shared_ptr<CruxAdapter> m_adapter;
    std::shared_ptr<OctoCruxSettings> m_settings;
    std::unique_ptr<QTemporaryDir> m_tmp;
    QByteArray m_savedPath;
};

void TestNotifier::init() {
    m_sourceDir = QStringLiteral(OCTOCRUX_SOURCE_DIR);
    m_printf = readFile(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8/cmd/printf-production-11token.out"));
    QVERIFY(!m_printf.isEmpty());
    m_runner = std::make_shared<MockCommandRunner>();
    setPrintf(m_printf);
    m_adapter = std::make_shared<CruxAdapter>(m_runner);
    m_adapter->setRootPrefix(m_sourceDir + QStringLiteral("/tests/fixtures/crux3.8_run2/files"));
    m_adapter->setPrivilegeHelperType(PrivilegeHelperType::None);
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    m_settings = std::make_shared<OctoCruxSettings>(m_tmp->filePath(QStringLiteral("octoCRUX.conf")));
    m_savedPath = qgetenv("PATH");
}

void TestNotifier::cleanup() {
    qputenv("PATH", m_savedPath);
    qunsetenv("OCTOCRUX_PROBE_LOG");
    qunsetenv("FAKE_PORTS_EXIT");
    qunsetenv("FAKE_PKCHECK_EXIT");
    qunsetenv("FAKE_SUDO_NEEDS_PASSWORD");
}

void TestNotifier::setPrintf(const QString &text) {
    m_runner->registerResponse(QStringLiteral("prt-get"),
        {QStringLiteral("printf"), QString::fromLatin1(PrintfParser::PRODUCTION_FORMAT)}, {0, text, QString()});
}

bool TestNotifier::installFakePorts(int exitCode) {
    const QString bin = m_tmp->filePath(QStringLiteral("bin"));
    QDir().mkpath(bin);
    if (!QFile::link(m_sourceDir + QStringLiteral("/tests/scripts/fake_ports.sh"), bin + QStringLiteral("/ports"))) {
        return false;
    }
    qputenv("PATH", bin.toLocal8Bit() + ':' + m_savedPath);
    qputenv("OCTOCRUX_PROBE_LOG", m_tmp->filePath(QStringLiteral("probe.log")).toLocal8Bit());
    qputenv("FAKE_PORTS_EXIT", QByteArray::number(exitCode));
    // Never run the real ports(8) from a test.
    return QStandardPaths::findExecutable(QStringLiteral("ports")) == bin + QStringLiteral("/ports");
}

bool TestNotifier::installFakeElevation(int pkcheckExit, bool sudoNeedsPassword) {
    if (!installFakePorts(0)) {
        return false;
    }
    const QString bin = m_tmp->filePath(QStringLiteral("bin"));
    for (const QString &tool : {QStringLiteral("pkcheck"), QStringLiteral("pkexec"), QStringLiteral("sudo")}) {
        if (!QFile::link(m_sourceDir + QStringLiteral("/tests/scripts/fake_%1.sh").arg(tool), bin + QLatin1Char('/') + tool)) {
            return false;
        }
        if (QStandardPaths::findExecutable(tool) != bin + QLatin1Char('/') + tool) {
            return false;
        }
    }
    qputenv("FAKE_PKCHECK_EXIT", QByteArray::number(pkcheckExit));
    if (sudoNeedsPassword) {
        qputenv("FAKE_SUDO_NEEDS_PASSWORD", "1");
    }
    return true;
}

QStringList TestNotifier::probe() const {
    return readFile(m_tmp->filePath(QStringLiteral("probe.log"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void TestNotifier::setSchedule(SyncMode mode) {
    SyncSchedule s;
    s.mode = mode;
    m_settings->setSyncSchedule(s);
}

void TestNotifier::testReadOnlySummaryOfRealFixture() {
    NotifierCore core(m_adapter, m_settings);
    QCOMPARE(core.statusText(), QStringLiteral("Checking for updates..."));
    core.refresh();
    QCOMPARE(core.summary().upgradable(), 0);
    QCOMPARE(core.summary().outdatedLocked.size(), 4);
    QCOMPARE(core.statusText(), QStringLiteral("System is up to date (4 locked skipped)"));
    QCOMPARE(core.syncAgeText(), QStringLiteral("Ports tree never synced by OctoCRUX"));
}

void TestNotifier::testNewUpdatesAreAnnouncedOnlyWhenTheCountGrows() {
    NotifierCore core(m_adapter, m_settings);
    QSignalSpy spyNew(&core, &NotifierCore::newUpdatesFound);
    core.refresh();
    QCOMPARE(spyNew.count(), 0); // only locked ports are outdated

    // Same fixture with gtk3 (installed, unlocked) marked outdated: one field changed.
    QStringList lines = m_printf.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() == 11 && f.at(0) == QLatin1String("gtk3")) {
            f[3] = QStringLiteral("diff");
            line = f.join(QLatin1Char('\t'));
        }
    }
    setPrintf(lines.join(QLatin1Char('\n')));
    core.refresh();
    QCOMPARE(spyNew.count(), 1);
    QCOMPARE(spyNew.first().at(0).toInt(), 1);
    QCOMPARE(core.statusText(), QStringLiteral("1 update available (4 locked skipped)"));
    core.refresh();               // unchanged: no repeated notification
    QCOMPARE(spyNew.count(), 1);
}

void TestNotifier::testSuccessfulSyncRecordsTimeAndRefreshes() {
    QVERIFY2(installFakePorts(0), "fake ports not first in PATH; refusing to sync");
    NotifierCore core(m_adapter, m_settings);
    const QDateTime fixedNow(QDate(2026, 10, 21), QTime(8, 30));
    core.setClock([fixedNow]() { return fixedNow; });
    QSignalSpy spyDone(&core, &NotifierCore::syncFinished);

    QVERIFY(core.syncNow());
    QVERIFY(core.isSyncing());
    QCOMPARE(core.statusText(), QStringLiteral("Synchronizing ports tree..."));
    QVERIFY(!core.syncNow());     // one sync at a time
    QVERIFY(spyDone.wait(5000));
    QCOMPARE(spyDone.first().at(0).toBool(), true);
    QVERIFY(!core.isSyncing());
    QCOMPARE(readFile(m_tmp->filePath(QStringLiteral("probe.log"))).trimmed(), QStringLiteral("-u"));
    QCOMPARE(m_settings->lastSync(), fixedNow);
    QCOMPARE(core.syncAgeText(), QStringLiteral("Ports tree synced today"));
    QCOMPARE(core.summary().outdatedLocked.size(), 4); // refreshed after the sync
}

void TestNotifier::testFailedSyncRecordsNothing() {
    QVERIFY2(installFakePorts(1), "fake ports not first in PATH; refusing to sync");
    NotifierCore core(m_adapter, m_settings);
    QSignalSpy spyDone(&core, &NotifierCore::syncFinished);
    QVERIFY(core.syncNow());
    QVERIFY(spyDone.wait(5000));
    QCOMPARE(spyDone.first().at(0).toBool(), false);
    QVERIFY(!m_settings->lastSync().isValid());
    QCOMPARE(core.syncAgeText(), QStringLiteral("Ports tree never synced by OctoCRUX"));
}

void TestNotifier::testPolkitRuleAndActionDefinition() {
    const QString rule = UnattendedSync::polkitRuleFor(QStringLiteral("dani77"));
    QVERIFY(rule.contains(QStringLiteral("action.id == \"org.octocrux.ports-sync\"")));
    QVERIFY(rule.contains(QStringLiteral("subject.user == \"dani77\" && subject.local && subject.active")));
    QVERIFY(rule.contains(QStringLiteral("return polkit.Result.YES;")));
    QCOMPARE(UnattendedSync::ruleFilePath(QStringLiteral("dani77")),
             QStringLiteral("/etc/polkit-1/rules.d/49-octocrux-ports-sync-dani77.rules"));
    // Names that could break out of the JavaScript string are refused.
    for (const QString &bad : {QStringLiteral("x\" || true || \""), QStringLiteral("Root"),
                               QStringLiteral(""), QStringLiteral("a b"), QStringLiteral("../etc")}) {
        QVERIFY2(UnattendedSync::polkitRuleFor(bad).isEmpty(), qPrintable(bad));
    }
    // The shipped action maps exactly "/usr/bin/ports -u" and needs admin auth by itself.
    const QString policy = readFile(m_sourceDir + QStringLiteral("/contrib/polkit/org.octocrux.ports-sync.policy"));
    QVERIFY(policy.contains(QStringLiteral("<action id=\"org.octocrux.ports-sync\">")));
    QVERIFY(policy.contains(QStringLiteral("<annotate key=\"org.freedesktop.policykit.exec.path\">/usr/bin/ports</annotate>")));
    QVERIFY(policy.contains(QStringLiteral("<annotate key=\"org.freedesktop.policykit.exec.argv1\">-u</annotate>")));
    QVERIFY(policy.contains(QStringLiteral("<allow_active>auth_admin</allow_active>")));
    QVERIFY(policy.contains(QStringLiteral("<allow_any>no</allow_any>")));
}

void TestNotifier::testScheduledSyncNeverByDefault() {
    QVERIFY2(installFakeElevation(0, false), "fakes not first in PATH; refusing to tick");
    NotifierCore core(m_adapter, m_settings);
    core.tick();
    QTest::qWait(300);
    QVERIFY(probe().isEmpty());
    QVERIFY(!core.isSyncing());
}

void TestNotifier::testScheduledSyncUsesPolkitWhenAuthorized() {
    QVERIFY2(installFakeElevation(0, false), "fakes not first in PATH; refusing to tick");
    setSchedule(SyncMode::OnceADay);
    NotifierCore core(m_adapter, m_settings);
    core.setBusyCheck([]() { return false; });
    QSignalSpy spyDone(&core, &NotifierCore::syncFinished);
    core.tick();
    QVERIFY(spyDone.wait(5000));
    QCOMPARE(spyDone.first().at(0).toBool(), true);
    const QString ports = m_tmp->filePath(QStringLiteral("bin/ports"));
    QCOMPARE(probe(), (QStringList{
        QStringLiteral("pkcheck --action-id org.octocrux.ports-sync --process %1").arg(QCoreApplication::applicationPid()),
        QStringLiteral("pkexec %1 -u").arg(ports),
        QStringLiteral("-u")}));
    QVERIFY(m_settings->lastSync().isValid());
    QVERIFY(!core.automaticSyncNeedsAuthorization());
    // Not due again right away.
    core.tick();
    QTest::qWait(300);
    QCOMPARE(probe().size(), 3);
}

void TestNotifier::testScheduledSyncFallsBackToSudoN() {
    QVERIFY2(installFakeElevation(2, false), "fakes not first in PATH; refusing to tick");
    setSchedule(SyncMode::OnceADay);
    NotifierCore core(m_adapter, m_settings);
    core.setBusyCheck([]() { return false; });
    QSignalSpy spyDone(&core, &NotifierCore::syncFinished);
    core.tick();
    QVERIFY(spyDone.wait(5000));
    QCOMPARE(spyDone.first().at(0).toBool(), true);
    const QStringList lines = probe();
    QCOMPARE(lines.size(), 3);
    QVERIFY(lines.at(0).startsWith(QStringLiteral("pkcheck ")));
    QCOMPARE(lines.at(1), QStringLiteral("sudo -n %1 -u").arg(m_tmp->filePath(QStringLiteral("bin/ports"))));
    QCOMPARE(lines.at(2), QStringLiteral("-u"));
    QVERIFY(m_settings->lastSync().isValid());
}

void TestNotifier::testScheduledSyncWithoutPasswordFreeWayOnlyReminds() {
    QVERIFY2(installFakeElevation(2, true), "fakes not first in PATH; refusing to tick");
    setSchedule(SyncMode::OnceADay);
    NotifierCore core(m_adapter, m_settings);
    core.setBusyCheck([]() { return false; });
    QSignalSpy spyDone(&core, &NotifierCore::syncFinished);
    QSignalSpy spyAuth(&core, &NotifierCore::automaticSyncNeedsAuthorizationChanged);
    core.tick();
    QVERIFY(spyDone.wait(5000));
    QCOMPARE(spyDone.first().at(0).toBool(), false);
    QVERIFY(!probe().contains(QStringLiteral("-u")));   // ports never ran
    QVERIFY(!m_settings->lastSync().isValid());
    QVERIFY(core.automaticSyncNeedsAuthorization());
    QCOMPARE(spyAuth.count(), 1);
    QCOMPARE(core.syncAgeText(), QStringLiteral("Automatic sync needs authorization (see Options)"));
    // The failed attempt postpones the next one: no retry on the next tick.
    const int before = probe().size();
    core.tick();
    QTest::qWait(300);
    QCOMPARE(probe().size(), before);
}

void TestNotifier::testScheduledSyncSkipsWithoutNetworkOrWhenBusy() {
    QVERIFY2(installFakeElevation(0, false), "fakes not first in PATH; refusing to tick");
    setSchedule(SyncMode::OnceADay);
    NotifierCore core(m_adapter, m_settings);
    core.setNetworkCheck([]() { return false; });
    core.setBusyCheck([]() { return false; });
    core.tick();
    core.setNetworkCheck([]() { return true; });
    core.setBusyCheck([]() { return true; });   // e.g. OctoCRUX is running prt-get
    core.tick();
    QTest::qWait(300);
    QVERIFY(probe().isEmpty());
    QVERIFY(!core.isSyncing());
}

QTEST_GUILESS_MAIN(TestNotifier)
#include "test_notifier.moc"
