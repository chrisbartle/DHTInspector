#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <vector>

struct PeerResultRow
{
    QString peer;         // address:port of the peer
    QStringList sources;  // nodes that returned it, address:port
};

// Peers from a get_peers search, each with the nodes that handed it back.
// A peer list is a claim by the node that returned it, so who said what
// matters as much as the peers themselves.
class PeerResultModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DhtController")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        PeerRole = Qt::UserRole + 1,
        FirstSourceRole,
        SourcesRole,
        SourceCountRole,
    };

    explicit PeerResultModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    void update(std::vector<PeerResultRow> rows);
    void clear();

signals:
    void countChanged();

private:
    std::vector<PeerResultRow> m_rows;
};
