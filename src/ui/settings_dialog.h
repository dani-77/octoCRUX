#pragma once

#include "octocrux/types.h"
#include <QDialog>
#include <optional>

class QComboBox;
class QSpinBox;

namespace OctoCRUX {

/**
 * @brief General OctoCRUX settings: the preferred privilege helper and the
 * cancel grace period.
 *
 * The dialog only edits values; the caller saves them in OctoCruxSettings and
 * applies them. Helpers not found in PATH stay selectable (a saved preference
 * may refer to a helper installed later) but are marked "not found"; OctoCRUX
 * then falls back to automatic detection.
 */
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(std::optional<PrivilegeHelperType> preferredHelper,
                   int cancelGraceSeconds,
                   QWidget *parent = nullptr);

    /** nullopt = automatic detection. */
    std::optional<PrivilegeHelperType> preferredHelper() const;
    int cancelGraceSeconds() const;

private:
    QComboBox *m_comboHelper = nullptr;
    QSpinBox *m_spinGrace = nullptr;
};

} // namespace OctoCRUX
