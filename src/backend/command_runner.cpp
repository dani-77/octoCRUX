#include "command_runner.h"
#include <QProcess>

namespace OctoCRUX {

CommandResult DefaultCommandRunner::run(const QString &program, const QStringList &arguments, int timeoutMs) {
    QProcess process;
    process.start(program, arguments);

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {-1, QString(), QStringLiteral("Command timed out")};
    }

    CommandResult result;
    result.exitCode = process.exitCode();
    result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    result.stdErr = QString::fromUtf8(process.readAllStandardError());
    return result;
}

} // namespace OctoCRUX
