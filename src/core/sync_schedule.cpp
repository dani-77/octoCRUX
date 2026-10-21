#include "sync_schedule.h"

#include <QTimeZone>
#include <QtGlobal>

namespace OctoCRUX {

SyncSchedule SyncSchedule::normalized() const {
    SyncSchedule s = *this;
    s.hour = qBound(0, s.hour, 23);
    s.intervalMinutes = qBound(kMinIntervalMinutes, s.intervalMinutes, kMaxIntervalMinutes);
    return s;
}

bool isSyncDue(const SyncSchedule &schedule, const QDateTime &lastSync, const QDateTime &now) {
    const SyncSchedule s = schedule.normalized();
    if (s.mode == SyncMode::Never) {
        return false;
    }
    if (!lastSync.isValid() || lastSync > now) {
        return true;
    }

    switch (s.mode) {
    case SyncMode::OnceADay:
        return lastSync.secsTo(now) >= 24 * 60 * 60;
    case SyncMode::OnceADayAt: {
        // Due once today's slot has passed and nothing has synced since it.
        const QDateTime slot(now.date(), QTime(s.hour, 0), now.timeZone());
        return now >= slot && lastSync < slot;
    }
    case SyncMode::EveryInterval:
        return lastSync.secsTo(now) >= static_cast<qint64>(s.intervalMinutes) * 60;
    case SyncMode::Never:
        break;
    }
    return false;
}

int daysSinceSync(const QDateTime &lastSync, const QDateTime &now) {
    if (!lastSync.isValid()) {
        return -1;
    }
    return static_cast<int>(qMax<qint64>(0, lastSync.secsTo(now)) / (24 * 60 * 60));
}

} // namespace OctoCRUX
