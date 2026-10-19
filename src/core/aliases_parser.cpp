#include "aliases_parser.h"
#include <QStringList>
#include <QSet>

namespace OctoCRUX {

AliasesMapping AliasesParser::parse(const QString &rawContent) {
    AliasesMapping mapping;
    if (rawContent.isEmpty()) {
        return mapping;
    }

    const QStringList lines = rawContent.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const int colonIndex = trimmed.indexOf(QLatin1Char(':'));
        if (colonIndex <= 0) {
            continue;
        }

        const QString provider = trimmed.left(colonIndex).trimmed();
        const QString virtualPkg = trimmed.mid(colonIndex + 1).trimmed();

        if (!provider.isEmpty() && !virtualPkg.isEmpty()) {
            mapping.providerToVirtual.insert(provider, virtualPkg);
            mapping.virtualToProvider.insert(virtualPkg, provider);
        }
    }

    return mapping;
}

void AliasesParser::applyAliases(QList<PortInfo> &ports, const AliasesMapping &aliases, bool onlyIfProviderInstalled) {
    if (aliases.isEmpty() || ports.isEmpty()) {
        return;
    }

    // Index installed status of ports
    QSet<QString> installedPortNames;
    if (onlyIfProviderInstalled) {
        for (const PortInfo &port : ports) {
            if (port.isInstalled()) {
                installedPortNames.insert(port.name);
            }
        }
    }

    for (PortInfo &port : ports) {
        if (aliases.hasVirtual(port.name)) {
            const QString provider = aliases.findProviderFor(port.name);
            if (!onlyIfProviderInstalled || installedPortNames.contains(provider)) {
                port.providedByAlias = provider;
            }
        }
    }
}

} // namespace OctoCRUX
