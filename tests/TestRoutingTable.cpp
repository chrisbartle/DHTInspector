#include "dhtcore/RoutingTable.h"

#include <QTest>

#include <algorithm>

using namespace dht;

namespace {

constexpr qint64 T0 = 1'000'000;

Endpoint publicEndpoint(int n, quint16 port = 6881)
{
    return Endpoint(QHostAddress(quint32((44u << 24) | quint32(n + 1))), port);
}

NodeId idWithPrefix(const NodeId &self, int sharedBits)
{
    return NodeId::randomWithPrefix(self, sharedBits, true);
}

} // namespace

class TestRoutingTable : public QObject
{
    Q_OBJECT

private slots:
    void addThenUpdate();
    void rejectsSelf();
    void splitsOnlyTheOwnBucket();
    void onePerPublicIp();
    void closestIsExact();
    void badNodeReplacedFromCache();
    void removedAfterRepeatedFailures();
    void nodeStatesFollowActivity();
    void endpointChangingIdReplacesEntry();
    void rebuildKeepsNodes();
    void randomIdLandsInBucket();
    void staleBucketSelection();
};

void TestRoutingTable::addThenUpdate()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    const NodeId id = idWithPrefix(self, 3);
    QCOMPARE(table.heardFrom(id, publicEndpoint(1), "LT01", 40, T0), RoutingTable::InsertResult::Added);
    QCOMPARE(table.heardFrom(id, publicEndpoint(1), "LT01", 60, T0 + 10), RoutingTable::InsertResult::Updated);
    QCOMPARE(table.size(), 1);
    const RoutingNode *node = table.find(publicEndpoint(1));
    QVERIFY(node);
    QCOMPARE(node->rttMs, 45); // (40*3 + 60) / 4
}

void TestRoutingTable::rejectsSelf()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    QCOMPARE(table.heardFrom(self, publicEndpoint(1), {}, 10, T0), RoutingTable::InsertResult::Rejected);
    QVERIFY(!table.wouldAccept(self));
    QCOMPARE(table.size(), 0);
}

void TestRoutingTable::splitsOnlyTheOwnBucket()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);

    // Far half of the keyspace: fills bucket 0, which stops splitting once
    // it no longer covers our own ID.
    for (int i = 0; i < RoutingTable::K; ++i)
        QCOMPARE(table.heardFrom(idWithPrefix(self, 0), publicEndpoint(i), {}, 10, T0), RoutingTable::InsertResult::Added);
    QCOMPARE(table.heardFrom(idWithPrefix(self, 0), publicEndpoint(100), {}, 10, T0), RoutingTable::InsertResult::Cached);
    QCOMPARE(table.bucketCount(), 2);

    // Nodes sharing 5 bits split the own bucket down to index 5.
    for (int i = 0; i < RoutingTable::K; ++i)
        QCOMPARE(table.heardFrom(idWithPrefix(self, 5), publicEndpoint(200 + i), {}, 10, T0), RoutingTable::InsertResult::Added);
    QCOMPARE(table.heardFrom(idWithPrefix(self, 5), publicEndpoint(300), {}, 10, T0), RoutingTable::InsertResult::Cached);

    QCOMPARE(table.size(), 2 * RoutingTable::K);
    QCOMPARE(table.bucketCount(), 7);
    for (const RoutingNode &n : table.allNodes()) {
        const int cpl = NodeId::commonPrefixLength(self, n.id);
        QCOMPARE(table.bucketIndexFor(n.id), std::min(cpl, table.bucketCount() - 1));
    }
}

void TestRoutingTable::onePerPublicIp()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    QCOMPARE(table.heardFrom(idWithPrefix(self, 1), publicEndpoint(1, 1000), {}, 10, T0), RoutingTable::InsertResult::Added);
    QCOMPARE(table.heardFrom(idWithPrefix(self, 2), publicEndpoint(1, 2000), {}, 10, T0), RoutingTable::InsertResult::Rejected);

    const Endpoint lan1(QHostAddress(QStringLiteral("192.168.1.5")), 1000);
    const Endpoint lan2(QHostAddress(QStringLiteral("192.168.1.5")), 2000);
    QCOMPARE(table.heardFrom(idWithPrefix(self, 3), lan1, {}, 10, T0), RoutingTable::InsertResult::Added);
    QCOMPARE(table.heardFrom(idWithPrefix(self, 4), lan2, {}, 10, T0), RoutingTable::InsertResult::Added);
}

void TestRoutingTable::closestIsExact()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    for (int i = 0; i < 60; ++i)
        table.heardFrom(NodeId::random(), publicEndpoint(i), {}, 10, T0);

    const NodeId target = NodeId::random();
    const auto closest = table.closest(target, 8);
    auto all = table.allNodes();
    QCOMPARE(int(closest.size()), std::min(8, int(all.size())));

    std::sort(all.begin(), all.end(), [&](const auto &a, const auto &b) { return NodeId::closer(target, a.id, b.id); });
    for (size_t i = 0; i < closest.size(); ++i)
        QCOMPARE(closest[i].id, all[i].id);
}

