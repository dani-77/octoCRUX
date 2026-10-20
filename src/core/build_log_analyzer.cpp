#include "build_log_analyzer.h"
#include <QRegularExpression>

namespace OctoCRUX {

namespace {

// Prefix pkgmk(8) puts on every error() line: info "ERROR: $1" -> echo "=======> $1"
const QString kPkgmkError = QStringLiteral("=======> ERROR: ");

// An exact pkgmk 5.40.12 error() message: fixed text before and after the
// variable part (a path, URL or variable name). Empty suffix = prefix match only.
struct PkgmkMessage {
    QString prefix;
    QString suffix;
};

const QList<PkgmkMessage> kSignatureMessages = {
    {QStringLiteral("Signature mismatch found:"), {}},
    {QStringLiteral("Failed verifying cryptographic digest"), {}},
    {QStringLiteral("Creating signature failed"), {}},
    {QStringLiteral("Refreshing signature failed"), {}},
};
const QList<PkgmkMessage> kChecksumMessages = {
    {QStringLiteral("Md5sum mismatch found:"), {}},
    {QStringLiteral("Md5sum not ok."), {}},
};
const QList<PkgmkMessage> kFootprintMessages = {
    {QStringLiteral("Footprint mismatch found:"), {}},
};
const QList<PkgmkMessage> kDownloadMessages = {
    {QStringLiteral("Downloading '"), QStringLiteral("' failed.")},
    {QStringLiteral("Source file '"), QStringLiteral("(use option -d to download).")},
    {QStringLiteral("Source file '"), QStringLiteral("(can not be downloaded, URL not specified).")},
    {QStringLiteral("Command '"), QStringLiteral("' not found.")}, // download program missing
};
const QList<PkgmkMessage> kPkgfileMessages = {
    {QStringLiteral("Variable '"), {}},   // Variable '<var>' not specified in <Pkgfile>.
    {QStringLiteral("Function 'build' not specified in"), {}},
    {QStringLiteral("File '"), QStringLiteral("' not found.")}, // Pkgfile or -cf config missing
};
const QList<PkgmkMessage> kDirectoryMessages = {
    {QStringLiteral("Directory '"), QStringLiteral("' does not exist.")},
    {QStringLiteral("Directory '"), QStringLiteral("' not writable.")},
    {QStringLiteral("Directory '"), QStringLiteral("' not readable.")},
    {QStringLiteral("File '"), QStringLiteral("' is not writable.")},
};
const QList<PkgmkMessage> kInstallMessages = {
    {QStringLiteral("Installing '"), QStringLiteral("' failed.")}, // pkgmk -i/-u only
};
const QString kBuildFailedMessage = QStringLiteral("Building '");      // Building '<package>' failed.
const QString kBuildingPhase = QStringLiteral("=======> Building '"); // info "Building '$TARGET'[ succeeded]."
// pkgmk runs build() as (set -e -x ; build), so its xtrace always starts with this line.
const QString kBuildTraceStart = QStringLiteral("+ build");

// sudo 1.9.17p2 through qt-sudo 2.4.1 when the password dialog is cancelled
// (tests/fixtures/crux3.8/qtsudo-auth). "Sorry, try again." alone is not an
// error: sudo retries, and the run may still succeed.
const QString kSudoPasswordRequired = QStringLiteral("sudo: a password is required");

// libc strerror(ENOSPC); printed by whichever tool hit the full filesystem.
const QString kNoSpace = QStringLiteral("No space left on device");
// prt-get 5.19.9 message fragment when its own pkgadd step fails. Taken from the
// binary's strings only; the full line has not been observed yet [hypothesis].
const QString kPrtGetPkgaddError = QStringLiteral("error while pkgadding");

bool hasPkgmkError(const QStringList &errorMessages, const QList<PkgmkMessage> &candidates) {
    for (const QString &msg : errorMessages) {
        for (const PkgmkMessage &candidate : candidates) {
            if (msg.startsWith(candidate.prefix) && msg.endsWith(candidate.suffix)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

OperationErrorKind BuildLogAnalyzer::classifyLog(const QString &logOutput, int exitCode) {
    if (exitCode == 0) {
        return OperationErrorKind::None;
    }

    // The exit code value is deliberately ignored: OctoCRUX runs prt-get, whose man
    // page leaves the value -> meaning mapping "to be determined", and pkgmk itself
    // exits 1 for footprint, build() and pkgadd failures alike. Only the log text
    // tells the failure classes apart.
    const QStringList lines = logOutput.split(QLatin1Char('\n'));

    // pkgmk prints "+ build" (xtrace, stderr) when build() starts, after unpacking.
    // A port's build ends with "Building '...' succeeded." (stdout) or
    // "ERROR: Building '...' failed." (stderr). The trace flag is reset at those end
    // lines rather than at the "=======> Building '...'." start line, because the
    // runner interleaves stdout and stderr in arrival order, and a fast unpack can
    // let "+ build" arrive before the start line; the previous port's end line is
    // separated from it by the whole install step.
    QStringList errorMessages;          // text after "=======> ERROR: "
    bool buildFailed = false;           // "ERROR: Building '...' failed." seen
    bool failedBuildHadTrace = false;   // build() had started in the port that failed
    bool traceInCurrentPort = false;
    for (const QString &line : lines) {
        if (line == kBuildTraceStart) {
            traceInCurrentPort = true;
        } else if (line.startsWith(kBuildingPhase) && line.endsWith(QStringLiteral("' succeeded."))) {
            traceInCurrentPort = false;
        } else if (line.startsWith(kPkgmkError)) {
            const QString msg = line.mid(kPkgmkError.size());
            errorMessages.append(msg);
            if (msg.startsWith(kBuildFailedMessage) && msg.endsWith(QStringLiteral("' failed."))) {
                buildFailed = true;
                failedBuildHadTrace = traceInCurrentPort;
                traceInCurrentPort = false;
            }
        }
    }

    // Authentication failed: the command never ran as root, so nothing else in the
    // log describes a build.
    for (const QString &line : lines) {
        if (line.trimmed() == kSudoPasswordRequired) {
            return OperationErrorKind::AuthenticationError;
        }
    }

    // Order matters: md5sum, signature and footprint failures are each followed by
    // the generic "Building '...' failed." line, and ENOSPC usually surfaces as a
    // build() failure, so the specific causes are checked before the generic one.
    if (logOutput.contains(kNoSpace)) {
        return OperationErrorKind::DiskSpaceError;
    }
    if (hasPkgmkError(errorMessages, kSignatureMessages)) {
        return OperationErrorKind::SignatureError;
    }
    if (hasPkgmkError(errorMessages, kChecksumMessages)) {
        return OperationErrorKind::ChecksumError;
    }
    if (hasPkgmkError(errorMessages, kFootprintMessages)) {
        return OperationErrorKind::FootprintError;
    }
    if (hasPkgmkError(errorMessages, kDownloadMessages)) {
        return OperationErrorKind::DownloadError;
    }
    if (hasPkgmkError(errorMessages, kPkgfileMessages)) {
        return OperationErrorKind::PkgfileError;
    }
    if (hasPkgmkError(errorMessages, kDirectoryMessages)) {
        return OperationErrorKind::SourceDirError;
    }
    if (hasPkgmkError(errorMessages, kInstallMessages) || logOutput.contains(kPrtGetPkgaddError)) {
        return OperationErrorKind::InstallError;
    }
    if (buildFailed) {
        // A build failure before build() started means unpack_source() failed (E_UNPACK).
        return failedBuildHadTrace ? OperationErrorKind::CompilationError
                                   : OperationErrorKind::UnpackError;
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
    case OperationErrorKind::AuthenticationError:
        return QStringLiteral("Authentication failed or was cancelled; nothing was run as root and no changes were made");
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
    // pkgmk: info "Building '$TARGET'." and error "Building '$TARGET' failed.", with
    // TARGET=$PKGMK_PACKAGE_DIR/$name#$version-$release.pkg.tar.<ext>. md5sum and
    // signature failures happen before the first form, so both are matched.
    static const QRegularExpression buildingRegex(
        QStringLiteral("^=======> (?:ERROR: )?Building '(?:[^']*/)?([^/'#]+)#[^']*'(?: failed)?\\.$"),
        QRegularExpression::MultilineOption
    );

    QRegularExpressionMatchIterator it = buildingRegex.globalMatch(logOutput);
    QString lastPort;
    while (it.hasNext()) {
        lastPort = it.next().captured(1);
    }

    return lastPort;
}

QList<BuildLogAnalyzer::FootprintFailure> BuildLogAnalyzer::extractFootprintFailures(const QString &logOutput) {
    static const QString mismatch = kPkgmkError + QStringLiteral("Footprint mismatch found:");
    QList<FootprintFailure> failures;
    bool inMismatch = false;
    bool sawNew = false;
    bool sawMissing = false;
    for (const QString &line : logOutput.split(QLatin1Char('\n'))) {
        if (line == mismatch) {
            inMismatch = true;
            sawNew = sawMissing = false;
            continue;
        }
        if (!inMismatch) {
            continue;
        }
        // check_footprint() prints the diff as "NEW       <entry>" / "MISSING   <entry>".
        if (line.startsWith(QStringLiteral("NEW "))) {
            sawNew = true;
        } else if (line.startsWith(QStringLiteral("MISSING "))) {
            sawMissing = true;
        } else if (line.startsWith(kPkgmkError)) {
            const QString port = extractFailingPort(line);
            if (!port.isEmpty()) {
                failures.append({port, sawNew && !sawMissing});
            }
            inMismatch = false;
        }
    }
    return failures;
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
    case OperationErrorKind::Cancelled:        return QStringLiteral("Cancelled by user");
    case OperationErrorKind::AuthenticationError: return QStringLiteral("Authentication failed");
    default:                                   return QStringLiteral("Unknown error");
    }
}

} // namespace OctoCRUX
