#include "dhtcore/Bep42.h"
#include "dhtcore/DhtEngine.h"
#include "dhtcore/Krpc.h"

#include <QDeadlineTimer>
#include <QNetworkDatagram>
#include <QTest>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QThread>
#include <QUdpSocket>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

using namespace dht;

namespace {

EngineConfig loopbackConfig(bool ipv6 = false)
{
    EngineConfig config;
    config.port = 0;
    config.allowLocalAddresses = true;
    config.bindAddressV4 = QHostAddress(QHostAddress::LocalHost);
    config.enableIpv6 = ipv6;
    config.bindAddressV6 = QHostAddress(QHostAddress::LocalHostIPv6);
    return config;
}

std::unique_ptr<DhtEngine> startEngineWith(const EngineConfig &config)
{
    auto engine = std::make_unique<DhtEngine>();
    QString error;
    if (!engine->start(config, &error)) {
        qWarning() << "engine failed to start:" << error;
        return nullptr;
    }
    return engine;
}

std::unique_ptr<DhtEngine> startEngine(bool ipv6 = false)
{
    return startEngineWith(loopbackConfig(ipv6));
}

quint16 portOf(const DhtEngine &engine, Family family = Family::IPv4)
{
    return engine.node(family)->port();
}

int nodeCount(const DhtEngine &engine, Family family = Family::IPv4)
{
    return engine.node(family)->familySnapshot().nodeCount;
}

// Sends a query and waits for the response or error carrying the same
// transaction id. Queries the engine sends us in the meantime (it pings
// unknown nodes that contact it) are skipped.
std::optional<krpc::Message> exchange(QUdpSocket &socket, quint16 port, const QByteArray &tid,
                                      const QByteArray &method, BValue::Dict args)
{
    socket.writeDatagram(krpc::encodeQuery(tid, method, std::move(args), {}), QHostAddress(QHostAddress::LocalHost), port);
    QDeadlineTimer deadline(3000);
    while (!deadline.hasExpired()) {
        if (!QTest::qWaitFor([&] { return socket.hasPendingDatagrams(); }, int(deadline.remainingTime())))
            break;
        const auto parsed = krpc::parse(socket.receiveDatagram().data());
        if (parsed.message && parsed.message->type != krpc::MessageType::Query && parsed.message->transactionId == tid)
            return parsed.message;
    }
    return std::nullopt;
}

BValue::Dict withId(const NodeId &id, BValue::Dict args = {})
{
    args.insert_or_assign("id", BValue(id.toBytes()));
    return args;
}

bool containsEndpoint(const std::vector<Endpoint> &list, const Endpoint &endpoint)
{
    return std::find(list.begin(), list.end(), endpoint) != list.end();
}

} // namespace

class TestEngine : public QObject
{
    Q_OBJECT

private slots:
    void swarmConvergesAndSharesPeers();
    void answersQueries();
    void rejectsBadQueries();
    void dualStackWant();
    void shutdownDiscardsState();
    void bindConflictIsReported();
    void publishesSnapshotsFromWorkerThread();
    void addNodeResolvesHostnames();
    void bep42Setting_data();
    void bep42Setting();
    void usesConfiguredNodeIds();
};

