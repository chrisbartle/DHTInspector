#pragma once

#include "dhtcore/NodeList.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <vector>

// One page of the network scan's node list, as the engine returned it.
class CatalogListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DhtController")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        AddressRole = Qt::UserRole + 1,  // ip:port, as the Probe tab takes it
        FamilyRole,
        StateRole,
        AnsweredRole,
        ClientRole,
        VersionRole,
        ClientKindRole,
        RawVersionRole,
        RttRole,           // ms, -1 unknown
        Bep42Role,
        NodeIdRole,
        NodeIdShortRole,
        NodesAtAddressRole,
        FailuresRole,
        SightingsRole,
        FirstSeenRole,     // "3 min ago"
        LastAnsweredRole,  // "12 s ago", "never"
        LastQueriedRole,
    };

    explicit CatalogListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    void setRows(std::vector<dht::NodeListRow> rows);
    void clear();

    static QString ago(qint64 ms);

signals:
    void countChanged();

private:
    std::vector<dht::NodeListRow> m_rows;
};
