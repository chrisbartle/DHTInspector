#include "SearchModels.h"

#include <utility>

PeerResultModel::PeerResultModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PeerResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PeerResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const PeerResultRow &row = m_rows[index.row()];

    switch (role) {
    case PeerRole: return row.peer;
    case FirstSourceRole: return row.sources.isEmpty() ? QStringLiteral("—") : row.sources.first();
    case SourcesRole: return row.sources.join(QStringLiteral("\n"));
    case SourceCountRole: return int(row.sources.size());
    }
    return {};
}

QHash<int, QByteArray> PeerResultModel::roleNames() const
{
    return {
        {PeerRole, "peer"},
        {FirstSourceRole, "firstSource"},
        {SourcesRole, "sources"},
        {SourceCountRole, "sourceCount"},
    };
}

void PeerResultModel::update(std::vector<PeerResultRow> rows)
{
    const int before = count();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (count() != before)
        emit countChanged();
}

void PeerResultModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}
