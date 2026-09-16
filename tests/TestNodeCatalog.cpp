#include "dhtcore/NodeCatalog.h"
#include "dhtcore/Support.h"

#include <QRandomGenerator>
#include <QTest>

#include <limits>
#include <set>
#include <vector>

using namespace dht;

namespace {

using State = CatalogEntry::State;

Endpoint v4(quint32 address, quint16 port = 6881)
{
    return Endpoint(QHostAddress(address), port);
}

} // namespace

class TestNodeCatalog : public QObject
{
    Q_OBJECT

private slots:
    void entryIsCompact();
    void addsAndFinds();
    void keepsFamiliesAndPortsApart();
    void countsStates();
    void keepsTheVersionField();
    void evictsTheSilentFirst();
    void loweringTheCapTrims();
    void refsGoStaleWhenASlotIsReused();
    void indexSurvivesChurn();
    void clearResets();
    void stampsTimes();
};

void TestNodeCatalog::entryIsCompact()
{
    QCOMPARE(sizeof(CatalogEntry), size_t(80));
    QVERIFY(NodeCatalog::bytesPerEntry() <= 100);
}

void TestNodeCatalog::addsAndFinds()
{
    NodeCatalog catalog(100);
    bool added = false;
    const auto slot = catalog.upsert(v4(0xCB007101), nowMs(), &added);
    QVERIFY(added);
    QCOMPARE(catalog.size(), 1);
    QCOMPARE(catalog.find(v4(0xCB007101)), slot);
    QCOMPARE(catalog.at(slot).endpoint(), v4(0xCB007101));
    QCOMPARE(catalog.at(slot).state, State::New);
    QVERIFY(catalog.at(slot).firstSeen > 0);

    // Same endpoint again: the same entry.
    QCOMPARE(catalog.upsert(v4(0xCB007101), nowMs(), &added), slot);
    QVERIFY(!added);
    QCOMPARE(catalog.size(), 1);
    QCOMPARE(catalog.find(v4(0xCB007102)), NodeCatalog::NoSlot);
}

void TestNodeCatalog::keepsFamiliesAndPortsApart()
{
    NodeCatalog catalog(100);
    const Endpoint a = v4(0xCB007101, 6881);
    const Endpoint b = v4(0xCB007101, 6882);
    const Endpoint c(QHostAddress(QStringLiteral("2001:db8::1")), 6881);
    // The IPv4-mapped spelling is the same host as plain IPv4.
    const Endpoint mapped(QHostAddress(QStringLiteral("::ffff:203.0.113.1")), 6881);

    const auto sa = catalog.upsert(a, 0);
    const auto sb = catalog.upsert(b, 0);
    const auto sc = catalog.upsert(c, 0);
    QCOMPARE(catalog.size(), 3);
    QVERIFY(sa != sb && sb != sc);
    QCOMPARE(catalog.find(mapped), sa);

    QVERIFY(catalog.at(sa).isIPv4());
    QCOMPARE(catalog.at(sa).family(), Family::IPv4);
    QCOMPARE(catalog.at(sc).family(), Family::IPv6);
    QCOMPARE(catalog.at(sc).endpoint(), c);
}

void TestNodeCatalog::countsStates()
{
    NodeCatalog catalog(100);
    const auto a = catalog.upsert(v4(1), 0);
    const auto b = catalog.upsert(v4(2), 0);
    catalog.upsert(v4(3), 0);
    QCOMPARE(catalog.count(State::New), 3);

    catalog.setState(a, State::Responsive);
    catalog.setState(b, State::Silent);
    catalog.setState(b, State::Silent);  // no double counting
    QCOMPARE(catalog.count(State::New), 1);
    QCOMPARE(catalog.count(State::Responsive), 1);
    QCOMPARE(catalog.count(State::Silent), 1);

    catalog.setState(a, State::Gone);
    QCOMPARE(catalog.count(State::Responsive), 0);
    QCOMPARE(catalog.count(State::Gone), 1);
}

void TestNodeCatalog::keepsTheVersionField()
{
    CatalogEntry e;
    e.setVersion(QByteArray("LT\x02\x0b", 4));
    QCOMPARE(e.versionBytes(), QByteArray("LT\x02\x0b", 4));

    e.setVersion({});
    QVERIFY(e.versionBytes().isEmpty());

    // Longer than four: the length survives, so it still reads as nonstandard.
    e.setVersion(QByteArray("ABCDEFG"));
    QCOMPARE(e.versionBytes().size(), 7);
    QCOMPARE(e.versionBytes().left(4), QByteArray("ABCD"));

    e.setVersion(QByteArray("X"));
    QCOMPARE(e.versionBytes(), QByteArray("X"));
}

void TestNodeCatalog::evictsTheSilentFirst()
{
    constexpr int Cap = 10;
    NodeCatalog catalog(Cap);
    for (int i = 0; i < Cap; ++i) {
        const auto s = catalog.upsert(v4(quint32(100 + i)), 0);
        catalog.setState(s, i == 7 ? State::Silent : State::Responsive);
    }
    QCOMPARE(catalog.size(), Cap);

    // Full: a newcomer replaces the one silent node, however the sample falls
    // (with ten entries, the sample covers them all).
    bool added = false;
    catalog.upsert(v4(500), 0, &added);
    QVERIFY(added);
    QCOMPARE(catalog.size(), Cap);
    QCOMPARE(catalog.evicted(), qint64(1));
    QCOMPARE(catalog.find(v4(107)), NodeCatalog::NoSlot);
    QCOMPARE(catalog.count(State::Silent), 0);
    QCOMPARE(catalog.count(State::Responsive), Cap - 1);
    QCOMPARE(catalog.count(State::New), 1);

    // Among equals, the one heard from longest ago goes.
    NodeCatalog aged(3);
    for (int i = 0; i < 3; ++i) {
        const auto s = aged.upsert(v4(quint32(200 + i)), 0);
        aged.setState(s, State::Responsive);
        aged.at(s).lastAnswered = quint32(50 - i * 10);  // 202 is the stalest
    }
    aged.upsert(v4(999), 0);
    QCOMPARE(aged.find(v4(202)), NodeCatalog::NoSlot);
    QVERIFY(aged.find(v4(200)) != NodeCatalog::NoSlot);
}

