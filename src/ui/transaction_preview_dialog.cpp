#include "transaction_preview_dialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace OctoCRUX {

namespace {

QString helperName(PrivilegeHelperType helper) {
    switch (helper) {
    case PrivilegeHelperType::QtSudo: return QStringLiteral("qt-sudo");
    case PrivilegeHelperType::Sudo:   return QStringLiteral("sudo");
    case PrivilegeHelperType::Doas:   return QStringLiteral("doas");
    case PrivilegeHelperType::Su:     return QStringLiteral("su");
    case PrivilegeHelperType::None:   break;
    }
    return QString();
}

QString formatBytes(quint64 bytes) {
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    return QStringLiteral("%1 GiB").arg(gib, 0, 'f', 1);
}

void appendList(QStringList &out, const QString &heading, const QStringList &items) {
    if (items.isEmpty()) {
        return;
    }
    out << QStringLiteral("%1 (%2):").arg(heading).arg(items.size());
    out << QStringLiteral("  ") + items.join(QStringLiteral(" "));
    out << QString();
}

QStringList portsWithAction(const TransactionPlan &plan, TransactionAction action) {
    QStringList names;
    for (const TransactionStep &step : plan.steps) {
        if (step.action == action && !names.contains(step.portName)) {
            names.append(step.portName);
        }
    }
    return names;
}

} // namespace

TransactionPreviewDialog::TransactionPreviewDialog(const QString &title,
                                                   const TransactionPlan &plan,
                                                   const QList<QStringList> &commands,
                                                   PrivilegeHelperType helper,
                                                   QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("transactionPreviewDialog"));
    setWindowTitle(title);
    setModal(true);
    resize(720, 480);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        QStringLiteral("Review the commands below. Nothing has run yet: the privilege helper is "
                       "only invoked after you press Apply."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_summary = new QPlainTextEdit(this);
    m_summary->setReadOnly(true);
    m_summary->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_summary->setPlainText(describe(plan, commands, helper));
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(this);
    m_applyButton = buttons->addButton(QStringLiteral("Apply"), QDialogButtonBox::AcceptRole);
    m_cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    // Cancel is the default: pressing Enter must never start a privileged operation.
    m_applyButton->setAutoDefault(false);
    m_applyButton->setDefault(false);
    m_cancelButton->setDefault(true);
    m_cancelButton->setFocus();
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString TransactionPreviewDialog::summaryText() const {
    return m_summary->toPlainText();
}

QPushButton* TransactionPreviewDialog::applyButton() const {
    return m_applyButton;
}

QPushButton* TransactionPreviewDialog::cancelButton() const {
    return m_cancelButton;
}

QString TransactionPreviewDialog::formatCommand(const QStringList &argv) {
    static const QRegularExpression needsQuoting(QStringLiteral("[^A-Za-z0-9_@%+=:,./-]"));
    QStringList parts;
    for (const QString &arg : argv) {
        if (!arg.isEmpty() && !arg.contains(needsQuoting)) {
            parts << arg;
        } else {
            QString quoted = arg;
            quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            parts << QLatin1Char('\'') + quoted + QLatin1Char('\'');
        }
    }
    return parts.join(QLatin1Char(' '));
}

QString TransactionPreviewDialog::describe(const TransactionPlan &plan,
                                           const QList<QStringList> &commands,
                                           PrivilegeHelperType helper) {
    QStringList out;

    out << QStringLiteral("Commands, in execution order:");
    for (int i = 0; i < commands.size(); ++i) {
        out << QStringLiteral("  %1. %2").arg(i + 1).arg(formatCommand(commands.at(i)));
    }
    const QString name = helperName(helper);
    if (name.isEmpty()) {
        out << QStringLiteral("No privilege helper: the commands run as the current user.");
    } else {
        out << QStringLiteral("%1 elevated invocation(s) through %2. Each one may ask for "
                              "authentication; whether credentials are reused between them "
                              "has not been verified.").arg(commands.size()).arg(name);
    }
    out << QString();

    appendList(out, QStringLiteral("To install"), portsWithAction(plan, TransactionAction::Install));
    appendList(out, QStringLiteral("To update"), portsWithAction(plan, TransactionAction::Upgrade));
    appendList(out, QStringLiteral("To reinstall"), portsWithAction(plan, TransactionAction::Reinstall));
    appendList(out, QStringLiteral("To remove"), portsWithAction(plan, TransactionAction::Remove));
    appendList(out, QStringLiteral("To lock"), portsWithAction(plan, TransactionAction::Lock));
    appendList(out, QStringLiteral("To unlock"), portsWithAction(plan, TransactionAction::Unlock));
    appendList(out, QStringLiteral("Skipped because locked"), plan.lockedSkipped);
    appendList(out, QStringLiteral("Installed dependents directly affected by removal"),
               plan.reverseDependentsDirect);
    QStringList indirect;
    for (const QString &dep : plan.reverseDependentsCascade) {
        if (!plan.reverseDependentsDirect.contains(dep)) {
            indirect << dep;
        }
    }
    appendList(out, QStringLiteral("Installed packages indirectly affected by removal"), indirect);

    QStringList warnings = plan.criticalWarnings;
    QStringList scripted;
    bool builds = false;
    for (const TransactionStep &step : plan.steps) {
        if (step.action == TransactionAction::Install || step.action == TransactionAction::Upgrade
            || step.action == TransactionAction::Reinstall) {
            builds = true;
            if ((step.hasPreInstall || step.hasPostInstall) && !scripted.contains(step.portName)) {
                QStringList which;
                if (step.hasPreInstall) which << QStringLiteral("pre-install");
                if (step.hasPostInstall) which << QStringLiteral("post-install");
                scripted << QStringLiteral("%1 (%2)").arg(step.portName, which.join(QStringLiteral(", ")));
            }
        }
    }
    if (!scripted.isEmpty()) {
        warnings << QStringLiteral("Ports with install scripts, run as root: %1")
                        .arg(scripted.join(QStringLiteral(", ")));
    }
    if (builds) {
        const FilesystemInfo &mount = plan.workMountInfo;
        if (mount.mountPoint.isEmpty()) {
            warnings << QStringLiteral("Work directory: mount could not be inspected; free space unknown.");
        } else if (mount.isTmpfs) {
            warnings << QStringLiteral("Work directory is on tmpfs (%1): %2 free of %3. Builds use RAM; "
                                       "a large port can fail with 'No space left on device'.")
                            .arg(mount.mountPoint, formatBytes(mount.availableBytes), formatBytes(mount.totalBytes));
        } else {
            warnings << QStringLiteral("Work directory on %1 (%2): %3 free.")
                            .arg(mount.mountPoint, mount.fsType, formatBytes(mount.availableBytes));
        }
    }
    if (!warnings.isEmpty()) {
        out << QStringLiteral("Warnings:");
        for (const QString &w : warnings) {
            out << QStringLiteral("  - ") + w;
        }
    }

    while (!out.isEmpty() && out.last().isEmpty()) {
        out.removeLast();
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace OctoCRUX
