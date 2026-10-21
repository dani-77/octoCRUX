#include "octocrux_settings.h"

namespace OctoCRUX {

namespace {
const QString kSyncMode = QStringLiteral("sync/mode");
const QString kSyncHour = QStringLiteral("sync/hour");
const QString kSyncInterval = QStringLiteral("sync/intervalMinutes");
const QString kLastSync = QStringLiteral("sync/lastSync");
} // namespace

OctoCruxSettings::OctoCruxSettings()
    : m_settings(std::make_unique<QSettings>(QStringLiteral("OctoCRUX"), QStringLiteral("octoCRUX")))
{
}

OctoCruxSettings::OctoCruxSettings(const QString &iniFile)
    : m_settings(std::make_unique<QSettings>(iniFile, QSettings::IniFormat))
{
}

SyncSchedule OctoCruxSettings::syncSchedule() const {
    SyncSchedule s;
    const int mode = m_settings->value(kSyncMode, static_cast<int>(SyncMode::Never)).toInt();
    s.mode = (mode >= 0 && mode <= static_cast<int>(SyncMode::EveryInterval))
        ? static_cast<SyncMode>(mode) : SyncMode::Never;
    s.hour = m_settings->value(kSyncHour, s.hour).toInt();
    s.intervalMinutes = m_settings->value(kSyncInterval, s.intervalMinutes).toInt();
    return s.normalized();
}

void OctoCruxSettings::setSyncSchedule(const SyncSchedule &schedule) {
    const SyncSchedule s = schedule.normalized();
    m_settings->setValue(kSyncMode, static_cast<int>(s.mode));
    m_settings->setValue(kSyncHour, s.hour);
    m_settings->setValue(kSyncInterval, s.intervalMinutes);
}

QDateTime OctoCruxSettings::lastSync() const {
    return m_settings->value(kLastSync).toDateTime();
}

void OctoCruxSettings::setLastSync(const QDateTime &when) {
    m_settings->setValue(kLastSync, when);
}

void OctoCruxSettings::sync() {
    m_settings->sync();
}

} // namespace OctoCRUX
