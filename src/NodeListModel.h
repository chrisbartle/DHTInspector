#pragma once

#include "dhtcore/Snapshot.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <vector>

// Routing-table nodes and seeds from the latest engine snapshot. Successive
// snapshots are merged row by row so views keep their scroll position.
class NodeListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DhtController")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        AddressRole = Qt::UserRole + 1,
        FamilyRole,
        NodeIdRole,
        NodeIdShortRole,
        SourceRole,
        StatusRole,
        RttRole,
        LastSeenRole,
        Bep42Role,
        ClientRole,
        BucketRole,
    };

    explicit NodeListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    // `rows` must be sorted by NodeRow::sortKey with unique keys.
    void update(std::vector<dht::NodeRow> rows);
    void clear();

    static QString formatClient(const QByteArray &version);
    static QString formatAge(qint64 ms);

signals:
    void countChanged();

private:
    std::vector<dht::NodeRow> m_rows;
};
