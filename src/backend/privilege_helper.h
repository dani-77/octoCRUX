#pragma once

#include "octocrux/types.h"
#include <QString>
#include <QStringList>

namespace OctoCRUX {

/**
 * @brief Dynamic privilege escalation helper discovery and command composition.
 * Ensures the GUI runs strictly unprivileged (UID 1000) and elevates only mutating commands.
 */
class PrivilegeHelper {
public:
    explicit PrivilegeHelper(PrivilegeHelperType preferredType = PrivilegeHelperType::None);

    /**
     * @brief Probes system PATH to discover available elevation binaries on CRUX.
     * Priority: qt-sudo -> sudo -> doas -> su.
     */
    static PrivilegeHelperType probeAvailableHelper();

    /**
     * @brief Wraps a target program and its arguments according to the helper's exact syntax.
     */
    static QStringList wrapCommand(
        const QString &program,
        const QStringList &arguments,
        PrivilegeHelperType type
    );

    /**
     * @brief Returns the executable binary name for the given helper type.
     */
    static QString helperBinary(PrivilegeHelperType type);

    PrivilegeHelperType helperType() const;
    void setHelperType(PrivilegeHelperType type);

    QStringList wrap(const QString &program, const QStringList &arguments) const;

private:
    PrivilegeHelperType m_type = PrivilegeHelperType::None;
};

} // namespace OctoCRUX
