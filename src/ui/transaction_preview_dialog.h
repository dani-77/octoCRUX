#pragma once

#include "octocrux/types.h"
#include <QDialog>
#include <QList>
#include <QStringList>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace OctoCRUX {

/**
 * @brief Confirmation dialog shown before any privileged transaction.
 *
 * Shows what OctoCRUX is about to do, before the privilege helper is invoked:
 * the exact command lines in execution order (as returned by
 * ICruxBackend::transactionCommands), the ports to install, update, reinstall
 * and remove, the locked ports skipped, the installed dependents affected by
 * removals, and warnings (pre/post-install scripts, work directory on tmpfs and
 * its free space). Cancel is the default button.
 */
class TransactionPreviewDialog : public QDialog {
    Q_OBJECT
public:
    TransactionPreviewDialog(const QString &title,
                             const TransactionPlan &plan,
                             const QList<QStringList> &commands,
                             PrivilegeHelperType helper,
                             QWidget *parent = nullptr);

    /** The plain-text summary shown in the dialog. */
    QString summaryText() const;
    QPushButton* applyButton() const;
    QPushButton* cancelButton() const;

    /** Builds the summary text; pure, so it can be tested without a window. */
    static QString describe(const TransactionPlan &plan,
                            const QList<QStringList> &commands,
                            PrivilegeHelperType helper);

    /** Build steps whose signature check is disabled for this run (shown in red). */
    static QStringList signatureCheckDisabled(const TransactionPlan &plan);

    /** The red warning banner, or nullptr when no signature check is disabled. */
    QLabel* signatureWarning() const;

    /** Renders an argument vector for display, single-quoting where a shell would need it. */
    static QString formatCommand(const QStringList &argv);

private:
    QLabel *m_signatureWarning = nullptr;
    QPlainTextEdit *m_summary = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

} // namespace OctoCRUX
