#pragma once

#include "sync_schedule.h"

#include <QDateTime>
#include <QSettings>
#include <memory>

namespace OctoCRUX {

/**
 * @brief Settings shared by octocrux and octocrux-notifier.
 *
 * Stored with QSettings under organization "OctoCRUX", application
 * "octoCRUX" (~/.config/OctoCRUX/octoCRUX.conf). Tests pass an explicit file.
 */
class OctoCruxSettings {
public:
    /** The user's settings file. */
    OctoCruxSettings();
    /** An explicit INI file (tests). */
    explicit OctoCruxSettings(const QString &iniFile);

    SyncSchedule syncSchedule() const;
    void setSyncSchedule(const SyncSchedule &schedule);

    /** Last successful `ports -u` run by OctoCRUX or its notifier; invalid if none. */
    QDateTime lastSync() const;
    void setLastSync(const QDateTime &when);

    void sync();

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace OctoCRUX
