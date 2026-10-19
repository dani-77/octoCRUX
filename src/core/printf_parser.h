#pragma once

#include "octocrux/types.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <optional>

namespace OctoCRUX {

/**
 * @brief Level 1 pure parser for prt-get printf tab-delimited streams.
 * Decoupled from I/O and process execution for high testability.
 */
class PrintfParser {
public:
    // Canonical 11-token production format: description (%d) placed strictly last to isolate embedded tabs
    static constexpr const char* PRODUCTION_FORMAT = "%n\t%v\t%r\t%i\t%l\t%p\t%R\t%E\t%O\t%e\t%d\n";
    static constexpr const char* FIXTURE_FORMAT = "%n\t%v\t%r\t%i\t%p\t%d\t%R\t%e\n";

    enum class Schema {
        Production11Token, // Canonical production format: %n\t%v\t%r\t%i\t%l\t%p\t%R\t%E\t%O\t%e\t%d
        Fixture8Token,     // Diagnostic run 1 format: %n\t%v\t%r\t%i\t%p\t%d\t%R\t%e
        AutoDetect         // Explicit opt-in for auto-detection
    };

    /**
     * @brief Parses the entire multi-line stdout of `prt-get printf`.
     * @param rawOutput The raw stdout from prt-get printf.
     * @param schema Schema to enforce (defaults strictly to Production11Token).
     * @return List of parsed PortInfo objects.
     */
    static QList<PortInfo> parseAll(const QString &rawOutput, Schema schema = Schema::Production11Token);

    /**
     * @brief Parses a single line of `prt-get printf` output.
     * @param line A single line of text.
     * @param schema Schema to enforce (defaults strictly to Production11Token).
     * @return PortInfo if line is valid, std::nullopt if line is empty or malformed.
     */
    static std::optional<PortInfo> parseLine(const QString &line, Schema schema = Schema::Production11Token);

    /**
     * @brief Extracts the collection name from a collection directory path.
     * E.g. "/usr/ports/core" -> "core", "/usr/ports/d77crux" -> "d77crux".
     */
    static QString extractCollectionName(const QString &collectionPath);

    /**
     * @brief Converts raw string to InstallStatus ("yes", "no", "diff").
     * Returns std::nullopt for any unknown value in strict parsing.
     */
    static std::optional<InstallStatus> parseInstallStatus(const QString &statusStr);

    /**
     * @brief Converts raw string to LockStatus ("yes", "no").
     * Returns std::nullopt for any unknown value in strict parsing.
     */
    static std::optional<LockStatus> parseLockStatus(const QString &lockStr);

    /**
     * @brief Cross-references and applies locks to a list of ports.
     * Useful when locks are read from /var/lib/pkg/prt-get.locker or prt-get listlocked.
     */
    static void applyLocks(QList<PortInfo> &ports, const QStringList &lockedPortNames);
};

} // namespace OctoCRUX
