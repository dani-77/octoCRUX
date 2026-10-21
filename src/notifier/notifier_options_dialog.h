#pragma once

#include "core/octocrux_settings.h"
#include "backend/privilege_helper.h"

#include <QDialog>
#include <memory>

class QLabel;
class QRadioButton;
class QSpinBox;

namespace OctoCRUX {

/**
 * @brief Notifier options: when to sync the ports tree automatically, and the
 * opt-in polkit rule that lets this user's scheduled sync run without a password.
 *
 * Turning automatic sync on offers to install the rule (one authentication via
 * the privilege helper); turning it off offers to remove it. Without the rule,
 * scheduled syncs still work where sudoers allows "sudo -n /usr/bin/ports -u";
 * otherwise the notifier only reminds.
 */
class NotifierOptionsDialog : public QDialog {
    Q_OBJECT
public:
    NotifierOptionsDialog(std::shared_ptr<OctoCruxSettings> settings,
                          PrivilegeHelperType helper,
                          QWidget *parent = nullptr);

    /** Where the polkit action definition to install comes from (source tree or prefix). */
    static QString policySourcePath();

protected:
    void accept() override;

private slots:
    void updateEnabledState();
    void installRule();
    void removeRule();

private:
    SyncSchedule scheduleFromUi() const;
    void refreshAuthorizationLabel();
    bool runElevated(const QString &program, const QStringList &args, const QString &what);
    static QString currentUserName();

    std::shared_ptr<OctoCruxSettings> m_settings;
    PrivilegeHelper m_helper;
    SyncMode m_initialMode = SyncMode::Never;

    QRadioButton *m_rbNever = nullptr;
    QRadioButton *m_rbOnceADay = nullptr;
    QRadioButton *m_rbOnceADayAt = nullptr;
    QRadioButton *m_rbEvery = nullptr;
    QSpinBox *m_spinHour = nullptr;
    QSpinBox *m_spinMinutes = nullptr;
    QLabel *m_lblAuthorization = nullptr;
};

} // namespace OctoCRUX
