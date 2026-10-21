#include "settings_dialog.h"
#include "backend/privilege_helper.h"
#include "core/octocrux_settings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace OctoCRUX {

namespace {
// Combo item data: -1 for automatic detection, otherwise the PrivilegeHelperType value.
constexpr int kAutoItem = -1;
} // namespace

SettingsDialog::SettingsDialog(std::optional<PrivilegeHelperType> preferredHelper,
                               int cancelGraceSeconds, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("OctoCRUX Settings"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_comboHelper = new QComboBox(this);
    m_comboHelper->setObjectName(QStringLiteral("helperCombo"));
    const QString detected = PrivilegeHelper::helperBinary(PrivilegeHelper::probeAvailableHelper());
    m_comboHelper->addItem(QStringLiteral("Automatic (detected: %1)")
                               .arg(detected.isEmpty() ? QStringLiteral("none") : detected),
                           kAutoItem);
    for (PrivilegeHelperType t : {PrivilegeHelperType::QtSudo, PrivilegeHelperType::Sudo,
                                  PrivilegeHelperType::Doas, PrivilegeHelperType::Su}) {
        const QString name = PrivilegeHelper::helperBinary(t);
        const bool found = !QStandardPaths::findExecutable(name).isEmpty();
        m_comboHelper->addItem(found ? name : QStringLiteral("%1 (not found)").arg(name), static_cast<int>(t));
    }
    const int current = preferredHelper.has_value()
        ? m_comboHelper->findData(static_cast<int>(*preferredHelper)) : 0;
    m_comboHelper->setCurrentIndex(current < 0 ? 0 : current);
    form->addRow(QStringLiteral("Privilege helper:"), m_comboHelper);

    auto *helperNote = new QLabel(QStringLiteral(
        "Used for every operation that needs root (install, remove, upgrade, lock, sync). "
        "qt-sudo shows a graphical password dialog; sudo, doas and su need a terminal to ask "
        "for a password. Automatic sync in the notifier does not use this: it never asks."), this);
    helperNote->setWordWrap(true);
    form->addRow(helperNote);

    m_spinGrace = new QSpinBox(this);
    m_spinGrace->setObjectName(QStringLiteral("cancelGraceSpin"));
    m_spinGrace->setRange(OctoCruxSettings::kMinCancelGraceSeconds, OctoCruxSettings::kMaxCancelGraceSeconds);
    m_spinGrace->setSuffix(QStringLiteral(" s"));
    m_spinGrace->setValue(cancelGraceSeconds);
    form->addRow(QStringLiteral("Cancel grace period:"), m_spinGrace);

    auto *graceNote = new QLabel(QStringLiteral(
        "After Cancel, how long to wait before warning that the operation is still running. "
        "OctoCRUX never kills the privileged process; pressing Cancel again asks it to stop again."), this);
    graceNote->setWordWrap(true);
    form->addRow(graceNote);

    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

std::optional<PrivilegeHelperType> SettingsDialog::preferredHelper() const {
    const int value = m_comboHelper->currentData().toInt();
    if (value == kAutoItem) {
        return std::nullopt;
    }
    return static_cast<PrivilegeHelperType>(value);
}

int SettingsDialog::cancelGraceSeconds() const {
    return m_spinGrace->value();
}

} // namespace OctoCRUX
