#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>

#include <algorithm>
#include <vector>

// A list model whose rows are replaced by difference rather than by reset.
//
// A reset makes a ListView throw its delegates away and jump back to the
// top, which on a list refreshed every couple of seconds means it cannot be
// scrolled at all. Here, rows that went are removed, new ones inserted and
// ones that changed place moved, so a view keeps its position; rows that
// stayed are reported as changed in place.
template <typename Row>
class KeyedListModel : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }

protected:
    // Brings the rows in line with `next`, in its order. keyOf identifies a
    // row across updates and should be unique within a list; duplicates are
    // tolerated, only matched less economically.
    template <typename KeyFn>
    void replaceRows(std::vector<Row> next, KeyFn keyOf)
    {
        QHash<QByteArray, int> wanted;
        wanted.reserve(int(next.size()));
        for (const Row &row : next)
            wanted.insert(keyOf(row), 0);

        // Rows that are gone, from the back so earlier indexes stay valid,
        // each run of them in one step.
        for (int i = int(m_rows.size()) - 1; i >= 0;) {
            if (wanted.contains(keyOf(m_rows[size_t(i)]))) {
                --i;
                continue;
            }
            int first = i;
            while (first > 0 && !wanted.contains(keyOf(m_rows[size_t(first - 1)])))
                --first;
            beginRemoveRows(QModelIndex(), first, i);
            m_rows.erase(m_rows.begin() + first, m_rows.begin() + i + 1);
            endRemoveRows();
            i = first - 1;
        }

        // Then position by position: already in place, moved up from
        // further down, or new. Every row kept is somewhere in `next`, so a
        // row that is not in place is either further down or new.
        for (int i = 0; i < int(next.size()); ++i) {
            const QByteArray key = keyOf(next[size_t(i)]);
            if (i < int(m_rows.size()) && keyOf(m_rows[size_t(i)]) == key) {
                m_rows[size_t(i)] = std::move(next[size_t(i)]);
                continue;
            }
            int from = -1;
            for (int j = i + 1; j < int(m_rows.size()); ++j) {
                if (keyOf(m_rows[size_t(j)]) == key) {
                    from = j;
                    break;
                }
            }
            if (from >= 0) {
                beginMoveRows(QModelIndex(), from, from, QModelIndex(), i);
                std::rotate(m_rows.begin() + i, m_rows.begin() + from, m_rows.begin() + from + 1);
                endMoveRows();
                m_rows[size_t(i)] = std::move(next[size_t(i)]);
            } else {
                beginInsertRows(QModelIndex(), i, i);
                m_rows.insert(m_rows.begin() + i, std::move(next[size_t(i)]));
                endInsertRows();
            }
        }

        // Only duplicate keys can leave rows over.
        if (m_rows.size() > next.size()) {
            beginRemoveRows(QModelIndex(), int(next.size()), int(m_rows.size()) - 1);
            m_rows.erase(m_rows.begin() + qsizetype(next.size()), m_rows.end());
            endRemoveRows();
        }

        // Rows that stayed can still have changed: ages, counts, deadlines.
        if (!m_rows.empty())
            emit dataChanged(index(0), index(int(m_rows.size()) - 1));
    }

    std::vector<Row> m_rows;
};
