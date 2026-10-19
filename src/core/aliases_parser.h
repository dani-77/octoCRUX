#pragma once

#include "octocrux/types.h"
#include <QString>
#include <QMap>
#include <QList>

namespace OctoCRUX {

/**
 * @brief Bidirectional mapping representing CRUX prt-get aliases.
 */
struct AliasesMapping {
    // Maps provider package (concrete, e.g. "rust-bin") -> virtual name (e.g. "rust")
    QMap<QString, QString> providerToVirtual;

    // Maps virtual name (e.g. "rust") -> provider package (concrete, e.g. "rust-bin")
    QMap<QString, QString> virtualToProvider;

    bool isEmpty() const { return providerToVirtual.isEmpty(); }
    int size() const { return providerToVirtual.size(); }

    QString findProviderFor(const QString &virtualName) const {
        return virtualToProvider.value(virtualName);
    }

    QString findVirtualFor(const QString &providerName) const {
        return providerToVirtual.value(providerName);
    }

    bool hasVirtual(const QString &virtualName) const {
        return virtualToProvider.contains(virtualName);
    }

    bool hasProvider(const QString &providerName) const {
        return providerToVirtual.contains(providerName);
    }
};

/**
 * @brief Level 1 pure parser for /etc/prt-get.aliases.
 * Parses lines in the format: "<provider>: <virtual>".
 */
class AliasesParser {
public:
    /**
     * @brief Parses the raw content of /etc/prt-get.aliases.
     * Ignores empty lines and comments starting with '#'.
     * @param rawContent String containing the file content.
     * @return AliasesMapping with bidirectional lookup tables.
     */
    static AliasesMapping parse(const QString &rawContent);

    /**
     * @brief Applies aliases to a collection of PortInfo objects.
     * For any port whose name matches a virtual package, if its concrete provider
     * is installed in `ports`, `port.providedByAlias` is set to the provider's name.
     * @param ports Mutable list of PortInfo objects.
     * @param aliases Parsed aliases mapping.
     * @param onlyIfProviderInstalled If true, only sets `providedByAlias` if provider is installed.
     */
    static void applyAliases(QList<PortInfo> &ports, const AliasesMapping &aliases, bool onlyIfProviderInstalled = true);
};

} // namespace OctoCRUX
