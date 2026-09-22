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
    void contactBudgetUnlimitedByDefault();
    void contactBudgetChargesOnlyNewEndpoints();
    void contactBudgetChangesLive();
    void contactBudgetForgetsOldEndpoints();
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

namespace {

Endpoint host(int n)
{
    return Endpoint(QHostAddress(QStringLiteral("203.0.113.%1").arg(n)), 6881);
}

} // namespace

void TestSupport::contactBudgetUnlimitedByDefault()
{
    ContactBudget budget;
    QVERIFY(!budget.isLimited());
    for (int i = 1; i < 50; ++i) {
        QVERIFY(budget.allows(host(i), 0));
        budget.record(host(i), 0);
    }
    QVERIFY(budget.allows(host(99), 0));
    // Counted even when nothing is charged, so the load can be shown.
    QCOMPARE(budget.contacts(), qint64(49));
    QCOMPARE(budget.tracked(), 49);
}

// Only an endpoint the router is not already tracking costs anything.
void TestSupport::contactBudgetChargesOnlyNewEndpoints()
{
    ContactBudget budget;
    budget.setLimit(2, 0);  // starts with a full second

    budget.record(host(1), 0);
    budget.record(host(2), 0);
    QVERIFY(!budget.allows(host(3), 0));

    // The endpoints already contacted stay free, however often we write.
    for (int i = 0; i < 5; ++i) {
        QVERIFY(budget.allows(host(1), 0));
        budget.record(host(1), 0);
    }
    QCOMPARE(budget.contacts(), qint64(2));
    // A different port is a different conversation to a router.
    QVERIFY(!budget.allows(Endpoint(host(1).address, 6882), 0));

    QVERIFY(!budget.allows(host(3), 400));   // 0.8 of a contact refilled
    QVERIFY(budget.allows(host(3), 500));
    budget.record(host(3), 500);
    QVERIFY(!budget.allows(host(4), 500));

    // Idle time saves up at most one second's worth.
    QVERIFY(budget.allows(host(4), 60'000));
    budget.record(host(4), 60'000);
    budget.record(host(5), 60'000);
    QVERIFY(!budget.allows(host(6), 60'000));
}

void TestSupport::contactBudgetChangesLive()
{
    ContactBudget budget;
    budget.setLimit(1, 0);
    budget.record(host(1), 0);
    QVERIFY(!budget.allows(host(2), 0));

    // Raising the limit refills faster from where it stands...
    budget.setLimit(100, 0);
    QVERIFY(budget.allows(host(2), 20));
    // ...lowering it caps what is saved up...
    budget.setLimit(1, 20);
    budget.record(host(2), 20);
    QVERIFY(!budget.allows(host(3), 20));
    // ...and lifting it altogether ends any wait.
    budget.setLimit(0, 20);
    QVERIFY(budget.allows(host(3), 20));
    QCOMPARE(budget.limit(), 0);
}

// Once a router would have dropped its entry, the endpoint is new again.
void TestSupport::contactBudgetForgetsOldEndpoints()
{
    ContactBudget budget;
    budget.setLimit(1, 0);
    budget.record(host(1), 0);

    const qint64 later = ContactBudget::WindowMs + 1;
    QVERIFY(budget.allows(host(1), later));
    budget.record(host(1), later);
    QCOMPARE(budget.contacts(), qint64(2));
    QVERIFY(!budget.allows(host(2), later));

    budget.prune(later);
    QCOMPARE(budget.tracked(), 1);
    budget.prune(later + ContactBudget::WindowMs);
    QCOMPARE(budget.tracked(), 0);
}

int runTestSupport(int argc, char **argv)
{
    TestSupport test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestSupport.moc"
