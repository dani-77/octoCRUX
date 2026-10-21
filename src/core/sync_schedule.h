#pragma once

#include <QDateTime>

namespace OctoCRUX {

/**
 * @brief When the notifier may run `ports -u` on its own.
 *
 * Adapted from the OctoXBPS notifier (notifier/mainwindow.cpp,
 * notifierTimerTimeout, commit 5f1e8b2), which offers the same four modes.
 * Differences, on purpose:
 *  - the default is Never: syncing rewrites /usr/ports as root, so it only
 *    happens automatically after the user chooses a mode;
 *  - "once a day at H" catches up later the same day if the machine was off
 *    at H (OctoXBPS required the check to land exactly in hour H);
 *  - the shortest interval is one hour (OctoXBPS: 5 minutes), because a
 *    ports tree sync is far heavier than an xbps repository index refresh.
 */
enum class SyncMode {
    Never = 0,          // never sync automatically (default)
    OnceADay = 1,       // at most once every 24 hours
    OnceADayAt = 2,     // once a day, at or after a given hour
    EveryInterval = 3   // every N minutes
};

struct SyncSchedule {
    static constexpr int kMinIntervalMinutes = 60;
    static constexpr int kMaxIntervalMinutes = 44640; // 31 days, as in OctoXBPS

    SyncMode mode = SyncMode::Never;
    int hour = 0;                // OnceADayAt: 0..23
    int intervalMinutes = 1440;  // EveryInterval

    /** Mode suggested when the user turns automatic sync on. */
    static constexpr SyncMode kSuggestedMode = SyncMode::OnceADay;

    /** Clamps hour and interval into their valid ranges. */
    SyncSchedule normalized() const;
};

/**
 * @brief Whether an automatic sync is due now.
 * @param lastSync time of the last successful sync; invalid if unknown.
 * A lastSync in the future (clock moved back) counts as due.
 */
bool isSyncDue(const SyncSchedule &schedule, const QDateTime &lastSync, const QDateTime &now);

/** Whole days since lastSync, or -1 if it is unknown. */
int daysSinceSync(const QDateTime &lastSync, const QDateTime &now);

} // namespace OctoCRUX
