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
                                                   QWidget *parent,
                                                   const BinaryCacheContext &binaries)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("transactionPreviewDialog"));
    setWindowTitle(title);
    setModal(true);
    resize(720, 480);

    auto *layout = new QVBoxLayout(this);

    const QStringList unsigned_ = signatureCheckDisabled(plan);
    if (!unsigned_.isEmpty()) {
        m_signatureWarning = new QLabel(
            QStringLiteral("Signature verification disabled for: %1\n"
                           "The port files and sources of these packages cannot be authenticated. "
                           "They will be built and installed as root.")
                .arg(unsigned_.join(QStringLiteral(", "))), this);
        m_signatureWarning->setObjectName(QStringLiteral("signatureWarning"));
        m_signatureWarning->setWordWrap(true);
        m_signatureWarning->setStyleSheet(QStringLiteral(
            "background-color: #c62828; color: white; font-weight: bold; padding: 8px; border-radius: 3px;"));
        layout->addWidget(m_signatureWarning);
    }

    auto *intro = new QLabel(
        QStringLiteral("Review the commands below. Nothing has run yet: the privilege helper is "
                       "only invoked after you press Apply."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_summary = new QPlainTextEdit(this);
    m_summary->setReadOnly(true);
    m_summary->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_summary->setPlainText(describe(plan, commands, helper, binaries));
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(this);
    const QStringList pending = pendingBinaryPorts(plan, binaries);
    if (!pending.isEmpty()) {
        // Postponing only shortens the run; it never starts anything by itself.
        m_postponeButton = buttons->addButton(
            QStringLiteral("Leave out %1 (binary on its way)").arg(pending.join(QStringLiteral(", "))),
            QDialogButtonBox::ActionRole);
        m_postponeButton->setObjectName(QStringLiteral("postponeButton"));
        m_postponeButton->setAutoDefault(false);
        connect(m_postponeButton, &QPushButton::clicked, this, [this]() { done(kPostponeResult); });
    }
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

QPushButton* TransactionPreviewDialog::postponeButton() const {
    return m_postponeButton;
}

QStringList TransactionPreviewDialog::pendingBinaryPorts(const TransactionPlan &plan, const BinaryCacheContext &binaries) {
    QStringList pending;
    if (!binaries.available) {
        return pending;
    }
    for (const TransactionStep &step : plan.steps) {
        if (step.action == TransactionAction::Upgrade && !pending.contains(step.portName)
            && binaries.report.entry(step.portName).status == BinaryStatus::Pending) {
            pending << step.portName;
        }
    }
    return pending;
}

QPushButton* TransactionPreviewDialog::cancelButton() const {
    return m_cancelButton;
}

QLabel* TransactionPreviewDialog::signatureWarning() const {
    return m_signatureWarning;
}

QStringList TransactionPreviewDialog::signatureCheckDisabled(const TransactionPlan &plan) {
    QStringList names;
    for (const TransactionStep &step : plan.steps) {
        if (step.overrides.ignoreSignature && !names.contains(step.portName)) {
            names << step.portName;
        }
    }
    return names;
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

QStringList TransactionPreviewDialog::describeBinaries(const TransactionPlan &plan,
                                                       const BinaryCacheContext &binaries,
                                                       QStringList &warnings) {
    QStringList out;
    if (!binaries.available) {
        return out;
    }
    QStringList ready, notPlaced, pending, locked, builds;
    QStringList largePending, largeBuilds;
    QStringList seen;
    for (const TransactionStep &step : plan.steps) {
        const bool buildStep = step.action == TransactionAction::Install || step.action == TransactionAction::Upgrade
                            || step.action == TransactionAction::Reinstall;
        // The locked port updated from its binary has its own warning.
        if (!buildStep || seen.contains(step.portName) || step.portName == plan.lockedBinaryPort) {
            continue;
        }
        seen << step.portName;
        const BinaryCacheReport::Entry e = binaries.report.entry(step.portName);
        const bool large = binaries.largePorts.contains(step.portName);
        const QString name = large ? step.portName + QStringLiteral(" [large]") : step.portName;
        switch (e.status) {
        case BinaryStatus::Ready:
            ready << name;
            break;
        case BinaryStatus::NotPlaced:
            notPlaced << name;
            break;
        case BinaryStatus::Pending: {
            const auto versions = BinaryCacheReport::treeAndRelease(e.detail);
            pending << (versions.first.isEmpty() ? QStringLiteral("%1 (%2)").arg(name, e.detail)
                                                 : QStringLiteral("%1 (port %2, binary %3)").arg(name, versions.first, versions.second));
            if (large) largePending << step.portName;
            break;
        }
        case BinaryStatus::LockedAvailable:
            locked << name;
            break;
        case BinaryStatus::Builds:
            builds << QStringLiteral("%1 (%2)").arg(name, e.detail);
            if (large) largeBuilds << step.portName;
            break;
        case BinaryStatus::Unknown:
            break;
        }
    }
    // Locked ports Upgrade All skipped although a binary is available.
    QStringList lockedSkippedWithBinary;
    for (const QString &port : plan.lockedSkipped) {
        if (binaries.report.entry(port).status == BinaryStatus::LockedAvailable) {
            lockedSkippedWithBinary << (binaries.largePorts.contains(port) ? port + QStringLiteral(" [large]") : port);
        }
    }
    if (ready.isEmpty() && notPlaced.isEmpty() && pending.isEmpty() && locked.isEmpty() && builds.isEmpty()
        && lockedSkippedWithBinary.isEmpty()) {
        return out;
    }

    QString snapshot;
    if (binaries.snapshotPublished.isValid() && binaries.now.isValid()) {
        const qint64 days = binaries.snapshotPublished.toLocalTime().date().daysTo(binaries.now.date());
        snapshot = days <= 0 ? QStringLiteral("published today")
                 : days == 1 ? QStringLiteral("published 1 day ago")
                             : QStringLiteral("published %1 days ago").arg(days);
        snapshot += days > 10 ? QStringLiteral("; new ones usually come every 7-10 days, so one may be due")
                              : QStringLiteral("; new ones usually come every 7-10 days");
    }
    const QString checked = binaries.report.checkedAt.isValid()
        ? QStringLiteral("report from %1").arg(binaries.report.checkedAt.toString(QStringLiteral("HH:mm")))
        : QString();
    QStringList about;
    if (!checked.isEmpty()) about << checked;
    if (!snapshot.isEmpty()) about << QStringLiteral("binary snapshot ") + snapshot;
    out << (about.isEmpty() ? QStringLiteral("Binary packages (d77crux-cache):")
                            : QStringLiteral("Binary packages (d77crux-cache; %1):").arg(about.join(QStringLiteral("; "))));
    const auto add = [&out](const QString &heading, const QStringList &items) {
        if (!items.isEmpty()) {
            out << QStringLiteral("  %1 (%2): %3").arg(heading).arg(items.size()).arg(items.join(QStringLiteral(", ")));
        }
    };
    add(QStringLiteral("Binary ready, installs without compiling"), ready);
    add(QStringLiteral("Binary available, not placed yet (run d77crux-cache as root before Apply, or it compiles)"), notPlaced);
    add(QStringLiteral("Binary on its way (the port is newer than the snapshot), compiles now"), pending);
    add(QStringLiteral("Binary available, but locked"), locked);
    add(QStringLiteral("Compiles from source"), builds);
    add(QStringLiteral("Locked and skipped, but a binary is available (right-click > Update from its binary)"),
        lockedSkippedWithBinary);
    out << QString();

    if (!largePending.isEmpty()) {
        warnings << QStringLiteral("%1 take(s) long to compile, and a binary should come with the next snapshot: "
                                   "consider leaving them out of this run.").arg(largePending.join(QStringLiteral(", ")));
    }
    if (!largeBuilds.isEmpty()) {
        warnings << QStringLiteral("%1 take(s) long to compile and no binary is expected.")
                        .arg(largeBuilds.join(QStringLiteral(", ")));
    }
    return out;
}

QString TransactionPreviewDialog::describe(const TransactionPlan &plan,
                                           const QList<QStringList> &commands,
                                           PrivilegeHelperType helper,
                                           const BinaryCacheContext &binaries) {
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
    appendList(out, QStringLiteral("Left out of this run, binary on its way"), plan.postponedForBinary);
    appendList(out, QStringLiteral("Installed dependents directly affected by removal"),
               plan.reverseDependentsDirect);
    QStringList indirect;
    for (const QString &dep : plan.reverseDependentsCascade) {
        if (!plan.reverseDependentsDirect.contains(dep)) {
            indirect << dep;
        }
    }
    appendList(out, QStringLiteral("Installed packages indirectly affected by removal"), indirect);

    QStringList exceptions;
    for (const TransactionStep &step : plan.steps) {
        if (!step.overrides.isDefault()) {
            const QString line = QStringLiteral("  %1: %2").arg(step.portName, step.overrides.describe());
            if (!exceptions.contains(line)) {
                exceptions << line;
            }
        }
    }
    if (!exceptions.isEmpty()) {
        out << QStringLiteral("Build check exceptions for this run (%1):").arg(exceptions.size());
        out << exceptions;
        out << QString();
    }

    QStringList warnings = plan.criticalWarnings;
    if (!plan.lockedBinaryPort.isEmpty()) {
        warnings << QStringLiteral("%1 is unlocked only for this run, and locked again at the end, also if a step "
                                   "fails or you cancel. If the binary is not in place after d77crux-cache, the run "
                                   "stops before the update: nothing is compiled.").arg(plan.lockedBinaryPort);
    }
    out << describeBinaries(plan, binaries, warnings);
    const QStringList unsignedPorts = signatureCheckDisabled(plan);
    if (!unsignedPorts.isEmpty()) {
        warnings << QStringLiteral("SIGNATURE VERIFICATION DISABLED for %1: the sources cannot be authenticated.")
                        .arg(unsignedPorts.join(QStringLiteral(", ")));
    }
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
