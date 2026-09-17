#include "dhtcore/Bep42.h"
#include "dhtcore/NetworkStats.h"
#include "dhtcore/NodeCatalog.h"
#include "dhtcore/Support.h"

#include <QRandomGenerator>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <random>

using namespace dht;

namespace {

using State = CatalogEntry::State;

struct NodeSpec
{
    QString address;
    quint16 port;
    State state;
    QByteArray version;
    int rttMs;
    bep42::Status bep42;
};

void fill(NodeCatalog &catalog, const std::vector<NodeSpec> &specs)
{
    for (const NodeSpec &n : specs) {
        const auto slot = catalog.upsert(Endpoint(QHostAddress(n.address), n.port), 0);
        catalog.setState(slot, n.state);
        CatalogEntry &e = catalog.at(slot);
        e.setVersion(n.version);
        e.rttMs = n.rttMs < 0 ? CatalogEntry::NoRtt : quint16(n.rttMs);
        e.bep42 = quint8(n.bep42);
        // Responsive and gone nodes have answered at some point.
        if (n.state == State::Responsive || n.state == State::Gone)
            e.lastAnswered = 1;
    }
}

const ClientTally *findTally(const std::vector<ClientTally> &list, const QString &name, const QString &version = {})
{
    const auto it = std::find_if(list.begin(), list.end(), [&](const ClientTally &t) {
        return t.name == name && t.version == version;
    });
    return it == list.end() ? nullptr : &*it;
}

// Distances of the k closest of N uniformly random points to a target, as
// fractions of the space: consecutive gaps are close to exponential with
// mean 1/N when N is large.
std::vector<NodeId> simulatedClosest(const NodeId &target, double n, int k, std::mt19937_64 &rng)
{
    std::exponential_distribution<double> gap(n);
    std::vector<NodeId> out;
    double distance = 0;
    for (int i = 0; i < k; ++i) {
        distance += gap(rng);
        const quint64 top = quint64(std::ldexp(std::min(distance, 0.999999), 64));
        NodeId offset;
        for (int b = 0; b < 8; ++b)
            offset[b] = quint8(top >> (56 - 8 * b));
        out.push_back(offset ^ target);
    }
    return out;
}

} // namespace

class TestNetworkStats : public QObject
{
    Q_OBJECT

private slots:
    void countsOnlyResponsiveNodesPerFamily();
    void groupsClients();
    void summarisesRoundTrips();
    void countsBep42AndPorts();
    void quantilesFromHistogram();
    void estimatesSizeOfSimulatedNetworks_data();
    void estimatesSizeOfSimulatedNetworks();
    void estimatesSizeFromRealIds();
    void summarisesEstimates();
    void emptyCatalogue();
    void countsAddressesNotNodes();
};

void TestNetworkStats::countsOnlyResponsiveNodesPerFamily()
{
    NodeCatalog catalog(100);
    fill(catalog, {
        {"203.0.113.1", 6881, State::Responsive, "LT\x02\x0b", 100, bep42::Status::Compliant},
        {"203.0.113.2", 6881, State::Responsive, "LT\x02\x0b", 200, bep42::Status::Compliant},
        {"203.0.113.3", 6881, State::Silent, "", -1, bep42::Status::Unknown},
        {"203.0.113.4", 6881, State::Gone, "UT\x01\x02", 50, bep42::Status::NonCompliant},
        {"203.0.113.5", 6881, State::New, "", -1, bep42::Status::Unknown},
        {"2001:db8::1", 6881, State::Responsive, "", 300, bep42::Status::NonCompliant},
    });
    const NetworkStatsSet set = computeNetworkStats(catalog, nowMs());
    QCOMPARE(set.ipv4.answeringIps, 2);
    QCOMPARE(set.ipv6.answeringIps, 1);
    QCOMPARE(set.all.answeringIps, 3);
    QCOMPARE(set.ipv4.heardIps, 5);
    QCOMPARE(set.ipv4.connectedIps, 3);  // the gone node answered once
    QCOMPARE(set.ipv4.rttWeight, 2.0);
    QCOMPARE(set.all.rttWeight, 3.0);
    // The gone node's µTorrent does not count.
    QVERIFY(!findTally(set.all.clients, QString(QChar(0x00b5)) + QStringLiteral("Torrent")));
    QVERIFY(set.computeMs >= 0);
}

