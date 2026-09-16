#pragma once

#include "dhtcore/Snapshot.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <vector>

struct ProbeExchange
{
    dht::ProbeResult result;
    QString time;  // when we asked, local clock
};

// Every question put to the node under examination, newest first.
class ProbeHistoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DhtController")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        MethodRole = Qt::UserRole + 1,
        EndpointRole,
        OutcomeRole,
        RttRole,
        TimeRole,
        SummaryRole,
    };

    explicit ProbeHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    void prepend(const ProbeExchange &exchange, int maxRows);
    void clear();
    const ProbeExchange *at(int row) const;

    // "ok", "timeout", "error 204" or why the query never left.
    static QString outcomeOf(const dht::ProbeResult &result);

signals:
    void countChanged();

private:
    std::vector<ProbeExchange> m_rows;
};
