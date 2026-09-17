#include "dhtcore/Bep42.h"
#include "dhtcore/Bep44.h"
#include "dhtcore/DhtEngine.h"
#include "dhtcore/Krpc.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QTest>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QThread>
#include <QUdpSocket>

#include <algorithm>
#include <cmath>
#include <set>
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
    // Every test node shares 127.0.0.1, and the per-host limit is per
    // address, so swarm tests would otherwise crawl.
    config.hostLimit = HostLimit{1000.0, 1000.0, 10000};
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

// The next query the engine sends us, as opposed to a reply.
std::optional<krpc::Message> waitForQuery(QUdpSocket &socket, int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        if (!QTest::qWaitFor([&] { return socket.hasPendingDatagrams(); }, int(deadline.remainingTime())))
            break;
        const auto parsed = krpc::parse(socket.receiveDatagram().data());
        if (parsed.message && parsed.message->type == krpc::MessageType::Query)
            return parsed.message;
    }
    return std::nullopt;
}

// A quick scan for loopback tests: short timeouts and revisit intervals.
EngineConfig monitorConfig()
{
    EngineConfig config = loopbackConfig();
    config.crawl.queryTimeoutMs = 400;
    config.crawl.retryDelayMs = 200;
    config.crawl.revisitIntervalMs = 800;
    config.crawl.silentRevisitIntervalMs = 800;
    config.crawl.historyIntervalMs = 400;
    return config;
}

Endpoint endpointOf(const DhtEngine &engine)
{
    return Endpoint(engine.node(Family::IPv4)->localAddress(), portOf(engine));
}

void introduce(DhtEngine &engine, const DhtEngine &to)
{
    const Endpoint e = endpointOf(to);
    engine.addNode(e.address.toString(), e.port);
}

// A chain of engines, each introduced only to the one before it, so a
// scan has to follow replies to find them all. Each has its own loopback
// address (127.0.0.firstHost and up), since the scan counts addresses.
std::vector<std::unique_ptr<DhtEngine>> startChain(int count, int firstHost = 10)
{
    std::vector<std::unique_ptr<DhtEngine>> chain;
    for (int i = 0; i < count; ++i) {
        EngineConfig config = loopbackConfig();
        config.bindAddressV4 = QHostAddress(QStringLiteral("127.0.0.%1").arg(firstHost + i));
        chain.push_back(startEngineWith(config));
        if (!chain.back())
            return {};
        if (i > 0)
            introduce(*chain.back(), *chain[i - 1]);
    }
    return chain;
}

int catalogCount(const DhtEngine &engine, CatalogEntry::State state)
{
    return engine.catalog().count(state);
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
    void storageSnapshotListsAnnouncedPeers();
    void bep44PutAndGet();
    void bep44MutableItems();
    void searchesAndPublishesAcrossASwarm();
    void probesASingleNode();
    void readOnlyModeAnswersNothing();
    void neverOverwhelmsOneHost();
    void sendLimitShedsRepliesAndCanBeLifted();
    void monitoringDiscoversTheWholeSwarm();
    void monitoringMarksSilentAndGoneNodes();
    void monitoringPausesAndHonoursTheCap();
    void monitoringDoesNotQueueBehindABusyHost();
    void censusCountsAddressesExactly();
    void censusChoosesSlicesAndCancels();
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

void TestEngine::storageSnapshotListsAnnouncedPeers()
{
    auto engine = startEngine();
    QVERIFY(engine);
    const quint16 port = portOf(*engine);

    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const NodeId clientId = NodeId::random();
    const NodeId infohash = NodeId::random();

    QCOMPARE(engine->storageSnapshot().infohashCount, 0);
    QCOMPARE(engine->storageSnapshot().ttlMs, PeerStorage::PeerTtlMs);

    BValue::Dict getPeersArgs;
    getPeersArgs.emplace("info_hash", BValue(infohash.toBytes()));
    auto reply = exchange(client, port, "g1", "get_peers", withId(clientId, std::move(getPeersArgs)));
    QVERIFY(reply);
    const QByteArray token = reply->body.stringAt("token").value_or(QByteArray());
    QVERIFY(!token.isEmpty());

    BValue::Dict announceArgs;
    announceArgs.emplace("info_hash", BValue(infohash.toBytes()));
    announceArgs.emplace("port", BValue(5555));
    announceArgs.emplace("token", BValue(token));
    reply = exchange(client, port, "a1", "announce_peer", withId(clientId, std::move(announceArgs)));
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);

    const StorageSnapshot stored = engine->storageSnapshot();
    QVERIFY(stored.running);
    QCOMPARE(stored.infohashCount, 1);
    QCOMPARE(stored.peerCount, 1);
    QVERIFY(!stored.truncated);
    QCOMPARE(int(stored.infohashes.size()), 1);

    const StoredInfohashRow &row = stored.infohashes[0];
    QCOMPARE(row.infohash, infohash);
    QCOMPARE(row.peerCount, 1);
    QVERIFY(row.lastAnnounceAgoMs >= 0 && row.lastAnnounceAgoMs < 5000);
    QCOMPARE(int(row.peers.size()), 1);

    const StoredPeerRow &peer = row.peers[0];
    QCOMPARE(peer.endpoint, Endpoint(QHostAddress(QHostAddress::LocalHost), 5555));
    QVERIFY(peer.ageMs >= 0 && peer.ageMs < 5000);
    QVERIFY(peer.expiresInMs > PeerStorage::PeerTtlMs - 5000 && peer.expiresInMs <= PeerStorage::PeerTtlMs);
    QCOMPARE(row.expiresInMs, peer.expiresInMs);

    // Stopping the engine empties the store.
    engine->shutdown();
    QCOMPARE(engine->storageSnapshot().infohashCount, 0);
    QVERIFY(!engine->storageSnapshot().running);
}

