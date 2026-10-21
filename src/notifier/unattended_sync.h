#pragma once

#include "backend/crux_process_runner.h"

#include <QObject>
#include <QString>

namespace OctoCRUX {

/**
 * @brief Runs `ports -u` as root without ever asking for a password.
 *
 * Used for the notifier's scheduled sync, where a password dialog popping up
 * at a random time is not acceptable. Two mechanisms, tried in order:
 *  1. polkit: if `pkcheck -a org.octocrux.ports-sync` says this process is
 *     authorized without interaction (the opt-in rule is installed), run
 *     `pkexec /usr/bin/ports -u`. The pre-check is needed because pkexec has
 *     no non-interactive mode. An unprivileged pkcheck cannot ask about
 *     pkexec's generic action with details ("Only trusted callers ... can
 *     pass details", polkit 128), hence a dedicated action
 *     (contrib/polkit/org.octocrux.ports-sync.policy).
 *  2. `sudo -n /usr/bin/ports -u`: works if sudoers allows that command
 *     without a password; otherwise sudo fails at once
 *     ("sudo: a password is required") and asks nothing.
 * If neither authorizes the sync, the result is NeedsAuthentication.
 */
class UnattendedSync : public QObject {
    Q_OBJECT
public:
    enum class Result { Synced, NeedsAuthentication, Failed };

    /**
     * pkcheck's answer for org.octocrux.ports-sync, without interaction. The action
     * allows active sessions only with authentication (auth_admin) and refuses
     * inactive ones, so the exit code tells the cases apart:
     *  0 Authorized, 2 NeedsAuthentication (active session, no rule),
     *  1 InactiveSession (polkit does not see a local active session: no
     *    ConsoleKit/elogind session; observed on the T480s, 2026-10-21),
     *  127 ActionNotRegistered ("Action ... is not registered", observed),
     *  Unavailable: no pkcheck, timeout or other errors.
     */
    enum class PolkitStatus { Authorized, NeedsAuthentication, InactiveSession, ActionNotRegistered, Unavailable };
    enum class Method { None, Polkit, Sudo };

    static constexpr const char *kActionId = "org.octocrux.ports-sync";

    explicit UnattendedSync(QObject *parent = nullptr);

    /** Starts the sync of the given absolute ports(8) path. finished() always follows. */
    void start(const QString &portsPath);
    bool isRunning() const { return m_runner.isRunning(); }

    static PolkitStatus polkitStatus();
    /** True if polkit authorizes org.octocrux.ports-sync for this process without asking. */
    static bool polkitAuthorized() { return polkitStatus() == PolkitStatus::Authorized; }
    /** One sentence explaining a status to the user. */
    static QString describe(PolkitStatus status, const QString &userName);

    /** The opt-in polkit rule for one user; empty if the user name is not safe to embed. */
    static QString polkitRuleFor(const QString &userName);
    static bool isValidUserName(const QString &userName);
    static QString ruleFilePath(const QString &userName);
    static QString actionFilePath();

signals:
    void finished(UnattendedSync::Result result, UnattendedSync::Method method, const QString &summary);

private slots:
    void onRunnerFinished(int exitCode, OperationErrorKind kind, const QString &summary);

private:
    CruxProcessRunner m_runner;
    Method m_method = Method::None;
};

} // namespace OctoCRUX
