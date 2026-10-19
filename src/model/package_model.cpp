#include "package_model.h"
#include <algorithm>

namespace OctoCRUX {

PackageModel::PackageModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void PackageModel::setPorts(const QList<PortInfo> &ports) {
    beginResetModel();
    m_ports = ports;
    rebuildIndex();
    endResetModel();
}

void PackageModel::clear() {
    beginResetModel();
    m_ports.clear();
    m_nameToRow.clear();
    endResetModel();
}

const QList<PortInfo>& PackageModel::ports() const {
    return m_ports;
}

std::optional<PortInfo> PackageModel::portAt(int row) const {
    if (row < 0 || row >= m_ports.size()) {
        return std::nullopt;
    }
    return m_ports.at(row);
}

std::optional<PortInfo> PackageModel::portByName(const QString &name) const {
    auto it = m_nameToRow.constFind(name);
    if (it != m_nameToRow.constEnd()) {
        return m_ports.at(it.value());
    }
    return std::nullopt;
}

int PackageModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_ports.size();
}

int PackageModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return PackageModelColumn::ColumnCount;
}

QVariant PackageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ports.size()) {
        return QVariant();
    }

    const PortInfo &info = m_ports.at(index.row());

    // Custom roles
    switch (role) {
    case PortNameRole:
        return info.name;
    case InstallStatusRole:
        return static_cast<int>(info.installStatus);
    case LockStatusRole:
        return static_cast<int>(info.lockStatus);
    case ProvidedByAliasRole:
        return info.providedByAlias;
    case CollectionRole:
        return info.collection;
    default:
        break;
    }

    // DisplayRole
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColumnName:
            return info.name;
        case ColumnVersion:
            if (info.release.isEmpty()) {
                return info.version;
            }
            return info.version + QLatin1Char('-') + info.release;
        case ColumnStatus:
            return formatStatus(info);
        case ColumnCollection:
            return info.collection;
        case ColumnDescription:
            return info.description;
        default:
            return QVariant();
        }
    }

    // ToolTipRole
    if (role == Qt::ToolTipRole) {
        QString tip = QStringLiteral("<b>%1</b> %2-%3<br/><b>Collection:</b> %4<br/><b>Status:</b> %5")
            .arg(info.name, info.version, info.release, info.collection, formatStatus(info));
        if (!info.dependencies.isEmpty()) {
            tip += QStringLiteral("<br/><b>Dependencies:</b> ") + info.dependencies.join(QStringLiteral(", "));
        }
        if (!info.description.isEmpty()) {
            tip += QStringLiteral("<br/><i>%1</i>").arg(info.description);
        }
        return tip;
    }

    return QVariant();
}

QVariant PackageModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case ColumnName:
            return QStringLiteral("Name");
        case ColumnVersion:
            return QStringLiteral("Version");
        case ColumnStatus:
            return QStringLiteral("Status");
        case ColumnCollection:
            return QStringLiteral("Collection");
        case ColumnDescription:
            return QStringLiteral("Description");
        default:
            return QVariant();
        }
    }
    return QVariant();
}

void PackageModel::sort(int column, Qt::SortOrder order) {
    layoutAboutToBeChanged();

    std::sort(m_ports.begin(), m_ports.end(), [column, order](const PortInfo &a, const PortInfo &b) {
        int cmp = 0;
        switch (column) {
        case ColumnName:
            cmp = a.name.compare(b.name, Qt::CaseInsensitive);
            break;
        case ColumnVersion: {
            const QString verA = a.version + QLatin1Char('-') + a.release;
            const QString verB = b.version + QLatin1Char('-') + b.release;
            cmp = verA.compare(verB, Qt::CaseInsensitive);
            break;
        }
        case ColumnStatus:
            cmp = formatStatus(a).compare(formatStatus(b), Qt::CaseInsensitive);
            break;
        case ColumnCollection:
            cmp = a.collection.compare(b.collection, Qt::CaseInsensitive);
            break;
        case ColumnDescription:
            cmp = a.description.compare(b.description, Qt::CaseInsensitive);
            break;
        default:
            cmp = a.name.compare(b.name, Qt::CaseInsensitive);
            break;
        }

        if (cmp == 0) {
            cmp = a.name.compare(b.name, Qt::CaseInsensitive);
        }

        return (order == Qt::AscendingOrder) ? (cmp < 0) : (cmp > 0);
    });

    rebuildIndex();
    layoutChanged();
}

QString PackageModel::formatStatus(const PortInfo &info) {
    if (info.isOutdatedLocked()) {
        return QStringLiteral("Outdated (Locked)");
    }
    if (info.isLockedUninstalled()) {
        if (!info.providedByAlias.isEmpty()) {
            return QStringLiteral("Virtual (%1) [Locked]").arg(info.providedByAlias);
        }
        return QStringLiteral("Locked");
    }
    if (info.installStatus == InstallStatus::Outdated) {
        return QStringLiteral("Outdated");
    }
    if (info.installStatus == InstallStatus::Installed) {
        if (info.lockStatus == LockStatus::Locked) {
            return QStringLiteral("Installed (Locked)");
        }
        return QStringLiteral("Installed");
    }
    if (!info.providedByAlias.isEmpty()) {
        return QStringLiteral("Virtual (%1)").arg(info.providedByAlias);
    }
    return QStringLiteral("Available");
}

void PackageModel::rebuildIndex() {
    m_nameToRow.clear();
    for (int i = 0; i < m_ports.size(); ++i) {
        m_nameToRow.insert(m_ports.at(i).name, i);
    }
}

} // namespace OctoCRUX