void TestEngine::bep44PutAndGet()
{
    auto engine = startEngine();
    QVERIFY(engine);
    const quint16 port = portOf(*engine);

    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const NodeId clientId = NodeId::random();

    // get on an unknown target still hands out a token and no value
    BValue::Dict firstGet;
    firstGet.emplace("target", BValue(NodeId::random().toBytes()));
    auto reply = exchange(client, port, "t1", "get", withId(clientId, std::move(firstGet)));
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);
    const QByteArray token = reply->body.stringAt("token").value_or(QByteArray());
    QVERIFY(!token.isEmpty());
    QVERIFY(!reply->body.find("v"));

    // put an immutable item, under the target from BEP 44's own example
    BValue::Dict put;
    put.emplace("token", BValue(token));
    put.emplace("v", BValue(QByteArray("Hello World!")));
    reply = exchange(client, port, "p1", "put", withId(clientId, std::move(put)));
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);
    QCOMPARE(engine->items().immutableCount(), 1);

    const NodeId target = bep44::immutableTarget("12:Hello World!");
    QCOMPARE(target.toHex(), QStringLiteral("e5f96f6f38320f0f33959cb4d3d656452117aadb"));
    QVERIFY(engine->items().immutableItem(target));

    // and it reaches the Data Store view
    const StorageSnapshot stored = engine->storageSnapshot();
    QCOMPARE(stored.immutableCount, 1);
    QCOMPARE(stored.mutableCount, 0);
    QCOMPARE(int(stored.items.size()), 1);
    QCOMPARE(stored.items[0].target, target);
    QCOMPARE(stored.items[0].value, QByteArray("12:Hello World!"));
    QVERIFY(!stored.items[0].isMutable);
    QVERIFY(stored.items[0].expiresInMs > bep44::ItemTtlMs - 5000);
    QCOMPARE(stored.itemTtlMs, bep44::ItemTtlMs);

    // and read it back
    BValue::Dict get;
    get.emplace("target", BValue(target.toBytes()));
    reply = exchange(client, port, "t2", "get", withId(clientId, std::move(get)));
    QVERIFY(reply);
    const BValue *value = reply->body.find("v");
    QVERIFY(value);
    QVERIFY(value->isString());
    QCOMPARE(value->toString(), QByteArray("Hello World!"));

    // a bad token is refused
    BValue::Dict badToken;
    badToken.emplace("token", BValue("nope"));
    badToken.emplace("v", BValue(QByteArray("x")));
    reply = exchange(client, port, "p2", "put", withId(clientId, std::move(badToken)));
    QVERIFY(reply);
    QCOMPARE(reply->errorCode, qint64(krpc::ProtocolError));

    // so is an oversized value, with BEP 44's own error code
    BValue::Dict tooBig;
    tooBig.emplace("token", BValue(token));
    tooBig.emplace("v", BValue(QByteArray(bep44::MaxValueBytes + 10, 'x')));
    reply = exchange(client, port, "p3", "put", withId(clientId, std::move(tooBig)));
    QVERIFY(reply);
    QCOMPARE(reply->errorCode, qint64(bep44::MessageTooBig));

    // an unsigned mutable put is refused
    BValue::Dict forged;
    forged.emplace("token", BValue(token));
    forged.emplace("v", BValue(QByteArray("Hello World!")));
    forged.emplace("k", BValue(QByteArray(bep44::PublicKeyBytes, 'k')));
    forged.emplace("seq", BValue(qint64(1)));
    forged.emplace("sig", BValue(QByteArray(bep44::SignatureBytes, 's')));
    reply = exchange(client, port, "p4", "put", withId(clientId, std::move(forged)));
    QVERIFY(reply);
    QCOMPARE(reply->errorCode, qint64(bep44::InvalidSignature));
    QCOMPARE(engine->items().mutableCount(), 0);
}

void TestEngine::bep44MutableItems()
{
    auto engine = startEngine();
    QVERIFY(engine);
    const quint16 port = portOf(*engine);

    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const NodeId clientId = NodeId::random();
    const ed25519::KeyPair keys = ed25519::randomKeyPair();
    const QByteArray salt = "foobar";

    const auto token = [&] {
        BValue::Dict args;
        args.emplace("target", BValue(NodeId::random().toBytes()));
        const auto reply = exchange(client, port, "t0", "get", withId(clientId, std::move(args)));
        return reply ? reply->body.stringAt("token").value_or(QByteArray()) : QByteArray();
    }();
    QVERIFY(!token.isEmpty());

    const auto put = [&](const QByteArray &tid, const QByteArray &bencodedValue, qint64 sequence,
                         std::optional<qint64> cas, bool corruptSignature) {
        const QByteArray signature =
            ed25519::sign(bep44::signingBuffer(salt, sequence, bencodedValue), keys.secretKey);
        BValue::Dict args;
        args.emplace("token", BValue(token));
        args.emplace("v", BValue(bencodedValue.mid(bencodedValue.indexOf(':') + 1)));
        args.emplace("k", BValue(keys.publicKey));
        args.emplace("salt", BValue(salt));
        args.emplace("seq", BValue(sequence));
        args.emplace("sig", BValue(corruptSignature ? QByteArray(bep44::SignatureBytes, 'x') : signature));
        if (cas)
            args.emplace("cas", BValue(*cas));
        return exchange(client, port, tid, "put", withId(clientId, std::move(args)));
    };

    // store, then read back through the target BEP 44 derives from key + salt
    auto reply = put("m1", "5:first", 1, std::nullopt, false);
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);
    QCOMPARE(engine->items().mutableCount(), 1);

    const NodeId target = bep44::mutableTarget(keys.publicKey, salt);
    const MutableItem *stored = engine->items().mutableItem(target);
    QVERIFY(stored);
    QCOMPARE(stored->sequence, qint64(1));
    QCOMPARE(stored->value, QByteArray("5:first"));

    BValue::Dict get;
    get.emplace("target", BValue(target.toBytes()));
    reply = exchange(client, port, "g1", "get", withId(clientId, std::move(get)));
    QVERIFY(reply);
    QCOMPARE(reply->body.stringAt("k").value_or(QByteArray()), keys.publicKey);
    QCOMPARE(reply->body.integerAt("seq").value_or(0), qint64(1));
    QCOMPARE(reply->body.stringAt("sig").value_or(QByteArray()).size(), bep44::SignatureBytes);
    QCOMPARE(reply->body.stringAt("v").value_or(QByteArray()), QByteArray("first"));

    // a getter that already has this sequence number is told the seq only
    BValue::Dict getSameSeq;
    getSameSeq.emplace("target", BValue(target.toBytes()));
    getSameSeq.emplace("seq", BValue(qint64(1)));
    reply = exchange(client, port, "g2", "get", withId(clientId, std::move(getSameSeq)));
    QVERIFY(reply);
    QCOMPARE(reply->body.integerAt("seq").value_or(0), qint64(1));
    QVERIFY(!reply->body.find("v"));
    QVERIFY(!reply->body.find("sig"));

    // a newer sequence replaces it
    reply = put("m2", "6:second", 2, std::nullopt, false);
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);
    QCOMPARE(engine->items().mutableItem(target)->value, QByteArray("6:second"));

    // an older one is refused
    reply = put("m3", "5:stale", 1, std::nullopt, false);
    QVERIFY(reply);
    QCOMPARE(reply->errorCode, qint64(bep44::SequenceTooLow));

    // so is a stale compare-and-swap
    reply = put("m4", "5:third", 3, 1, false);
    QVERIFY(reply);
    QCOMPARE(reply->errorCode, qint64(bep44::CasMismatch));
    reply = put("m5", "5:third", 3, 2, false);
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);

    // and a bad signature never reaches storage
    reply = put("m6", "6:forged", 4, std::nullopt, true);
    QVERIFY(reply);
    QCOMPARE(reply->errorCode, qint64(bep44::InvalidSignature));
    QCOMPARE(engine->items().mutableItem(target)->value, QByteArray("5:third"));
}

