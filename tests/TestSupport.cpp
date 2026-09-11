#include "dhtcore/Support.h"

#include <QTest>

using namespace dht;

class TestSupport : public QObject
{
    Q_OBJECT

private slots:
    void tokensValidateForTenMinutes();
    void tokensAreBoundToAddress();
    void voterNeedsAgreement();
    void voterIgnoresRepeatVoter();
    void voterFollowsAddressChange();
    void storageBasics();
    void storageExpiresPeers();
    void storagePeerCap();
    void rateLimiterRefills();
};

void TestSupport::tokensValidateForTenMinutes()
{
    TokenManager tokens;
    const QHostAddress address(QStringLiteral("203.0.113.9"));
    const qint64 t0 = 1'000'000;
    const QByteArray token = tokens.generate(address, t0);

    QVERIFY(tokens.validate(token, address, t0 + TokenManager::RotationMs - 1));
    QVERIFY(tokens.validate(token, address, t0 + TokenManager::RotationMs + 1)); // previous secret
    QVERIFY(!tokens.validate(token, address, t0 + 2 * TokenManager::RotationMs + 1));
}

void TestSupport::tokensAreBoundToAddress()
{
    TokenManager tokens;
    const QByteArray token = tokens.generate(QHostAddress(QStringLiteral("203.0.113.9")), 0);
    QVERIFY(!tokens.validate(token, QHostAddress(QStringLiteral("203.0.113.10")), 0));
    QVERIFY(!tokens.validate(QByteArray("nope"), QHostAddress(QStringLiteral("203.0.113.9")), 0));
}

void TestSupport::voterNeedsAgreement()
{
    ExternalIpVoter voter;
    const QHostAddress us(QStringLiteral("198.51.100.20"));
    QVERIFY(!voter.addVote(QHostAddress(QStringLiteral("8.8.8.1")), us, 1));
    QVERIFY(!voter.addVote(QHostAddress(QStringLiteral("8.8.8.2")), us, 2));
    QVERIFY(voter.consensus().isNull());
    QVERIFY(voter.addVote(QHostAddress(QStringLiteral("8.8.8.3")), us, 3));
    QCOMPARE(voter.consensus(), us);
    QVERIFY(!voter.addVote(QHostAddress(QStringLiteral("8.8.8.4")), us, 4)); // unchanged
}

void TestSupport::voterIgnoresRepeatVoter()
{
    ExternalIpVoter voter;
    const QHostAddress liar(QStringLiteral("8.8.8.8"));
    for (int i = 0; i < 10; ++i)
        voter.addVote(liar, QHostAddress(QStringLiteral("192.0.2.1")), i);
    QVERIFY(voter.consensus().isNull());
    QCOMPARE(voter.voterCount(), 1);
}

void TestSupport::voterFollowsAddressChange()
{
    ExternalIpVoter voter;
    const QHostAddress oldAddress(QStringLiteral("198.51.100.20"));
    const QHostAddress newAddress(QStringLiteral("198.51.100.99"));
    for (int i = 0; i < 4; ++i)
        voter.addVote(QHostAddress(quint32(0x08080800 + i)), oldAddress, i);
    QCOMPARE(voter.consensus(), oldAddress);

    // The same voters now report the new address.
    bool changed = false;
    for (int i = 0; i < 4; ++i)
        changed = voter.addVote(QHostAddress(quint32(0x08080800 + i)), newAddress, 100 + i) || changed;
    QVERIFY(changed);
    QCOMPARE(voter.consensus(), newAddress);
}

void TestSupport::storageBasics()
{
    PeerStorage storage;
    const NodeId infohash = NodeId::random();
    const Endpoint v4(QHostAddress(QStringLiteral("203.0.113.1")), 1000);
    const Endpoint v6(QHostAddress(QStringLiteral("2001:db8::1")), 2000);

    storage.announce(infohash, v4, 0);
    storage.announce(infohash, v4, 5); // refresh, not a new peer
    storage.announce(infohash, v6, 5);
    QCOMPARE(storage.infohashCount(), 1);
    QCOMPARE(storage.peerCount(), 2);

    const auto peers4 = storage.peers(infohash, Family::IPv4, 10);
    QCOMPARE(int(peers4.size()), 1);
    QCOMPARE(peers4[0], v4);
    QCOMPARE(int(storage.peers(infohash, Family::IPv6, 10).size()), 1);
    QVERIFY(storage.peers(NodeId::random(), Family::IPv4, 10).empty());

    const auto snapshot = storage.snapshot();
    QCOMPARE(int(snapshot.size()), 1);
    QCOMPARE(int(snapshot[0].peers.size()), 2);

    storage.clear();
    QCOMPARE(storage.infohashCount(), 0);
    QCOMPARE(storage.peerCount(), 0);
}

void TestSupport::storageExpiresPeers()
{
    PeerStorage storage;
    const NodeId infohash = NodeId::random();
    storage.announce(infohash, Endpoint(QHostAddress(QStringLiteral("203.0.113.1")), 1), 0);
    storage.announce(infohash, Endpoint(QHostAddress(QStringLiteral("203.0.113.2")), 1), PeerStorage::PeerTtlMs / 2);

    storage.expire(PeerStorage::PeerTtlMs);
    QCOMPARE(storage.peerCount(), 1);
    storage.expire(PeerStorage::PeerTtlMs * 2);
    QCOMPARE(storage.peerCount(), 0);
    QCOMPARE(storage.infohashCount(), 0);
}

void TestSupport::storagePeerCap()
{
    PeerStorage storage;
    const NodeId infohash = NodeId::random();
    for (int i = 0; i < PeerStorage::MaxPeersPerInfohash + 25; ++i)
        storage.announce(infohash, Endpoint(QHostAddress(quint32(0xCB007100 + i)), 6881), i);
    QCOMPARE(storage.peerCount(), PeerStorage::MaxPeersPerInfohash);
    QCOMPARE(int(storage.peers(infohash, Family::IPv4, 100).size()), 100);
}

void TestSupport::rateLimiterRefills()
{
    RateLimiter limiter(10.0, 5.0);
    const QHostAddress a(QStringLiteral("203.0.113.1"));
    for (int i = 0; i < 5; ++i)
        QVERIFY(limiter.allow(a, 0));
    QVERIFY(!limiter.allow(a, 0));
    QVERIFY(limiter.allow(a, 100)); // one token back after 100 ms at 10/s
    QVERIFY(limiter.allow(QHostAddress(QStringLiteral("203.0.113.2")), 0));
}

int runTestSupport(int argc, char **argv)
{
    TestSupport test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestSupport.moc"
