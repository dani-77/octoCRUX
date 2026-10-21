#include "unattended_sync.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace OctoCRUX {

UnattendedSync::UnattendedSync(QObject *parent)
    : QObject(parent)
{
    connect(&m_runner, &CruxProcessRunner::processFinished, this, &UnattendedSync::onRunnerFinished);
}

UnattendedSync::PolkitStatus UnattendedSync::polkitStatus() {
    const QString pkcheck = QStandardPaths::findExecutable(QStringLiteral("pkcheck"));
    if (pkcheck.isEmpty()) {
        return PolkitStatus::Unavailable;
    }
    // Without --allow-user-interaction pkcheck never prompts: exit 0 means
    // authorized as is, anything else (challenge, denied, unknown action) does not.
    QProcess p;
    p.start(pkcheck, {QStringLiteral("--action-id"), QString::fromLatin1(kActionId),
                      QStringLiteral("--process"), QString::number(QCoreApplication::applicationPid())});
    p.closeWriteChannel();
    if (!p.waitForFinished(10000)) {
        p.kill();
        p.waitForFinished(1000);
        return PolkitStatus::Unavailable;
    }
    if (p.exitStatus() != QProcess::NormalExit) {
        return PolkitStatus::Unavailable;
    }
    switch (p.exitCode()) {
    case 0:   return PolkitStatus::Authorized;
    case 1:   return PolkitStatus::InactiveSession;
    case 2:   return PolkitStatus::NeedsAuthentication;
    case 127: return PolkitStatus::ActionNotRegistered;
    default:  return PolkitStatus::Unavailable;
    }
}

QString UnattendedSync::describe(PolkitStatus status, const QString &userName) {
    switch (status) {
    case PolkitStatus::Authorized:
        return QStringLiteral("Unattended sync is authorized through polkit for user \"%1\" "
                              "(exactly \"/usr/bin/ports -u\").").arg(userName);
    case PolkitStatus::NeedsAuthentication:
        return QStringLiteral("polkit would ask for a password: the rule for user \"%1\" is not "
                              "installed. Use \"Allow unattended sync\".").arg(userName);
    case PolkitStatus::InactiveSession:
        return QStringLiteral("polkit does not see this desktop session as local and active "
                              "(no ConsoleKit/elogind session is registered), so a polkit rule "
                              "cannot apply. To fix it, start the X session with ck-launch-session "
                              "(see README, \"Notifier and automatic sync\").");
    case PolkitStatus::ActionNotRegistered:
        return QStringLiteral("The polkit action org.octocrux.ports-sync is not installed yet.");
    case PolkitStatus::Unavailable:
        break;
    }
    return QStringLiteral("polkit is not available (no pkcheck, or it did not answer).");
}

void UnattendedSync::start(const QString &portsPath) {
    QStringList argv;
    if (polkitAuthorized()) {
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (!pkexec.isEmpty()) {
            m_method = Method::Polkit;
            argv = {pkexec, portsPath, QStringLiteral("-u")};
        }
    }
    if (argv.isEmpty()) {
        const QString sudo = QStandardPaths::findExecutable(QStringLiteral("sudo"));
        if (!sudo.isEmpty()) {
            m_method = Method::Sudo;
            argv = {sudo, QStringLiteral("-n"), portsPath, QStringLiteral("-u")};
        }
    }
    if (argv.isEmpty() || !m_runner.startProcess(argv.first(), argv.mid(1))) {
        m_method = Method::None;
        QTimer::singleShot(0, this, [this]() {
            emit finished(Result::NeedsAuthentication, Method::None,
                          QStringLiteral("No unattended way to run ports -u (no polkit rule, no sudo)"));
        });
    }
}

void UnattendedSync::onRunnerFinished(int exitCode, OperationErrorKind kind, const QString &summary) {
    Result result = Result::Failed;
    if (exitCode == 0 && kind == OperationErrorKind::None) {
        result = Result::Synced;
    } else if (kind == OperationErrorKind::AuthenticationError) {
        // sudo -n: "sudo: a password is required" (BuildLogAnalyzer)
        result = Result::NeedsAuthentication;
    } else if (m_method == Method::Polkit && (exitCode == 126 || exitCode == 127)) {
        // pkexec: 126 = authorization could not be obtained / dismissed,
        // 127 = not authorized (pkexec(1)). The pre-check makes this unlikely.
        result = Result::NeedsAuthentication;
    }
    emit finished(result, m_method, summary);
}

bool UnattendedSync::isValidUserName(const QString &userName) {
    // POSIX-portable user names; also keeps the name safe inside the JS rule.
    static const QRegularExpression valid(QStringLiteral("^[a-z_][a-z0-9_-]{0,31}$"));
    return valid.match(userName).hasMatch();
}

QString UnattendedSync::polkitRuleFor(const QString &userName) {
    if (!isValidUserName(userName)) {
        return QString();
    }
    return QStringLiteral(
        "// Installed by OctoCRUX when user \"%1\" turned on automatic ports tree sync.\n"
        "// Lets that user, in a local active session, run exactly \"/usr/bin/ports -u\"\n"
        "// (action org.octocrux.ports-sync) as root without a password.\n"
        "// Remove this file, or choose \"Never\" in the notifier options, to undo it.\n"
        "polkit.addRule(function(action, subject) {\n"
        "    if (action.id == \"%2\" &&\n"
        "        subject.user == \"%1\" && subject.local && subject.active) {\n"
        "        return polkit.Result.YES;\n"
        "    }\n"
        "});\n").arg(userName, QString::fromLatin1(kActionId));
}

QString UnattendedSync::ruleFilePath(const QString &userName) {
    return QStringLiteral("/etc/polkit-1/rules.d/49-octocrux-ports-sync-%1.rules").arg(userName);
}

QString UnattendedSync::actionFilePath() {
    return QStringLiteral("/usr/share/polkit-1/actions/org.octocrux.ports-sync.policy");
}

} // namespace OctoCRUX
