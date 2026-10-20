#include "crux_process_runner.h"
#include "core/build_log_analyzer.h"

#include <QRegularExpression>

namespace OctoCRUX {

CruxProcessRunner::CruxProcessRunner(QObject *parent)
    : QObject(parent)
{
    m_cancelWatchdog.setSingleShot(true);
    m_cancelWatchdog.setInterval(kDefaultCancelGracePeriodMs);
    connect(&m_cancelWatchdog, &QTimer::timeout,
            this, &CruxProcessRunner::onCancelGracePeriodElapsed);
}

CruxProcessRunner::~CruxProcessRunner() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        m_process->waitForFinished(1000);
    }
}

bool CruxProcessRunner::isRunning() const {
    return m_process && (m_process->state() != QProcess::NotRunning);
}

int CruxProcessRunner::exitCode() const {
    return m_exitCode;
}

OperationErrorKind CruxProcessRunner::errorKind() const {
    return m_errorKind;
}

QString CruxProcessRunner::summary() const {
    return m_summary;
}

QString CruxProcessRunner::capturedLog() const {
    return m_accumulatedLog;
}

void CruxProcessRunner::setCancelGracePeriodMs(int ms) {
    m_cancelWatchdog.setInterval(ms);
}

int CruxProcessRunner::cancelGracePeriodMs() const {
    return m_cancelWatchdog.interval();
}

bool CruxProcessRunner::startProcess(
    const QString &program,
    const QStringList &arguments,
    const QString &workingDirectory,
    const QProcessEnvironment &environment)
{
    if (isRunning()) {
        return false;
    }

    m_exitCode = 0;
    m_errorKind = OperationErrorKind::None;
    m_summary.clear();
    m_accumulatedLog.clear();
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_wasCancelled = false;
    m_cancelWatchdog.stop();

    m_process = std::make_unique<QProcess>();

    if (!workingDirectory.isEmpty()) {
        m_process->setWorkingDirectory(workingDirectory);
    }

    m_process->setProcessEnvironment(environment);

    connect(m_process.get(), &QProcess::readyReadStandardOutput,
            this, &CruxProcessRunner::onReadyReadStandardOutput);
    connect(m_process.get(), &QProcess::readyReadStandardError,
            this, &CruxProcessRunner::onReadyReadStandardError);
    connect(m_process.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &CruxProcessRunner::onProcessFinished);

    m_process->start(program, arguments);
    if (!m_process->waitForStarted(2000)) {
        return false;
    }

    // Nothing is ever written to the child. Close its stdin so an interactive
    // prompt in a Pkgfile (or anything else reading stdin) gets EOF and fails,
    // instead of blocking forever on a pipe nobody writes to.
    m_process->closeWriteChannel();
    return true;
}

void CruxProcessRunner::cancel() {
    if (!isRunning()) {
        return;
    }

    m_wasCancelled = true;
    emit outputReceived(QStringLiteral("\n[octoCRUX] Cancellation requested by user. Terminating process...\n"), true);

    // Request graceful termination (SIGTERM on Linux). This reaches only the direct
    // child (the privilege helper); whether it relays the signal to the root build
    // tree is open test 2.10.3.
    m_process->terminate();
    m_cancelWatchdog.start();
}

void CruxProcessRunner::onCancelGracePeriodElapsed() {
    if (!isRunning()) {
        return;
    }

    const qint64 pid = m_process->processId();
    const int elapsedMs = m_cancelWatchdog.interval();
    emit outputReceived(QStringLiteral(
        "[octoCRUX] Warning: process %1 (%2) is still running %3 s after the cancellation request. "
        "OctoCRUX does not force-kill it: killing the privilege helper could leave root build "
        "processes running unsupervised. Press Cancel again to resend the request, or wait. "
        "If it does not stop, check 'ps -ef' for prt-get/pkgmk and stop them from a root terminal.")
        .arg(pid)
        .arg(m_process->program())
        .arg(elapsedMs / 1000.0, 0, 'g', 3), true);
    emit cancellationStalled(pid, elapsedMs);
}

void CruxProcessRunner::processOutputBuffer(QByteArray &buffer, bool isError) {
    int newlineIdx = -1;
    while ((newlineIdx = buffer.indexOf('\n')) != -1) {
        const QByteArray rawLine = buffer.left(newlineIdx);
        buffer.remove(0, newlineIdx + 1);

        QString line = QString::fromUtf8(rawLine);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }

        m_accumulatedLog.append(line);
        m_accumulatedLog.append(QLatin1Char('\n'));

        // Check for phase transitions (pkgutils/pkgmk exact format: =======> Building '...')
        static const QRegularExpression phaseRegex(QStringLiteral("^=======>\\s+(.*)$"));
        QRegularExpressionMatch match = phaseRegex.match(line);
        if (match.hasMatch()) {
            emit phaseChanged(match.captured(1).trimmed());
        }

        emit outputReceived(line, isError);
    }
}

void CruxProcessRunner::onReadyReadStandardOutput() {
    if (!m_process) return;
    m_stdoutBuffer.append(m_process->readAllStandardOutput());
    processOutputBuffer(m_stdoutBuffer, false);
}

void CruxProcessRunner::onReadyReadStandardError() {
    if (!m_process) return;
    m_stderrBuffer.append(m_process->readAllStandardError());
    processOutputBuffer(m_stderrBuffer, true);
}

void CruxProcessRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_cancelWatchdog.stop();

    // Flush any remaining buffers
    if (!m_stdoutBuffer.isEmpty()) {
        const QString line = QString::fromUtf8(m_stdoutBuffer);
        m_accumulatedLog.append(line);
        emit outputReceived(line, false);
        m_stdoutBuffer.clear();
    }
    if (!m_stderrBuffer.isEmpty()) {
        const QString line = QString::fromUtf8(m_stderrBuffer);
        m_accumulatedLog.append(line);
        emit outputReceived(line, true);
        m_stderrBuffer.clear();
    }

    m_exitCode = exitCode;

    if (m_wasCancelled) {
        // QProcess only defines exitCode() for a normal exit; say how the helper ended.
        m_errorKind = OperationErrorKind::Cancelled;
        m_summary = exitStatus == QProcess::NormalExit
            ? QStringLiteral("Operation was cancelled by user (helper exited with code %1)").arg(exitCode)
            : QStringLiteral("Operation was cancelled by user (helper terminated by a signal)");
    } else if (exitStatus == QProcess::CrashExit) {
        m_errorKind = OperationErrorKind::GeneralError;
        m_summary = QStringLiteral("Process terminated unexpectedly (crashed or killed)");
    } else {
        m_errorKind = BuildLogAnalyzer::classifyLog(m_accumulatedLog, exitCode);
        m_summary = BuildLogAnalyzer::extractSummary(m_accumulatedLog, m_errorKind);
    }

    emit processFinished(m_exitCode, m_errorKind, m_summary);
}

} // namespace OctoCRUX
