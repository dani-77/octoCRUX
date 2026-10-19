#include "printf_parser.h"
#include <QSet>

namespace OctoCRUX {

QList<PortInfo> PrintfParser::parseAll(const QString &rawOutput, Schema schema) {
    QList<PortInfo> results;
    if (rawOutput.isEmpty()) {
        return results;
    }

    const QStringList lines = rawOutput.split(QLatin1Char('\n'));
    results.reserve(lines.size());

    for (const QString &line : lines) {
        auto port = parseLine(line, schema);
        if (port.has_value()) {
            results.append(std::move(port.value()));
        }
    }

    return results;
}

std::optional<PortInfo> PrintfParser::parseLine(const QString &line, Schema schema) {
    if (line.trimmed().isEmpty()) {
        return std::nullopt;
    }

    QString sanitized = line;
    if (sanitized.endsWith(QLatin1Char('\r'))) {
        sanitized.chop(1);
    }

    const QStringList tokens = sanitized.split(QLatin1Char('\t'));
    const int tokenCount = tokens.size();

    Schema effectiveSchema = schema;
    if (effectiveSchema == Schema::AutoDetect) {
        if (tokenCount == 8) {
            effectiveSchema = Schema::Fixture8Token;
        } else if (tokenCount >= 11) {
            effectiveSchema = Schema::Production11Token;
        } else {
            return std::nullopt;
        }
    }

    QString name;
    QString version;
    QString release;
    InstallStatus installStatus = InstallStatus::NotInstalled;
    LockStatus lockStatus = LockStatus::Unlocked;
    QString collectionPath;
    QString description;
    bool hasReadme = false;
    bool hasPreInstall = false;
    bool hasPostInstall = false;
    QStringList rawDependencies;

    if (effectiveSchema == Schema::Fixture8Token) {
        if (tokenCount != 8) {
            return std::nullopt;
        }
        name = tokens[0];
        version = tokens[1];
        release = tokens[2];
        auto statusOpt = parseInstallStatus(tokens[3]);
        if (!statusOpt.has_value()) {
            return std::nullopt;
        }
        installStatus = statusOpt.value();
        lockStatus = LockStatus::Unlocked;
        collectionPath = tokens[4];
        description = tokens[5];
        hasReadme = (tokens[6].trimmed().compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0);
        rawDependencies = tokens[7].split(QLatin1Char(','), Qt::SkipEmptyParts);
    } else if (effectiveSchema == Schema::Production11Token) {
        if (tokenCount < 11) {
            return std::nullopt;
        }
        name = tokens[0];
        version = tokens[1];
        release = tokens[2];
        auto statusOpt = parseInstallStatus(tokens[3]);
        if (!statusOpt.has_value()) {
            return std::nullopt;
        }
        installStatus = statusOpt.value();

        auto lockOpt = parseLockStatus(tokens[4]);
        if (!lockOpt.has_value()) {
            return std::nullopt;
        }
        lockStatus = lockOpt.value();

        collectionPath = tokens[5];
        hasReadme = (tokens[6].trimmed().compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0);
        hasPreInstall = (tokens[7].trimmed().compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0);
        hasPostInstall = (tokens[8].trimmed().compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0);
        rawDependencies = tokens[9].split(QLatin1Char(','), Qt::SkipEmptyParts);
        // %d is strictly the last token: join any remaining tokens in case description contained embedded tabs
        description = tokens.mid(10).join(QLatin1Char('\t'));
    } else {
        return std::nullopt;
    }

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        return std::nullopt;
    }

    PortInfo info;
    info.name = trimmedName;
    info.version = version.trimmed();
    info.release = release.trimmed();
    info.installStatus = installStatus;
    info.lockStatus = lockStatus;
    info.collectionPath = collectionPath.trimmed();
    info.collection = extractCollectionName(collectionPath);
    info.description = description;
    info.hasReadme = hasReadme;
    info.hasPreInstall = hasPreInstall;
    info.hasPostInstall = hasPostInstall;

    for (const QString &dep : rawDependencies) {
        const QString trimmedDep = dep.trimmed();
        if (!trimmedDep.isEmpty()) {
            info.dependencies.append(trimmedDep);
        }
    }

    return info;
}

QString PrintfParser::extractCollectionName(const QString &collectionPath) {
    QString path = collectionPath.trimmed();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    if (path.isEmpty()) {
        return QString();
    }
    return path.section(QLatin1Char('/'), -1);
}

std::optional<InstallStatus> PrintfParser::parseInstallStatus(const QString &statusStr) {
    const QString s = statusStr.trimmed().toLower();
    if (s == QLatin1String("yes")) {
        return InstallStatus::Installed;
    }
    if (s == QLatin1String("no")) {
        return InstallStatus::NotInstalled;
    }
    if (s == QLatin1String("diff")) {
        return InstallStatus::Outdated;
    }
    return std::nullopt;
}

std::optional<LockStatus> PrintfParser::parseLockStatus(const QString &lockStr) {
    const QString s = lockStr.trimmed().toLower();
    if (s == QLatin1String("yes")) {
        return LockStatus::Locked;
    }
    if (s == QLatin1String("no")) {
        return LockStatus::Unlocked;
    }
    return std::nullopt;
}

void PrintfParser::applyLocks(QList<PortInfo> &ports, const QStringList &lockedPortNames) {
    if (lockedPortNames.isEmpty()) {
        return;
    }

    QSet<QString> lockSet;
    for (const QString &locked : lockedPortNames) {
        const QString trimmed = locked.trimmed();
        if (!trimmed.isEmpty()) {
            lockSet.insert(trimmed);
        }
    }

    for (PortInfo &port : ports) {
        if (lockSet.contains(port.name)) {
            port.lockStatus = LockStatus::Locked;
        }
    }
}

} // namespace OctoCRUX