void TestNetworkStats::groupsClients()
{
    NodeCatalog catalog(100);
    std::vector<NodeSpec> specs;
    const auto add = [&](int count, const char *version, int length) {
        for (int i = 0; i < count; ++i) {
            specs.push_back({QStringLiteral("198.51.100.%1").arg(specs.size() + 1), 6881, State::Responsive,
                             QByteArray(version, length), 50, bep42::Status::Compliant});
        }
    };
    add(5, "LT\x02\x0b", 4);   // libtorrent 2.0.11
    add(3, "LT\x01\x2f", 4);   // libtorrent 1.2.15
    add(4, "lt\x10\x11", 4);   // rtorrent's libTorrent 0.16.17
    add(2, "UT\xab\xcd", 4);   // µTorrent, unpublished layout
    add(1, "UT\x12\x34", 4);
    add(6, "", 0);             // no version
    add(2, "ZZ\x01\x02", 4);   // unknown code
    add(1, "LT\x01", 3);       // nonstandard
    fill(catalog, specs);

    const NetworkStats s = computeNetworkStats(catalog, nowMs()).all;
    QCOMPARE(s.answeringIps, 24);

    // By name, most common first.
    QCOMPARE(s.clients.front().name, QStringLiteral("libtorrent (Rasterbar)"));
    QCOMPARE(s.clients.front().count, 8.0);
    QCOMPARE(s.clients.front().kind, QStringLiteral("known"));
    QCOMPARE(findTally(s.clients, QStringLiteral("no version sent"))->count, 6.0);
    QCOMPARE(findTally(s.clients, QStringLiteral("no version sent"))->kind, QStringLiteral("absent"));
    QCOMPARE(findTally(s.clients, QStringLiteral("libTorrent (Rakshasa)"))->count, 4.0);
    const QString micro = QString(QChar(0x00b5)) + QStringLiteral("Torrent");
    QCOMPARE(findTally(s.clients, micro)->count, 3.0);
    QCOMPARE(findTally(s.clients, QStringLiteral("unknown client ZZ"))->kind, QStringLiteral("unknown"));
    QCOMPARE(findTally(s.clients, QStringLiteral("libtorrent (Rasterbar), nonstandard 3-byte field"))->count, 1.0);
    double total = 0;
    for (const ClientTally &t : s.clients)
        total += t.count;
    QCOMPARE(total, 24.0);

    // By version.
    QCOMPARE(findTally(s.versions, QStringLiteral("libtorrent (Rasterbar)"), QStringLiteral("2.0.11"))->count, 5.0);
    QCOMPARE(findTally(s.versions, QStringLiteral("libtorrent (Rasterbar)"), QStringLiteral("1.2.15"))->count, 3.0);
    QCOMPARE(findTally(s.versions, QStringLiteral("libTorrent (Rakshasa)"), QStringLiteral("0.16.17"))->count, 4.0);
    QCOMPARE(findTally(s.versions, micro, QStringLiteral("bytes ab cd"))->count, 2.0);
    QCOMPARE(findTally(s.versions, micro, QStringLiteral("bytes 12 34"))->count, 1.0);
    QVERIFY(std::is_sorted(s.versions.begin(), s.versions.end(),
                           [](const ClientTally &a, const ClientTally &b) { return a.count > b.count; }));
}

void TestNetworkStats::summarisesRoundTrips()
{
    NodeCatalog catalog(200);
    std::vector<NodeSpec> specs;
    for (int i = 1; i <= 100; ++i) {
        specs.push_back({QStringLiteral("192.0.2.%1").arg(i), 6881, State::Responsive, "", i * 10,
                         bep42::Status::Unknown});
    }
    specs.push_back({"192.0.2.200", 6881, State::Responsive, "", 9000, bep42::Status::Unknown});  // beyond the scale
    specs.push_back({"192.0.2.201", 6881, State::Responsive, "", -1, bep42::Status::Unknown});    // never timed
    fill(catalog, specs);

    const NetworkStats s = computeNetworkStats(catalog, nowMs()).all;
    QCOMPARE(s.answeringIps, 102);
    QCOMPARE(s.rttWeight, 101.0);
    QCOMPARE(s.rttMedianMs, 510);
    QCOMPARE(s.rttP90Ms, 910);
    QCOMPARE(s.rttP99Ms, 1000);
    QVERIFY(qAbs(s.rttMeanMs - (50500.0 + 9000.0) / 101) < 0.01);
    QCOMPARE(int(s.rttHistogram.size()), NetworkStats::MaxRttMs + 1);
    QCOMPARE(s.rttHistogram[NetworkStats::MaxRttMs], 1.0);  // the slow one, clamped
    QCOMPARE(s.rttHistogram[500], 1.0);
}

