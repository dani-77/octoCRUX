#include "dependency_parser.h"

namespace OctoCRUX {

QStringList DependencyParser::parseDependents(const QString &rawOutput) {
    QStringList result;
    if (rawOutput.isEmpty()) {
        return result;
    }

    const QStringList lines = rawOutput.split(QLatin1Char('\n'));
    result.reserve(lines.size());

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }

    return result;
}

QStringList DependencyParser::parseQuickdep(const QString &rawOutput) {
    QStringList result;
    if (rawOutput.isEmpty()) {
        return result;
    }

    // quickdep returns a space-separated list of ports on one or more lines
    const QString sanitized = rawOutput.simplified();
    if (sanitized.isEmpty()) {
        return result;
    }

    const QStringList tokens = sanitized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    result.reserve(tokens.size());

    for (const QString &token : tokens) {
        const QString trimmed = token.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }

    return result;
}

QString DependencyParser::extractTargetPort(const QStringList &quickdepOrder) {
    if (quickdepOrder.isEmpty()) {
        return QString();
    }
    return quickdepOrder.last();
}

QStringList DependencyParser::resolveQuickdepWithAliases(
    const QStringList &rawOrder,
    const AliasesMapping &aliases,
    const QSet<QString> &installedPackages)
{
    if (rawOrder.isEmpty() || aliases.isEmpty()) {
        return rawOrder;
    }

    QStringList resolved;
    resolved.reserve(rawOrder.size());

    for (const QString &port : rawOrder) {
        if (aliases.hasVirtual(port)) {
            const QString provider = aliases.findProviderFor(port);
            if (installedPackages.contains(provider)) {
                resolved.append(provider);
                continue;
            }
        }
        resolved.append(port);
    }

    return resolved;
}

} // namespace OctoCRUX
