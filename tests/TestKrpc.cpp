#include "dhtcore/Endpoint.h"
#include "dhtcore/Krpc.h"

#include <QTest>

using namespace dht;

class TestKrpc : public QObject
{
    Q_OBJECT

private slots:
    void parsesSpecPing();
    void queryRoundTrip();
    void responseCarriesRequesterAddress();
    void errorRoundTrip();
    void rejectsMalformed_data();
    void rejectsMalformed();
    void compactNodesRoundTrip();
    void compactNodesFlagBadLength();
    void decodePeersFiltersFamily();
    void endpointCompactRoundTrip();
    void mappedAddressesNormalize();
    void parseHostPort_data();
    void parseHostPort();
    void usableRemote();
    void escapesBytes();
    void describesAResponseInFull();
    void readOnlyQueriesCarryTheFlag();
};

void TestKrpc::parsesSpecPing()
{
    // The ping query example from BEP 5.
    const auto r = krpc::parse("d1:ad2:id20:abcdefghij0123456789e1:q4:ping1:t2:aa1:y1:qe");
    QVERIFY2(r.message, qPrintable(r.error));
    QCOMPARE(r.message->type, krpc::MessageType::Query);
    QCOMPARE(r.message->method, QByteArray("ping"));
    QCOMPARE(r.message->transactionId, QByteArray("aa"));
    QVERIFY(r.message->senderId());
    QCOMPARE(r.message->senderId()->toBytes(), QByteArray("abcdefghij0123456789"));
}

void TestKrpc::queryRoundTrip()
{
    const NodeId id = NodeId::random();
    BValue::Dict args;
    args.emplace("id", BValue(id.toBytes()));
    const QByteArray version("DI\x00\x01", 4);
    const auto r = krpc::parse(krpc::encodeQuery("xy", "find_node", std::move(args), version));
    QVERIFY(r.message);
    QCOMPARE(r.message->method, QByteArray("find_node"));
    QCOMPARE(r.message->transactionId, QByteArray("xy"));
    QCOMPARE(*r.message->senderId(), id);
    QCOMPARE(r.message->version, version);
    QVERIFY(!r.message->reportedAddress);
}

// BEP 43: "ro" goes in the top-level dictionary of a query, not in "a",
// and only when the sender is read-only.
void TestKrpc::readOnlyQueriesCarryTheFlag()
{
    BValue::Dict args;
    args.emplace("id", BValue(NodeId::random().toBytes()));

    const QByteArray plain = krpc::encodeQuery("aa", "ping", args, {});
    QVERIFY(!plain.contains("2:roi"));
    const auto parsedPlain = krpc::parse(plain);
    QVERIFY(parsedPlain.message);
    QVERIFY(!parsedPlain.message->readOnly);

    const QByteArray flagged = krpc::encodeQuery("aa", "ping", args, {}, true);
    QVERIFY(flagged.contains("2:roi1e"));
    const auto parsedFlagged = krpc::parse(flagged);
    QVERIFY(parsedFlagged.message);
    QVERIFY(parsedFlagged.message->readOnly);
    // Top level, so the arguments are untouched.
    QVERIFY(!parsedFlagged.message->body.integerAt("ro"));
}

void TestKrpc::responseCarriesRequesterAddress()
{
    const Endpoint requester(QHostAddress(QStringLiteral("198.51.100.7")), 51413);
    BValue::Dict values;
    values.emplace("id", BValue(NodeId::random().toBytes()));
    const auto r = krpc::parse(krpc::encodeResponse("t1", std::move(values), {}, requester));
    QVERIFY(r.message);
    QCOMPARE(r.message->type, krpc::MessageType::Response);
    QVERIFY(r.message->reportedAddress);
    QCOMPARE(*r.message->reportedAddress, requester);
}

void TestKrpc::errorRoundTrip()
{
    const auto r = krpc::parse(krpc::encodeError("e1", krpc::ProtocolError, "invalid token", {}));
    QVERIFY(r.message);
    QCOMPARE(r.message->type, krpc::MessageType::Error);
    QCOMPARE(r.message->errorCode, qint64(203));
    QCOMPARE(r.message->errorMessage, QByteArray("invalid token"));
}

