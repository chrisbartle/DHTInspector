#pragma once

#include "KeyedListModel.h"

#include "dhtcore/Snapshot.h"

#include <QtQml/qqmlregistration.h>

#include <vector>

// Infohashes this node is holding peers for.
class StoredInfohashModel : public KeyedListModel<dht::StoredInfohashRow>
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DhtController")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        InfohashRole = Qt::UserRole + 1,
        PeerCountRole,
        LastAnnounceRole,
        ExpiresInRole,
    };

    explicit StoredInfohashModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    void update(std::vector<dht::StoredInfohashRow> rows);
    void clear();

signals:
    void countChanged();

};

// Peers stored for one infohash.
class StoredPeerModel : public KeyedListModel<dht::StoredPeerRow>
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

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    void update(std::vector<dht::StoredPeerRow> rows);
    void clear();

signals:
    void countChanged();

};

// BEP 44 items held by this node, immutable and mutable together.
class StoredItemModel : public KeyedListModel<dht::StoredItemRow>
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DhtController")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        TargetRole,
        ValueRole,
        RawValueRole,
        ValueSizeRole,
        SequenceRole,
        SaltRole,
        PublicKeyRole,
        ExpiresInRole,
    };

    explicit StoredItemModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    void update(std::vector<dht::StoredItemRow> rows);
    void clear();

signals:
    void countChanged();

};

// "27m", or "expired" once the deadline has passed.
QString formatRemaining(qint64 ms);

// Readable text for a stored value: as characters when it is printable,
// otherwise as hex. Truncated for display.
QString previewValue(const QByteArray &value, int maxChars = 48);

// A stored BEP 44 value is bencoded. Show what it decodes to when that is
// a plain string or number, and the bencoding itself otherwise.
QString decodedPreview(const QByteArray &bencodedValue, int maxChars = 48);
