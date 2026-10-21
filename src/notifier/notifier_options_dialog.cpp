#include "notifier_options_dialog.h"
#include "unattended_sync.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTemporaryFile>
#include <QVBoxLayout>

#include <pwd.h>
#include <unistd.h>

namespace OctoCRUX {

NotifierOptionsDialog::NotifierOptionsDialog(std::shared_ptr<OctoCruxSettings> settings,
                                             PrivilegeHelperType helper, QWidget *parent)
    : QDialog(parent)
    , m_settings(std::move(settings))
{
    m_helper.setHelperType(helper);
    setWindowTitle(QStringLiteral("OctoCRUX Notifier Options"));
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(QStringLiteral(
        "Automatic sync runs \"ports -u\" as root, which rewrites the ports tree under "
        "/usr/ports. It never installs or upgrades packages. Without automatic sync the "
        "notifier only reminds you how long ago the tree was synchronized."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *grid = new QGridLayout();
    m_rbNever = new QRadioButton(QStringLiteral("Never (remind only)"), this);
    m_rbOnceADay = new QRadioButton(QStringLiteral("Once a day"), this);
    m_rbOnceADayAt = new QRadioButton(QStringLiteral("Once a day, at or after"), this);
    m_rbEvery = new QRadioButton(QStringLiteral("Every"), this);
    m_spinHour = new QSpinBox(this);
    m_spinHour->setRange(0, 23);
    m_spinHour->setSuffix(QStringLiteral(" h"));
    m_spinMinutes = new QSpinBox(this);
    m_spinMinutes->setRange(SyncSchedule::kMinIntervalMinutes, SyncSchedule::kMaxIntervalMinutes);
    m_spinMinutes->setSingleStep(60);
    m_spinMinutes->setSuffix(QStringLiteral(" minutes"));
    grid->addWidget(m_rbNever, 0, 0, 1, 2);
    grid->addWidget(m_rbOnceADay, 1, 0, 1, 2);
    grid->addWidget(m_rbOnceADayAt, 2, 0);
    grid->addWidget(m_spinHour, 2, 1);
    grid->addWidget(m_rbEvery, 3, 0);
    grid->addWidget(m_spinMinutes, 3, 1);
    layout->addLayout(grid);

    m_lblAuthorization = new QLabel(this);
    m_lblAuthorization->setWordWrap(true);
    layout->addWidget(m_lblAuthorization);

    auto *ruleRow = new QHBoxLayout();
    auto *btnInstall = new QPushButton(QStringLiteral("Allow unattended sync (install polkit rule)..."), this);
    auto *btnRemove = new QPushButton(QStringLiteral("Remove polkit rule..."), this);
    btnInstall->setAutoDefault(false);
    btnRemove->setAutoDefault(false);
    ruleRow->addWidget(btnInstall);
    ruleRow->addWidget(btnRemove);
    layout->addLayout(ruleRow);
    connect(btnInstall, &QPushButton::clicked, this, &NotifierOptionsDialog::installRule);
    connect(btnRemove, &QPushButton::clicked, this, &NotifierOptionsDialog::removeRule);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    const SyncSchedule s = m_settings->syncSchedule();
    m_initialMode = s.mode;
    m_spinHour->setValue(s.hour);
    m_spinMinutes->setValue(s.intervalMinutes);
    switch (s.mode) {
    case SyncMode::Never:         m_rbNever->setChecked(true); break;
    case SyncMode::OnceADay:      m_rbOnceADay->setChecked(true); break;
    case SyncMode::OnceADayAt:    m_rbOnceADayAt->setChecked(true); break;
    case SyncMode::EveryInterval: m_rbEvery->setChecked(true); break;
    }
    for (QRadioButton *rb : {m_rbNever, m_rbOnceADay, m_rbOnceADayAt, m_rbEvery}) {
        connect(rb, &QRadioButton::toggled, this, &NotifierOptionsDialog::updateEnabledState);
    }
    updateEnabledState();
    refreshAuthorizationLabel();
}

QString NotifierOptionsDialog::policySourcePath() {
    // Installed layout first (<prefix>/bin/../share/octocrux/polkit), then the source tree.
    const QString installed = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../share/octocrux/polkit/org.octocrux.ports-sync.policy"));
    if (QFileInfo::exists(installed)) {
        return QDir::cleanPath(installed);
    }
#ifdef OCTOCRUX_SOURCE_POLICY
    if (QFileInfo::exists(QStringLiteral(OCTOCRUX_SOURCE_POLICY))) {
        return QStringLiteral(OCTOCRUX_SOURCE_POLICY);
    }
#endif
    return QString();
}

QString NotifierOptionsDialog::currentUserName() {
    const struct passwd *pw = getpwuid(getuid());
    return pw ? QString::fromLocal8Bit(pw->pw_name) : QString();
}

SyncSchedule NotifierOptionsDialog::scheduleFromUi() const {
    SyncSchedule s;
    s.mode = m_rbOnceADay->isChecked()   ? SyncMode::OnceADay
           : m_rbOnceADayAt->isChecked() ? SyncMode::OnceADayAt
           : m_rbEvery->isChecked()      ? SyncMode::EveryInterval
                                         : SyncMode::Never;
    s.hour = m_spinHour->value();
    s.intervalMinutes = m_spinMinutes->value();
    return s.normalized();
}

void NotifierOptionsDialog::updateEnabledState() {
    m_spinHour->setEnabled(m_rbOnceADayAt->isChecked());
    m_spinMinutes->setEnabled(m_rbEvery->isChecked());
}

void NotifierOptionsDialog::refreshAuthorizationLabel() {
    if (UnattendedSync::polkitAuthorized()) {
        m_lblAuthorization->setText(QStringLiteral(
            "Unattended sync is authorized through polkit for user \"%1\" (exactly \"/usr/bin/ports -u\").")
            .arg(currentUserName()));
    } else {
        m_lblAuthorization->setText(QStringLiteral(
            "Unattended sync is not authorized through polkit. Scheduled syncs will use "
            "\"sudo -n /usr/bin/ports -u\" if your sudoers allows it without a password; "
            "otherwise the notifier only reminds you. It never shows a password dialog on its own."));
    }
}

bool NotifierOptionsDialog::runElevated(const QString &program, const QStringList &args, const QString &what) {
    const QStringList argv = m_helper.wrap(program, args);
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(argv.first(), argv.mid(1));
    p.closeWriteChannel();
    // The helper may show its own password dialog; allow time for the user.
    const bool finished = p.waitForFinished(5 * 60 * 1000);
    const bool ok = finished && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("OctoCRUX"),
            QStringLiteral("Could not %1.\n\n%2").arg(what, QString::fromLocal8Bit(p.readAll()).trimmed()));
    }
    return ok;
}

void NotifierOptionsDialog::installRule() {
    const QString user = currentUserName();
    const QString rule = UnattendedSync::polkitRuleFor(user);
    if (rule.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("OctoCRUX"),
            QStringLiteral("The user name \"%1\" cannot be used in a polkit rule.").arg(user));
        return;
    }
    const auto answer = QMessageBox::question(this, QStringLiteral("Allow unattended sync"),
        QStringLiteral("This installs a polkit rule that lets user \"%1\", in a local active session, "
                       "run exactly \"/usr/bin/ports -u\" as root without a password:\n\n  %2\n\n"
                       "plus the action definition, if missing:\n\n  %3\n\n"
                       "The privilege helper may ask for your password.")
            .arg(user, UnattendedSync::ruleFilePath(user), UnattendedSync::actionFilePath()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }

    if (!QFileInfo::exists(UnattendedSync::actionFilePath())) {
        const QString source = policySourcePath();
        if (source.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("OctoCRUX"),
                QStringLiteral("The polkit action definition (org.octocrux.ports-sync.policy) was not found."));
            return;
        }
        if (!runElevated(QStringLiteral("install"),
                         {QStringLiteral("-D"), QStringLiteral("-m"), QStringLiteral("0644"),
                          source, UnattendedSync::actionFilePath()},
                         QStringLiteral("install the polkit action"))) {
            return;
        }
    }