void TestKrpc::rejectsMalformed_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<bool>("tidRecovered");
    QTest::newRow("not bencode") << QByteArray("hello") << false;
    QTest::newRow("not a dict") << QByteArray("i1e") << false;
    QTest::newRow("no t") << QByteArray("d1:y1:qe") << false;
    QTest::newRow("no y") << QByteArray("d1:t2:aae") << true;
    QTest::newRow("unknown y") << QByteArray("d1:t2:aa1:y1:ze") << true;
    QTest::newRow("query without a") << QByteArray("d1:q4:ping1:t2:aa1:y1:qe") << true;
    QTest::newRow("query without q") << QByteArray("d1:ade1:t2:aa1:y1:qe") << true;
    QTest::newRow("response without r") << QByteArray("d1:t2:aa1:y1:re") << true;
}

void TestKrpc::rejectsMalformed()
{
    QFETCH(QByteArray, input);
    QFETCH(bool, tidRecovered);
    const auto r = krpc::parse(input);
    QVERIFY(!r.message);
    QVERIFY(!r.error.isEmpty());
    QCOMPARE(!r.transactionId.isEmpty(), tidRecovered);
}

void TestKrpc::compactNodesRoundTrip()
{
    std::vector<krpc::CompactNode> nodes = {
        {NodeId::random(), Endpoint(QHostAddress(QStringLiteral("203.0.113.1")), 6881)},
        {NodeId::random(), Endpoint(QHostAddress(QStringLiteral("2001:db8::5")), 6882)},
        {NodeId::random(), Endpoint(QHostAddress(QStringLiteral("198.51.100.2")), 1)},
    };

    const QByteArray v4 = krpc::encodeNodes(nodes, Family::IPv4);
    QCOMPARE(v4.size(), 2 * 26);
    const auto decoded4 = krpc::decodeNodes(v4, Family::IPv4);
    QVERIFY(!decoded4.malformed);
    QCOMPARE(int(decoded4.nodes.size()), 2);
    QCOMPARE(decoded4.nodes[0].id, nodes[0].id);
    QCOMPARE(decoded4.nodes[0].endpoint, nodes[0].endpoint);
    QCOMPARE(decoded4.nodes[1].endpoint, nodes[2].endpoint);

    const QByteArray v6 = krpc::encodeNodes(nodes, Family::IPv6);
    QCOMPARE(v6.size(), 38);
    const auto decoded6 = krpc::decodeNodes(v6, Family::IPv6);
    QCOMPARE(int(decoded6.nodes.size()), 1);
    QCOMPARE(decoded6.nodes[0].endpoint, nodes[1].endpoint);
}

void TestKrpc::compactNodesFlagBadLength()
{
    std::vector<krpc::CompactNode> nodes = {
        {NodeId::random(), Endpoint(QHostAddress(QStringLiteral("203.0.113.1")), 6881)},
    };
    const QByteArray data = krpc::encodeNodes(nodes, Family::IPv4) + "xyz";
    const auto decoded = krpc::decodeNodes(data, Family::IPv4);
    QVERIFY(decoded.malformed);
    QCOMPARE(int(decoded.nodes.size()), 1);
}

void TestKrpc::decodePeersFiltersFamily()
{
    const Endpoint a(QHostAddress(QStringLiteral("203.0.113.1")), 1000);
    const Endpoint b(QHostAddress(QStringLiteral("2001:db8::1")), 2000);
    BValue values(BValue::List{BValue(a.toCompact()), BValue(b.toCompact()), BValue("junk"), BValue(7)});

    const auto v4 = krpc::decodePeers(&values, Family::IPv4);
    QCOMPARE(int(v4.size()), 1);
    QCOMPARE(v4[0], a);

    const auto v6 = krpc::decodePeers(&values, Family::IPv6);
    QCOMPARE(int(v6.size()), 1);
    QCOMPARE(v6[0], b);

    QVERIFY(krpc::decodePeers(nullptr, Family::IPv4).empty());
}

void TestKrpc::endpointCompactRoundTrip()
{
    const Endpoint v4(QHostAddress(QStringLiteral("1.2.3.4")), 0xABCD);
    QCOMPARE(v4.toCompact(), QByteArray("\x01\x02\x03\x04\xAB\xCD", 6));
    QCOMPARE(*Endpoint::fromCompact(v4.toCompact()), v4);

    const Endpoint v6(QHostAddress(QStringLiteral("2001:db8::ff")), 6881);
    QCOMPARE(v6.toCompact().size(), 18);
    QCOMPARE(*Endpoint::fromCompact(v6.toCompact()), v6);
    QCOMPARE(v6.toString(), QStringLiteral("[2001:db8::ff]:6881"));

    QVERIFY(!Endpoint::fromCompact("12345"));
}

