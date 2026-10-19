#pragma once

#include "octocrux/types.h"
#include <QString>
#include <QStringList>

namespace OctoCRUX {

/**
 * @brief Pure log analyzer and error classifier for pkgmk, prt-get, and pkgadd builds.
 * Maps compiler output, packaging errors, and exit codes to OperationErrorKind.
 */
class BuildLogAnalyzer {
public:
    /**
     * @brief Classifies a build failure based on exitCode and captured log content.
     */
    static OperationErrorKind classifyLog(const QString &logOutput, int exitCode);

    /**
     * @brief Extracts an informative, concise summary of the failure suitable for UI display.
     */
    static QString extractSummary(const QString &logOutput, OperationErrorKind kind);

    /**
     * @brief Extracts the specific failing port name from prt-get / sysup output if present.
     */
    static QString extractFailingPort(const QString &logOutput);

    /**
     * @brief Returns a human-readable string representation of the error kind.
     */
    static QString errorKindToString(OperationErrorKind kind);
};

} // namespace OctoCRUX
