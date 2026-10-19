#include "package_filter_proxy_model.h"

namespace OctoCRUX {

PackageFilterProxyModel::PackageFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void PackageFilterProxyModel::setSearchText(const QString &text) {
    if (m_searchText != text) {
        m_searchText = text.trimmed();
        invalidate();
    }
}

QString PackageFilterProxyModel::searchText() const {
    return m_searchText;
}

void PackageFilterProxyModel::setSearchDescription(bool enabled) {
    if (m_searchDescription != enabled) {
        m_searchDescription = enabled;
        invalidate();
    }
}

bool PackageFilterProxyModel::searchDescription() const {
    return m_searchDescription;
}

void PackageFilterProxyModel::setCollectionFilter(const QString &collection) {
    const QString trimmed = collection.trimmed();
    if (m_collectionFilter != trimmed) {
        m_collectionFilter = trimmed;
        invalidate();
    }
}

QString PackageFilterProxyModel::collectionFilter() const {
    return m_collectionFilter;
}

void PackageFilterProxyModel::setStatusFilter(StatusFilter filter) {
    if (m_statusFilter != filter) {
        m_statusFilter = filter;
        invalidate();
    }
}

StatusFilter PackageFilterProxyModel::statusFilter() const {
    return m_statusFilter;
}

std::optional<PortInfo> PackageFilterProxyModel::portAtProxyRow(int proxyRow) const {
    const QModelIndex proxyIndex = index(proxyRow, 0);
    if (!proxyIndex.isValid()) {
        return std::nullopt;
    }

    const QModelIndex srcIndex = mapToSource(proxyIndex);
    auto *pkgModel = qobject_cast<PackageModel*>(sourceModel());
    if (pkgModel) {
        return pkgModel->portAt(srcIndex.row());
    }

    return std::nullopt;
}

bool PackageFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    Q_UNUSED(sourceParent);

    auto *pkgModel = qobject_cast<PackageModel*>(sourceModel());
    if (!pkgModel) {
        return true;
    }

    auto portOpt = pkgModel->portAt(sourceRow);
    if (!portOpt.has_value()) {
        return false;
    }

    const PortInfo &info = portOpt.value();

    // 1. Status Filter
    switch (m_statusFilter) {
    case StatusFilter::All:
        break;
    case StatusFilter::Installed:
        if (!info.isInstalled() && info.providedByAlias.isEmpty()) {
            return false;
        }
        break;
    case StatusFilter::NotInstalled:
        if (info.isInstalled() || !info.providedByAlias.isEmpty()) {
            return false;
        }
        break;
    case StatusFilter::Outdated:
        if (info.installStatus != InstallStatus::Outdated) {
            return false;
        }
        break;
    case StatusFilter::Locked:
        if (info.lockStatus != LockStatus::Locked) {
            return false;
        }
        break;
    }

    // 2. Collection Filter
    if (!m_collectionFilter.isEmpty() && m_collectionFilter.compare(QStringLiteral("All"), Qt::CaseInsensitive) != 0) {
        if (info.collection.compare(m_collectionFilter, Qt::CaseInsensitive) != 0) {
            return false;
        }
    }

    // 3. Search Text Filter
    if (!m_searchText.isEmpty()) {
        const bool matchName = info.name.contains(m_searchText, Qt::CaseInsensitive);
        if (matchName) {
            return true;
        }

        if (m_searchDescription && info.description.contains(m_searchText, Qt::CaseInsensitive)) {
            return true;
        }

        return false;
    }

    return true;
}

} // namespace OctoCRUX
