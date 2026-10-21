#pragma once

#include "sync_schedule.h"
#include "octocrux/types.h"

#include <QDateTime>
#include <QSettings>
#include <memory>
#include <optional>

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

    /**
     * Last successful `ports -u` run by OctoCRUX or its notifier; invalid if none.
     * Stored as ISO 8601 with the UTC offset; the older binary @DateTime value
     * is still read (and rewritten as ISO on the next sync).
     */
    QDateTime lastSync() const;
    void setLastSync(const QDateTime &when);

    /**
     * Privilege helper chosen in Settings; nullopt means automatic detection
     * (the default). Stored as its binary name ("qt-sudo", "sudo", "doas", "su")
     * or "auto"; an unknown value reads as automatic.
     */
    std::optional<PrivilegeHelperType> preferredHelper() const;
    void setPreferredHelper(std::optional<PrivilegeHelperType> helper);

    /** Seconds Cancel waits before warning that the operation is still running. */
    static constexpr int kMinCancelGraceSeconds = 5;
    static constexpr int kMaxCancelGraceSeconds = 120;
    static constexpr int kDefaultCancelGraceSeconds = 10;
    /** Clamped to [kMinCancelGraceSeconds, kMaxCancelGraceSeconds]. */
    int cancelGraceSeconds() const;
    void setCancelGraceSeconds(int seconds);

    void sync();

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace OctoCRUX