void TestNetworkStats::countsBep42AndPorts()
{
    NodeCatalog catalog(100);
    fill(catalog, {
        {"203.0.113.1", 6881, State::Responsive, "", 1, bep42::Status::Compliant},
        {"203.0.113.2", 6881, State::Responsive, "", 1, bep42::Status::Compliant},
        {"203.0.113.3", 51413, State::Responsive, "", 1, bep42::Status::NonCompliant},
        {"203.0.113.4", 51413, State::Responsive, "", 1, bep42::Status::Exempt},
        {"203.0.113.5", 51413, State::Responsive, "", 1, bep42::Status::Unknown},
        {"203.0.113.6", 40000, State::Responsive, "", 1, bep42::Status::Compliant},
    });
    const NetworkStats s = computeNetworkStats(catalog, nowMs(), 2).all;
    QCOMPARE(s.bep42[int(bep42::Status::Compliant)], 3.0);
    QCOMPARE(s.bep42[int(bep42::Status::NonCompliant)], 1.0);
    QCOMPARE(s.bep42[int(bep42::Status::Exempt)], 1.0);
    QCOMPARE(s.bep42[int(bep42::Status::Unknown)], 1.0);

    QCOMPARE(s.distinctPorts, 3);
    QCOMPARE(s.defaultPortCount, 2.0);
    QCOMPARE(int(s.topPorts.size()), 2);  // only as many as asked for
    QCOMPARE(s.topPorts[0], std::make_pair(quint16(51413), 3.0));
    QCOMPARE(s.topPorts[1], std::make_pair(quint16(6881), 2.0));
}

void TestNetworkStats::quantilesFromHistogram()
{
    std::vector<double> h(10, 0.0);
    QCOMPARE(histogramQuantile(h, 0, 0.5), -1);
    h[2] = 1;
    h[7] = 3;
    QCOMPARE(histogramQuantile(h, 4, 0.0), 2);
    QCOMPARE(histogramQuantile(h, 4, 0.25), 2);
    QCOMPARE(histogramQuantile(h, 4, 0.26), 7);
    QCOMPARE(histogramQuantile(h, 4, 1.0), 7);

    // Fractional weights that add up to whole numbers.
    std::vector<double> thirds(5, 0.0);
    thirds[1] = 1.0 / 3;
    thirds[2] = 1.0 / 3;
    thirds[3] = 1.0 / 3;
    QCOMPARE(histogramQuantile(thirds, 1.0, 1.0), 3);
    QCOMPARE(histogramQuantile(thirds, 1.0, 0.5), 2);
}

void TestNetworkStats::estimatesSizeOfSimulatedNetworks_data()
{
    QTest::addColumn<double>("size");
    QTest::newRow("ten thousand") << 1e4;
    QTest::newRow("one million") << 1e6;
    QTest::newRow("twenty million") << 2e7;
}

// The median of many per-lookup estimates lands close to the true size.
void TestNetworkStats::estimatesSizeOfSimulatedNetworks()
{
    QFETCH(double, size);
    std::mt19937_64 rng(7);
    SizeEstimator estimator;
    for (int i = 0; i < SizeEstimator::MaxSamples; ++i) {
        const NodeId target = NodeId::random();
        estimator.add(estimateNetworkSize(target, simulatedClosest(target, size, 8, rng)));
    }
    const SizeEstimate e = estimator.summary();
    QCOMPARE(e.samples, SizeEstimator::MaxSamples);
    QVERIFY2(qAbs(e.median / size - 1) < 0.1, qPrintable(QStringLiteral("median %1 for %2").arg(e.median).arg(size)));
    QVERIFY(e.low <= e.median && e.median <= e.high);
    QVERIFY2(e.low < size * 1.05 && e.high > size * 0.95,
             qPrintable(QStringLiteral("interval %1..%2 for %3").arg(e.low).arg(e.high).arg(size)));
}

// The same with real IDs and exact nearest neighbours.
void TestNetworkStats::estimatesSizeFromRealIds()
{
    constexpr int N = 20000;
    std::vector<NodeId> ids(N);
    for (NodeId &id : ids)
        id = NodeId::random();

    SizeEstimator estimator;
    for (int s = 0; s < 150; ++s) {
        const NodeId target = NodeId::random();
        std::vector<NodeId> closest(8);
        std::partial_sort_copy(ids.begin(), ids.end(), closest.begin(), closest.end(),
                               [&](const NodeId &a, const NodeId &b) { return NodeId::closer(target, a, b); });
        estimator.add(estimateNetworkSize(target, closest));
    }
    const double median = estimator.summary().median;
    QVERIFY2(qAbs(median / N - 1) < 0.15, qPrintable(QString::number(median)));

    // Too few nodes to say anything.
    QCOMPARE(estimateNetworkSize(NodeId::random(), {NodeId::random(), NodeId::random()}), 0.0);
}

