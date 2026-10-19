#include "privilege_helper.h"
#include <QStandardPaths>

namespace OctoCRUX {

PrivilegeHelper::PrivilegeHelper(PrivilegeHelperType preferredType)
    : m_type(preferredType)
{
}

PrivilegeHelperType PrivilegeHelper::probeAvailableHelper() {
    // 1. qt-sudo (CRUX GUI elevation tool)
    if (!QStandardPaths::findExecutable(QStringLiteral("qt-sudo")).isEmpty()) {
        return PrivilegeHelperType::QtSudo;
    }
    // 2. sudo
    if (!QStandardPaths::findExecutable(QStringLiteral("sudo")).isEmpty()) {
        return PrivilegeHelperType::Sudo;
    }
    // 3. doas
    if (!QStandardPaths::findExecutable(QStringLiteral("doas")).isEmpty()) {
        return PrivilegeHelperType::Doas;
    }
    // 4. su
    if (!QStandardPaths::findExecutable(QStringLiteral("su")).isEmpty()) {
        return PrivilegeHelperType::Su;
    }

    return PrivilegeHelperType::None;
}

QString PrivilegeHelper::helperBinary(PrivilegeHelperType type) {
    switch (type) {
    case PrivilegeHelperType::QtSudo: return QStringLiteral("qt-sudo");
    case PrivilegeHelperType::Sudo:   return QStringLiteral("sudo");
    case PrivilegeHelperType::Doas:   return QStringLiteral("doas");
    case PrivilegeHelperType::Su:     return QStringLiteral("su");
    case PrivilegeHelperType::None:
    default:
        return QString();
    }
}

QStringList PrivilegeHelper::wrapCommand(
    const QString &program,
    const QStringList &arguments,
    PrivilegeHelperType type)
{
    QStringList wrapped;

    switch (type) {
    case PrivilegeHelperType::QtSudo:
        // Verified on CRUX 3.8: 'qt-sudo -d command [arguments...]'
        wrapped.append(QStringLiteral("qt-sudo"));
        wrapped.append(QStringLiteral("-d"));
        wrapped.append(program);
        wrapped.append(arguments);
        break;

    case PrivilegeHelperType::Sudo:
        wrapped.append(QStringLiteral("sudo"));
        wrapped.append(program);
        wrapped.append(arguments);
        break;

    case PrivilegeHelperType::Doas:
        wrapped.append(QStringLiteral("doas"));
        wrapped.append(program);
        wrapped.append(arguments);
        break;

    case PrivilegeHelperType::Su: {
        wrapped.append(QStringLiteral("su"));
        wrapped.append(QStringLiteral("-c"));
        QStringList fullCmd = { program };
        fullCmd.append(arguments);
        QStringList quotedCmd;
        for (const QString &part : fullCmd) {
            if (part.contains(QLatin1Char(' ')) || part.contains(QLatin1Char('\t'))) {
                quotedCmd.append(QLatin1Char('\"') + part + QLatin1Char('\"'));
            } else {
                quotedCmd.append(part);
            }
        }
        wrapped.append(quotedCmd.join(QLatin1Char(' ')));
        break;
    }

    case PrivilegeHelperType::None:
    default:
        wrapped.append(program);
        wrapped.append(arguments);
        break;
    }

    return wrapped;
}

PrivilegeHelperType PrivilegeHelper::helperType() const {
    return m_type;
}

void PrivilegeHelper::setHelperType(PrivilegeHelperType type) {
    m_type = type;
}

QStringList PrivilegeHelper::wrap(const QString &program, const QStringList &arguments) const {
    return wrapCommand(program, arguments, m_type);
}

} // namespace OctoCRUX