void TestEngine::swarmConvergesAndSharesPeers()
{
    constexpr int N = 6;
    std::vector<std::unique_ptr<DhtEngine>> engines;
    for (int i = 0; i < N; ++i) {
        engines.push_back(startEngine());
        QVERIFY(engines.back());
    }

    // Join one at a time so each newcomer learns about everyone before it.
    const quint16 seed = portOf(*engines[0]);
    for (int i = 1; i < N; ++i) {
        engines[i]->addNode(QStringLiteral("127.0.0.1"), seed);
        QTRY_VERIFY_WITH_TIMEOUT(nodeCount(*engines[0]) >= i, 5000);
    }

    const auto converged = [&] {
        return std::all_of(engines.begin(), engines.end(), [](const auto &e) { return nodeCount(*e) == N - 1; });
    };
    QTRY_VERIFY_WITH_TIMEOUT(converged(), 10000);

    const EngineSnapshot snapshot = engines[0]->snapshot();
    QCOMPARE(int(snapshot.nodes.size()), N - 1);
    for (const NodeRow &row : snapshot.nodes) {
        QCOMPARE(row.source, NodeRow::Source::Routing);
        QCOMPARE(row.status, NodeRow::Status::Good);
        QCOMPARE(row.bep42, bep42::Status::Exempt);
    }

    const NodeId infohash = NodeId::random();
    int accepted = -1;
    engines[1]->announce(infohash, 7777, false, [&](int n) { accepted = n; });
    QTRY_VERIFY_WITH_TIMEOUT(accepted >= 0, 10000);
    QVERIFY(accepted > 0);

    std::vector<Endpoint> peers;
    bool done = false;
    engines[4]->getPeers(infohash, [&](const std::vector<Endpoint> &found) {
        peers = found;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY(containsEndpoint(peers, Endpoint(QHostAddress(QHostAddress::LocalHost), 7777)));
}

void TestEngine::answersQueries()
{
    auto engine = startEngine();
    QVERIFY(engine);
    const quint16 port = portOf(*engine);

    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const Endpoint clientEndpoint(QHostAddress(QHostAddress::LocalHost), client.localPort());
    const NodeId clientId = NodeId::random();

    // ping, with BEP 42 "ip" and our version string
    auto reply = exchange(client, port, "p1", "ping", withId(clientId));
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);
    QCOMPARE(*reply->senderId(), engine->node(Family::IPv4)->id());
    QVERIFY(reply->reportedAddress);
    QCOMPARE(*reply->reportedAddress, clientEndpoint);
    QCOMPARE(reply->version, clientVersion());

    // find_node returns what the routing table knows
    auto other = startEngine();
    QVERIFY(other);
    other->addNode(QStringLiteral("127.0.0.1"), port);
    QTRY_COMPARE_WITH_TIMEOUT(nodeCount(*engine), 1, 5000);

    BValue::Dict findArgs;
    findArgs.emplace("target", BValue(NodeId::random().toBytes()));
    reply = exchange(client, port, "f1", "find_node", withId(clientId, std::move(findArgs)));
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);
    const auto nodes = krpc::decodeNodes(reply->body.stringAt("nodes").value_or(QByteArray()), Family::IPv4);
    QCOMPARE(int(nodes.nodes.size()), 1);
    QCOMPARE(nodes.nodes[0].id, other->node(Family::IPv4)->id());
    QCOMPARE(nodes.nodes[0].endpoint.port, portOf(*other));

    // get_peers issues a token that announce_peer then accepts
    const NodeId infohash = NodeId::random();
    const auto getPeersArgs = [&] {
        BValue::Dict a;
        a.emplace("info_hash", BValue(infohash.toBytes()));
        return withId(clientId, std::move(a));
    };
    reply = exchange(client, port, "g1", "get_peers", getPeersArgs());
    QVERIFY(reply);
    const QByteArray token = reply->body.stringAt("token").value_or(QByteArray());
    QVERIFY(!token.isEmpty());
    QVERIFY(!reply->body.listAt("values"));

    BValue::Dict announceArgs;
    announceArgs.emplace("info_hash", BValue(infohash.toBytes()));
    announceArgs.emplace("port", BValue(5555));
    announceArgs.emplace("token", BValue(token));
    reply = exchange(client, port, "a1", "announce_peer", withId(clientId, announceArgs));
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);
    QCOMPARE(engine->storage().peerCount(), 1);

    // implied_port uses the source port instead
    announceArgs.insert_or_assign("port", BValue(1));
    announceArgs.insert_or_assign("implied_port", BValue(1));
    reply = exchange(client, port, "a2", "announce_peer", withId(clientId, announceArgs));
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);

    reply = exchange(client, port, "g2", "get_peers", getPeersArgs());
    QVERIFY(reply);
    const auto peers = krpc::decodePeers(reply->body.listAt("values"), Family::IPv4);
    QVERIFY(containsEndpoint(peers, Endpoint(QHostAddress(QHostAddress::LocalHost), 5555)));
    QVERIFY(containsEndpoint(peers, clientEndpoint));
}

void TestEngine::rejectsBadQueries()
{
    auto engine = startEngine();
    QVERIFY(engine);
    const quint16 port = portOf(*engine);

    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const NodeId clientId = NodeId::random();

    auto reply = exchange(client, port, "u1", "frobnicate", withId(clientId));
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Error);
    QCOMPARE(reply->errorCode, qint64(krpc::MethodUnknown));

    BValue::Dict badToken;
    badToken.emplace("info_hash", BValue(NodeId::random().toBytes()));
    badToken.emplace("port", BValue(1234));
    badToken.emplace("token", BValue("nope"));
    reply = exchange(client, port, "b1", "announce_peer", withId(clientId, std::move(badToken)));
    QVERIFY(reply);
    QCOMPARE(reply->errorCode, qint64(krpc::ProtocolError));
    QCOMPARE(engine->storage().peerCount(), 0);

    reply = exchange(client, port, "b2", "find_node", withId(clientId));
    QVERIFY(reply);
    QCOMPARE(reply->errorCode, qint64(krpc::ProtocolError));

    reply = exchange(client, port, "b3", "ping", {});
    QVERIFY(reply);
    QCOMPARE(reply->errorCode, qint64(krpc::ProtocolError));

    client.writeDatagram("garbage", QHostAddress(QHostAddress::LocalHost), port);
    QTRY_COMPARE_WITH_TIMEOUT(engine->snapshot().stats.malformedIn, qint64(1), 3000);
}

