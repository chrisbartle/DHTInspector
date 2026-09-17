#include "CatalogModels.h"

CatalogListModel::CatalogListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CatalogListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QString CatalogListModel::ago(qint64 ms)
{
    if (ms < 0)
        return tr("never");
    const qint64 s = ms / 1000;
    if (s < 60)
        return tr("%1 s ago").arg(s);
    if (s < 3600)
        return tr("%1 min ago").arg(s / 60);
    return tr("%1 h %2 min ago").arg(s / 3600).arg((s % 3600) / 60);
}

QVariant CatalogListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const dht::NodeListRow &row = m_rows[index.row()];

    switch (role) {
    case AddressRole: return row.endpoint.toString();
    case FamilyRole: return dht::familyName(row.endpoint.family());
    case StateRole: return dht::stateName(row.state);
    case AnsweredRole: return row.answered;
    case ClientRole: return row.client;
    case VersionRole: return row.version;
    case ClientKindRole: return row.clientKind;
    case RawVersionRole: return QString::fromLatin1(row.rawVersion.toHex(' '));
    case RttRole: return row.rttMs;
    case Bep42Role: return row.answered ? dht::bep42::statusName(row.bep42) : QString();
    case NodeIdRole: return row.id.toHex();
    case NodeIdShortRole: return row.id.toHex().left(12) + QChar(0x2026);
    case NodesAtAddressRole: return row.nodesAtAddress;
    case FailuresRole: return row.failures;
    case SightingsRole: return row.sightings;
    case FirstSeenRole: return ago(row.firstSeenAgoMs);
    case LastAnsweredRole: return ago(row.lastAnsweredAgoMs);
    case LastQueriedRole: return ago(row.lastQueriedAgoMs);
    }
    return {};
}

QHash<int, QByteArray> CatalogListModel::roleNames() const
{
    return {
        {AddressRole, "address"},
        {FamilyRole, "family"},
        {StateRole, "state"},
        {AnsweredRole, "answered"},
        {ClientRole, "client"},
        {VersionRole, "version"},
        {ClientKindRole, "clientKind"},
        {RawVersionRole, "rawVersion"},
        {RttRole, "rtt"},
        {Bep42Role, "bep42"},
        {NodeIdRole, "nodeId"},
        {NodeIdShortRole, "nodeIdShort"},
        {NodesAtAddressRole, "nodesAtAddress"},
        {FailuresRole, "failures"},
        {SightingsRole, "sightings"},
        {FirstSeenRole, "firstSeen"},
        {LastAnsweredRole, "lastAnswered"},
        {LastQueriedRole, "lastQueried"},
    };
}

void CatalogListModel::setRows(std::vector<dht::NodeListRow> rows)
{
    const bool countChanges = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (countChanges)
        emit countChanged();
}

void CatalogListModel::clear()
{
    setRows({});
}
