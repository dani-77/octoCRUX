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

private:
    void setPrintf(const QString &text);
    bool installFakePorts(int exitCode);

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

QTEST_GUILESS_MAIN(TestNotifier)
#include "test_notifier.moc"