void TestRoutingTable::badNodeReplacedFromCache()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    for (int i = 0; i < RoutingTable::K; ++i)
        table.heardFrom(idWithPrefix(self, 0), publicEndpoint(i), {}, 10, T0);
    const Endpoint cached = publicEndpoint(50);
    QCOMPARE(table.heardFrom(idWithPrefix(self, 0), cached, {}, 10, T0), RoutingTable::InsertResult::Cached);

    table.failed(publicEndpoint(0), T0 + 1);
    QVERIFY(table.contains(publicEndpoint(0)));
    QCOMPARE(RoutingTable::stateOf(*table.find(publicEndpoint(0)), T0 + 1), RoutingNode::State::Questionable);

    table.failed(publicEndpoint(0), T0 + 2);
    QVERIFY(!table.contains(publicEndpoint(0)));
    QVERIFY(table.contains(cached));
    QCOMPARE(table.size(), RoutingTable::K);
}

void TestRoutingTable::removedAfterRepeatedFailures()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    table.heardFrom(idWithPrefix(self, 2), publicEndpoint(1), {}, 10, T0);
    for (int i = 0; i < RoutingTable::RemoveFailCount - 1; ++i)
        table.failed(publicEndpoint(1), T0 + i);
    QVERIFY(table.contains(publicEndpoint(1)));
    table.failed(publicEndpoint(1), T0 + 10);
    QVERIFY(!table.contains(publicEndpoint(1)));
}

void TestRoutingTable::nodeStatesFollowActivity()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    const NodeId id = idWithPrefix(self, 1);
    table.heardFrom(id, publicEndpoint(1), {}, 10, T0);

    const auto state = [&](qint64 now) { return RoutingTable::stateOf(*table.find(publicEndpoint(1)), now); };
    QCOMPARE(state(T0), RoutingNode::State::Good);

    const qint64 later = T0 + RoutingTable::GoodWindowMs + 1;
    QCOMPARE(state(later), RoutingNode::State::Questionable);

    // Having answered before, a node that keeps querying us stays good.
    QVERIFY(table.queriedBy(id, publicEndpoint(1), later));
    QCOMPARE(state(later), RoutingNode::State::Good);

    QVERIFY(!table.queriedBy(NodeId::random(), publicEndpoint(1), later));
}

void TestRoutingTable::endpointChangingIdReplacesEntry()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    table.heardFrom(idWithPrefix(self, 1), publicEndpoint(1), {}, 10, T0);
    const NodeId restarted = idWithPrefix(self, 4);
    table.heardFrom(restarted, publicEndpoint(1), {}, 10, T0 + 5);
    QCOMPARE(table.size(), 1);
    QCOMPARE(table.find(publicEndpoint(1))->id, restarted);
}

void TestRoutingTable::rebuildKeepsNodes()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    for (int i = 0; i < 20; ++i)
        table.heardFrom(NodeId::random(), publicEndpoint(i), {}, 10, T0);
    const int before = table.size();

    const NodeId newSelf = NodeId::random();
    table.rebuild(newSelf, T0 + 100);
    QCOMPARE(table.selfId(), newSelf);
    QVERIFY(table.size() >= std::min(before, RoutingTable::K));
    for (const RoutingNode &n : table.allNodes()) {
        const int cpl = NodeId::commonPrefixLength(newSelf, n.id);
        QCOMPARE(table.bucketIndexFor(n.id), std::min(cpl, table.bucketCount() - 1));
    }
}

void TestRoutingTable::randomIdLandsInBucket()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    for (int i = 0; i < RoutingTable::K + 1; ++i)
        table.heardFrom(idWithPrefix(self, 3), publicEndpoint(i), {}, 10, T0);
    QVERIFY(table.bucketCount() > 2);

    const int last = table.bucketCount() - 1;
    for (int b = 0; b < last; ++b) {
        for (int i = 0; i < 20; ++i)
            QCOMPARE(NodeId::commonPrefixLength(self, table.randomIdInBucket(b)), b);
    }
    for (int i = 0; i < 20; ++i)
        QVERIFY(NodeId::commonPrefixLength(self, table.randomIdInBucket(last)) >= last);
}

void TestRoutingTable::staleBucketSelection()
{
    const NodeId self = NodeId::random();
    RoutingTable table(self);
    table.heardFrom(idWithPrefix(self, 0), publicEndpoint(1), {}, 10, T0);
    QVERIFY(!table.staleBucket(T0 + 1000, 15 * 60 * 1000));

    const qint64 later = T0 + 16 * 60 * 1000;
    const auto stale = table.staleBucket(later, 15 * 60 * 1000);
    QVERIFY(stale);
    table.touchBucket(*stale, later);
    QVERIFY(!table.staleBucket(later, 15 * 60 * 1000));
}

int runTestRoutingTable(int argc, char **argv)
{
    TestRoutingTable test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestRoutingTable.moc"
