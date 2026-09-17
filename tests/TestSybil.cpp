#include "dhtcore/Bep42.h"
#include "dhtcore/NodeCatalog.h"
#include "dhtcore/Sybil.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace dht;

namespace {

using State = CatalogEntry::State;

// An answering node with a random ID, unless one is given.
CatalogEntry &addNode(NodeCatalog &catalog, const QString &address, quint16 port,
                      std::optional<NodeId> id = std::nullopt, State state = State::Responsive)
{
    const auto slot = catalog.upsert(Endpoint(QHostAddress(address), port), 0);
    catalog.setState(slot, state);
    CatalogEntry &e = catalog.at(slot);
    e.id = id ? *id : NodeId::random();
    e.lastAnswered = 1;
    return e;
}

// Scattered public addresses with nothing unusual about them.
void addBackground(NodeCatalog &catalog, int count)
{
    for (int i = 0; i < count; ++i)
        addNode(catalog, QStringLiteral("%1.%2.%3.1").arg(11 + i % 80).arg(i / 80 % 250).arg(i % 7), 6881);
}

const SuspectAddress *findAddress(const std::vector<SuspectAddress> &list, const QString &address)
{
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&](const SuspectAddress &s) { return s.address == QHostAddress(address); });
    return it == list.end() ? nullptr : &*it;
}

NodeId idWithPrefix(quint8 first, quint8 second)
{
    NodeId id = NodeId::random();
    id[0] = first;
    id[1] = second;
    return id;
}

} // namespace

class TestSybil : public QObject
{
    Q_OBJECT

private slots:
    void poissonTail();
    void subnets();
    void quietNetworkHasNoSignals();
    void manyNodesOnOneAddress();
    void denseSubnet();
    void sharedIds();
    void denseIdWindow();
    void pointsToSelf();
    void flagsAddressesWithSeveralSignals();
    void filtersBySignal();
    void ignoresNodesThatDoNotAnswer();
};

void TestSybil::poissonTail()
{
    QCOMPARE(dht::poissonTail(3.0, 0), 1.0);
    QCOMPARE(dht::poissonTail(0.0, 1), 0.0);
    // P(X >= 1) = 1 - e^-lambda.
    QVERIFY(std::abs(dht::poissonTail(0.5, 1) - (1 - std::exp(-0.5))) < 1e-12);
    // Both sides of the mean, against direct sums.
    const auto direct = [](double lambda, int k) {
        double below = 0;
        double term = std::exp(-lambda);
        for (int j = 0; j < k; ++j) {
            below += term;
            term *= lambda / (j + 1);
        }
        return 1 - below;
    };
    for (const auto &[lambda, k] : {std::pair{2.0, 5}, std::pair{10.0, 4}, std::pair{10.0, 25}, std::pair{0.01, 3}}) {
        const double expected = direct(lambda, k);
        QVERIFY2(std::abs(dht::poissonTail(lambda, k) - expected) <= 1e-9 * std::max(1.0, expected) + 1e-15,
                 qPrintable(QStringLiteral("lambda %1 k %2").arg(lambda).arg(k)));
    }
    // Far tails stay positive and tiny rather than rounding to zero.
    const double far = dht::poissonTail(0.001, 20);
    QVERIFY(far > 0 && far < 1e-70);
}

void TestSybil::subnets()
{
    int bits = 0;
    QCOMPARE(subnetOf(QHostAddress(QStringLiteral("203.0.113.77")), &bits).toString(), QStringLiteral("203.0.113.0"));
    QCOMPARE(bits, 24);
    QCOMPARE(subnetOf(QHostAddress(QStringLiteral("2001:db8:1:2:3::4")), &bits).toString(), QStringLiteral("2001:db8:1::"));
    QCOMPARE(bits, 48);
}

