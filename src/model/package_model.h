#pragma once

#include "octocrux/types.h"
#include <QAbstractTableModel>
#include <QList>
#include <QMap>
#include <optional>

namespace OctoCRUX {

enum PackageModelColumn {
    ColumnName = 0,
    ColumnVersion = 1,
    ColumnStatus = 2,
    ColumnCollection = 3,
    ColumnDescription = 4,
    ColumnCount = 5
};

enum PackageModelRole {
    PortInfoRole = Qt::UserRole + 1,
    PortNameRole = Qt::UserRole + 2,
    InstallStatusRole = Qt::UserRole + 3,
    LockStatusRole = Qt::UserRole + 4,
    ProvidedByAliasRole = Qt::UserRole + 5,
    CollectionRole = Qt::UserRole + 6
};

/**
 * @brief Table model representing the catalog of ports across the three domain axes.
 */
class PackageModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit PackageModel(QObject *parent = nullptr);
    ~PackageModel() override = default;

    void setPorts(const QList<PortInfo> &ports);
    void clear();

    const QList<PortInfo>& ports() const;
    std::optional<PortInfo> portAt(int row) const;
    std::optional<PortInfo> portByName(const QString &name) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    static QString formatStatus(const PortInfo &info);

private:
    void rebuildIndex();

    QList<PortInfo> m_ports;
    QMap<QString, int> m_nameToRow;
};

} // namespace OctoCRUX
