#include "dhtcore/Krpc.h"
#include "dhtcore/Lookup.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTest>

#include <optional>

using namespace dht;

namespace {

Endpoint at(int host, quint16 port = 6881)
{
    return Endpoint(QHostAddress(QStringLiteral("127.0.0.%1").arg(host)), port);
}

// An ID whose distance to the all-zero target is set by its first byte.
NodeId idAt(quint8 first, quint8 second = 0)
{
    NodeId id;
    id[0] = first;
    id[1] = second;
    return id;
}

// A reply listing `nodes`, as a node answering find_node would send.
RpcReply response(const NodeId &from, const std::vector<krpc::CompactNode> &nodes)
{
    RpcReply reply;
    reply.status = RpcReply::Status::Response;
    reply.message.type = krpc::MessageType::Response;
    BValue::Dict body;
    body.emplace("id", BValue(from.toBytes()));
    if (!nodes.empty())
        body.emplace("nodes", BValue(krpc::encodeNodes(nodes, Family::IPv4)));
    reply.message.body = BValue(std::move(body));
    return reply;
}

RpcReply timeout()
{
    RpcReply reply;
    reply.status = RpcReply::Status::Timeout;
    return reply;
}

// A lookup wired to a scripted network: every query is recorded and
// answered only when the test says so.
class Harness
{
public:
    struct Sent
    {
        Endpoint to;
        RpcManager::Callback callback;
        int timeoutMs = 0;
        bool answered = false;
        RpcManager::SentFn onSent;
        bool left = false;  // handed to the network, rather than queued
    };

    // With `autoSend` off, queries sit as if the per-host limit were
    // holding them back until the test releases them.
    explicit Harness(int slowAfterMs = 120, int timeoutMs = 400, bool autoSend = true)
    {
        lookup = new Lookup(
            Lookup::Kind::FindNode, NodeId(), Family::IPv4, idAt(0xff), true,
            [this, autoSend](const Endpoint &to, const QByteArray &, BValue::Dict, RpcManager::Callback callback,
                             int timeoutMs, RpcManager::SentFn onSent) {
                sent.push_back({to, std::move(callback), timeoutMs, false, std::move(onSent), false});
                if (autoSend)
                    release(sent.size() - 1);
            },
            [this](const Lookup::Result &r) { result = r; });
        lookup->setTimeouts(slowAfterMs, timeoutMs);
    }

    void release(size_t index)
    {
        sent[index].left = true;
        if (sent[index].onSent)
            sent[index].onSent();
    }
    void releaseAll()
    {
        for (size_t i = 0; i < sent.size(); ++i) {
            if (!sent[i].left)
                release(i);
        }
    }
    ~Harness()
    {
        if (lookup)
            delete lookup;
    }

    void answer(size_t index, const std::vector<krpc::CompactNode> &nodes = {})
    {
        QVERIFY2(index < sent.size(), "no such query");
        // The callback can start more queries, which may move the vector.
        const RpcManager::Callback callback = sent[index].callback;
        sent[index].answered = true;
        callback(response(idAt(0x10, quint8(index)), nodes));
    }
    void fail(size_t index)
    {
        const RpcManager::Callback callback = sent[index].callback;
        sent[index].answered = true;
        callback(timeout());
    }
    // Queries still waiting for an answer.
    int open() const
    {
        int n = 0;
        for (const Sent &s : sent)
            n += s.answered ? 0 : 1;
        return n;
    }
    bool queriedTwice() const
    {
        QSet<Endpoint> seen;
        for (const Sent &s : sent) {
            if (seen.contains(s.to))
                return true;
            seen.insert(s.to);
        }
        return false;
    }

    QPointer<Lookup> lookup;
    std::vector<Sent> sent;
    std::optional<Lookup::Result> result;
};

} // namespace

class TestLookup : public QObject
{
    Q_OBJECT

private slots:
    void keepsAlphaQueriesInFlight();
    void slowQueriesFreeTheirSlot();
    void countsASlowAnswerThatArrives();
    void finishesOnceTheClosestHaveAnswered();
    void waitsForSomethingCloser();
    void endsWhenNothingIsLeftToAsk();
    void passesItsTimeoutToTheQuery();
    void waitsForQueriesThatHaveNotLeftYet();
};

void TestLookup::keepsAlphaQueriesInFlight()
{
    Harness h;
    for (int i = 1; i <= 10; ++i)
        h.lookup->addCandidate(idAt(quint8(i)), at(i));
    h.lookup->start();

    // Three at a time, closest first, and each answer makes room for one more.
    QCOMPARE(int(h.sent.size()), Lookup::Alpha);
    QCOMPARE(h.sent[0].to, at(1));
    QCOMPARE(h.sent[2].to, at(3));
    h.answer(0);
    QCOMPARE(int(h.sent.size()), Lookup::Alpha + 1);
    h.fail(1);
    QCOMPARE(int(h.sent.size()), Lookup::Alpha + 2);
    QVERIFY(!h.queriedTwice());
    QVERIFY(!h.result);
}

void TestLookup::slowQueriesFreeTheirSlot()
{
    Harness h(120, 5000);
    for (int i = 1; i <= 10; ++i)
        h.lookup->addCandidate(idAt(quint8(i)), at(i));
    h.lookup->start();
    QCOMPARE(int(h.sent.size()), Lookup::Alpha);

    // Nobody answers: after the slow mark, three more go out anyway, up to
    // the cap on outstanding queries.
    QTRY_COMPARE_WITH_TIMEOUT(int(h.sent.size()), 2 * Lookup::Alpha, 2000);
    QCOMPARE(h.open(), Lookup::MaxOutstanding);
    QTest::qWait(400);
    QCOMPARE(int(h.sent.size()), Lookup::MaxOutstanding);  // no further pile-up
    QVERIFY(!h.queriedTwice());
    QVERIFY(!h.result);  // the slow ones may still answer
}

