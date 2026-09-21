#include "KeyedListModel.h"

#include <QAbstractItemModelTester>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <vector>

namespace {

struct Row
{
    QByteArray key;
    int value = 0;
};

class Model : public KeyedListModel<Row>
{
public:
    using KeyedListModel::KeyedListModel;

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= int(m_rows.size()))
            return {};
        const Row &row = m_rows[size_t(index.row())];
        // Only its own roles: the model tester checks the standard ones for
        // the right types.
        if (role == Qt::DisplayRole)
            return row.key;
        if (role == Qt::UserRole)
            return row.value;
        return {};
    }

    void update(std::vector<Row> rows)
    {
        replaceRows(std::move(rows), [](const Row &row) { return row.key; });
    }

    QByteArrayList keys() const
    {
        QByteArrayList out;
        for (const Row &row : m_rows)
            out << row.key;
        return out;
    }
    QList<int> values() const
    {
        QList<int> out;
        for (const Row &row : m_rows)
            out << row.value;
        return out;
    }
};

std::vector<Row> rows(const QByteArrayList &keys, int value = 0)
{
    std::vector<Row> out;
    for (const QByteArray &key : keys)
        out.push_back({key, value});
    return out;
}

} // namespace

class TestKeyedListModel : public QObject
{
    Q_OBJECT

private slots:
    void insertsAtTheTopWithoutReset();
    void movesARowThatComesBackToTheTop();
    void removesRowsThatWent();
    void reportsRowsThatStayedAsChanged();
    void toleratesDuplicateKeys();
    void randomUpdatesAlwaysEndInTheWantedOrder();
};

// What a view cares about: a new row appears as an insertion, and nothing
// resets, so a scrolled view is not thrown back to the top.
void TestKeyedListModel::insertsAtTheTopWithoutReset()
{
    Model model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.update(rows({"a", "b", "c"}));

    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.update(rows({"x", "a", "b", "c"}));

    QCOMPARE(model.keys(), (QByteArrayList{"x", "a", "b", "c"}));
    QCOMPARE(resets.count(), 0);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 0);
    QCOMPARE(removed.count(), 0);
}

// A re-announced infohash jumps to the top of a newest-first list: that is
// one move, not a removal and an insertion.
void TestKeyedListModel::movesARowThatComesBackToTheTop()
{
    Model model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.update(rows({"a", "b", "c", "d"}));

    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.update(rows({"c", "a", "b", "d"}));

    QCOMPARE(model.keys(), (QByteArrayList{"c", "a", "b", "d"}));
    QCOMPARE(resets.count(), 0);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
}

void TestKeyedListModel::removesRowsThatWent()
{
    Model model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.update(rows({"a", "b", "c", "d", "e"}));

    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.update(rows({"a", "e"}));

    QCOMPARE(model.keys(), (QByteArrayList{"a", "e"}));
    QCOMPARE(resets.count(), 0);
    // b, c and d were next to each other, so they go in one step.
    QCOMPARE(removed.count(), 1);

    model.update({});
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(resets.count(), 0);
}

// Ages and deadlines change on every refresh, so rows that stayed must
// still show their new values.
void TestKeyedListModel::reportsRowsThatStayedAsChanged()
{
    Model model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.update(rows({"a", "b"}, 1));

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.update(rows({"a", "b"}, 2));

    QCOMPARE(model.values(), (QList<int>{2, 2}));
    QCOMPARE(model.data(model.index(1), Qt::UserRole).toInt(), 2);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).value<QModelIndex>().row(), 0);
    QCOMPARE(changed.first().at(1).value<QModelIndex>().row(), 1);
}

void TestKeyedListModel::toleratesDuplicateKeys()
{
    Model model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.update(rows({"a", "a", "b"}));
    QCOMPARE(model.keys(), (QByteArrayList{"a", "a", "b"}));
    model.update(rows({"b", "a"}));
    QCOMPARE(model.keys(), (QByteArrayList{"b", "a"}));
    model.update(rows({"a", "b", "b", "a"}));
    QCOMPARE(model.keys(), (QByteArrayList{"a", "b", "b", "a"}));
}

// The bookkeeping between insertions, moves and removals is where a diff
// goes wrong, so throw many random updates at it with the model tester
// watching every signal, and check the result each time.
void TestKeyedListModel::randomUpdatesAlwaysEndInTheWantedOrder()
{
    Model model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QRandomGenerator rng(20260921);

    QByteArrayList pool;
    for (int i = 0; i < 40; ++i)
        pool << QByteArray::number(i);

    for (int round = 0; round < 2000; ++round) {
        QByteArrayList keys = pool;
        std::shuffle(keys.begin(), keys.end(), rng);
        keys = keys.mid(0, int(rng.bounded(41)));
        model.update(rows(keys, round));
        QCOMPARE(model.keys(), keys);
        const QList<int> values = model.values();
        QVERIFY(std::all_of(values.begin(), values.end(), [&](int v) { return v == round; }));
        if (QTest::currentTestFailed())
            return;
    }
    QCOMPARE(resets.count(), 0);
}

int runTestKeyedListModel(int argc, char **argv)
{
    TestKeyedListModel test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestKeyedListModel.moc"