void TestEngine::searchesAndPublishesAcrossASwarm()
{
    // Four nodes that know each other, so lookups have somewhere to go.
    constexpr int N = 4;
    std::vector<std::unique_ptr<DhtEngine>> engines;
    for (int i = 0; i < N; ++i) {
        engines.push_back(startEngine());
        QVERIFY(engines.back());
    }
    const quint16 seed = portOf(*engines[0]);
    for (int i = 1; i < N; ++i) {
        engines[i]->addNode(QStringLiteral("127.0.0.1"), seed);
        QTRY_VERIFY_WITH_TIMEOUT(nodeCount(*engines[0]) >= i, 5000);
    }
    QTRY_VERIFY_WITH_TIMEOUT(std::all_of(engines.begin(), engines.end(),
                                         [](const auto &e) { return nodeCount(*e) == N - 1; }),
                             10000);

    DhtEngine &publisher = *engines[0];
    DhtEngine &searcher = *engines[3];

    // --- an immutable item, published and then found -------------------------
    const QByteArray value = bencode(BValue(QByteArray("hello from the swarm")));
    PublishResult published;
    bool done = false;
    connect(&publisher, &DhtEngine::publishFinished, this, [&](const PublishResult &r) {
        published = r;
        done = true;
    });
    publisher.publishImmutable(value);
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QCOMPARE(published.kind, PublishResult::Kind::Immutable);
    QVERIFY(published.error.isEmpty());
    QVERIFY(published.accepted > 0);
    QCOMPARE(published.target, bep44::immutableTarget(value));

    ItemSearchResult found;
    done = false;
    connect(&searcher, &DhtEngine::itemSearchFinished, this, [&](const ItemSearchResult &r) {
        found = r;
        done = true;
    });
    searcher.searchItem(published.target, QByteArray());
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY2(found.found, "the immutable item was not found");
    QVERIFY(!found.isMutable);
    QCOMPARE(found.value, value);
    QVERIFY(found.responded > 0);

    // --- a mutable item, then an update ---------------------------------------
    const ed25519::KeyPair keys = ed25519::randomKeyPair();
    const QByteArray salt = "a-salt";
    done = false;
    publisher.publishMutable(keys.publicKey, keys.secretKey, salt, 1, bencode(BValue(QByteArray("first"))), std::nullopt);
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QCOMPARE(published.kind, PublishResult::Kind::Mutable);
    QVERIFY(published.error.isEmpty());
    QVERIFY(published.accepted > 0);
    QCOMPARE(published.target, bep44::mutableTarget(keys.publicKey, salt));

    done = false;
    searcher.searchItem(published.target, salt);
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY2(found.found, "the mutable item was not found");
    QVERIFY(found.isMutable);
    QCOMPARE(found.sequence, qint64(1));
    QCOMPARE(found.value, bencode(BValue(QByteArray("first"))));
    QCOMPARE(found.publicKey, keys.publicKey);

    const NodeId mutableTarget = published.target;
    done = false;
    publisher.publishMutable(keys.publicKey, keys.secretKey, salt, 2, bencode(BValue(QByteArray("second"))), std::nullopt);
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY(published.accepted > 0);

    done = false;
    searcher.searchItem(mutableTarget, salt);
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QCOMPARE(found.sequence, qint64(2));
    QCOMPARE(found.value, bencode(BValue(QByteArray("second"))));

    // --- announcing a peer, then searching for it ------------------------------
    const NodeId infohash = NodeId::random();
    done = false;
    publisher.announcePeer(infohash, 4242, false);
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QCOMPARE(published.kind, PublishResult::Kind::Announce);
    QVERIFY(published.accepted > 0);

    PeerSearchResult peers;
    done = false;
    connect(&searcher, &DhtEngine::peerSearchFinished, this, [&](const PeerSearchResult &r) {
        peers = r;
        done = true;
    });
    searcher.searchPeers(infohash);
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QCOMPARE(peers.infohash, infohash);
    const Endpoint announced(QHostAddress(QHostAddress::LocalHost), 4242);
    QVERIFY(containsEndpoint(peers.peers, announced));

    // Every peer is attributed to the node that returned it.
    QVERIFY(!peers.sightings.empty());
    bool attributed = false;
    for (const PeerSighting &sighting : peers.sightings) {
        QVERIFY(sighting.source.isValid());
        QVERIFY(sighting.source.address.isLoopback());
        QVERIFY(sighting.source.port != portOf(searcher));  // never the searcher itself
        if (sighting.peer == announced)
            attributed = true;
    }
    QVERIFY(attributed);

    // --- a target nobody has ----------------------------------------------------
    done = false;
    searcher.searchItem(NodeId::random(), QByteArray());
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY(!found.found);
    QVERIFY(found.queried > 0);
}

void TestEngine::probesASingleNode()
{
    auto engine = startEngine();
    QVERIFY(engine);
    auto target = startEngine();
    QVERIFY(target);
    const Endpoint targetEndpoint(QHostAddress(QHostAddress::LocalHost), portOf(*target));

    std::vector<ProbeResult> results;
    connect(engine.get(), &DhtEngine::probeFinished, this,
            [&](const ProbeResult &result) { results.push_back(result); });

    // ping
    engine->probe(targetEndpoint, "ping", {});
    QTRY_COMPARE_WITH_TIMEOUT(int(results.size()), 1, 5000);
    QCOMPARE(results[0].method, QByteArray("ping"));
    QCOMPARE(results[0].endpoint, targetEndpoint);
    QVERIFY(!results[0].timedOut);
    QVERIFY(!results[0].isError);
    QVERIFY(!results[0].request.isEmpty());
    QVERIFY(!results[0].response.isEmpty());
    QVERIFY(results[0].rttMs >= 0);
    QVERIFY2(results[0].decoded.contains(QStringLiteral("type: response")), qPrintable(results[0].decoded));
    QVERIFY(results[0].decoded.contains(target->node(Family::IPv4)->id().toHex()));

    // an unknown method comes back as an error, in full
    results.clear();
    engine->probe(targetEndpoint, "frobnicate", {});
    QTRY_COMPARE_WITH_TIMEOUT(int(results.size()), 1, 5000);
    QVERIFY(results[0].isError);
    QCOMPARE(results[0].errorCode, qint64(krpc::MethodUnknown));
    QVERIFY(results[0].decoded.contains(QStringLiteral("type: error")));

    // a node that is not there times out, and says so
    results.clear();
    engine->probe(Endpoint(QHostAddress(QHostAddress::LocalHost), 1), "ping", {});
    QTRY_COMPARE_WITH_TIMEOUT(int(results.size()), 1, 8000);
    QVERIFY(results[0].timedOut);
    QVERIFY(results[0].response.isEmpty());
    QVERIFY(!results[0].request.isEmpty());

    // announce chains get_peers for a token, and reports both exchanges
    results.clear();
    const NodeId infohash = NodeId::random();
    engine->probeAnnounce(targetEndpoint, infohash, 6881, false);
    QTRY_COMPARE_WITH_TIMEOUT(int(results.size()), 2, 8000);
    QCOMPARE(results[0].method, QByteArray("get_peers"));
    QVERIFY(!results[0].token.isEmpty());
    QCOMPARE(results[1].method, QByteArray("announce_peer"));
    QVERIFY(!results[1].isError);
    QCOMPARE(target->storage().peerCount(), 1);
}

