#pragma once

#include "dhtcore/Snapshot.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <vector>

// Infohashes this node is holding peers for.
class StoredInfohashModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DhtController")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        InfohashRole = Qt::UserRole + 1,
        InfohashShortRole,
        PeerCountRole,
        LastAnnounceRole,
        ExpiresInRole,
    };

    explicit StoredInfohashModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    void update(std::vector<dht::StoredInfohashRow> rows);
    void clear();

signals:
    void countChanged();

private:
    std::vector<dht::StoredInfohashRow> m_rows;
};

// Peers stored for one infohash.
class StoredPeerModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DhtController")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        AddressRole = Qt::UserRole + 1,
        FamilyRole,
        AnnouncedRole,
        ExpiresInRole,
    };

    explicit StoredPeerModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    void update(std::vector<dht::StoredPeerRow> rows);
    void clear();

signals:
    void countChanged();

private:
    std::vector<dht::StoredPeerRow> m_rows;
};

// BEP 44 items held by this node, immutable and mutable together.
class StoredItemModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DhtController")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        TargetRole,
        TargetShortRole,
        ValueRole,
        RawValueRole,
        ValueSizeRole,
        SequenceRole,
        SaltRole,
        PublicKeyRole,
        ExpiresInRole,
    };

    explicit StoredItemModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    void update(std::vector<dht::StoredItemRow> rows);
    void clear();

signals:
    void countChanged();

private:
    std::vector<dht::StoredItemRow> m_rows;
};

// "27m", or "expired" once the deadline has passed.
QString formatRemaining(qint64 ms);

// Readable text for a stored value: as characters when it is printable,
// otherwise as hex. Truncated for display.
QString previewValue(const QByteArray &value, int maxChars = 48);

// A stored BEP 44 value is bencoded. Show what it decodes to when that is
// a plain string or number, and the bencoding itself otherwise.
QString decodedPreview(const QByteArray &bencodedValue, int maxChars = 48);
