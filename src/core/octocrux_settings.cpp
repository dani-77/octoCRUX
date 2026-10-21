#include "octocrux_settings.h"
#include "backend/privilege_helper.h"

#include <algorithm>

namespace OctoCRUX {

namespace {
const QString kSyncMode = QStringLiteral("sync/mode");
const QString kSyncHour = QStringLiteral("sync/hour");
const QString kSyncInterval = QStringLiteral("sync/intervalMinutes");
const QString kLastSync = QStringLiteral("sync/lastSync");
const QString kPrivilegeHelper = QStringLiteral("privilege/helper");
const QString kCancelGrace = QStringLiteral("operations/cancelGraceSeconds");
const QString kAutoHelper = QStringLiteral("auto");
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
    const QVariant v = m_settings->value(kLastSync);
    if (v.typeId() == QMetaType::QDateTime) {
        return v.toDateTime(); // written before 2026-10-21 as a binary @DateTime(...)
    }
    const QDateTime parsed = QDateTime::fromString(v.toString(), Qt::ISODate);
    return parsed.isValid() ? parsed.toLocalTime() : QDateTime();
}

void OctoCruxSettings::setLastSync(const QDateTime &when) {
    if (!when.isValid()) {
        m_settings->remove(kLastSync);
        return;
    }
    // ISO 8601 with the UTC offset, readable in the file: 2026-10-21T08:47:00+01:00
    m_settings->setValue(kLastSync, when.toOffsetFromUtc(when.offsetFromUtc()).toString(Qt::ISODate));
}

std::optional<PrivilegeHelperType> OctoCruxSettings::preferredHelper() const {
    return PrivilegeHelper::fromBinaryName(m_settings->value(kPrivilegeHelper, kAutoHelper).toString());
}

void OctoCruxSettings::setPreferredHelper(std::optional<PrivilegeHelperType> helper) {
    const QString name = helper.has_value() ? PrivilegeHelper::helperBinary(*helper) : QString();
    m_settings->setValue(kPrivilegeHelper, name.isEmpty() ? kAutoHelper : name);
}

int OctoCruxSettings::cancelGraceSeconds() const {
    bool ok = false;
    const int s = m_settings->value(kCancelGrace, kDefaultCancelGraceSeconds).toInt(&ok);
    return ok ? std::clamp(s, kMinCancelGraceSeconds, kMaxCancelGraceSeconds) : kDefaultCancelGraceSeconds;
}

void OctoCruxSettings::setCancelGraceSeconds(int seconds) {
    m_settings->setValue(kCancelGrace, std::clamp(seconds, kMinCancelGraceSeconds, kMaxCancelGraceSeconds));
}

void OctoCruxSettings::sync() {
    m_settings->sync();
}

} // namespace OctoCRUX
