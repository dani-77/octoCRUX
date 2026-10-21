#pragma once

#include "octocrux/types.h"
#include <QList>
#include <QStringList>

namespace OctoCRUX {

/**
 * @brief Outdated packages split the way OctoCRUX upgrades them: unlocked ones
 * are what Upgrade All would update, locked ones are skipped (OPEN_QUESTIONS 2.5:
 * `prt-get diff` hides them; the printf %i/%l axes do not).
 */
struct UpdateSummary {
    QStringList outdatedUnlocked;
    QStringList outdatedLocked;

    int upgradable() const { return outdatedUnlocked.size(); }

    static UpdateSummary fromPorts(const QList<PortInfo> &ports);
};

} // namespace OctoCRUX