void TestLookup::countsASlowAnswerThatArrives()
{
    Harness h(120, 5000);
    for (int i = 1; i <= 6; ++i)
        h.lookup->addCandidate(idAt(quint8(i)), at(i));
    h.lookup->start();
    QTRY_VERIFY_WITH_TIMEOUT(int(h.sent.size()) > Lookup::Alpha, 2000);

    // A query that went slow still counts if it answers before its timeout.
    h.answer(0);
    for (size_t i = 1; i < h.sent.size(); ++i)
        h.fail(i);
    QTRY_VERIFY_WITH_TIMEOUT(h.result.has_value(), 5000);
    QCOMPARE(int(h.result->closest.size()), 1);
    QCOMPARE(h.result->closest[0].endpoint, at(1));
    QCOMPARE(h.result->responded, 1);
    QCOMPARE(h.result->queried, int(h.sent.size()));
}

void TestLookup::finishesOnceTheClosestHaveAnswered()
{
    Harness h(120, 5000);
    // Three middling nodes to start from.
    for (int i = 0; i < 3; ++i)
        h.lookup->addCandidate(idAt(0x80, quint8(i)), at(50 + i));
    h.lookup->start();
    QCOMPARE(int(h.sent.size()), 3);

    // The first hands over eight much closer nodes; the other two never answer.
    std::vector<krpc::CompactNode> closer;
    for (int i = 0; i < Lookup::K; ++i)
        closer.push_back({idAt(0x01, quint8(i)), at(10 + i)});
    QElapsedTimer clock;
    clock.start();
    h.answer(0, closer);

    // The two that have yet to answer still hold their slots, so only one
    // of the closer nodes is asked at first; answering frees the way.
    QCOMPARE(int(h.sent.size()), 4);
    for (size_t i = 3; i < h.sent.size(); ++i) {
        h.answer(i);
        if (h.result)
            break;
    }
    QVERIFY(h.result.has_value());
    QVERIFY(clock.elapsed() < 1000);  // no timeout was waited for
    QCOMPARE(int(h.result->closest.size()), Lookup::K);
    for (int i = 0; i < Lookup::K; ++i)
        QCOMPARE(h.result->closest[size_t(i)].endpoint, at(10 + i));
    QCOMPARE(h.result->hops, 1);
    QCOMPARE(h.open(), 2);  // the stragglers, never answered
    QVERIFY(!h.queriedTwice());
}

void TestLookup::waitsForSomethingCloser()
{
    Harness h(120, 5000);
    // Eight nodes that answer, and one closer that is still being asked.
    h.lookup->addCandidate(idAt(0x01), at(1));
    for (int i = 0; i < Lookup::K; ++i)
        h.lookup->addCandidate(idAt(0x02, quint8(i)), at(10 + i));
    h.lookup->start();

    size_t answered = 0;
    while (answered < h.sent.size()) {
        // Answer everything except the closest, which stays silent.
        for (size_t i = 0; i < h.sent.size(); ++i) {
            if (!h.sent[i].answered && h.sent[i].to != at(1))
                h.answer(i);
        }
        ++answered;
        if (h.open() <= 1)
            break;
    }
    QVERIFY2(!h.result, "the closest node is still outstanding");
    h.fail(0);
    QTRY_VERIFY(h.result.has_value());
    QCOMPARE(int(h.result->closest.size()), Lookup::K);
    QCOMPARE(h.result->closest[0].endpoint, at(10));  // the silent one is not in it
}

void TestLookup::endsWhenNothingIsLeftToAsk()
{
    Harness h;
    h.lookup->addCandidate(idAt(0x01), at(1));
    h.lookup->addCandidate(idAt(0x02), at(2));
    h.lookup->start();
    QCOMPARE(int(h.sent.size()), 2);
    h.fail(0);
    QVERIFY(!h.result);
    h.fail(1);
    QVERIFY(h.result.has_value());
    QVERIFY(h.result->closest.empty());
    QCOMPARE(h.result->queried, 2);
    QCOMPARE(h.result->responded, 0);
    QCOMPARE(h.result->hops, -1);
    QVERIFY(h.result->durationMs >= 0);
}

void TestLookup::passesItsTimeoutToTheQuery()
{
    Harness h(250, 900);
    h.lookup->addCandidate(idAt(0x01), at(1));
    h.lookup->start();
    QCOMPARE(int(h.sent.size()), 1);
    QCOMPARE(h.sent[0].timeoutMs, 900);

    // A slow mark longer than the timeout is capped at it.
    Harness capped(5000, 800);
    capped.lookup->addCandidate(idAt(0x01), at(1));
    capped.lookup->start();
    QCOMPARE(capped.sent[0].timeoutMs, 800);
    QTest::qWait(300);
    QCOMPARE(int(capped.sent.size()), 1);  // nothing more to ask anyway
}

// A query waiting its turn under the per-host limit has not had its
// chance, so it must not free its slot and let another pile in behind it.
void TestLookup::waitsForQueriesThatHaveNotLeftYet()
{
    Harness h(120, 5000, false);
    for (int i = 1; i <= 10; ++i)
        h.lookup->addCandidate(idAt(quint8(i)), at(i));
    h.lookup->start();
    QCOMPARE(int(h.sent.size()), Lookup::Alpha);

    QTest::qWait(400);
    QCOMPARE(int(h.sent.size()), Lookup::Alpha);  // nothing left, nothing added

    h.releaseAll();
    QTRY_COMPARE_WITH_TIMEOUT(int(h.sent.size()), 2 * Lookup::Alpha, 2000);
}

int runTestLookup(int argc, char **argv)
{
    TestLookup test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestLookup.moc"
