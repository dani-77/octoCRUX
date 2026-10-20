#pragma once

#include "octocrux/types.h"
#include <QString>
#include <QStringList>

namespace OctoCRUX {

/**
 * @brief Pure log analyzer and error classifier for pkgmk builds run by prt-get.
 *
 * Classification is driven by the exact message lines pkgmk(8) prints
 * ("=======> ERROR: ..."), taken from pkgutils 5.40.12 as shipped on CRUX 3.8,
 * not by the process exit code: prt-get documents no exit code mapping,
 * and pkgmk exits 1 for several distinct failures. See OPEN_QUESTIONS.md 2.14.
 */
class BuildLogAnalyzer {
public:
    /**
     * @brief Classifies a failed operation from its captured log.
     * @param exitCode only distinguishes success (0) from failure (non-zero);
     *        its value is never mapped to an error kind.
     */
    static OperationErrorKind classifyLog(const QString &logOutput, int exitCode);

    /**
     * @brief Extracts an informative, concise summary of the failure suitable for UI display.
     */
    static QString extractSummary(const QString &logOutput, OperationErrorKind kind);

    /**
     * @brief Returns the name of the last port pkgmk started building, if any.
     * Parsed from "=======> Building '<dir>/<name>#<version>-<release>.pkg.tar.<ext>'."
     */
    static QString extractFailingPort(const QString &logOutput);

    /**
     * @brief Returns a human-readable string representation of the error kind.
     */
    static QString errorKindToString(OperationErrorKind kind);
};

} // namespace OctoCRUX