void TestNodeCatalog::loweringTheCapTrims()
{
    NodeCatalog catalog(1000);
    for (int i = 0; i < 1000; ++i)
        catalog.upsert(v4(quint32(1000 + i)), 0);
    catalog.setCap(100);
    QCOMPARE(catalog.size(), 1000);  // nothing until trimmed

    QCOMPARE(catalog.trim(300), 300);
    QCOMPARE(catalog.size(), 700);
    catalog.trim(NodeCatalog::MaxCap);
    QCOMPARE(catalog.size(), 100);
    QCOMPARE(catalog.evicted(), qint64(900));
    QCOMPARE(catalog.count(State::New), 100);

    // Every survivor is still reachable through the index.
    int reachable = 0;
    catalog.forEach([&](NodeCatalog::Slot slot, const CatalogEntry &e) {
        if (catalog.find(e.endpoint()) == slot)
            ++reachable;
    });
    QCOMPARE(reachable, 100);

    // A zero cap stores nothing.
    catalog.setCap(0);
    catalog.trim(NodeCatalog::MaxCap);
    QCOMPARE(catalog.upsert(v4(1), 0), NodeCatalog::NoSlot);
    QCOMPARE(catalog.size(), 0);
}

void TestNodeCatalog::refsGoStaleWhenASlotIsReused()
{
    NodeCatalog catalog(1);
    const auto first = catalog.upsert(v4(1), 0);
    const NodeCatalog::Ref ref = catalog.ref(first);
    QVERIFY(catalog.isCurrent(ref));

    const auto second = catalog.upsert(v4(2), 0);  // evicts the first
    QCOMPARE(second, first);                       // same slot, reused
    QVERIFY(!catalog.isCurrent(ref));
    QVERIFY(catalog.isCurrent(catalog.ref(second)));
    QVERIFY(!catalog.isCurrent({NodeCatalog::Slot(12345), 1}));
}

// Random additions and evictions against a reference set: the index must
// always find exactly the live entries.
void TestNodeCatalog::indexSurvivesChurn()
{
    constexpr int Cap = 5000;
    NodeCatalog catalog(Cap);
    QRandomGenerator rng(42);
    std::set<quint64> everAdded;

    for (int round = 0; round < 60000; ++round) {
        const quint32 address = 0x0A000000 | rng.bounded(20000u);  // collisions on purpose
        const quint16 port = quint16(6881 + rng.bounded(3u));
        catalog.upsert(v4(address, port), 0);
        everAdded.insert((quint64(address) << 16) | port);
        if (round % 7 == 0)
            catalog.setState(catalog.find(v4(address, port)), State(rng.bounded(5u)));
        if (round == 30000) {
            catalog.setCap(Cap / 2);
            catalog.trim(NodeCatalog::MaxCap);
            catalog.setCap(Cap);
        }
    }

    QCOMPARE(catalog.size(), Cap);
    int live = 0;
    int counted[5] = {};
    catalog.forEach([&](NodeCatalog::Slot slot, const CatalogEntry &e) {
        ++live;
        ++counted[int(e.state)];
        QCOMPARE(catalog.find(e.endpoint()), slot);
    });
    QCOMPARE(live, Cap);
    for (int s = 0; s < 5; ++s)
        QCOMPARE(catalog.count(State(s)), counted[s]);

    // Anything not live is not found.
    int found = 0;
    for (quint64 key : everAdded) {
        if (catalog.find(v4(quint32(key >> 16), quint16(key & 0xffff))) != NodeCatalog::NoSlot)
            ++found;
    }
    QCOMPARE(found, Cap);
}

void TestNodeCatalog::clearResets()
{
    NodeCatalog catalog(100);
    for (int i = 0; i < 50; ++i)
        catalog.upsert(v4(quint32(i + 1)), 0);
    QVERIFY(catalog.memoryBytes() > 0);
    catalog.clear();
    QCOMPARE(catalog.size(), 0);
    QCOMPARE(catalog.count(State::New), 0);
    QCOMPARE(catalog.find(v4(1)), NodeCatalog::NoSlot);
    QCOMPARE(catalog.memoryBytes(), qint64(0));
    QCOMPARE(catalog.cap(), 100);
    QVERIFY(catalog.upsert(v4(1), 0) != NodeCatalog::NoSlot);
}

void TestNodeCatalog::stampsTimes()
{
    NodeCatalog catalog(10);
    const qint64 t0 = nowMs();
    const quint32 a = catalog.stamp(t0);
    QVERIFY(a >= 1);
    const quint32 b = catalog.stamp(t0 + 5000);
    QCOMPARE(catalog.ageMs(a, t0 + 5000), qint64(b - a) * NodeCatalog::TickMs);
    QVERIFY(catalog.ageMs(a, t0 + 5000) >= 4900);
    QCOMPARE(catalog.ageMs(0, t0), std::numeric_limits<qint64>::max());
}

int runTestNodeCatalog(int argc, char **argv)
{
    TestNodeCatalog test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestNodeCatalog.moc"