void TestSybil::quietNetworkHasNoSignals()
{
    NodeCatalog catalog(10000);
    addBackground(catalog, 3000);
    const SybilReport r = computeSybilReport(catalog);
    QCOMPARE(r.answeringNodes, 3000);
    QCOMPARE(r.manyNodesCount, 0);
    QCOMPARE(r.denseSubnetCount, 0);
    QCOMPARE(r.sharedIdCount, 0);
    QCOMPARE(r.selfPointerCount, 0);
    QCOMPARE(r.flaggedCount, 0);
    // Random IDs: a spurious dense window is possible but should be rare.
    QVERIFY(r.denseWindows.size() <= 1);
    QVERIFY(r.addressSignals);
}

void TestSybil::manyNodesOnOneAddress()
{
    NodeCatalog catalog(1000);
    addBackground(catalog, 100);
    for (quint16 p = 0; p < SybilReport::ManyNodesAt; ++p) {
        CatalogEntry &e = addNode(catalog, QStringLiteral("198.51.100.7"), 7000 + p);
        e.bep42 = quint8(p == 0 ? bep42::Status::Compliant : bep42::Status::NonCompliant);
    }
    // One short of the threshold.
    for (quint16 p = 0; p < SybilReport::ManyNodesAt - 1; ++p)
        addNode(catalog, QStringLiteral("198.51.100.8"), 7000 + p);

    const SybilReport r = computeSybilReport(catalog);
    QCOMPARE(r.manyNodesCount, 1);
    const SuspectAddress *s = findAddress(r.manyNodes, QStringLiteral("198.51.100.7"));
    QVERIFY(s);
    QCOMPARE(s->answeringNodes, SybilReport::ManyNodesAt);
    QCOMPARE(s->compliantNodes, 1);
    QVERIFY(s->distinctPrefixes >= 1 && s->distinctPrefixes <= SybilReport::ManyNodesAt);
    QCOMPARE(s->suspicion, quint8(ManyNodes));
    QVERIFY(!findAddress(r.manyNodes, QStringLiteral("198.51.100.8")));
}

void TestSybil::denseSubnet()
{
    NodeCatalog catalog(1000);
    addBackground(catalog, 100);
    for (int i = 0; i < SybilReport::DenseSubnetAt; ++i)
        addNode(catalog, QStringLiteral("192.0.2.%1").arg(10 + i), 6881);
    for (int i = 0; i < SybilReport::DenseSubnetAt; ++i)
        addNode(catalog, QStringLiteral("2001:db8:5:%1::1").arg(i + 1, 0, 16), 6881);
    // Silent nodes in a subnet do not count.
    for (int i = 0; i < SybilReport::DenseSubnetAt; ++i)
        addNode(catalog, QStringLiteral("192.0.3.%1").arg(10 + i), 6881, std::nullopt, i == 0 ? State::Responsive : State::Silent);

    const SybilReport r = computeSybilReport(catalog);
    QCOMPARE(r.denseSubnetCount, 2);
    std::vector<QString> subnets;
    for (const SuspectSubnet &s : r.denseSubnets) {
        subnets.push_back(QStringLiteral("%1/%2").arg(s.base.toString()).arg(s.bits));
        QCOMPARE(s.addresses, SybilReport::DenseSubnetAt);
        QCOMPARE(s.answeringNodes, SybilReport::DenseSubnetAt);
    }
    QVERIFY(std::count(subnets.begin(), subnets.end(), QStringLiteral("192.0.2.0/24")) == 1);
    QVERIFY(std::count(subnets.begin(), subnets.end(), QStringLiteral("2001:db8:5::/48")) == 1);
    QCOMPARE(r.addressSignals->at(addNode(catalog, QStringLiteral("192.0.2.10"), 6881).address), quint8(DenseSubnet));
}