void TestNetworkStats::summarisesEstimates()
{
    SizeEstimator estimator;
    QCOMPARE(estimator.summary().samples, 0);
    for (double v : {5.0, 1.0, 3.0})
        estimator.add(v);
    estimator.add(0);                           // ignored
    estimator.add(std::nan(""));                // ignored
    SizeEstimate e = estimator.summary();
    QCOMPARE(e.samples, 3);
    QCOMPARE(e.median, 3.0);
    QCOMPARE(e.low, 1.0);
    QCOMPARE(e.high, 5.0);

    estimator.add(7.0);
    QCOMPARE(estimator.summary().median, 4.0);

    for (int i = 0; i < SizeEstimator::MaxSamples + 50; ++i)
        estimator.add(100.0);
    e = estimator.summary();
    QCOMPARE(e.samples, SizeEstimator::MaxSamples);  // oldest dropped
    QCOMPARE(e.median, 100.0);
}

void TestNetworkStats::emptyCatalogue()
{
    NodeCatalog catalog(10);
    const NetworkStatsSet set = computeNetworkStats(catalog, nowMs());
    QCOMPARE(set.all.answeringIps, 0);
    QCOMPARE(set.all.heardIps, 0);
    QVERIFY(set.all.clients.empty());
    QCOMPARE(set.all.rttMedianMs, -1);
    QCOMPARE(set.all.distinctPorts, 0);
}

// One address running four nodes counts as one address, split evenly
// between what its nodes report; unroutable addresses are counted apart.
void TestNetworkStats::countsAddressesNotNodes()
{
    NodeCatalog catalog(100);
    fill(catalog, {
        {"203.0.113.9", 6881, State::Responsive, "LT\x02\x0b", 100, bep42::Status::Compliant},
        {"203.0.113.9", 6882, State::Responsive, "LT\x02\x0b", 200, bep42::Status::Compliant},
        {"203.0.113.9", 6883, State::Responsive, "UT\x01\x02", 300, bep42::Status::NonCompliant},
        {"203.0.113.9", 6884, State::Responsive, "", 400, bep42::Status::NonCompliant},
        {"203.0.113.9", 6885, State::Silent, "", -1, bep42::Status::Unknown},
        {"198.51.100.1", 6881, State::Responsive, "LT\x02\x0b", 50, bep42::Status::Compliant},
        {"198.51.100.2", 6881, State::Silent, "", -1, bep42::Status::Unknown},
        {"198.51.100.3", 6881, State::Gone, "", -1, bep42::Status::Unknown},
        {"198.51.100.4", 6881, State::New, "", -1, bep42::Status::Unknown},
        {"198.51.100.4", 6882, State::New, "", -1, bep42::Status::Unknown},
        {"10.0.0.1", 6881, State::Unroutable, "", -1, bep42::Status::Unknown},
        {"10.0.0.1", 6882, State::Unroutable, "", -1, bep42::Status::Unknown},
    });
    // The gone node answered once.
    catalog.at(catalog.find(Endpoint(QHostAddress(QStringLiteral("198.51.100.3")), 6881))).lastAnswered = 5;
    for (quint16 port = 6881; port <= 6884; ++port)
        catalog.at(catalog.find(Endpoint(QHostAddress(QStringLiteral("203.0.113.9")), port))).lastAnswered = 5;
    catalog.at(catalog.find(Endpoint(QHostAddress(QStringLiteral("198.51.100.1")), 6881))).lastAnswered = 5;

    const NetworkStats s = computeNetworkStats(catalog, nowMs()).ipv4;
    QCOMPARE(s.heardIps, 5);        // .9, .1, .2, .3, .4
    QCOMPARE(s.connectedIps, 3);    // .9, .1, .3
    QCOMPARE(s.answeringIps, 2);    // .9, .1
    QCOMPARE(s.unroutableIps, 1);
    QCOMPARE(s.answeringNodes, 5);
    QCOMPARE(s.multiNodeIps, 1);
    QCOMPARE(s.maxNodesPerIp, 4);
    QCOMPARE(s.nodesPerIp(), 2.5);

    // .9 is one address: half libtorrent, a quarter µTorrent, a quarter none.
    QCOMPARE(findTally(s.clients, QStringLiteral("libtorrent (Rasterbar)"))->count, 1.5);
    QCOMPARE(findTally(s.clients, QString(QChar(0x00b5)) + QStringLiteral("Torrent"))->count, 0.25);
    QCOMPARE(findTally(s.clients, QStringLiteral("no version sent"))->count, 0.25);
    QCOMPARE(s.bep42[int(bep42::Status::Compliant)], 1.5);
    QCOMPARE(s.bep42[int(bep42::Status::NonCompliant)], 0.5);
    QCOMPARE(s.rttWeight, 2.0);
    QCOMPARE(s.rttMeanMs, (0.25 * (100 + 200 + 300 + 400) + 50) / 2.0);
    QCOMPARE(s.distinctPorts, 4);
    QCOMPARE(s.defaultPortCount, 1.25);
}

int runTestNetworkStats(int argc, char **argv)
{
    TestNetworkStats test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestNetworkStats.moc"