void TestKrpc::mappedAddressesNormalize()
{
    const Endpoint mapped(QHostAddress(QStringLiteral("::ffff:1.2.3.4")), 80);
    const Endpoint plain(QHostAddress(QStringLiteral("1.2.3.4")), 80);
    QCOMPARE(mapped, plain);
    QCOMPARE(qHash(mapped), qHash(plain));
    QCOMPARE(mapped.family(), Family::IPv4);
}

void TestKrpc::parseHostPort_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<QString>("host");
    QTest::addColumn<int>("port");
    QTest::addColumn<bool>("literal");

    QTest::newRow("v4") << "67.215.246.10:6881" << true << "67.215.246.10" << 6881 << true;
    QTest::newRow("v4 padded") << "  1.2.3.4:1  " << true << "1.2.3.4" << 1 << true;
    QTest::newRow("v6") << "[2001:db8::1]:6881" << true << "2001:db8::1" << 6881 << true;
    QTest::newRow("hostname") << "router.bittorrent.com:6881" << true << "router.bittorrent.com" << 6881 << false;
    QTest::newRow("single label") << "localhost:9000" << true << "localhost" << 9000 << false;

    QTest::newRow("no port") << "1.2.3.4" << false << "" << 0 << false;
    QTest::newRow("empty port") << "1.2.3.4:" << false << "" << 0 << false;
    QTest::newRow("port zero") << "1.2.3.4:0" << false << "" << 0 << false;
    QTest::newRow("port too big") << "1.2.3.4:70000" << false << "" << 0 << false;
    QTest::newRow("port not numeric") << "1.2.3.4:abc" << false << "" << 0 << false;
    QTest::newRow("no host") << ":6881" << false << "" << 0 << false;
    QTest::newRow("bare v6") << "2001:db8::1:6881" << false << "" << 0 << false;
    QTest::newRow("v6 no port") << "[2001:db8::1]" << false << "" << 0 << false;
    QTest::newRow("bad v6") << "[zz::1]:6881" << false << "" << 0 << false;
    QTest::newRow("bad octet") << "1.2.3.256:6881" << false << "" << 0 << false;
    QTest::newRow("short v4") << "1.2.3:6881" << false << "" << 0 << false;
    QTest::newRow("unspecified") << "0.0.0.0:6881" << false << "" << 0 << false;
    QTest::newRow("multicast") << "224.0.0.1:6881" << false << "" << 0 << false;
    QTest::newRow("bad hostname") << "bad_host!:6881" << false << "" << 0 << false;
    QTest::newRow("empty") << "" << false << "" << 0 << false;
}

void TestKrpc::parseHostPort()
{
    QFETCH(QString, input);
    QFETCH(bool, valid);
    QFETCH(QString, host);
    QFETCH(int, port);
    QFETCH(bool, literal);

    QString error;
    const auto parsed = dht::parseHostPort(input, &error);
    QCOMPARE(parsed.has_value(), valid);
    if (!valid) {
        QVERIFY(!error.isEmpty());
        return;
    }
    QCOMPARE(parsed->host, host);
    QCOMPARE(int(parsed->port), port);
    QCOMPARE(!parsed->literal.isNull(), literal);
}