int runTestEngine(int argc, char **argv)
{
    TestEngine test;
    return QTest::qExec(&test, argc, argv);
}

// BEP 43: read-only nodes flag their own queries and answer none of ours.
void TestEngine::readOnlyModeAnswersNothing()
{
    EngineConfig config = loopbackConfig();
    config.readOnly = true;
    auto engine = startEngineWith(config);
    QVERIFY(engine);
    const quint16 port = portOf(*engine);

    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const NodeId clientId = NodeId::random();

    // What it sends carries "ro" so nobody keeps it in a routing table.
    engine->addNode(QStringLiteral("127.0.0.1"), client.localPort());
    const auto outgoing = waitForQuery(client, 5000);
    QVERIFY(outgoing);
    QVERIFY(outgoing->readOnly);

    // What it receives is dropped: no response, and no error either.
    QVERIFY(!exchange(client, port, "r1", "ping", withId(clientId)));
    QCOMPARE(engine->node(Family::IPv4)->stats().readOnlyDropped, qint64(1));
    QCOMPARE(nodeCount(*engine), 0);

    // An announce cannot get through while nothing is answered.
    BValue::Dict announceArgs;
    announceArgs.emplace("info_hash", BValue(NodeId::random().toBytes()));
    announceArgs.emplace("port", BValue(5555));
    announceArgs.emplace("token", BValue(QByteArray("whatever")));
    QVERIFY(!exchange(client, port, "r2", "announce_peer", withId(clientId, std::move(announceArgs))));
    QCOMPARE(engine->storage().peerCount(), 0);

    // Turning it off while running restores normal service.
    engine->setReadOnly(false);
    const auto reply = exchange(client, port, "r3", "ping", withId(clientId));
    QVERIFY(reply);
    QCOMPARE(reply->type, krpc::MessageType::Response);
    QCOMPARE(*reply->senderId(), engine->node(Family::IPv4)->id());
    QCOMPARE(engine->node(Family::IPv4)->stats().readOnlyDropped, qint64(2));
}