    // Root installs a copy of a file this process wrote and closed, not a path it reopens later.
    QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("octocrux-rule-XXXXXX.rules")));
    if (!tmp.open() || tmp.write(rule.toUtf8()) < 0 || !tmp.flush()) {
        QMessageBox::warning(this, QStringLiteral("OctoCRUX"), QStringLiteral("Could not write a temporary file."));
        return;
    }
    tmp.close();
    if (runElevated(QStringLiteral("install"),
                    {QStringLiteral("-D"), QStringLiteral("-m"), QStringLiteral("0644"),
                     tmp.fileName(), UnattendedSync::ruleFilePath(user)},
                    QStringLiteral("install the polkit rule"))) {
        QMessageBox::information(this, QStringLiteral("OctoCRUX"),
            UnattendedSync::polkitAuthorized()
                ? QStringLiteral("Rule installed: unattended sync is now authorized.")
                : QStringLiteral("Rule installed, but polkit does not report the sync as authorized yet "
                                 "(is polkitd running, and is this a local active session?)."));
    }
    refreshAuthorizationLabel();
}

void NotifierOptionsDialog::removeRule() {
    const QString user = currentUserName();
    if (!UnattendedSync::isValidUserName(user)) {
        return;
    }
    const auto answer = QMessageBox::question(this, QStringLiteral("Remove polkit rule"),
        QStringLiteral("Remove %1? Scheduled syncs will then need another password-free way "
                       "(sudo -n) or will only remind you.").arg(UnattendedSync::ruleFilePath(user)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Yes) {
        runElevated(QStringLiteral("rm"), {QStringLiteral("-f"), UnattendedSync::ruleFilePath(user)},
                    QStringLiteral("remove the polkit rule"));
    }
    refreshAuthorizationLabel();
}

void NotifierOptionsDialog::accept() {
    const SyncSchedule s = scheduleFromUi();
    m_settings->setSyncSchedule(s);
    m_settings->sync();

    // Offer the rule exactly when the user turns automatic sync on or off.
    if (m_initialMode == SyncMode::Never && s.mode != SyncMode::Never && !UnattendedSync::polkitAuthorized()) {
        installRule();
    } else if (m_initialMode != SyncMode::Never && s.mode == SyncMode::Never && UnattendedSync::polkitAuthorized()) {
        removeRule();
    }
    QDialog::accept();
}

} // namespace OctoCRUX
