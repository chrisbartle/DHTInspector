#include "ProbeModels.h"

ProbeHistoryModel::ProbeHistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QString ProbeHistoryModel::outcomeOf(const dht::ProbeResult &result)
{
    if (result.timedOut)
        return QStringLiteral("timeout");
    if (result.isError)
        return QStringLiteral("error %1").arg(result.errorCode);
    if (result.response.isEmpty())
        return QStringLiteral("not sent");
    return QStringLiteral("ok");
}

int ProbeHistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ProbeHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const ProbeExchange &row = m_rows[index.row()];

    switch (role) {
    case MethodRole: return QString::fromLatin1(row.result.method);
    case EndpointRole: return row.result.endpoint.toString();
    case OutcomeRole: return outcomeOf(row.result);
    case RttRole: return row.result.rttMs >= 0 && !row.result.timedOut
                             ? QStringLiteral("%1 ms").arg(row.result.rttMs)
                             : QStringLiteral("—");
    case TimeRole: return row.time;
    case SummaryRole: return row.result.summary;
    }
    return {};
}

QHash<int, QByteArray> ProbeHistoryModel::roleNames() const
{
    return {
        {MethodRole, "method"},
        {EndpointRole, "endpoint"},
        {OutcomeRole, "outcome"},
        {RttRole, "rtt"},
        {TimeRole, "time"},
        {SummaryRole, "summary"},
    };
}

void ProbeHistoryModel::prepend(const ProbeExchange &exchange, int maxRows)
{
    beginInsertRows(QModelIndex(), 0, 0);
    m_rows.insert(m_rows.begin(), exchange);
    endInsertRows();

    if (int(m_rows.size()) > maxRows) {
        beginRemoveRows(QModelIndex(), int(m_rows.size()) - 1, int(m_rows.size()) - 1);
        m_rows.pop_back();
        endRemoveRows();
    }
    emit countChanged();
}

void ProbeHistoryModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

const ProbeExchange *ProbeHistoryModel::at(int row) const
{
    if (row < 0 || row >= int(m_rows.size()))
        return nullptr;
    return &m_rows[row];
}
