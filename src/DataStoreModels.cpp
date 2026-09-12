#include "DataStoreModels.h"

#include "NodeListModel.h"

#include "dhtcore/Bencode.h"

#include <algorithm>

QString formatRemaining(qint64 ms)
{
    return ms <= 0 ? QStringLiteral("expired") : NodeListModel::formatAge(ms);
}

QString previewValue(const QByteArray &value, int maxChars)
{
    const bool printable = std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 0x20 && c < 0x7f) || c == '\n' || c == '\t';
    });
    QString text = printable ? QString::fromLatin1(value) : QString::fromLatin1(value.toHex());
    text.replace(QChar(u'\n'), QChar(u' ')).replace(QChar(u'\t'), QChar(u' '));
    if (text.size() > maxChars)
        text = text.left(maxChars) + QChar(0x2026);
    return text;
}

QString decodedPreview(const QByteArray &bencodedValue, int maxChars)
{
    const dht::BDecodeResult decoded = dht::bdecode(bencodedValue);
    if (decoded.ok()) {
        if (decoded.value.isString())
            return previewValue(decoded.value.toString(), maxChars);
        if (decoded.value.isInteger())
            return QString::number(decoded.value.toInteger());
    }
    return previewValue(bencodedValue, maxChars);
}

// --- StoredItemModel -------------------------------------------------------

StoredItemModel::StoredItemModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int StoredItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant StoredItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const dht::StoredItemRow &row = m_rows[index.row()];

    switch (role) {
    case KindRole: return row.isMutable ? QStringLiteral("mutable") : QStringLiteral("immutable");
    case TargetRole: return row.target.toHex();
    case TargetShortRole: return row.target.toHex().left(20) + QChar(0x2026);
    case ValueRole: return decodedPreview(row.value);
    case RawValueRole: return previewValue(row.value, 120);
    case ValueSizeRole: return int(row.value.size());
    case SequenceRole: return row.isMutable ? QString::number(row.sequence) : QStringLiteral("—");
    case SaltRole: return row.salt.isEmpty() ? QStringLiteral("—") : previewValue(row.salt, 16);
    case PublicKeyRole: return row.publicKey.isEmpty() ? QString() : QString::fromLatin1(row.publicKey.toHex());
    case ExpiresInRole: return formatRemaining(row.expiresInMs);
    }
    return {};
}

QHash<int, QByteArray> StoredItemModel::roleNames() const
{
    return {
        {KindRole, "kind"},
        {TargetRole, "target"},
        {TargetShortRole, "targetShort"},
        {ValueRole, "value"},
        {RawValueRole, "rawValue"},
        {ValueSizeRole, "valueSize"},
        {SequenceRole, "sequence"},
        {SaltRole, "salt"},
        {PublicKeyRole, "publicKey"},
        {ExpiresInRole, "expiresIn"},
    };
}

void StoredItemModel::update(std::vector<dht::StoredItemRow> rows)
{
    const int before = count();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (count() != before)
        emit countChanged();
}

void StoredItemModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

// --- StoredInfohashModel ---------------------------------------------------

StoredInfohashModel::StoredInfohashModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int StoredInfohashModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant StoredInfohashModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const dht::StoredInfohashRow &row = m_rows[index.row()];

    switch (role) {
    case InfohashRole: return row.infohash.toHex();
    case InfohashShortRole: return row.infohash.toHex().left(20) + QChar(0x2026);
    case PeerCountRole: return row.peerCount;
    case LastAnnounceRole: return NodeListModel::formatAge(row.lastAnnounceAgoMs);
    case ExpiresInRole: return formatRemaining(row.expiresInMs);
    }
    return {};
}

QHash<int, QByteArray> StoredInfohashModel::roleNames() const
{
    return {
        {InfohashRole, "infohash"},
        {InfohashShortRole, "infohashShort"},
        {PeerCountRole, "peerCount"},
        {LastAnnounceRole, "lastAnnounce"},
        {ExpiresInRole, "expiresIn"},
    };
}

void StoredInfohashModel::update(std::vector<dht::StoredInfohashRow> rows)
{
    const int before = count();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (count() != before)
        emit countChanged();
}

void StoredInfohashModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

// --- StoredPeerModel -------------------------------------------------------

StoredPeerModel::StoredPeerModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int StoredPeerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant StoredPeerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const dht::StoredPeerRow &row = m_rows[index.row()];

    switch (role) {
    case AddressRole: return row.endpoint.toString();
    case FamilyRole: return dht::familyName(row.endpoint.family());
    case AnnouncedRole: return NodeListModel::formatAge(row.ageMs);
    case ExpiresInRole: return formatRemaining(row.expiresInMs);
    }
    return {};
}

QHash<int, QByteArray> StoredPeerModel::roleNames() const
{
    return {
        {AddressRole, "address"},
        {FamilyRole, "family"},
        {AnnouncedRole, "announced"},
        {ExpiresInRole, "expiresIn"},
    };
}

void StoredPeerModel::update(std::vector<dht::StoredPeerRow> rows)
{
    const int before = count();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (count() != before)
        emit countChanged();
}

void StoredPeerModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}