void TestEngine::dualStackWant()
{
    auto engine = startEngine(true);
    QVERIFY(engine);
    if (!engine->node(Family::IPv6))
        QSKIP("IPv6 loopback is not available");
    auto other = startEngine(true);
    QVERIFY(other && other->node(Family::IPv6));

    other->addNode(QStringLiteral("127.0.0.1"), portOf(*engine));
    other->addNode(QStringLiteral("::1"), portOf(*engine, Family::IPv6));
    QTRY_VERIFY_WITH_TIMEOUT(nodeCount(*engine) == 1 && nodeCount(*engine, Family::IPv6) == 1, 5000);

    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    BValue::Dict args;
    args.emplace("target", BValue(NodeId::random().toBytes()));
    args.emplace("want", BValue(BValue::List{BValue("n4"), BValue("n6")}));
    const auto reply = exchange(client, portOf(*engine), "w1", "find_node", withId(NodeId::random(), std::move(args)));
    QVERIFY(reply);
    const auto v4 = krpc::decodeNodes(reply->body.stringAt("nodes").value_or(QByteArray()), Family::IPv4);
    const auto v6 = krpc::decodeNodes(reply->body.stringAt("nodes6").value_or(QByteArray()), Family::IPv6);
    QCOMPARE(int(v4.nodes.size()), 1);
    QCOMPARE(int(v6.nodes.size()), 1);
    QCOMPARE(v6.nodes[0].endpoint, Endpoint(QHostAddress(QHostAddress::LocalHostIPv6), portOf(*other, Family::IPv6)));
}

void TestEngine::shutdownDiscardsState()
{
    auto engine = startEngine();
    QVERIFY(engine);
    auto other = startEngine();
    QVERIFY(other);
    other->addNode(QStringLiteral("127.0.0.1"), portOf(*engine));
    QTRY_COMPARE_WITH_TIMEOUT(nodeCount(*engine), 1, 5000);
    const NodeId firstId = engine->node(Family::IPv4)->id();

    engine->shutdown();
    QVERIFY(!engine->isRunning());
    QVERIFY(!engine->node(Family::IPv4));
    QVERIFY(engine->snapshot().nodes.empty());

    QVERIFY(engine->start(loopbackConfig()));
    QCOMPARE(nodeCount(*engine), 0);
    QVERIFY(engine->node(Family::IPv4)->id() != firstId);
}

