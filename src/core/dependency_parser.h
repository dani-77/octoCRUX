#pragma once

#include "aliases_parser.h"
#include <QString>
#include <QStringList>
#include <QSet>

namespace OctoCRUX {

/**
 * @brief Level 1 pure parser for dependency and impact query outputs:
 * - `prt-get dependent <port>` (installed direct dependents)
 * - `prt-get dependent --all <port>` (all declared dependents)
 * - `prt-get quickdep <port>` (recursive installation order)
 */
class DependencyParser {
public:
    /**
     * @brief Parses line-by-line output of `prt-get dependent` (with or without --all).
     * Trims whitespace and excludes blank lines.
     */
    static QStringList parseDependents(const QString &rawOutput);

    /**
     * @brief Parses whitespace-delimited output of `prt-get quickdep <port>`.
     * The returned list preserves CRUX recursive dependency order, where the target
     * port is the last element.
     */
    static QStringList parseQuickdep(const QString &rawOutput);

    /**
     * @brief Extracts the target port from a quickdep order list (the last element).
     */
    static QString extractTargetPort(const QStringList &quickdepOrder);

    /**
     * @brief Replaces virtual package names in the quickdep order with their concrete installed providers.
     * E.g., if "rust" is in the order and "rust-bin" is installed (and mapped in aliases),
     * "rust" is replaced with "rust-bin".
     * @param rawOrder Original order returned by quickdep.
     * @param aliases Parsed aliases mapping.
     * @param installedPackages Set of currently installed package names.
     * @return Transformed list with satisfied virtuals resolved to concrete providers.
     */
    static QStringList resolveQuickdepWithAliases(
        const QStringList &rawOrder,
        const AliasesMapping &aliases,
        const QSet<QString> &installedPackages
    );
};

} // namespace OctoCRUX
