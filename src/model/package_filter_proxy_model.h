#pragma once

#include "package_model.h"
#include <QSortFilterProxyModel>

namespace OctoCRUX {

enum class StatusFilter {
    All,
    Installed,     // Installed, Outdated, or satisfied by installed virtual alias
    NotInstalled,  // Not installed and not satisfied by virtual alias
    Outdated,      // Outdated packages (both unlocked and locked)
    Locked         // Locked packages (installed, outdated, or uninstalled)
};

/**
 * @brief Multi-axis filter and sorting proxy model for PackageModel.
 */
class PackageFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit PackageFilterProxyModel(QObject *parent = nullptr);
    ~PackageFilterProxyModel() override = default;

    void setSearchText(const QString &text);
    QString searchText() const;

    void setSearchDescription(bool enabled);
    bool searchDescription() const;

    void setCollectionFilter(const QString &collection);
    QString collectionFilter() const;

    void setStatusFilter(StatusFilter filter);
    StatusFilter statusFilter() const;

    std::optional<PortInfo> portAtProxyRow(int proxyRow) const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_searchText;
    bool m_searchDescription = true;
    QString m_collectionFilter;
    StatusFilter m_statusFilter = StatusFilter::All;
};

} // namespace OctoCRUX
