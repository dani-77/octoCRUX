#pragma once

#include "octocrux/types.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <memory>

namespace OctoCRUX {

/**
 * @brief Headless asynchronous process executor for CRUX package operations.
 * Streams stdout and stderr line-by-line without blocking the GUI event loop,
 * tracks compilation phases, and provides timeout-guarded cancellation.
 */
class CruxProcessRunner : public QObject {
    Q_OBJECT
public:
    explicit CruxProcessRunner(QObject *parent = nullptr);
    ~CruxProcessRunner() override;

    bool isRunning() const;
    int exitCode() const;
    OperationErrorKind errorKind() const;
    QString summary() const;
    QString capturedLog() const;

    /**
     * @brief Launches the process asynchronously with non-blocking line streaming.
     * The child's stdin is closed right after start, so reads from it see EOF.
     */
    bool startProcess(
        const QString &program,
        const QStringList &arguments,
        const QString &workingDirectory = QString(),
        const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment()
    );

    /**
     * @brief Cancels the active process gracefully with SIGTERM/terminate.
     * Does not send SIGKILL to prevent leaving orphaned root child build processes.
     */
    void cancel();

signals:
    void outputReceived(const QString &line, bool isError);
    void phaseChanged(const QString &phase);
    void processFinished(int exitCode, OperationErrorKind errorKind, const QString &summary);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void processOutputBuffer(QByteArray &buffer, bool isError);

    std::unique_ptr<QProcess> m_process;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QString m_accumulatedLog;

    int m_exitCode = 0;
    OperationErrorKind m_errorKind = OperationErrorKind::None;
    QString m_summary;
    bool m_wasCancelled = false;
};

} // namespace OctoCRUX