void TestEngine::bindConflictIsReported()
{
    QUdpSocket blocker;
    QVERIFY(blocker.bind(QHostAddress(QHostAddress::LocalHost), 0, QAbstractSocket::DontShareAddress));

    DhtEngine engine;
    EngineConfig config = loopbackConfig();
    config.port = blocker.localPort();
    QString error;
    QVERIFY(!engine.start(config, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!engine.isRunning());
}

void TestEngine::publishesSnapshotsFromWorkerThread()
{
    // The application runs the engine on its own thread, so everything the
    // engine owns (timers included) has to move there with it.
    QTest::failOnWarning(QRegularExpression(QStringLiteral("another thread")));
    qRegisterMetaType<EngineSnapshot>();

    QThread thread;
    auto *engine = new DhtEngine;
    engine->moveToThread(&thread);
    thread.start();
    const auto cleanup = qScopeGuard([&] {
        QMetaObject::invokeMethod(engine, &DhtEngine::shutdown, Qt::BlockingQueuedConnection);
        QObject::connect(engine, &QObject::destroyed, &thread, &QThread::quit, Qt::DirectConnection);
        engine->deleteLater();
        thread.wait();
    });

    int snapshots = 0;
    connect(engine, &DhtEngine::snapshotReady, this, [&](const EngineSnapshot &) { ++snapshots; });

    EngineConfig config = loopbackConfig();
    config.snapshotIntervalMs = 100;
    bool started = false;
    QMetaObject::invokeMethod(engine, [&] { started = engine->start(config); }, Qt::BlockingQueuedConnection);
    QVERIFY(started);

    // One snapshot is published directly by start(); more need the timer.
    QTRY_VERIFY_WITH_TIMEOUT(snapshots >= 3, 3000);
}

void TestEngine::addNodeResolvesHostnames()
{
    auto engine = startEngine();
    QVERIFY(engine);
    auto other = startEngine();
    QVERIFY(other);

    QStringList notices;
    connect(other.get(), &DhtEngine::notice, this, [&](const QString &message, bool) { notices << message; });

    // "localhost" resolves without leaving the machine. IPv6 is off, so
    // only its IPv4 address is used.
    other->addNode(QStringLiteral("localhost"), portOf(*engine));
    QTRY_COMPARE_WITH_TIMEOUT(nodeCount(*other), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(nodeCount(*engine), 1, 5000);

    const EngineSnapshot snapshot = other->snapshot();
    QCOMPARE(int(snapshot.nodes.size()), 1);
    QCOMPARE(snapshot.nodes[0].endpoint, Endpoint(QHostAddress(QHostAddress::LocalHost), portOf(*engine)));
    const QString allNotices = notices.join(QStringLiteral(" | "));
    QVERIFY2(allNotices.contains(QStringLiteral("Contacting localhost")), qPrintable(allNotices));
}

void TestEngine::bep42Setting_data()
{
    QTest::addColumn<bool>("enabled");
    QTest::newRow("enabled") << true;
    QTest::newRow("disabled") << false;
}

void TestEngine::bep42Setting()
{
    QFETCH(bool, enabled);
    const QHostAddress external(QStringLiteral("203.0.113.50"));

    // Start from a configured ID that is certainly not compliant for the
    // address the fake nodes are about to report.
    NodeId initial = bep42::generate(external);
    initial[0] ^= 0x80;

    EngineConfig config = loopbackConfig();
    config.bep42 = enabled;
    config.nodeIdV4 = initial;
    auto engine = startEngineWith(config);
    QVERIFY(engine);
    DhtNode *node = engine->node(Family::IPv4);
    QCOMPARE(node->id(), initial);

    // Three fake nodes on distinct loopback addresses, all answering every
    // query with "you are 203.0.113.50". A public address is not exempt.
    std::vector<std::unique_ptr<QUdpSocket>> fakes;
    for (int i = 2; i <= 4; ++i) {
        auto fake = std::make_unique<QUdpSocket>();
        if (!fake->bind(QHostAddress(QStringLiteral("127.0.0.%1").arg(i)), 0))
            QSKIP("Cannot bind additional loopback addresses on this platform");
        QUdpSocket *socket = fake.get();
        const NodeId fakeId = NodeId::random();
        connect(socket, &QUdpSocket::readyRead, this, [socket, fakeId, external] {
            while (socket->hasPendingDatagrams()) {
                const QNetworkDatagram datagram = socket->receiveDatagram();
                const auto parsed = krpc::parse(datagram.data());
                if (!parsed.message || parsed.message->type != krpc::MessageType::Query)
                    continue;
                BValue::Dict values;
                values.emplace("id", BValue(fakeId.toBytes()));
                socket->writeDatagram(datagram.makeReply(
                    krpc::encodeResponse(parsed.message->transactionId, std::move(values), {}, Endpoint(external, 6881))));
            }
        });
        engine->addNode(socket->localAddress().toString(), socket->localPort());
        fakes.push_back(std::move(fake));
    }

    // The address is learned either way.
    QTRY_COMPARE_WITH_TIMEOUT(node->externalAddress(), external, 5000);

    if (enabled) {
        QTRY_VERIFY_WITH_TIMEOUT(node->id() != initial, 2000);
        QVERIFY(bep42::isCompliant(node->id(), external));
        QCOMPARE(node->familySnapshot().bep42, bep42::Status::Compliant);
    } else {
        QTest::qWait(300);
        QCOMPARE(node->id(), initial);
        QCOMPARE(node->familySnapshot().bep42, bep42::Status::NonCompliant);
    }

    // Replies carry "ip" only with BEP 42 on.
    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const auto reply = exchange(client, portOf(*engine), "p1", "ping", withId(NodeId::random()));
    QVERIFY(reply);
    QCOMPARE(reply->reportedAddress.has_value(), enabled);
}

void TestEngine::usesConfiguredNodeIds()
{
    const NodeId v4 = NodeId::random();
    const NodeId v6 = NodeId::random();
    EngineConfig config = loopbackConfig(true);
    config.nodeIdV4 = v4;
    config.nodeIdV6 = v6;
    auto engine = startEngineWith(config);
    QVERIFY(engine);
    QCOMPARE(engine->node(Family::IPv4)->id(), v4);
    QCOMPARE(engine->snapshot().ipv4.id, v4);
    if (engine->node(Family::IPv6))
        QCOMPARE(engine->node(Family::IPv6)->id(), v6);

    // Other nodes see the configured ID.
    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const auto reply = exchange(client, portOf(*engine), "p1", "ping", withId(NodeId::random()));
    QVERIFY(reply);
    QCOMPARE(*reply->senderId(), v4);

    // Without a configured ID, a start picks a random one.
    auto other = startEngine();
    QVERIFY(other);
    QVERIFY(other->node(Family::IPv4)->id() != v4);
}

int runTestEngine(int argc, char **argv)
{
    TestEngine test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestEngine.moc"