void TestSybil::sharedIds()
{
    NodeCatalog catalog(1000);
    addBackground(catalog, 100);
    const NodeId shared = NodeId::random();
    addNode(catalog, QStringLiteral("198.51.100.1"), 6881, shared);
    addNode(catalog, QStringLiteral("198.51.100.200"), 6881, shared);
    addNode(catalog, QStringLiteral("2001:db8::9"), 6881, shared);
    // The same ID twice on one address is not "several addresses".
    const NodeId local = NodeId::random();
    addNode(catalog, QStringLiteral("203.0.113.5"), 1000, local);
    addNode(catalog, QStringLiteral("203.0.113.5"), 1001, local);

    const SybilReport r = computeSybilReport(catalog);
    QCOMPARE(r.sharedIdCount, 1);
    QCOMPARE(r.sharedIds[0].id, shared);
    QCOMPARE(r.sharedIds[0].addresses, 3);
    QCOMPARE(int(r.sharedIds[0].sample.size()), 3);
    CatalogEntry probe;
    probe.address = catalog.at(catalog.find(Endpoint(QHostAddress(QStringLiteral("2001:db8::9")), 6881))).address;
    QVERIFY(r.addressSignals->at(probe.address) & SharedId);
}

void TestSybil::denseIdWindow()
{
    NodeCatalog catalog(5000);
    addBackground(catalog, 2000);
    // 30 IDs packed under one 16-bit prefix, on scattered addresses: about
    // 0.03 would land there by chance.
    for (int i = 0; i < 30; ++i)
        addNode(catalog, QStringLiteral("100.%1.0.1").arg(i + 1), 6881, idWithPrefix(0xab, 0xcd));

    const SybilReport r = computeSybilReport(catalog);
    QVERIFY(r.denseWindowCount >= 1);
    NodeId packed;
    packed[0] = 0xab;
    packed[1] = 0xcd;
    const auto it = std::find_if(r.denseWindows.begin(), r.denseWindows.end(), [&](const DenseIdWindow &w) {
        return w.bits <= 16 && NodeId::commonPrefixLength(w.prefix, packed) >= w.bits;
    });
    QVERIFY(it != r.denseWindows.end());
    QVERIFY(it->nodes >= 30);
    QVERIFY(it->chance < SybilReport::DenseWindowChance);
    QVERIFY(it->expected < it->nodes);
    QVERIFY(it->addresses >= 30);
    // Least likely first.
    for (size_t i = 1; i < r.denseWindows.size(); ++i)
        QVERIFY(r.denseWindows[i - 1].chance <= r.denseWindows[i].chance);
    CatalogEntry probe;
    probe.address = catalog.at(catalog.find(Endpoint(QHostAddress(QStringLiteral("100.1.0.1")), 6881))).address;
    QVERIFY(r.addressSignals->at(probe.address) & DenseIds);
}

void TestSybil::pointsToSelf()
{
    NodeCatalog catalog(1000);
    addBackground(catalog, 50);
    addNode(catalog, QStringLiteral("198.51.100.1"), 6881).selfListShare = 90;
    addNode(catalog, QStringLiteral("198.51.100.2"), 6881).selfListShare = quint8(SybilReport::PointsToSelfAt - 1);
    addNode(catalog, QStringLiteral("198.51.100.3"), 6881).selfListShare = quint8(SybilReport::PointsToSelfAt);

    const SybilReport r = computeSybilReport(catalog);
    QCOMPARE(r.selfPointerCount, 2);
    QCOMPARE(r.selfPointers[0].address.toString(), QStringLiteral("198.51.100.1"));
    QCOMPARE(r.selfPointers[0].port, quint16(6881));
    QCOMPARE(r.selfPointers[0].sharePercent, 90);
    QCOMPARE(r.selfPointers[1].sharePercent, SybilReport::PointsToSelfAt);
}

