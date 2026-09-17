#include "dhtcore/NodeList.h"
#include "dhtcore/Support.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace dht;

namespace {

using State = CatalogEntry::State;

struct Spec
{
    QString address;
    quint16 port;
    State state;
    QByteArray version;
    int rttMs;
    bep42::Status bep42;
};

void fill(NodeCatalog &catalog, const std::vector<Spec> &specs, qint64 now)
{
    for (const Spec &n : specs) {
        const auto slot = catalog.upsert(Endpoint(QHostAddress(n.address), n.port), now);
        catalog.setState(slot, n.state);
        CatalogEntry &e = catalog.at(slot);
        e.id = NodeId::random();
        e.setVersion(n.version);
        e.rttMs = n.rttMs < 0 ? CatalogEntry::NoRtt : quint16(n.rttMs);
        e.bep42 = quint8(n.bep42);
        if (n.state == State::Responsive || n.state == State::Gone)
            e.lastAnswered = catalog.stamp(now);
    }
}

// A small network covering every filter.
const std::vector<Spec> &network()
{
    static const std::vector<Spec> specs = {
        {"203.0.113.1", 6881, State::Responsive, "LT\x02\x0b", 120, bep42::Status::Compliant},
        {"203.0.113.2", 6881, State::Responsive, "LT\x01\x2f", 40, bep42::Status::NonCompliant},
        {"203.0.113.3", 51413, State::Gone, "UT\x01\x02", 300, bep42::Status::Compliant},
        {"203.0.113.4", 6881, State::Silent, "", -1, bep42::Status::Unknown},
        {"198.51.100.9", 7000, State::Responsive, "LT\x02\x0b", 80, bep42::Status::Compliant},
        {"198.51.100.9", 7001, State::Responsive, "", 90, bep42::Status::NonCompliant},
        {"198.51.100.9", 7002, State::New, "", -1, bep42::Status::Unknown},
        {"10.0.0.1", 6881, State::Unroutable, "", -1, bep42::Status::Unknown},
        {"2001:db8::1", 6881, State::Responsive, "LT\x01\x01", 200, bep42::Status::Compliant},
        {"2001:db8:1::2", 6882, State::Silent, "", -1, bep42::Status::Unknown},
    };
    return specs;
}

QStringList addresses(const NodeListPage &page)
{
    QStringList out;
    for (const NodeListRow &row : page.rows)
        out << row.endpoint.toString();
    return out;
}

} // namespace

class TestNodeList : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void parsesAddressFilters();
    void matchesSubnets();
    void filtersByFamilyStateAndPort();
    void filtersByAddress();
    void filtersByClientAndVersion();
    void filtersByBep42AndRoundTrip();
    void filtersByNodesAtAddress();
    void sorts();
    void pages();
    void fillsRows();
    void exportsCsv();

private:
    NodeCatalog m_catalog{1000};
    ClientLabelCache m_labels;
    qint64 m_now = 0;

    NodeListPage run(const NodeQuery &q) { return queryNodes(m_catalog, q, m_now, m_labels); }
};

void TestNodeList::init()
{
    m_catalog.clear();
    m_now = nowMs();
    fill(m_catalog, network(), m_now);
}

