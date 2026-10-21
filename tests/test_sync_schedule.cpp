#include <QTest>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include "core/sync_schedule.h"
#include "core/octocrux_settings.h"

using namespace OctoCRUX;

class TestSyncSchedule : public QObject {
    Q_OBJECT

private slots:
    void testNeverIsNeverDue();
    void testUnknownLastSyncIsDue();
    void testOnceADayNeeds24Hours();
    void testOnceADayAtCatchesUpAfterTheHour();
    void testEveryIntervalAndClamping();
    void testClockMovedBackIsDue();
    void testDaysSinceSync();
    void testSettingsRoundTripAndDefaults();
    void testGeneralSettingsRoundTripAndInvalidValues();
    void testLegacyBinaryLastSyncIsStillRead();

private:
    static QDateTime at(int day, int hour, int minute = 0) {
        return QDateTime(QDate(2026, 10, day), QTime(hour, minute));
    }
};

void TestSyncSchedule::testNeverIsNeverDue() {
    SyncSchedule s; // default
    QCOMPARE(s.mode, SyncMode::Never);
    QVERIFY(!isSyncDue(s, QDateTime(), at(21, 8)));
    QVERIFY(!isSyncDue(s, at(1, 0), at(21, 8)));
}

void TestSyncSchedule::testUnknownLastSyncIsDue() {
    SyncSchedule s;
    s.mode = SyncMode::OnceADay;
    QVERIFY(isSyncDue(s, QDateTime(), at(21, 8)));
}

void TestSyncSchedule::testOnceADayNeeds24Hours() {
    SyncSchedule s;
    s.mode = SyncSchedule::kSuggestedMode;
    QCOMPARE(s.mode, SyncMode::OnceADay);
    QVERIFY(!isSyncDue(s, at(20, 23, 59), at(21, 0, 1)));  // calendar day changed, not 24 h
    QVERIFY(!isSyncDue(s, at(20, 8), at(21, 7, 59)));
    QVERIFY(isSyncDue(s, at(20, 8), at(21, 8)));
}

void TestSyncSchedule::testOnceADayAtCatchesUpAfterTheHour() {
    SyncSchedule s;
    s.mode = SyncMode::OnceADayAt;
    s.hour = 9;
    QVERIFY(!isSyncDue(s, at(20, 9, 5), at(21, 8, 59)));   // before today's slot
    QVERIFY(isSyncDue(s, at(20, 9, 5), at(21, 9, 0)));     // at the slot
    QVERIFY(isSyncDue(s, at(20, 9, 5), at(21, 15, 0)));    // machine was off at 9: catch up
    QVERIFY(!isSyncDue(s, at(21, 9, 1), at(21, 15, 0)));   // already synced after today's slot
    QVERIFY(!isSyncDue(s, at(21, 8, 0), at(21, 8, 30)));   // synced early today, slot not reached
}

void TestSyncSchedule::testEveryIntervalAndClamping() {
    SyncSchedule s;
    s.mode = SyncMode::EveryInterval;
    s.intervalMinutes = 120;
    QVERIFY(!isSyncDue(s, at(21, 6, 0), at(21, 7, 59)));
    QVERIFY(isSyncDue(s, at(21, 6, 0), at(21, 8, 0)));

    // Shorter than one hour is raised to one hour (a ports tree sync is heavy).
    s.intervalMinutes = 5;
    QCOMPARE(s.normalized().intervalMinutes, SyncSchedule::kMinIntervalMinutes);
    QVERIFY(!isSyncDue(s, at(21, 6, 0), at(21, 6, 30)));
    QVERIFY(isSyncDue(s, at(21, 6, 0), at(21, 7, 0)));
    s.intervalMinutes = 1000000;
    QCOMPARE(s.normalized().intervalMinutes, SyncSchedule::kMaxIntervalMinutes);
    s.hour = 42;
    QCOMPARE(s.normalized().hour, 23);
}

void TestSyncSchedule::testClockMovedBackIsDue() {
    SyncSchedule s;
    s.mode = SyncMode::OnceADay;
    QVERIFY(isSyncDue(s, at(25, 8), at(21, 8)));
}

void TestSyncSchedule::testDaysSinceSync() {
    QCOMPARE(daysSinceSync(QDateTime(), at(21, 8)), -1);
    QCOMPARE(daysSinceSync(at(21, 1), at(21, 8)), 0);
    QCOMPARE(daysSinceSync(at(18, 8), at(21, 8)), 3);
    QCOMPARE(daysSinceSync(at(25, 8), at(21, 8)), 0); // future timestamp: not negative
}

