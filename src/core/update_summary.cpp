#include "update_summary.h"

namespace OctoCRUX {

UpdateSummary UpdateSummary::fromPorts(const QList<PortInfo> &ports) {
    UpdateSummary summary;
    for (const PortInfo &p : ports) {
        if (p.installStatus != InstallStatus::Outdated) {
            continue;
        }
        if (p.lockStatus == LockStatus::Locked) {
            summary.outdatedLocked << p.name;
        } else {
            summary.outdatedUnlocked << p.name;
        }
    }
    return summary;
}

} // namespace OctoCRUX