void TestNodeList::parsesAddressFilters()
{
    QHostAddress address;
    int bits = 0;
    QString error;

    QVERIFY(parseAddressFilter(QString(), &address, &bits));
    QVERIFY(address.isNull());
    QCOMPARE(bits, -1);

    QVERIFY(parseAddressFilter(QStringLiteral(" 203.0.113.7 "), &address, &bits));
    QCOMPARE(address, QHostAddress(QStringLiteral("203.0.113.7")));
    QCOMPARE(bits, 32);

    QVERIFY(parseAddressFilter(QStringLiteral("203.0.113.0/24"), &address, &bits));
    QCOMPARE(bits, 24);

    QVERIFY(parseAddressFilter(QStringLiteral("2001:db8::/32"), &address, &bits));
    QCOMPARE(address.protocol(), QAbstractSocket::IPv6Protocol);
    QCOMPARE(bits, 32);

    // The mapped spelling is the IPv4 address.
    QVERIFY(parseAddressFilter(QStringLiteral("::ffff:203.0.113.7"), &address, &bits));
    QCOMPARE(address.protocol(), QAbstractSocket::IPv4Protocol);
    QCOMPARE(bits, 32);

    QVERIFY(!parseAddressFilter(QStringLiteral("example.org"), &address, &bits, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!parseAddressFilter(QStringLiteral("300.1.2.0/24"), &address, &bits));
}

void TestNodeList::matchesSubnets()
{
    const auto key = [](const char *text) {
        NodeCatalog c(1);
        const auto slot = c.upsert(Endpoint(QHostAddress(QString::fromLatin1(text)), 1), 0);
        return c.at(slot).address;
    };
    const QHostAddress v4net(QStringLiteral("203.0.113.0"));
    QVERIFY(addressInSubnet(key("203.0.113.200"), v4net, 24));
    QVERIFY(!addressInSubnet(key("203.0.114.1"), v4net, 24));
    QVERIFY(addressInSubnet(key("203.0.114.1"), v4net, 16));
    QVERIFY(addressInSubnet(key("203.0.113.128"), QHostAddress(QStringLiteral("203.0.113.128")), 25));
    QVERIFY(!addressInSubnet(key("203.0.113.127"), QHostAddress(QStringLiteral("203.0.113.128")), 25));
    // /0 of IPv4 is every IPv4 address, and no IPv6 one.
    QVERIFY(addressInSubnet(key("8.8.8.8"), v4net, 0));
    QVERIFY(!addressInSubnet(key("2001:db8::1"), v4net, 0));

    const QHostAddress v6net(QStringLiteral("2001:db8::"));
    QVERIFY(addressInSubnet(key("2001:db8:ffff::1"), v6net, 32));
    QVERIFY(!addressInSubnet(key("2001:db9::1"), v6net, 32));
    QVERIFY(!addressInSubnet(key("203.0.113.1"), v6net, 32));
    // No filter.
    QVERIFY(addressInSubnet(key("203.0.113.1"), QHostAddress(), -1));
}

void TestNodeList::filtersByFamilyStateAndPort()
{
    NodeQuery q;
    QCOMPARE(run(q).matchedNodes, 10);
    QCOMPARE(run(q).matchedAddresses, 8);

    q.family = Family::IPv6;
    QCOMPARE(run(q).matchedNodes, 2);
    q.family = Family::IPv4;
    QCOMPARE(run(q).matchedNodes, 8);

    q.family.reset();
    q.states = 1u << int(State::Responsive);
    QCOMPARE(run(q).matchedNodes, 5);
    QCOMPARE(run(q).matchedAddresses, 4);
    q.states = (1u << int(State::Silent)) | (1u << int(State::Gone));
    QCOMPARE(run(q).matchedNodes, 3);

    q.states = NodeQuery::AllStates;
    q.port = 6881;
    QCOMPARE(run(q).matchedNodes, 5);
}

void TestNodeList::filtersByAddress()
{
    NodeQuery q;
    QVERIFY(parseAddressFilter(QStringLiteral("203.0.113.0/30"), &q.subnet, &q.subnetBits));
    QCOMPARE(addresses(run(q)), QStringList({"203.0.113.1:6881", "203.0.113.2:6881", "203.0.113.3:51413"}));

    QVERIFY(parseAddressFilter(QStringLiteral("198.51.100.9"), &q.subnet, &q.subnetBits));
    QCOMPARE(run(q).matchedNodes, 3);
    QCOMPARE(run(q).matchedAddresses, 1);

    QVERIFY(parseAddressFilter(QStringLiteral("2001:db8::/48"), &q.subnet, &q.subnetBits));
    QCOMPARE(addresses(run(q)), QStringList({"[2001:db8::1]:6881"}));
}

void TestNodeList::filtersByClientAndVersion()
{
    NodeQuery q;
    q.client = QStringLiteral("libtorrent (Rasterbar)");
    QCOMPARE(run(q).matchedNodes, 4);  // two 2.0.11, one 1.2.15, one 1.1

    q.version = QStringLiteral("2.0.11");
    QCOMPARE(addresses(run(q)), QStringList({"198.51.100.9:7000", "203.0.113.1:6881"}));

    // Only nodes that answered say what they run, so "no version sent"
    // excludes the silent and unasked ones.
    q.client = QStringLiteral("no version sent");
    q.version.clear();
    QCOMPARE(addresses(run(q)), QStringList({"198.51.100.9:7001"}));

    q.client = QString(QChar(0x00b5)) + QStringLiteral("Torrent");
    q.version = QStringLiteral("bytes 01 02");
    QCOMPARE(addresses(run(q)), QStringList({"203.0.113.3:51413"}));
}

void TestNodeList::filtersByBep42AndRoundTrip()
{
    NodeQuery q;
    q.bep42 = bep42::Status::Compliant;
    QCOMPARE(run(q).matchedNodes, 4);
    q.bep42 = bep42::Status::Unknown;
    QCOMPARE(run(q).matchedNodes, 0);  // unknown only among nodes that answered

    q.bep42.reset();
    q.minRttMs = 80;
    q.maxRttMs = 150;
    QCOMPARE(addresses(run(q)), QStringList({"198.51.100.9:7000", "198.51.100.9:7001", "203.0.113.1:6881"}));
    q.minRttMs = -1;
    q.maxRttMs = 50;
    QCOMPARE(addresses(run(q)), QStringList({"203.0.113.2:6881"}));
}

void TestNodeList::filtersByNodesAtAddress()
{
    NodeQuery q;
    q.minNodesAtAddress = 2;
    const NodeListPage page = run(q);
    QCOMPARE(page.matchedNodes, 3);
    QCOMPARE(page.matchedAddresses, 1);
    for (const NodeListRow &row : page.rows)
        QCOMPARE(row.nodesAtAddress, 3);
    q.minNodesAtAddress = 4;
    QCOMPARE(run(q).matchedNodes, 0);
}

void TestNodeList::sorts()
{
    NodeQuery q;
    q.states = 1u << int(State::Responsive);

    q.sort = NodeQuery::Sort::RoundTrip;
    QCOMPARE(addresses(run(q)), QStringList({"203.0.113.2:6881", "198.51.100.9:7000", "198.51.100.9:7001",
                                             "203.0.113.1:6881", "[2001:db8::1]:6881"}));
    q.descending = true;
    QCOMPARE(addresses(run(q)).front(), QStringLiteral("[2001:db8::1]:6881"));

    // Nodes without a value go last, whichever way.
    q.states = NodeQuery::AllStates;
    QCOMPARE(run(q).rows.back().rttMs, -1);
    q.descending = false;
    QCOMPARE(run(q).rows.back().rttMs, -1);
    QCOMPARE(run(q).rows.front().rttMs, 40);

    q.sort = NodeQuery::Sort::NodesAtAddress;
    q.descending = true;
    QCOMPARE(run(q).rows.front().nodesAtAddress, 3);

    q.sort = NodeQuery::Sort::Client;
    q.descending = false;
    const NodeListPage page = run(q);
    QCOMPARE(page.rows.front().client, QStringLiteral("libtorrent (Rasterbar)"));
    QCOMPARE(page.rows.front().version, QStringLiteral("1.1"));
    QVERIFY(!page.rows.back().answered);

    // Address order: IPv4 (mapped) before IPv6, then by port.
    q.sort = NodeQuery::Sort::Address;
    const QStringList all = addresses(run(q));
    QCOMPARE(all.front(), QStringLiteral("10.0.0.1:6881"));
    QVERIFY(all.indexOf(QStringLiteral("198.51.100.9:7000")) < all.indexOf(QStringLiteral("198.51.100.9:7001")));
    QCOMPARE(all.back(), QStringLiteral("[2001:db8:1::2]:6882"));
}

void TestNodeList::pages()
{
    NodeQuery q;
    q.limit = 4;
    NodeListPage page = run(q);
    QCOMPARE(page.offset, 0);
    QCOMPARE(int(page.rows.size()), 4);
    const QStringList first = addresses(page);

    q.offset = 4;
    page = run(q);
    QCOMPARE(int(page.rows.size()), 4);
    for (const QString &a : addresses(page))
        QVERIFY(!first.contains(a));

    q.offset = 8;
    QCOMPARE(int(run(q).rows.size()), 2);

    // Past the end: the last page.
    q.offset = 500;
    page = run(q);
    QCOMPARE(page.offset, 8);
    QCOMPARE(int(page.rows.size()), 2);

    // Nothing matching: an empty first page.
    q.port = 1;
    page = run(q);
    QCOMPARE(page.matchedNodes, 0);
    QCOMPARE(page.offset, 0);
    QVERIFY(page.rows.empty());
}

void TestNodeList::fillsRows()
{
    NodeQuery q;
    QVERIFY(parseAddressFilter(QStringLiteral("203.0.113.1"), &q.subnet, &q.subnetBits));
    NodeListPage page = run(q);
    QCOMPARE(int(page.rows.size()), 1);
    const NodeListRow &row = page.rows.front();
    QCOMPARE(row.state, State::Responsive);
    QVERIFY(row.answered);
    QCOMPARE(row.client, QStringLiteral("libtorrent (Rasterbar)"));
    QCOMPARE(row.version, QStringLiteral("2.0.11"));
    QCOMPARE(row.clientKind, QStringLiteral("known"));
    QCOMPARE(row.rawVersion, QByteArray("LT\x02\x0b"));
    QCOMPARE(row.rttMs, 120);
    QCOMPARE(row.bep42, bep42::Status::Compliant);
    QCOMPARE(row.nodesAtAddress, 1);
    QVERIFY(row.firstSeenAgoMs >= 0);
    QVERIFY(row.lastAnsweredAgoMs >= 0);
    QCOMPARE(row.lastQueriedAgoMs, qint64(-1));

    // A silent node says nothing about its software.
    QVERIFY(parseAddressFilter(QStringLiteral("203.0.113.4"), &q.subnet, &q.subnetBits));
    const NodeListRow silent = run(q).rows.front();
    QVERIFY(!silent.answered);
    QVERIFY(silent.client.isEmpty());
    QCOMPARE(silent.lastAnsweredAgoMs, qint64(-1));
    QCOMPARE(silent.bep42, bep42::Status::Unknown);
}

void TestNodeList::exportsCsv()
{
    // A client name with a comma in it must be quoted.
    fill(m_catalog, {{"192.0.2.50", 6881, State::Responsive, "LT\x01", 10, bep42::Status::Compliant}}, m_now);

    NodeQuery q;
    q.states = 1u << int(State::Responsive);
    q.sort = NodeQuery::Sort::RoundTrip;
    const NodeExport nodes = collectNodes(m_catalog, q, m_now, m_labels);
    QCOMPARE(int(nodes.entries.size()), 6);
    QCOMPARE(int(nodes.nodesAtAddress.size()), 6);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("nodes.csv"));
    QString error;
    QVERIFY2(writeNodesCsv(nodes, path, &error), qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = file.readAll().split('\n');
    QCOMPARE(lines.size(), 1 + 6 + 1);  // header, rows, trailing newline
    QVERIFY(lines[0].startsWith("address,port,family,state,answered,client,version"));
    QVERIFY(lines.last().isEmpty());

    // Sorted by round trip: the 10 ms node first.
    const QByteArray first = lines[1];
    QVERIFY2(first.startsWith("192.0.2.50,6881,ipv4,responsive,yes,\"libtorrent (Rasterbar), nonstandard 3-byte field\","),
             first.constData());
    QVERIFY(first.contains(",4c5401,10,compliant,"));
    QVERIFY(lines[2].startsWith("203.0.113.2,6881,ipv4,responsive,yes,libtorrent (Rasterbar),1.2.15,known,4c54012f,40,"));
    QVERIFY(lines[3].contains(",3,"));  // 198.51.100.9 has three nodes

    // A bad path fails with a reason.
    QVERIFY(!writeNodesCsv(nodes, dir.filePath(QStringLiteral("missing/dir/nodes.csv")), &error));
    QVERIFY(!error.isEmpty());
}

int runTestNodeList(int argc, char **argv)
{
    TestNodeList test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestNodeList.moc"
