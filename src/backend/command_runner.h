#pragma once

#include <QString>
#include <QStringList>
#include <memory>

namespace OctoCRUX {

struct CommandResult {
    int exitCode = -1;
    QString stdOut;
    QString stdErr;

    bool succeeded() const { return exitCode == 0; }
};

/**
 * @brief Abstract interface for executing commands.
 * Allows switching between real QProcess in production and mock runners in unit tests.
 */
class ICommandRunner {
public:
    virtual ~ICommandRunner() = default;
    virtual CommandResult run(const QString &program, const QStringList &arguments, int timeoutMs = 30000) = 0;
};

/**
 * @brief Default command runner using QProcess with explicit argument vectors.
 */
class DefaultCommandRunner : public ICommandRunner {
public:
    CommandResult run(const QString &program, const QStringList &arguments, int timeoutMs = 30000) override;
};

} // namespace OctoCRUX