// However much is asked of one host, it gets no more than the per-host
// allowance, and the queries that had to wait still succeed.
void TestEngine::neverOverwhelmsOneHost()
{
    EngineConfig config = loopbackConfig();
    config.hostLimit = HostLimit{};  // the real defaults
    auto engine = startEngineWith(config);
    QVERIFY(engine);

    QUdpSocket node;
    QVERIFY(node.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const Endpoint nodeEndpoint(QHostAddress(QHostAddress::LocalHost), node.localPort());
    const NodeId nodeId = NodeId::random();

    std::vector<ProbeResult> results;
    connect(engine.get(), &DhtEngine::probeFinished, this,
            [&](const ProbeResult &r) { results.push_back(r); });

    // The node answers everything at once and notes when each query came.
    QElapsedTimer clock;
    clock.start();
    std::vector<qint64> arrivals;
    int pings = 0;
    connect(&node, &QUdpSocket::readyRead, this, [&] {
        while (node.hasPendingDatagrams()) {
            const QNetworkDatagram d = node.receiveDatagram();
            const auto parsed = krpc::parse(d.data());
            if (!parsed.message || parsed.message->type != krpc::MessageType::Query)
                continue;
            arrivals.push_back(clock.elapsed());
            if (parsed.message->method == "ping")
                ++pings;
            BValue::Dict values;
            values.emplace("id", BValue(nodeId.toBytes()));
            node.writeDatagram(krpc::encodeResponse(parsed.message->transactionId, values, {}, Endpoint()),
                               d.senderAddress(), quint16(d.senderPort()));
        }
    });

    constexpr int Asked = 10;
    for (int i = 0; i < Asked; ++i)
        engine->probe(nodeEndpoint, "ping", {});

    QTRY_COMPARE_WITH_TIMEOUT(int(results.size()), Asked, 10000);
    QCOMPARE(pings, Asked);
    for (const ProbeResult &r : results)
        QVERIFY2(!r.timedOut && !r.isError && r.errorMessage.isEmpty(), qPrintable(r.summary));

    // Burst of two, then two a second: never more than four in a second,
    // and the eight that waited took about four seconds.
    for (qint64 start : arrivals) {
        const auto inWindow = std::count_if(arrivals.begin(), arrivals.end(),
                                            [&](qint64 t) { return t >= start && t < start + 1000; });
        QVERIFY2(inWindow <= 4, qPrintable(QStringLiteral("%1 queries within a second").arg(inWindow)));
    }
    QVERIFY2(arrivals.back() >= 3500, qPrintable(QString::number(arrivals.back())));

    const EngineStats stats = engine->node(Family::IPv4)->stats();
    QVERIFY2(stats.queriesDelayed >= Asked - 2, qPrintable(QString::number(stats.queriesDelayed)));
    QCOMPARE(stats.queriesRefused, qint64(0));
    QCOMPARE(stats.timeouts, qint64(0));
    QCOMPARE(stats.queriesWaiting, 0);
}

// Over the send limit incoming queries go unanswered; lifting the limit
// while running restores service at once.
void TestEngine::sendLimitShedsRepliesAndCanBeLifted()
{
    EngineConfig config = loopbackConfig();
    config.sendLimit = 300;  // a few replies' worth
    auto engine = startEngineWith(config);
    QVERIFY(engine);
    QCOMPARE(engine->sendLimit(), qint64(300));
    const quint16 port = portOf(*engine);

    QUdpSocket client;
    QVERIFY(client.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const NodeId clientId = NodeId::random();

    const auto pingBurst = [&](const QByteArray &prefix, int count) {
        for (int i = 0; i < count; ++i) {
            const QByteArray tid = prefix + QByteArray::number(i);
            client.writeDatagram(krpc::encodeQuery(tid, "ping", withId(clientId), {}),
                                 QHostAddress(QHostAddress::LocalHost), port);
        }
    };
    // Replies to our pings, ignoring the engine's own queries to us.
    const auto collectReplies = [&](const QByteArray &prefix, int waitMs) {
        int replies = 0;
        qint64 bytes = 0;
        QDeadlineTimer deadline(waitMs);
        while (!deadline.hasExpired()) {
            if (!QTest::qWaitFor([&] { return client.hasPendingDatagrams(); }, int(deadline.remainingTime())))
                break;
            const QByteArray data = client.receiveDatagram().data();
            const auto parsed = krpc::parse(data);
            if (parsed.message && parsed.message->type == krpc::MessageType::Response
                && parsed.message->transactionId.startsWith(prefix)) {
                ++replies;
                bytes += data.size();
            }
        }
        return std::make_pair(replies, bytes);
    };

    pingBurst("a", 30);
    const auto [limitedReplies, limitedBytes] = collectReplies("a", 800);
    QVERIFY2(limitedReplies > 0 && limitedReplies < 30, qPrintable(QString::number(limitedReplies)));
    // A full second's worth, what refilled in the wait, and one overdraw.
    QVERIFY2(limitedBytes <= 300 + 300 * 0.8 + 150, qPrintable(QString::number(limitedBytes)));
    const EngineStats stats = engine->node(Family::IPv4)->stats();
    QVERIFY2(stats.repliesShed >= 30 - limitedReplies, qPrintable(QString::number(stats.repliesShed)));

    engine->setSendLimit(0);
    QCOMPARE(engine->sendLimit(), qint64(0));
    pingBurst("b", 10);
    const auto [freeReplies, freeBytes] = collectReplies("b", 800);
    Q_UNUSED(freeBytes);
    QCOMPARE(freeReplies, 10);
}

void TestEngine::monitoringDiscoversTheWholeSwarm()
{
    constexpr int N = 12;
    auto chain = startChain(N);
    QCOMPARE(int(chain.size()), N);

    auto monitor = startEngineWith(monitorConfig());
    QVERIFY(monitor);
    introduce(*monitor, *chain.back());
    QTRY_VERIFY_WITH_TIMEOUT(nodeCount(*monitor) >= 1, 5000);

    QCOMPARE(monitor->snapshot().crawl.phase, CrawlSnapshot::Phase::Off);
    monitor->setMonitoring(true);
    QVERIFY(monitor->isMonitoring());
    QTRY_COMPARE_WITH_TIMEOUT(catalogCount(*monitor, CatalogEntry::State::Responsive), N, 20000);

    // Each of them, with what it said about itself, and never ourselves.
    std::set<quint16> ports;
    const NodeId self = monitor->node(Family::IPv4)->id();
    monitor->catalog().forEach([&](NodeCatalog::Slot, const CatalogEntry &e) {
        QVERIFY(e.id != self);
        if (e.state != CatalogEntry::State::Responsive)
            return;
        ports.insert(e.port);
        QCOMPARE(e.versionBytes(), clientVersion());
        QVERIFY(e.rttMs != CatalogEntry::NoRtt);
        QCOMPARE(bep42::Status(e.bep42), bep42::Status::Exempt);  // loopback
        QVERIFY(e.lastAnswered > 0 && e.answeringSince > 0);
    });
    for (const auto &engine : chain)
        QCOMPARE(int(ports.count(portOf(*engine))), 1);

    const CrawlSnapshot crawl = monitor->snapshot().crawl;
    QCOMPARE(crawl.responsive, N);
    QVERIFY(crawl.queries >= N);
    QVERIFY(crawl.answers >= N);
    QCOMPARE(crawl.notSent, qint64(0));
    QVERIFY(crawl.phase != CrawlSnapshot::Phase::Off);
    QVERIFY(crawl.memoryBytes > 0);

    // Statistics catch up within a few seconds.
    QTRY_VERIFY_WITH_TIMEOUT(monitor->snapshot().crawl.stats && monitor->snapshot().crawl.stats->all.answeringIps == N,
                             10000);
    const auto stats = monitor->snapshot().crawl.stats;
    QCOMPARE(stats->ipv4.answeringIps, N);
    QCOMPARE(stats->ipv4.connectedIps, N);
    QVERIFY(stats->ipv4.heardIps >= N);
    QCOMPARE(stats->ipv4.answeringNodes, N);
    QCOMPARE(stats->ipv4.multiNodeIps, 0);
    QCOMPARE(stats->ipv6.answeringIps, 0);
    QCOMPARE(int(stats->all.clients.size()), 1);
    QCOMPARE(stats->all.clients[0].name, QStringLiteral("DHT Inspector"));
    QCOMPARE(stats->all.clients[0].count, double(N));
    QCOMPARE(stats->all.versions[0].version, QStringLiteral("0.1"));
    QCOMPARE(stats->all.bep42[int(bep42::Status::Exempt)], double(N));
    QCOMPARE(stats->all.rttWeight, double(N));
    QVERIFY(stats->all.rttMedianMs >= 0);
    QCOMPARE(stats->all.distinctPorts, N);

    // Every answering node gets its feature checks. Ours support BEP 51 and
    // BEP 44, answer an unknown query with 204 and send "ip"; being IPv4
    // only here, they list no IPv6 nodes.
    const auto checked = [&] {
        int done = 0;
        monitor->catalog().forEach([&](NodeCatalog::Slot, const CatalogEntry &e) {
            done += e.state == CatalogEntry::State::Responsive && e.featuresDone() ? 1 : 0;
        });
        return done;
    };
    QTRY_COMPARE_WITH_TIMEOUT(checked(), N, 20000);
    monitor->catalog().forEach([&](NodeCatalog::Slot, const CatalogEntry &e) {
        if (e.state != CatalogEntry::State::Responsive)
            return;
        using F = CatalogEntry;
        QVERIFY(e.has(F::Has51));
        QVERIFY(e.has(F::Has44));
        QVERIFY(e.has(F::Tested32) && !e.has(F::Has32));
        QVERIFY(e.has(F::TestedIp) && e.has(F::SendsIp));
        QVERIFY(e.has(F::Answers204) && !e.has(F::AnswersOther));
        QVERIFY(!e.has(F::FeatureQueued));
        QVERIFY(e.selfListShare != F::NoShare);
        QVERIFY(!e.has(F::ListsBogons));
    });
    QTRY_VERIFY_WITH_TIMEOUT(monitor->snapshot().crawl.stats->all.answers204.tested == double(N), 10000);
    const auto features = monitor->snapshot().crawl.stats;
    QCOMPARE(features->all.bep51.yes, double(N));
    QCOMPARE(features->all.bep44.yes, double(N));
    QCOMPARE(features->all.bep32.tested, double(N));
    QCOMPARE(features->all.bep32.yes, 0.0);
    QCOMPARE(features->all.sendsIp.yes, double(N));
    QCOMPARE(features->all.answers204.yes, double(N));
    QCOMPARE(features->all.bep51SamplesMedian, 0.0);
    QCOMPARE(monitor->snapshot().crawl.featureWaiting, 0);
    QVERIFY(monitor->snapshot().crawl.featureQueries >= 3 * N);
    // All on 127.0.0.x: one dense subnet, and every node lists only it.
    QCOMPARE(features->suspicious.denseSubnetCount, 1);
    QCOMPARE(features->suspicious.selfPointerCount, N);
    QVERIFY(features->suspicious.flaggedCount >= N - 1);

    // A read-only node querying us shows up among the queries to us.
    QUdpSocket ro;
    QVERIFY(ro.bind(QHostAddress(QStringLiteral("127.0.0.99")), 0));
    BValue::Dict args = withId(NodeId::random());
    ro.writeDatagram(krpc::encodeQuery("ro", "ping", std::move(args), "XX\x01\x02", true),
                     QHostAddress(QHostAddress::LocalHost), portOf(*monitor));
    QTRY_VERIFY_WITH_TIMEOUT(monitor->snapshot().crawl.stats->inbound.readOnlyAddresses == 1, 10000);
    const InboundSummary inbound = monitor->snapshot().crawl.stats->inbound;
    QVERIFY(inbound.queries >= 1);
    QVERIFY(inbound.addresses >= 1);
    QVERIFY(std::any_of(inbound.methods.begin(), inbound.methods.end(),
                        [](const auto &m) { return m.first == QLatin1String("ping"); }));

    // And lookups for random IDs start producing size estimates, and a
    // measure of how lookups perform.
    QTRY_VERIFY_WITH_TIMEOUT(monitor->snapshot().crawl.sizeV4.samples > 0, 15000);
    QVERIFY(monitor->snapshot().crawl.sizeV4.median > 0);
    QCOMPARE(monitor->snapshot().crawl.sizeV6.samples, 0);
    const LookupPerformance lookups = monitor->snapshot().crawl.lookupV4;
    QVERIFY(lookups.samples > 0);
    QVERIFY(lookups.medianMs >= 0 && lookups.p90Ms >= lookups.medianMs);
    QVERIFY(lookups.medianQueries > 0);
    QVERIFY(lookups.medianHops >= 0);
    QVERIFY(lookups.responseRate > 0.9);  // everyone answers on loopback
    QVERIFY(lookups.fullShare > 0);  // early ones ran with few nodes known
    QCOMPARE(monitor->snapshot().crawl.lookupV6.samples, 0);

    // The history samples while scanning, and a pause leaves a gap.
    QTRY_VERIFY_WITH_TIMEOUT(monitor->snapshot().crawl.history && monitor->snapshot().crawl.history->size() >= 3,
                             10000);
    auto history = monitor->snapshot().crawl.history;
    const HistorySample latest = history->back();
    QCOMPARE(latest.value(Metric::AnsweringIps), double(N));
    QCOMPARE(latest.value(Metric::Bep51Share), 1.0);
    QVERIFY(latest.value(Metric::QueriesPerSecond) >= 0);
    QVERIFY(latest.value(Metric::SizeEstimate) > 0);
    QVERIFY(latest.value(Metric::LookupMedianMs) >= 0);
    QVERIFY(std::isnan(latest.value(Metric::RttMedianMs)) || latest.value(Metric::RttMedianMs) >= 0);
    QCOMPARE(int(latest.clientShares.size()), 1);
    QCOMPARE(latest.clientShares[0].second, 1.0);
    QVERIFY(history->front().gapBefore);
    for (size_t i = 1; i < history->size(); ++i) {
        QVERIFY(!(*history)[i].gapBefore);
        QVERIFY((*history)[i].atMs > (*history)[i - 1].atMs);
    }
    monitor->setMonitoring(false);
    const size_t paused = monitor->snapshot().crawl.history->size();
    QTest::qWait(1200);
    QCOMPARE(monitor->snapshot().crawl.history->size(), paused);  // nothing while paused
    monitor->setMonitoring(true);
    QTRY_VERIFY_WITH_TIMEOUT(monitor->snapshot().crawl.history->size() > paused, 10000);
    QVERIFY((*monitor->snapshot().crawl.history)[paused].gapBefore);
}

void TestEngine::monitoringMarksSilentAndGoneNodes()
{
    constexpr int N = 5;
    auto chain = startChain(N);
    QCOMPARE(int(chain.size()), N);

    // A socket that never answers, and a node that lists it.
    QUdpSocket dead;
    QVERIFY(dead.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const Endpoint deadEndpoint(QHostAddress(QHostAddress::LocalHost), dead.localPort());

    QUdpSocket lister;
    QVERIFY(lister.bind(QHostAddress(QHostAddress::LocalHost), 0));
    const NodeId listerId = NodeId::random();
    connect(&lister, &QUdpSocket::readyRead, this, [&] {
        while (lister.hasPendingDatagrams()) {
            const QNetworkDatagram d = lister.receiveDatagram();
            const auto parsed = krpc::parse(d.data());
            if (!parsed.message || parsed.message->type != krpc::MessageType::Query)
                continue;
            BValue::Dict values;
            values.emplace("id", BValue(listerId.toBytes()));
            values.emplace("nodes", BValue(krpc::encodeNodes({{NodeId::random(), deadEndpoint}}, Family::IPv4)));
            lister.writeDatagram(krpc::encodeResponse(parsed.message->transactionId, values, {}, Endpoint()),
                                 d.senderAddress(), quint16(d.senderPort()));
        }
    });

    auto monitor = startEngineWith(monitorConfig());
    QVERIFY(monitor);
    introduce(*monitor, *chain.back());
    monitor->addNode(QStringLiteral("127.0.0.1"), lister.localPort());
    QTRY_VERIFY_WITH_TIMEOUT(nodeCount(*monitor) >= 2, 5000);

    monitor->setMonitoring(true);
    QTRY_COMPARE_WITH_TIMEOUT(catalogCount(*monitor, CatalogEntry::State::Responsive), N + 1, 20000);
    QTRY_COMPARE_WITH_TIMEOUT(catalogCount(*monitor, CatalogEntry::State::Silent), 1, 10000);
    const auto deadSlot = monitor->catalog().find(deadEndpoint);
    QVERIFY(deadSlot != NodeCatalog::NoSlot);
    QCOMPARE(monitor->catalog().at(deadSlot).state, CatalogEntry::State::Silent);
    QVERIFY(monitor->catalog().at(deadSlot).failures >= 2);
    QCOMPARE(monitor->catalog().at(deadSlot).lastAnswered, quint32(0));

    // Two nodes stop. Once rechecked without an answer they are gone rather
    // than silent, because they answered before.
    const Endpoint stoppedA = endpointOf(*chain[1]);
    const Endpoint stoppedB = endpointOf(*chain[3]);
    chain[1].reset();
    chain[3].reset();
    QTRY_COMPARE_WITH_TIMEOUT(catalogCount(*monitor, CatalogEntry::State::Gone), 2, 15000);
    for (const Endpoint &stopped : {stoppedA, stoppedB}) {
        const auto slot = monitor->catalog().find(stopped);
        QVERIFY(slot != NodeCatalog::NoSlot);
        QCOMPARE(monitor->catalog().at(slot).state, CatalogEntry::State::Gone);
    }
    QCOMPARE(catalogCount(*monitor, CatalogEntry::State::Responsive), N + 1 - 2);
    QVERIFY(monitor->snapshot().crawl.timeouts >= 6);

    // Churn: two addresses stopped within the hour, after short spells.
    const auto churn = [&] { return monitor->snapshot().crawl.stats->ipv4.churn; };
    QTRY_COMPARE_WITH_TIMEOUT(churn().departedLastHour, 2, 10000);
    QCOMPARE(churn().sessionBins[0], 2);
    QVERIFY(churn().medianSessionMs >= 0);
    QCOMPARE(churn().returnedLastHour, 0);
    // Everyone answering came up within the last ten minutes, nobody an
    // hour ago.
    QCOMPARE(churn().uptimeBins[0], monitor->snapshot().crawl.stats->ipv4.answeringIps);
    QCOMPARE(churn().survivalBase[0], 0);

    // One comes back on the same address and port: a return, not an arrival.
    EngineConfig again = loopbackConfig();
    again.bindAddressV4 = stoppedA.address;
    again.port = stoppedA.port;
    chain[1] = startEngineWith(again);
    QVERIFY(chain[1]);
    const auto slotA = monitor->catalog().find(stoppedA);
    QTRY_COMPARE_WITH_TIMEOUT(monitor->catalog().at(slotA).state, CatalogEntry::State::Responsive, 15000);
    QVERIFY(monitor->catalog().at(slotA).has(CatalogEntry::Rejoined));
    QTRY_COMPARE_WITH_TIMEOUT(churn().returnedLastHour, 1, 10000);
    QCOMPARE(churn().departedLastHour, 1);
    // Nodes that were never away are not returns.
    const auto slotLive = monitor->catalog().find(endpointOf(*chain[0]));
    QVERIFY(!monitor->catalog().at(slotLive).has(CatalogEntry::Rejoined));

    // The history saw it all, with a sample every few hundred milliseconds.
    const auto history = monitor->snapshot().crawl.history;
    QVERIFY(history && history->size() >= 5);
    QVERIFY(history->front().gapBefore);
    double mostDeparted = 0;
    for (const HistorySample &s : *history) {
        QVERIFY(s.spanMs > 0);
        if (!std::isnan(s.value(Metric::DepartedPerHour)))
            mostDeparted = std::max(mostDeparted, s.value(Metric::DepartedPerHour));
    }
    QCOMPARE(mostDeparted, 2.0);
}

void TestEngine::monitoringPausesAndHonoursTheCap()
{
    constexpr int N = 10;
    auto chain = startChain(N);
    QCOMPARE(int(chain.size()), N);

    EngineConfig config = monitorConfig();
    config.catalogCap = 4;
    auto monitor = startEngineWith(config);
    QVERIFY(monitor);
    introduce(*monitor, *chain.back());
    QTRY_VERIFY_WITH_TIMEOUT(nodeCount(*monitor) >= 1, 5000);

    // Never more than the cap, however much it hears of.
    monitor->setMonitoring(true);
    QDeadlineTimer watch(4000);
    while (!watch.hasExpired()) {
        QVERIFY2(monitor->catalog().size() <= 4, qPrintable(QString::number(monitor->catalog().size())));
        QTest::qWait(50);
    }
    QVERIFY(monitor->catalog().evicted() > 0);

    // Paused: no new queries, and what it knows stays.
    monitor->setMonitoring(false);
    QCOMPARE(monitor->snapshot().crawl.phase, CrawlSnapshot::Phase::Off);
    QTest::qWait(1000);  // let anything already sent come back
    const qint64 queries = monitor->snapshot().crawl.queries;
    const int known = monitor->catalog().size();
    QVERIFY(known > 0);
    QTest::qWait(1500);
    QCOMPARE(monitor->snapshot().crawl.queries, queries);
    QCOMPARE(monitor->catalog().size(), known);

    // Lowering the cap while paused trims at once.
    monitor->setCatalogCap(2);
    QCOMPARE(monitor->catalog().size(), 2);

    // Resuming picks up where it left off; shutting down discards it all.
    monitor->setMonitoring(true);
    QTRY_VERIFY_WITH_TIMEOUT(monitor->snapshot().crawl.queries > queries, 5000);
    monitor->shutdown();
    QCOMPARE(monitor->catalog().size(), 0);
    QVERIFY(!monitor->isMonitoring());
}

// One address, many ports: the scan asks them all, but never lets more
// than a couple of queries wait behind that address, and pausing stops the
// traffic promptly.
void TestEngine::monitoringDoesNotQueueBehindABusyHost()
{
    constexpr int Ports = 30;
    std::vector<std::unique_ptr<QUdpSocket>> nodes;
    std::vector<NodeId> ids;
    for (int i = 0; i < Ports; ++i) {
        nodes.push_back(std::make_unique<QUdpSocket>());
        QVERIFY(nodes.back()->bind(QHostAddress(QHostAddress::LocalHost), 0));
        ids.push_back(NodeId::random());
    }
    // Every one of them answers, listing all the others.
    std::vector<krpc::CompactNode> all;
    for (int i = 0; i < Ports; ++i)
        all.push_back({ids[i], Endpoint(QHostAddress(QHostAddress::LocalHost), nodes[i]->localPort())});
    int answered = 0;
    for (int i = 0; i < Ports; ++i) {
        QUdpSocket *socket = nodes[i].get();
        const NodeId id = ids[i];
        connect(socket, &QUdpSocket::readyRead, this, [&, socket, id] {
            while (socket->hasPendingDatagrams()) {
                const QNetworkDatagram d = socket->receiveDatagram();
                const auto parsed = krpc::parse(d.data());
                if (!parsed.message || parsed.message->type != krpc::MessageType::Query)
                    continue;
                ++answered;
                BValue::Dict values;
                values.emplace("id", BValue(id.toBytes()));
                values.emplace("nodes", BValue(krpc::encodeNodes(all, Family::IPv4)));
                socket->writeDatagram(krpc::encodeResponse(parsed.message->transactionId, values, {}, Endpoint()),
                                      d.senderAddress(), quint16(d.senderPort()));
            }
        });
    }

    // The real per-host limit this time: two a second for the one address.
    // Size-estimating lookups would share that allowance, so they are off.
    EngineConfig config = monitorConfig();
    config.hostLimit = HostLimit{};
    config.crawl.sizeEstimateIntervalMs = 0;
    auto monitor = startEngineWith(config);
    QVERIFY(monitor);
    monitor->addNode(QStringLiteral("127.0.0.1"), nodes[0]->localPort());
    QTRY_VERIFY_WITH_TIMEOUT(nodeCount(*monitor) >= 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(monitor->node(Family::IPv4)->stats().activeLookups, 0, 60000);

    monitor->setMonitoring(true);
    QDeadlineTimer watch(6000);
    int mostQueued = 0;
    while (!watch.hasExpired()) {
        mostQueued = std::max(mostQueued, monitor->node(Family::IPv4)->rpcQueued());
        QTest::qWait(20);
    }
    // The crawler's own share stays at MaxQueuedPerHost; an ordinary lookup
    // may add its three. Without the limit this reaches the dozens.
    QVERIFY2(mostQueued <= Crawler::MaxQueuedPerHost + Lookup::Alpha, qPrintable(QString::number(mostQueued)));
    QVERIFY(catalogCount(*monitor, CatalogEntry::State::Responsive) >= 8);  // about two a second
    QCOMPARE(monitor->snapshot().crawl.notSent, qint64(0));
    QVERIFY(answered > 0);

    // Paused: little is left waiting, so traffic stops promptly.
    monitor->setMonitoring(false);
    QTest::qWait(1200);
    QVERIFY2(monitor->node(Family::IPv4)->rpcQueued() <= Lookup::Alpha,
             qPrintable(QString::number(monitor->node(Family::IPv4)->rpcQueued())));
}

namespace {

// A node that answers every query, listing whatever it is given.
struct Lister
{
    QUdpSocket socket;
    NodeId id = NodeId::random();
    std::vector<krpc::CompactNode> lists;

    bool start(QObject *context, const QHostAddress &address)
    {
        if (!socket.bind(address, 0))
            return false;
        QObject::connect(&socket, &QUdpSocket::readyRead, context, [this] {
            while (socket.hasPendingDatagrams()) {
                const QNetworkDatagram d = socket.receiveDatagram();
                const auto parsed = krpc::parse(d.data());
                if (!parsed.message || parsed.message->type != krpc::MessageType::Query)
                    continue;
                BValue::Dict values;
                values.emplace("id", BValue(id.toBytes()));
                values.emplace("nodes", BValue(krpc::encodeNodes(lists, Family::IPv4)));
                socket.writeDatagram(krpc::encodeResponse(parsed.message->transactionId, values, {}, Endpoint()),
                                     d.senderAddress(), quint16(d.senderPort()));
            }
        });
        return true;
    }

    Endpoint endpoint() const { return Endpoint(socket.localAddress(), socket.localPort()); }
};

EngineConfig counterConfig()
{
    EngineConfig config = loopbackConfig();
    config.census.slices = 2;
    config.census.sliceBits = 0;  // the whole ID space: the count is exact
    config.census.queryTimeoutMs = 400;
    config.crawl.sizeEstimateIntervalMs = 0;
    return config;
}

} // namespace

// With the slice covering the whole ID space, the count is exact: every
// address heard of, and every address that answered, counted once however
// many nodes it runs.
void TestEngine::censusCountsAddressesExactly()
{
    constexpr int N = 10;
    auto chain = startChain(N, 30);
    QCOMPARE(int(chain.size()), N);

    // Two nodes sharing one address.
    std::vector<std::unique_ptr<DhtEngine>> shared;
    for (int i = 0; i < 2; ++i) {
        EngineConfig config = loopbackConfig();
        config.bindAddressV4 = QHostAddress(QStringLiteral("127.0.0.50"));
        shared.push_back(startEngineWith(config));
        QVERIFY(shared.back());
        introduce(*shared.back(), *chain[i]);
    }
    introduce(*chain[5], *shared[0]);
    introduce(*chain[6], *shared[1]);

    // A node that answers, listing one that never does.
    QUdpSocket dead;
    QVERIFY(dead.bind(QHostAddress(QStringLiteral("127.0.0.61")), 0));
    Lister lister;
    QVERIFY(lister.start(this, QHostAddress(QStringLiteral("127.0.0.60"))));
    lister.lists = {{NodeId::random(), Endpoint(dead.localAddress(), dead.localPort())},
                    {shared[0]->node(Family::IPv4)->id(), endpointOf(*shared[0])},
                    {shared[1]->node(Family::IPv4)->id(), endpointOf(*shared[1])}};

    auto counter = startEngineWith(counterConfig());
    QVERIFY(counter);
    introduce(*counter, *chain.back());
    counter->addNode(QStringLiteral("127.0.0.60"), lister.endpoint().port);
    QTRY_VERIFY_WITH_TIMEOUT(nodeCount(*counter) >= 2, 5000);

    QCOMPARE(counter->snapshot().census.state, CensusSnapshot::State::Idle);
    counter->startCensus();
    QVERIFY(counter->isCensusRunning());
    QCOMPARE(counter->snapshot().census.slicesTotal, 2);
    QTRY_COMPARE_WITH_TIMEOUT(counter->snapshot().census.state, CensusSnapshot::State::Done, 60000);

    const CensusSnapshot census = counter->snapshot().census;
    QCOMPARE(census.slicesDone, 2);
    QCOMPARE(int(census.slices.size()), 2);
    for (const CensusSlice &slice : census.slices) {
        QCOMPARE(slice.bits, 0);
        // N chain addresses, the shared one, the lister: all answered.
        QCOMPARE(slice.ipsAnswered, N + 2);
        QCOMPARE(slice.ipsHeard, N + 3);  // and the one that never answers
        QVERIFY(slice.nodesAnswered >= N + 3);  // both shared nodes are found
        QCOMPARE(slice.heardEstimate, double(N + 3));
        QCOMPARE(slice.connectedEstimate, double(N + 2));
        QVERIFY(slice.rounds >= 3);  // at least two quiet rounds after the first
        QVERIFY(slice.queries > 0);
    }
    QCOMPARE(census.ipv4.slices, 2);
    QCOMPARE(census.ipv4.heard, double(N + 3));
    QCOMPARE(census.ipv4.heardLow, double(N + 3));
    QCOMPARE(census.ipv4.heardHigh, double(N + 3));
    QCOMPARE(census.ipv4.connected, double(N + 2));
    QCOMPARE(census.ipv6.slices, 0);
    QVERIFY(census.queries > 0);
    QVERIFY(!counter->isCensusRunning());
}

// Slice width follows the size estimate; cancelling stops the count.
void TestEngine::censusChoosesSlicesAndCancels()
{
    auto chain = startChain(4, 70);
    QCOMPARE(int(chain.size()), 4);

    EngineConfig config = counterConfig();
    config.census.sliceBits = -1;
    config.census.slices = 50;
    auto counter = startEngineWith(config);
    QVERIFY(counter);
    introduce(*counter, *chain.back());
    QTRY_VERIFY_WITH_TIMEOUT(nodeCount(*counter) >= 1, 5000);

    // No size estimate yet: the default width.
    counter->startCensus();
    QVERIFY(counter->isCensusRunning());
    QCOMPARE(counter->snapshot().census.bits, config.census.defaultSliceBits);
    QCOMPARE(counter->snapshot().census.slicesTotal, 50);

    counter->cancelCensus();
    QVERIFY(!counter->isCensusRunning());
    CensusSnapshot census = counter->snapshot().census;
    QCOMPARE(census.state, CensusSnapshot::State::Cancelled);
    const qint64 queries = census.queries;
    QTest::qWait(1000);
    QCOMPARE(counter->snapshot().census.queries, queries);

    // Shutting down discards it.
    counter->shutdown();
    QCOMPARE(counter->snapshot().census.state, CensusSnapshot::State::Idle);
}

#include "TestEngine.moc"
