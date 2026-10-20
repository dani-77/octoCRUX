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
     * @brief Requests cancellation of the active process with SIGTERM (QProcess::terminate).
     * Never escalates to SIGKILL: killing the privilege helper could leave the root
     * build tree running unsupervised. If the process is still alive after
     * cancelGracePeriodMs(), cancellationStalled() and a warning line are emitted.
     * Calling cancel() again resends SIGTERM and restarts the grace period.
     */
    void cancel();

    /** Default delay between a cancellation request and the "still running" warning. */
    static constexpr int kDefaultCancelGracePeriodMs = 10000;

    void setCancelGracePeriodMs(int ms);
    int cancelGracePeriodMs() const;

signals:
    void outputReceived(const QString &line, bool isError);
    /**
     * Latest complete state of a terminal line redrawn with '\r' (progress meters).
     * Not logged: only the final state, when the line ends with '\n', reaches
     * outputReceived() and capturedLog().
     */
    void transientOutput(const QString &text);
    void phaseChanged(const QString &phase);
    void processFinished(int exitCode, OperationErrorKind errorKind, const QString &summary);
    /**
     * The process ignored a cancellation request for the whole grace period.
     * Emitted alongside the warning line on outputReceived(). Nothing in the UI
     * connects to it yet: users see only that output line in the Output tab.
     */
    void cancellationStalled(qint64 pid, int elapsedMs);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onCancelGracePeriodElapsed();

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

    QTimer m_cancelWatchdog;
};

} // namespace OctoCRUX
