#include "dhtcore/Inbound.h"

#include <QTest>

#include <algorithm>

using namespace dht;

namespace {

krpc::Message query(const QByteArray &method, const QByteArray &version = {}, bool readOnly = false)
{
    krpc::Message m;
    m.type = krpc::MessageType::Query;
    m.method = method;
    m.version = version;
    m.readOnly = readOnly;
    return m;
}

Endpoint at(const char *address, quint16 port = 6881)
{
    return Endpoint(QHostAddress(QString::fromLatin1(address)), port);
}

qint64 methodCount(const InboundSummary &s, const QString &name)
{
    const auto it = std::find_if(s.methods.begin(), s.methods.end(), [&](const auto &m) { return m.first == name; });
    return it == s.methods.end() ? 0 : it->second;
}

double clientCount(const InboundSummary &s, const QString &name)
{
    const auto it = std::find_if(s.clients.begin(), s.clients.end(), [&](const ClientTally &t) { return t.name == name; });
    return it == s.clients.end() ? 0 : it->count;
}

} // namespace

class TestInbound : public QObject
{
    Q_OBJECT

private slots:
    void countsByAddress();
    void namesOnlyKnownMethods();
    void stopsTrackingAtTheCap();
};

void TestInbound::countsByAddress()
{
    InboundTally tally;
    QCOMPARE(tally.summary().queries, 0);

    // One address, two ports, one of them read-only: one read-only address.
    tally.record(at("203.0.113.1", 1000), query("ping", "LT\x02\x0b"));
    tally.record(at("203.0.113.1", 1001), query("find_node", "LT\x02\x0b", true));
    tally.record(at("203.0.113.2"), query("get_peers", "UT\x01\x02"));
    tally.record(at("203.0.113.2"), query("get_peers"));  // no version: keeps the one seen
    tally.record(at("2001:db8::1"), query("ping"));
    tally.record(at("::ffff:203.0.113.2"), query("ping"));  // the same host as .2

    const InboundSummary s = tally.summary();
    QCOMPARE(s.queries, 6);
    QCOMPARE(s.addresses, 3);
    QCOMPARE(s.ipv4Addresses, 2);
    QCOMPARE(s.ipv6Addresses, 1);
    QCOMPARE(s.readOnlyAddresses, 1);
    QCOMPARE(s.untrackedQueries, 0);
    QCOMPARE(tally.trackedAddresses(), 3);

    QCOMPARE(clientCount(s, QStringLiteral("libtorrent (Rasterbar)")), 1.0);
    QCOMPARE(clientCount(s, QString(QChar(0x00b5)) + QStringLiteral("Torrent")), 1.0);
    QCOMPARE(clientCount(s, QStringLiteral("no version sent")), 1.0);
    QCOMPARE(s.clients.front().count, 1.0);

    QCOMPARE(methodCount(s, QStringLiteral("ping")), 3);
    QCOMPARE(methodCount(s, QStringLiteral("get_peers")), 2);
    QCOMPARE(methodCount(s, QStringLiteral("find_node")), 1);
    QCOMPARE(s.methods.front().first, QStringLiteral("ping"));  // most first

    tally.clear();
    QCOMPARE(tally.summary().queries, 0);
    QCOMPARE(tally.summary().addresses, 0);
    QVERIFY(tally.summary().methods.empty());
}

void TestInbound::namesOnlyKnownMethods()
{
    InboundTally tally;
    for (const char *method : {"ping", "find_node", "get_peers", "announce_peer", "get", "put", "sample_infohashes"})
        tally.record(at("198.51.100.1"), query(method));
    tally.record(at("198.51.100.1"), query("vote"));
    tally.record(at("198.51.100.1"), query("\xff\xfe junk"));
    const InboundSummary s = tally.summary();
    QCOMPARE(int(s.methods.size()), 8);
    QCOMPARE(methodCount(s, QStringLiteral("other")), 2);
    QCOMPARE(methodCount(s, QStringLiteral("sample_infohashes")), 1);
}

void TestInbound::stopsTrackingAtTheCap()
{
    InboundTally tally;
    for (int i = 0; i < InboundTally::MaxAddresses; ++i) {
        const quint32 a = 0x0b000000u + quint32(i);
        tally.record(Endpoint(QHostAddress(a), 6881), query("ping"));
    }
    QCOMPARE(tally.trackedAddresses(), InboundTally::MaxAddresses);
    tally.record(at("203.0.113.1"), query("ping", {}, true));
    tally.record(Endpoint(QHostAddress(quint32(0x0b000000u)), 6881), query("ping", {}, true));  // already known

    const InboundSummary s = tally.summary();
    QCOMPARE(s.addresses, InboundTally::MaxAddresses);
    QCOMPARE(s.untrackedQueries, 1);
    QCOMPARE(s.queries, qint64(InboundTally::MaxAddresses) + 2);
    QCOMPARE(s.readOnlyAddresses, 1);
}

int runTestInbound(int argc, char **argv)
{
    TestInbound test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestInbound.moc"