void TestSyncSchedule::testSettingsRoundTripAndDefaults() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("octoCRUX.conf"));
    {
        OctoCruxSettings settings(file);
        QCOMPARE(settings.syncSchedule().mode, SyncMode::Never); // no automatic sync by default
        QVERIFY(!settings.lastSync().isValid());
        SyncSchedule s;
        s.mode = SyncMode::OnceADayAt;
        s.hour = 30;                 // clamped on save
        settings.setSyncSchedule(s);
        settings.setLastSync(at(21, 7, 17));
        settings.sync();
    }
    OctoCruxSettings reread(file);
    QCOMPARE(reread.syncSchedule().mode, SyncMode::OnceADayAt);
    QCOMPARE(reread.syncSchedule().hour, 23);
    QCOMPARE(reread.lastSync(), at(21, 7, 17));

    // Readable in the file: ISO 8601 with the UTC offset, not a binary @DateTime(...).
    QFile raw(file);
    QVERIFY(raw.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(raw.readAll());
    const QDateTime when = at(21, 7, 17);
    QVERIFY2(text.contains(QStringLiteral("lastSync=") + when.toOffsetFromUtc(when.offsetFromUtc()).toString(Qt::ISODate)),
             qPrintable(text));
    QVERIFY(!text.contains(QStringLiteral("@DateTime")));
}

void TestSyncSchedule::testLegacyBinaryLastSyncIsStillRead() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("octoCRUX.conf"));
    {
        // How builds before 2026-10-21 wrote it: QSettings' binary @DateTime(...).
        QSettings old(file, QSettings::IniFormat);
        old.setValue(QStringLiteral("sync/lastSync"), at(21, 8, 47));
    }
    {
        QFile raw(file);
        QVERIFY(raw.open(QIODevice::ReadOnly));
        QVERIFY(raw.readAll().contains("@DateTime("));
    }
    QCOMPARE(OctoCruxSettings(file).lastSync(), at(21, 8, 47));

    // Garbage reads as "never synced", so the notifier says so instead of crashing.
    {
        QSettings old(file, QSettings::IniFormat);
        old.setValue(QStringLiteral("sync/lastSync"), QStringLiteral("yesterday"));
    }
    QVERIFY(!OctoCruxSettings(file).lastSync().isValid());
}

void TestSyncSchedule::testGeneralSettingsRoundTripAndInvalidValues() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("octoCRUX.conf"));
    {
        OctoCruxSettings settings(file);
        QVERIFY(!settings.preferredHelper().has_value()); // automatic by default
        QCOMPARE(settings.cancelGraceSeconds(), OctoCruxSettings::kDefaultCancelGraceSeconds);
        settings.setPreferredHelper(PrivilegeHelperType::Doas);
        settings.setCancelGraceSeconds(1000); // clamped on save
        settings.sync();
    }
    {
        OctoCruxSettings reread(file);
        QCOMPARE(reread.preferredHelper(), std::optional<PrivilegeHelperType>(PrivilegeHelperType::Doas));
        QCOMPARE(reread.cancelGraceSeconds(), OctoCruxSettings::kMaxCancelGraceSeconds);
        reread.setPreferredHelper(std::nullopt);
        reread.sync();
    }
    {
        QSettings raw(file, QSettings::IniFormat);
        QCOMPARE(raw.value(QStringLiteral("privilege/helper")).toString(), QStringLiteral("auto"));
        // Hand-edited values: an unknown helper reads as automatic, bad numbers as defaults or limits.
        raw.setValue(QStringLiteral("privilege/helper"), QStringLiteral("pkexec"));
        raw.setValue(QStringLiteral("operations/cancelGraceSeconds"), QStringLiteral("soon"));
    }
    {
        OctoCruxSettings reread(file);
        QVERIFY(!reread.preferredHelper().has_value());
        QCOMPARE(reread.cancelGraceSeconds(), OctoCruxSettings::kDefaultCancelGraceSeconds);
    }
    {
        QSettings raw(file, QSettings::IniFormat);
        raw.setValue(QStringLiteral("operations/cancelGraceSeconds"), 0);
    }
    QCOMPARE(OctoCruxSettings(file).cancelGraceSeconds(), OctoCruxSettings::kMinCancelGraceSeconds);
}

QTEST_GUILESS_MAIN(TestSyncSchedule)
#include "test_sync_schedule.moc"
