#include "crux_process_runner.h"
#include "core/build_log_analyzer.h"

#include <QRegularExpression>

namespace OctoCRUX {

CruxProcessRunner::CruxProcessRunner(QObject *parent)
    : QObject(parent)
    , m_killTimer(new QTimer(this))
{
    m_killTimer->setSingleShot(true);
    connect(m_killTimer, &QTimer::timeout, this, &CruxProcessRunner::onKillTimeout);
}

CruxProcessRunner::~CruxProcessRunner() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
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
    return m_process->waitForStarted(2000);
}

void CruxProcessRunner::cancel() {
    if (!isRunning()) {
        return;
    }

    m_wasCancelled = true;
    emit outputReceived(QStringLiteral("\n[octoCRUX] Cancellation requested by user. Terminating process...\n"), true);

    // Request termination (SIGTERM on Linux)
    m_process->terminate();

    // Start 3000ms watchdog timer to escalate to SIGKILL if the process does not terminate
    m_killTimer->start(3000);
}

void CruxProcessRunner::onKillTimeout() {
    if (isRunning()) {
        emit outputReceived(QStringLiteral("\n[octoCRUX] Process did not terminate within timeout. Forcing termination (SIGKILL)...\n"), true);
        m_process->kill();
    }
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

        // Check for phase transitions (e.g. ======> Building '...')
        static const QRegularExpression phaseRegex(QStringLiteral("^======>\\s+(.*)$"));
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
    if (m_killTimer->isActive()) {
        m_killTimer->stop();
    }

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

    if (m_wasCancelled || exitStatus == QProcess::CrashExit) {
        m_errorKind = OperationErrorKind::GeneralError;
        m_summary = m_wasCancelled
            ? QStringLiteral("Operation was cancelled by user")
            : QStringLiteral("Process terminated unexpectedly (crashed or killed)");
    } else {
        m_errorKind = BuildLogAnalyzer::classifyLog(m_accumulatedLog, exitCode);
        m_summary = BuildLogAnalyzer::extractSummary(m_accumulatedLog, m_errorKind);
    }

    emit processFinished(m_exitCode, m_errorKind, m_summary);
}

} // namespace OctoCRUX