void TestKrpc::usableRemote()
{
    const auto ep = [](const char *ip, quint16 port) { return Endpoint(QHostAddress(QString::fromLatin1(ip)), port); };
    QVERIFY(isUsableRemote(ep("8.8.8.8", 6881), false));
    QVERIFY(!isUsableRemote(ep("8.8.8.8", 0), false));
    QVERIFY(!isUsableRemote(ep("224.0.0.1", 6881), false));
    QVERIFY(!isUsableRemote(ep("255.255.255.255", 6881), false));
    QVERIFY(!isUsableRemote(ep("0.1.2.3", 6881), false));
    QVERIFY(!isUsableRemote(ep("10.0.0.1", 6881), false));
    QVERIFY(isUsableRemote(ep("10.0.0.1", 6881), true));
    QVERIFY(!isUsableRemote(ep("127.0.0.1", 6881), false));
    QVERIFY(isUsableRemote(ep("127.0.0.1", 6881), true));

    // Why the others are unusable.
    using P = AddressProblem;
    QCOMPARE(addressProblem(ep("8.8.8.8", 6881), false), P::None);
    QCOMPARE(addressProblem(ep("8.8.8.8", 0), false), P::ZeroPort);
    QCOMPARE(addressProblem(ep("0.0.0.0", 6881), false), P::Unspecified);
    QCOMPARE(addressProblem(ep("0.1.2.3", 6881), false), P::Unspecified);
    QCOMPARE(addressProblem(ep("::", 6881), false), P::Unspecified);
    QCOMPARE(addressProblem(ep("192.168.1.2", 6881), false), P::Local);
    QCOMPARE(addressProblem(ep("192.168.1.2", 0), false), P::Local);  // the address is the bigger problem
    QCOMPARE(addressProblem(ep("fe80::1", 6881), false), P::Local);
    QCOMPARE(addressProblem(ep("fd00::1", 6881), false), P::Local);
    QCOMPARE(addressProblem(ep("::1", 6881), false), P::Local);
    QCOMPARE(addressProblem(ep("::ffff:10.1.2.3", 6881), false), P::Local);
    QCOMPARE(addressProblem(ep("224.0.0.1", 6881), false), P::Multicast);
    QCOMPARE(addressProblem(ep("255.255.255.255", 6881), false), P::Multicast);
    QCOMPARE(addressProblem(ep("ff02::1", 6881), false), P::Multicast);
    for (const char *reserved : {"100.64.0.1", "100.127.255.254", "192.0.0.8", "192.0.2.1", "198.18.0.1",
                                 "198.19.255.1", "198.51.100.1", "203.0.113.1", "240.0.0.1", "2001:db8::1",
                                 "100::1", "4000::1"}) {
        QVERIFY2(addressProblem(ep(reserved, 6881), false) == P::Reserved, reserved);
        QVERIFY2(addressProblem(ep(reserved, 6881), true) == P::Reserved, reserved);
    }
    for (const char *fine : {"100.63.255.255", "100.128.0.1", "198.20.0.1", "2001:470::1", "2a00:1450::1",
                             "3fff:ffff::1"}) {
        QVERIFY2(addressProblem(ep(fine, 6881), false) == P::None, fine);
    }
    QCOMPARE(addressProblem(ep("10.0.0.1", 0), true), P::ZeroPort);
    QCOMPARE(addressProblemName(P::ZeroPort), QStringLiteral("Port 0"));
}

void TestKrpc::escapesBytes()
{
    QCOMPARE(krpc::escapeBytes("plain text"), QStringLiteral("plain text"));
    QCOMPARE(krpc::escapeBytes(QByteArray("a\x00\x1f\x7f", 4)), QStringLiteral("a\\x00\\x1f\\x7f"));
}

void TestKrpc::describesAResponseInFull()
{
    const NodeId id = NodeId::random();
    const Endpoint node(QHostAddress(QStringLiteral("67.215.246.10")), 6881);
    const Endpoint peer(QHostAddress(QStringLiteral("203.0.113.9")), 51413);

    BValue::Dict values;
    values.emplace("id", BValue(id.toBytes()));
    values.emplace("token", BValue(QByteArray::fromHex("aabbccdd")));
    values.emplace("nodes", BValue(krpc::encodeNodes({{id, node}}, Family::IPv4)));
    values.emplace("values", BValue(BValue::List{BValue(peer.toCompact())}));

    const Endpoint requester(QHostAddress(QStringLiteral("198.51.100.7")), 1234);
    const auto parsed = krpc::parse(krpc::encodeResponse("aa", std::move(values), QByteArray("DG\x00\x01", 4), requester));
    QVERIFY(parsed.message);

    const QString text = krpc::describe(*parsed.message);
    QVERIFY2(text.contains(QStringLiteral("type: response")), qPrintable(text));
    QVERIFY2(text.contains(id.toHex()), qPrintable(text));                       // id as hex
    QVERIFY2(text.contains(QStringLiteral("aabbccdd")), qPrintable(text));       // token as hex
    QVERIFY2(text.contains(node.toString()), qPrintable(text));                  // compact node expanded
    QVERIFY2(text.contains(peer.toString()), qPrintable(text));                  // compact peer expanded
    QVERIFY2(text.contains(requester.toString()), qPrintable(text));             // the ip field
    QVERIFY2(text.contains(QStringLiteral("1 node(s)")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("1 peer(s)")), qPrintable(text));

    // An error reply says so, with its code and text.
    const auto failed = krpc::parse(krpc::encodeError("bb", 203, "invalid token", {}));
    QVERIFY(failed.message);
    const QString errorText = krpc::describe(*failed.message);
    QVERIFY2(errorText.contains(QStringLiteral("type: error")), qPrintable(errorText));
    QVERIFY2(errorText.contains(QStringLiteral("203")), qPrintable(errorText));
    QVERIFY2(errorText.contains(QStringLiteral("invalid token")), qPrintable(errorText));
}

int runTestKrpc(int argc, char **argv)
{
    TestKrpc test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestKrpc.moc"

#include <utility>