void TestSybil::flagsAddressesWithSeveralSignals()
{
    NodeCatalog catalog(1000);
    addBackground(catalog, 100);
    // A /24 full of addresses running several nodes each, all listing each other.
    for (int a = 0; a < SybilReport::DenseSubnetAt; ++a) {
        for (quint16 p = 0; p < SybilReport::ManyNodesAt; ++p)
            addNode(catalog, QStringLiteral("198.18.4.%1").arg(a + 1), 5000 + p).selfListShare = 100;
    }
    // One address with just one signal, and scattered nodes with another.
    for (quint16 p = 0; p < SybilReport::ManyNodesAt; ++p)
        addNode(catalog, QStringLiteral("203.0.113.9"), 5000 + p);
    for (int i = 0; i < 15; ++i)
        addNode(catalog, QStringLiteral("45.1.%1.1").arg(i), 6881).selfListShare = 100;

    const SybilReport r = computeSybilReport(catalog);
    QCOMPARE(r.flaggedCount, SybilReport::DenseSubnetAt);
    QCOMPARE(r.manyNodesCount, SybilReport::DenseSubnetAt + 1);
    for (const SuspectAddress &s : r.flagged) {
        QVERIFY(s.suspicion & ManyNodes);
        QVERIFY(s.suspicion & DenseSubnet);
        QVERIFY(s.suspicion & PointsToSelf);
    }
    QVERIFY(!findAddress(r.flagged, QStringLiteral("203.0.113.9")));
    QCOMPARE(r.selfPointerCount, SybilReport::DenseSubnetAt * SybilReport::ManyNodesAt + 15);
    QCOMPARE(int(r.selfPointers.size()), SybilReport::MaxListed);  // trimmed
}

void TestSybil::filtersBySignal()
{
    NodeCatalog catalog(1000);
    for (quint16 p = 0; p < SybilReport::ManyNodesAt; ++p)
        addNode(catalog, QStringLiteral("203.0.113.9"), 5000 + p);
    CatalogEntry &self = addNode(catalog, QStringLiteral("198.51.100.1"), 6881);
    self.selfListShare = 80;
    addNode(catalog, QStringLiteral("198.51.100.2"), 6881);
    const SybilReport r = computeSybilReport(catalog);
    const AddressSignals *signalMap = r.addressSignals.get();

    const auto at = [&](const QString &address, quint16 port) -> const CatalogEntry & {
        return catalog.at(catalog.find(Endpoint(QHostAddress(address), port)));
    };
    const CatalogEntry &many = at(QStringLiteral("203.0.113.9"), 5000);
    const CatalogEntry &pointer = at(QStringLiteral("198.51.100.1"), 6881);
    const CatalogEntry &plain = at(QStringLiteral("198.51.100.2"), 6881);

    QVERIFY(matchesSignals(plain, 0, signalMap));
    QVERIFY(matchesSignals(many, ManyNodes, signalMap));
    QVERIFY(!matchesSignals(plain, ManyNodes, signalMap));
    QVERIFY(!matchesSignals(many, DenseSubnet, signalMap));
    QVERIFY(matchesSignals(pointer, PointsToSelf, signalMap));
    QVERIFY(matchesSignals(pointer, PointsToSelf, nullptr));  // judged on the node itself
    QVERIFY(!matchesSignals(many, PointsToSelf, signalMap));
    QVERIFY(!matchesSignals(many, SeveralSignals, signalMap));
    QVERIFY(matchesSignals(many, ManyNodes | PointsToSelf, signalMap));
    QVERIFY(!matchesSignals(many, ManyNodes, nullptr));
}

void TestSybil::ignoresNodesThatDoNotAnswer()
{
    NodeCatalog catalog(1000);
    const NodeId shared = NodeId::random();
    for (quint16 p = 0; p < 10; ++p) {
        addNode(catalog, QStringLiteral("203.0.113.9"), 5000 + p, shared, State::Gone);
        addNode(catalog, QStringLiteral("203.0.113.%1").arg(20 + p), 6881, shared, State::Silent).selfListShare = 100;
    }
    const SybilReport r = computeSybilReport(catalog);
    QCOMPARE(r.answeringNodes, 0);
    QCOMPARE(r.manyNodesCount + r.denseSubnetCount + r.sharedIdCount + r.selfPointerCount + r.flaggedCount, 0);
    QVERIFY(r.denseWindows.empty());
    QCOMPARE(r.addressSignals->size(), 0);
}

int runTestSybil(int argc, char **argv)
{
    TestSybil test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestSybil.moc"
