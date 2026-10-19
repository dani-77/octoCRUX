#include "build_log_analyzer.h"
#include <QRegularExpression>

namespace OctoCRUX {

OperationErrorKind BuildLogAnalyzer::classifyLog(const QString &logOutput, int exitCode) {
    if (exitCode == 0) {
        return OperationErrorKind::None;
    }

    // 1. Direct exit codes from pkgmk(8)
    switch (exitCode) {
    case 2: return OperationErrorKind::PkgfileError;
    case 3: return OperationErrorKind::SourceDirError;
    case 4: return OperationErrorKind::DownloadError;
    case 5: return OperationErrorKind::UnpackError;
    case 6: return OperationErrorKind::ChecksumError;
    case 7: return OperationErrorKind::FootprintError;
    case 8: return OperationErrorKind::CompilationError;
    case 9: return OperationErrorKind::InstallError;
    default:
        break;
    }

    // 2. Pattern-based classification for prt-get wrapper or generic exit code (1 / 255)
    // Priority 1: Out of disk space (ENOSPC)
    if (logOutput.contains(QStringLiteral("No space left on device"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("ENOSPC"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("out of disk space"), Qt::CaseInsensitive)) {
        return OperationErrorKind::DiskSpaceError;
    }

    // Priority 2: Footprint discrepancies
    if (logOutput.contains(QStringLiteral("Footprint mismatch"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("Footprint differences"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("footprint error"), Qt::CaseInsensitive)) {
        return OperationErrorKind::FootprintError;
    }

    // Priority 3: Checksum mismatches
    if (logOutput.contains(QStringLiteral("md5sum mismatch"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("MD5 mismatch"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("checksum failed"), Qt::CaseInsensitive)) {
        return OperationErrorKind::ChecksumError;
    }

    // Priority 4: Signify signature failures
    if (logOutput.contains(QStringLiteral("signify:"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("signature verification failed"), Qt::CaseInsensitive)) {
        return OperationErrorKind::SignatureError;
    }

    // Priority 5: Download failures
    if (logOutput.contains(QStringLiteral("failure downloading"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("download failed"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("curl: ("), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("404 Not Found"), Qt::CaseInsensitive)) {
        return OperationErrorKind::DownloadError;
    }

    // Priority 6: Unpack failures
    if (logOutput.contains(QStringLiteral("error unpacking"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("tar: Error"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("unpacking failed"), Qt::CaseInsensitive)) {
        return OperationErrorKind::UnpackError;
    }

    // Priority 7: Pkgfile syntax errors
    if (logOutput.contains(QStringLiteral("Pkgfile is invalid"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("syntax error in Pkgfile"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("variable 'name' not set"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("variable 'version' not set"), Qt::CaseInsensitive)) {
        return OperationErrorKind::PkgfileError;
    }

    // Priority 8: Source/work directory permission errors
    if (logOutput.contains(QStringLiteral("lacking read/write permissions"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive)) {
        return OperationErrorKind::SourceDirError;
    }

    // Priority 9: Installation errors via pkgadd
    if (logOutput.contains(QStringLiteral("pkgadd:"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("error while installing package"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("failed to install package"), Qt::CaseInsensitive)) {
        return OperationErrorKind::InstallError;
    }

    // Priority 10: Compiler / build function errors
    if (logOutput.contains(QStringLiteral(": error:"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("fatal error:"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("build function"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("make: ***"), Qt::CaseInsensitive) ||
        logOutput.contains(QStringLiteral("ninja: build stopped"), Qt::CaseInsensitive)) {
        return OperationErrorKind::CompilationError;
    }

    return OperationErrorKind::GeneralError;
}

QString BuildLogAnalyzer::extractSummary(const QString &logOutput, OperationErrorKind kind) {
    switch (kind) {
    case OperationErrorKind::None:
        return QStringLiteral("Operation completed successfully");
    case OperationErrorKind::DiskSpaceError:
        return QStringLiteral("Disk space exhausted (ENOSPC) during build in work directory");
    case OperationErrorKind::FootprintError:
        return QStringLiteral("Footprint mismatch: package contents differ from port's .footprint");
    case OperationErrorKind::ChecksumError:
        return QStringLiteral("Checksum error: MD5 mismatch in downloaded source archive");
    case OperationErrorKind::SignatureError:
        return QStringLiteral("Signature error: digital signature verification failed");
    case OperationErrorKind::DownloadError:
        return QStringLiteral("Download failed: could not retrieve source file from upstream mirror");
    case OperationErrorKind::UnpackError:
        return QStringLiteral("Unpack error: failed to extract source archive");
    case OperationErrorKind::PkgfileError:
        return QStringLiteral("Invalid Pkgfile: syntax error or missing mandatory variables");
    case OperationErrorKind::SourceDirError:
        return QStringLiteral("Permission or directory error in source/work directory");
    case OperationErrorKind::CompilationError:
        return QStringLiteral("Compilation error: compiler or build() command returned failure");
    case OperationErrorKind::InstallError:
        return QStringLiteral("Installation error: pkgadd failed to install package");
    case OperationErrorKind::GeneralError:
    default: {
        const QString failing = extractFailingPort(logOutput);
        if (!failing.isEmpty()) {
            return QStringLiteral("Operation failed while processing port '%1'").arg(failing);
        }
        return QStringLiteral("Operation failed with error");
    }
    }
}

QString BuildLogAnalyzer::extractFailingPort(const QString &logOutput) {
    static const QRegularExpression prtGetFailedRegex(
        QStringLiteral("prt-get:\\s+(?:failed to (?:build|install)|error in)\\s+['\"]?([a-zA-Z0-9_\\-\\.\\+]+)['\"]?"),
        QRegularExpression::CaseInsensitiveOption
    );

    QRegularExpressionMatch match = prtGetFailedRegex.match(logOutput);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    static const QRegularExpression buildingRegex(
        QStringLiteral("======>\\s+Building\\s+['\"]?([a-zA-Z0-9_\\-\\.\\+]+)['\"]?"),
        QRegularExpression::CaseInsensitiveOption
    );

    // Look for the last "Building <port>" line in the log
    QRegularExpressionMatchIterator it = buildingRegex.globalMatch(logOutput);
    QString lastPort;
    while (it.hasNext()) {
        lastPort = it.next().captured(1);
    }

    return lastPort;
}

QString BuildLogAnalyzer::errorKindToString(OperationErrorKind kind) {
    switch (kind) {
    case OperationErrorKind::None:             return QStringLiteral("Success");
    case OperationErrorKind::GeneralError:     return QStringLiteral("General error");
    case OperationErrorKind::PkgfileError:     return QStringLiteral("Pkgfile error");
    case OperationErrorKind::SourceDirError:   return QStringLiteral("Source directory error");
    case OperationErrorKind::DownloadError:    return QStringLiteral("Download error");
    case OperationErrorKind::UnpackError:      return QStringLiteral("Unpack error");
    case OperationErrorKind::ChecksumError:    return QStringLiteral("Checksum mismatch");
    case OperationErrorKind::FootprintError:   return QStringLiteral("Footprint mismatch");
    case OperationErrorKind::CompilationError: return QStringLiteral("Compilation error");
    case OperationErrorKind::InstallError:     return QStringLiteral("Installation error");
    case OperationErrorKind::SignatureError:   return QStringLiteral("Signature verification error");
    case OperationErrorKind::DiskSpaceError:   return QStringLiteral("Out of disk space (ENOSPC)");
    case OperationErrorKind::ProtectedPackage: return QStringLiteral("Core package protection");
    default:                                   return QStringLiteral("Unknown error");
    }
}

} // namespace OctoCRUX
