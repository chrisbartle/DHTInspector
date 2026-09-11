#include "NodeListModel.h"

namespace {

QString sourceName(dht::NodeRow::Source source)
{
    switch (source) {
    case dht::NodeRow::Source::Routing: return QStringLiteral("routing");
    case dht::NodeRow::Source::Injected: return QStringLiteral("injected");
    case dht::NodeRow::Source::Bootstrap: return QStringLiteral("bootstrap");
    }
    return {};
}

QString statusName(dht::NodeRow::Status status)
{
    switch (status) {
    case dht::NodeRow::Status::Good: return QStringLiteral("good");
    case dht::NodeRow::Status::Questionable: return QStringLiteral("questionable");
    case dht::NodeRow::Status::Bad: return QStringLiteral("bad");
    case dht::NodeRow::Status::Querying: return QStringLiteral("querying");
    case dht::NodeRow::Status::Responded: return QStringLiteral("responded");
    case dht::NodeRow::Status::NoResponse: return QStringLiteral("no response");
    }
    return {};
}

bool sameRow(const dht::NodeRow &a, const dht::NodeRow &b)
{
    return a.hasId == b.hasId && a.id == b.id && a.source == b.source && a.status == b.status
        && a.rttMs == b.rttMs && a.bep42 == b.bep42 && a.version == b.version && a.bucket == b.bucket
        && a.lastSeenAgoMs / 1000 == b.lastSeenAgoMs / 1000;
}

} // namespace

NodeListModel::NodeListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int NodeListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant NodeListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const dht::NodeRow &row = m_rows[index.row()];

    switch (role) {
    case AddressRole: return row.endpoint.toString();
    case FamilyRole: return dht::familyName(row.family);
    case NodeIdRole: return row.hasId ? row.id.toHex() : QString();
    case NodeIdShortRole: return row.hasId ? row.id.toHex().left(16) + QChar(0x2026) : QStringLiteral("—");
    case SourceRole: return sourceName(row.source);
    case StatusRole: return statusName(row.status);
    case RttRole: return row.rttMs;
    case LastSeenRole: return formatAge(row.lastSeenAgoMs);
    case Bep42Role: return dht::bep42::statusName(row.bep42);
    case ClientRole: return formatClient(row.version);
    case BucketRole: return row.bucket;
    }
    return {};
}

QHash<int, QByteArray> NodeListModel::roleNames() const
{
    return {
        {AddressRole, "address"},
        {FamilyRole, "family"},
        {NodeIdRole, "nodeId"},
        {NodeIdShortRole, "nodeIdShort"},
        {SourceRole, "source"},
        {StatusRole, "status"},
        {RttRole, "rtt"},
        {LastSeenRole, "lastSeen"},
        {Bep42Role, "bep42"},
        {ClientRole, "client"},
        {BucketRole, "bucket"},
    };
}

void NodeListModel::update(std::vector<dht::NodeRow> fresh)
{
    const int before = count();
    int i = 0;
    size_t j = 0;

    // Both lists are sorted by sortKey: walk them together, removing,
    // inserting or updating one row at a time.
    while (i < int(m_rows.size()) || j < fresh.size()) {
        const bool oldLeft = i < int(m_rows.size());
        const bool newLeft = j < fresh.size();

        if (oldLeft && (!newLeft || m_rows[i].sortKey < fresh[j].sortKey)) {
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.erase(m_rows.begin() + i);
            endRemoveRows();
        } else if (newLeft && (!oldLeft || fresh[j].sortKey < m_rows[i].sortKey)) {
            beginInsertRows(QModelIndex(), i, i);
            m_rows.insert(m_rows.begin() + i, std::move(fresh[j]));
            endInsertRows();
            ++i;
            ++j;
        } else {
            if (!sameRow(m_rows[i], fresh[j])) {
                m_rows[i] = std::move(fresh[j]);
                emit dataChanged(index(i), index(i));
            } else {
                m_rows[i].lastSeenAgoMs = fresh[j].lastSeenAgoMs;
            }
            ++i;
            ++j;
        }
    }

    if (count() != before)
        emit countChanged();
}

void NodeListModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

QString NodeListModel::formatClient(const QByteArray &version)
{
    if (version.isEmpty())
        return QStringLiteral("—");
    const auto printable = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (version.size() >= 2 && printable(version[0]) && printable(version[1])) {
        QString text = QString::fromLatin1(version.left(2));
        if (version.size() > 2)
            text += QLatin1Char(' ') + QString::fromLatin1(version.mid(2).toHex());
        return text;
    }
    return QString::fromLatin1(version.toHex());
}

QString NodeListModel::formatAge(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("—");
    const qint64 s = ms / 1000;
    if (s < 60)
        return QStringLiteral("%1s").arg(s);
    if (s < 3600)
        return QStringLiteral("%1m").arg(s / 60);
    return QStringLiteral("%1h").arg(s / 3600);
}
