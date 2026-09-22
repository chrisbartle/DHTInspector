#include "dhtcore/Krpc.h"
#include "dhtcore/RpcManager.h"
#include "dhtcore/Support.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <optional>
#include <vector>

using namespace dht;

namespace {

struct Sent
{
    QByteArray datagram;
    Endpoint to;
    qint64 atMs;
};

// Records what the manager sends, and when, instead of using a socket.
struct Recorder
{
    QElapsedTimer clock;
    std::vector<Sent> sent;

    Recorder() { clock.start(); }

    RpcManager::SendFn sendFn()
    {
        return [this](const QByteArray &d, const Endpoint &to) {
            sent.push_back({d, to, clock.elapsed()});
            return true;
        };
    }

    int countTo(const QHostAddress &address) const
    {
        return int(std::count_if(sent.begin(), sent.end(), [&](const Sent &s) { return s.to.address == address; }));
    }
};

QByteArray methodOf(const QByteArray &datagram)
{
    const auto parsed = krpc::parse(datagram);
    return parsed.message ? parsed.message->method : QByteArray();
}

} // namespace

class TestRpcManager : public QObject
{
    Q_OBJECT

private slots:
    void holdsEachHostToItsAllowance();
    void hostsAreCountedByAddress();
    void timeoutStartsWhenSent();
    void replyToADelayedQueryMatches();
    void refusesBeyondTheQueueLater();
    void cancelAllEmptiesTheQueue();
    void defaultStaysUnderTheBanThreshold();
    void sharesATightBudgetAcrossHosts();
    void liftingTheBudgetReleasesTheQueue();
    void failedSendIsNotATimeout();
    void pendingCapHoldsQueriesBack();

private:
    const QHostAddress hostA{QStringLiteral("127.0.0.1")};
    const QHostAddress hostB{QStringLiteral("127.0.0.2")};
};

void TestRpcManager::holdsEachHostToItsAllowance()
{
    Recorder rec;
    RpcManager rpc(rec.sendFn());
    rpc.setHostLimit({2.0, 2.0, 64});

    for (int i = 0; i < 6; ++i)
        rpc.query(Endpoint(hostA, 1000), "q" + QByteArray::number(i), {}, {}, nullptr, 60000);

    // The burst goes at once, the rest wait.
    QCOMPARE(int(rec.sent.size()), 2);
    QCOMPARE(rpc.queuedCount(), 4);
    QCOMPARE(rpc.delayedCount(), qint64(4));

    QTRY_COMPARE_WITH_TIMEOUT(int(rec.sent.size()), 6, 5000);
    QCOMPARE(rpc.queuedCount(), 0);

    // In order, and spread out: four more allowances at two a second.
    for (int i = 0; i < 6; ++i)
        QCOMPARE(methodOf(rec.sent[i].datagram), "q" + QByteArray::number(i));
    QVERIFY2(rec.sent.back().atMs >= 1700, qPrintable(QString::number(rec.sent.back().atMs)));

    // Never more than burst + rate in any one-second window.
    for (const Sent &start : rec.sent) {
        const auto inWindow = std::count_if(rec.sent.begin(), rec.sent.end(), [&](const Sent &s) {
            return s.atMs >= start.atMs && s.atMs < start.atMs + 1000;
        });
        QVERIFY2(inWindow <= 4, qPrintable(QString::number(inWindow)));
    }
}

void TestRpcManager::hostsAreCountedByAddress()
{
    Recorder rec;
    RpcManager rpc(rec.sendFn());
    rpc.setHostLimit({2.0, 2.0, 64});

    // Ports on one address share an allowance; another address has its own.
    rpc.query(Endpoint(hostA, 1000), "a", {}, {}, nullptr, 60000);
    rpc.query(Endpoint(hostA, 2000), "b", {}, {}, nullptr, 60000);
    rpc.query(Endpoint(hostA, 3000), "c", {}, {}, nullptr, 60000);
    rpc.query(Endpoint(hostB, 1000), "d", {}, {}, nullptr, 60000);
    rpc.query(Endpoint(hostB, 1000), "e", {}, {}, nullptr, 60000);

    QCOMPARE(rec.countTo(hostA), 2);
    QCOMPARE(rec.countTo(hostB), 2);
    QCOMPARE(rpc.queuedCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(rec.countTo(hostA), 3, 2000);
    QCOMPARE(rec.sent.back().to.port, quint16(3000));
}

void TestRpcManager::timeoutStartsWhenSent()
{
    Recorder rec;
    RpcManager rpc(rec.sendFn());
    rpc.setHostLimit({1.0, 1.0, 64});

    std::vector<qint64> timedOutAt(3, -1);
    std::vector<RpcReply::Status> statuses(3, RpcReply::Status::Response);
    for (int i = 0; i < 3; ++i) {
        rpc.query(Endpoint(hostA, 1000), "q" + QByteArray::number(i), {}, {},
                  [&, i](const RpcReply &reply) {
                      statuses[i] = reply.status;
                      timedOutAt[i] = rec.clock.elapsed();
                  },
                  300);
    }

    QTRY_VERIFY_WITH_TIMEOUT(timedOutAt[2] >= 0, 5000);
    QCOMPARE(int(rec.sent.size()), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(statuses[i], RpcReply::Status::Timeout);
        const qint64 waited = timedOutAt[i] - rec.sent[i].atMs;
        QVERIFY2(waited >= 290, qPrintable(QStringLiteral("query %1 timed out %2 ms after sending").arg(i).arg(waited)));
    }
    // The last one was held back about two seconds before it was sent.
    QVERIFY2(rec.sent[2].atMs >= 1700, qPrintable(QString::number(rec.sent[2].atMs)));
}

void TestRpcManager::replyToADelayedQueryMatches()
{
    Recorder rec;
    RpcManager rpc(rec.sendFn());
    rpc.setHostLimit({1.0, 1.0, 64});

    const Endpoint node(hostA, 1000);
    std::optional<RpcReply> second;
    rpc.query(node, "first", {}, {}, nullptr, 60000);
    rpc.query(node, "second", {}, {}, [&](const RpcReply &r) { second = r; }, 60000);

    QTRY_COMPARE_WITH_TIMEOUT(int(rec.sent.size()), 2, 3000);
    const auto query = krpc::parse(rec.sent[1].datagram);
    QVERIFY(query.message);

    BValue::Dict values;
    values.emplace("id", BValue(NodeId::random().toBytes()));
    const QByteArray response = krpc::encodeResponse(query.message->transactionId, values, {}, Endpoint());
    const auto parsed = krpc::parse(response);
    QVERIFY(parsed.message);
    QVERIFY(rpc.handleReply(*parsed.message, response, node));

    QVERIFY(second);
    QCOMPARE(second->status, RpcReply::Status::Response);
    QCOMPARE(second->request, rec.sent[1].datagram);
    // Measured from sending, not from when it was asked for.
    QVERIFY2(second->rttMs < 500, qPrintable(QString::number(second->rttMs)));
}

void TestRpcManager::refusesBeyondTheQueueLater()
{
    Recorder rec;
    RpcManager rpc(rec.sendFn());
    rpc.setHostLimit({1.0, 1.0, 2});

    const Endpoint node(hostA, 1000);
    rpc.query(node, "sent", {}, {}, nullptr, 60000);
    rpc.query(node, "waits1", {}, {}, nullptr, 60000);
    rpc.query(node, "waits2", {}, {}, nullptr, 60000);

    std::optional<RpcReply> refused;
    rpc.query(node, "refused", {}, {}, [&](const RpcReply &r) { refused = r; }, 60000);

    // Not from inside query(): callers may be mid-update when they ask.
    QVERIFY(!refused);
    QCOMPARE(rpc.refusedCount(), qint64(1));
    QCOMPARE(rpc.queuedCount(), 2);

    QTRY_VERIFY_WITH_TIMEOUT(refused.has_value(), 1000);
    QCOMPARE(refused->status, RpcReply::Status::Throttled);
    QCOMPARE(refused->from, node);
    QVERIFY(refused->request.isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(int(rec.sent.size()), 3, 4000);
    for (const Sent &s : rec.sent)
        QVERIFY(methodOf(s.datagram) != "refused");
}

void TestRpcManager::cancelAllEmptiesTheQueue()
{
    Recorder rec;
    RpcManager rpc(rec.sendFn());
    rpc.setHostLimit({2.0, 1.0, 64});

    for (int i = 0; i < 5; ++i)
        rpc.query(Endpoint(hostA, 1000), "q", {}, {}, nullptr, 60000);
    QCOMPARE(int(rec.sent.size()), 1);

    rpc.cancelAll();
    QCOMPARE(rpc.queuedCount(), 0);
    QCOMPARE(rpc.pendingCount(), 0);
    QTest::qWait(1200);
    QCOMPARE(int(rec.sent.size()), 1);
}

// libtorrent bans an address that averages more than 5 packets a second
// over 10 seconds (dht_block_ratelimit, 50 in 10 s), so the defaults must
// stay well below that however much is asked for.
void TestRpcManager::defaultStaysUnderTheBanThreshold()
{
    const HostLimit limit;
    QCOMPARE(limit.ratePerSecond, 2.0);
    QCOMPARE(limit.burst, 2.0);
    QVERIFY(limit.burst + limit.ratePerSecond * 10 < 50);
    QVERIFY(limit.burst + limit.ratePerSecond * 1 < 5);

    Recorder rec;
    RpcManager rpc(rec.sendFn());  // defaults
    for (int i = 0; i < 40; ++i)
        rpc.query(Endpoint(hostA, 1000), "q", {}, {}, nullptr, 60000);

    QTest::qWait(3000);
    // Burst of two plus two a second for three seconds, with slack for
    // timer granularity.
    QVERIFY2(rec.sent.size() <= 9, qPrintable(QString::number(rec.sent.size())));
    QVERIFY2(rec.sent.size() >= 6, qPrintable(QString::number(rec.sent.size())));
}

namespace {

// About 200 bytes once encoded, so the budget is easy to reason about.
BValue::Dict padded()
{
    BValue::Dict args;
    args.emplace("pad", BValue(QByteArray(180, 'x')));
    return args;
}

QHostAddress hostNumber(int i)
{
    return QHostAddress(quint32(0x7f000100 + i));  // 127.0.1.i
}

} // namespace

void TestRpcManager::sharesATightBudgetAcrossHosts()
{
    Recorder rec;
    ContactBudget budget;
    constexpr int Limit = 4;  // new endpoints a second
    budget.setLimit(Limit, nowMs());
    RpcManager rpc([&](const QByteArray &d, const Endpoint &to) {
        rec.sent.push_back({d, to, rec.clock.elapsed()});
        return true;
    });
    rpc.setHostLimit({1000.0, 1000.0, 64});  // only the budget matters here
    rpc.setBudget(&budget);

    constexpr int Hosts = 10;
    constexpr int PerHost = 3;
    for (int h = 0; h < Hosts; ++h) {
        for (int q = 0; q < PerHost; ++q)
            rpc.query(Endpoint(hostNumber(h), 1000), "q", padded(), {}, nullptr, 60000);
    }
    const int immediate = int(rec.sent.size());
    QVERIFY2(immediate > 0 && immediate < Hosts * PerHost, qPrintable(QString::number(immediate)));

    // A newcomer does not jump ahead of hosts already waiting for the budget.
    rpc.query(Endpoint(hostNumber(99), 1000), "late", padded(), {}, nullptr, 60000);
    QCOMPARE(int(rec.sent.size()), immediate);

    QTRY_COMPARE_WITH_TIMEOUT(int(rec.sent.size()), Hosts * PerHost + 1, 10000);

    // Endpoints first written to by any moment stay within a full second's
    // allowance, the rate since, and a little timer slack. Later queries to
    // an endpoint already contacted are free and do not count.
    std::vector<Endpoint> seen;
    for (const Sent &s : rec.sent) {
        if (std::find(seen.begin(), seen.end(), s.to) != seen.end())
            continue;
        seen.push_back(s.to);
        const double allowed = Limit + Limit * s.atMs / 1000.0 + 1 + Limit * 0.1;
        QVERIFY2(seen.size() <= allowed,
                 qPrintable(QStringLiteral("%1 endpoints by %2 ms").arg(seen.size()).arg(s.atMs)));
    }

    // Hosts waiting for the allowance were reached in turn, so the newcomer
    // is contacted only after those already waiting. Repeat queries to a
    // host already contacted cost nothing and do not count here.
    const auto lateAt = std::find(seen.begin(), seen.end(), Endpoint(hostNumber(99), 1000));
    QVERIFY(lateAt != seen.end());
    QVERIFY2(lateAt - seen.begin() >= 5, qPrintable(QString::number(lateAt - seen.begin())));
}

void TestRpcManager::liftingTheBudgetReleasesTheQueue()
{
    Recorder rec;
    ContactBudget budget;
    budget.setLimit(1, nowMs());
    RpcManager rpc([&](const QByteArray &d, const Endpoint &to) {
        rec.sent.push_back({d, to, rec.clock.elapsed()});
        return true;
    });
    rpc.setHostLimit({1000.0, 1000.0, 64});
    rpc.setBudget(&budget);

    for (int h = 0; h < 20; ++h)
        rpc.query(Endpoint(hostNumber(h), 1000), "q", padded(), {}, nullptr, 60000);
    QTest::qWait(400);
    const int beforeLift = int(rec.sent.size());
    QVERIFY2(beforeLift < 6, qPrintable(QString::number(beforeLift)));

    const qint64 liftedAt = rec.clock.elapsed();
    budget.setLimit(0, nowMs());
    QTRY_COMPARE_WITH_TIMEOUT(int(rec.sent.size()), 20, 2000);
    QVERIFY2(rec.sent.back().atMs - liftedAt < 500, qPrintable(QString::number(rec.sent.back().atMs - liftedAt)));
}

void TestRpcManager::failedSendIsNotATimeout()
{
    bool accept = false;
    int attempts = 0;
    RpcManager rpc([&](const QByteArray &, const Endpoint &) {
        ++attempts;
        return accept;
    });

    std::optional<RpcReply> reply;
    rpc.query(Endpoint(hostA, 1000), "q", {}, {}, [&](const RpcReply &r) { reply = r; }, 200);
    QCOMPARE(attempts, 1);
    QVERIFY(!reply);  // reported later, not from inside query()
    QCOMPARE(rpc.pendingCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(reply.has_value(), 1000);
    QCOMPARE(reply->status, RpcReply::Status::Throttled);
    QCOMPARE(rpc.sendFailures(), qint64(1));

    // Nothing is left behind to time out later.
    QTest::qWait(400);
    QCOMPARE(reply->status, RpcReply::Status::Throttled);

    accept = true;
    reply.reset();
    rpc.query(Endpoint(hostB, 1000), "q", {}, {}, [&](const RpcReply &r) { reply = r; }, 200);
    QTRY_VERIFY_WITH_TIMEOUT(reply.has_value(), 1000);
    QCOMPARE(reply->status, RpcReply::Status::Timeout);
}

void TestRpcManager::pendingCapHoldsQueriesBack()
{
    Recorder rec;
    RpcManager rpc(rec.sendFn());
    rpc.setHostLimit({1000.0, 1000.0, 64});
    rpc.setMaxPending(3);

    for (int h = 0; h < 5; ++h)
        rpc.query(Endpoint(hostNumber(h), 1000), "q", {}, {}, nullptr, 300);
    QCOMPARE(int(rec.sent.size()), 3);
    QCOMPARE(rpc.pendingCount(), 3);
    QCOMPARE(rpc.queuedCount(), 2);

    // As the first ones time out, the waiting ones go.
    QTRY_COMPARE_WITH_TIMEOUT(int(rec.sent.size()), 5, 2000);
    QCOMPARE(rpc.refusedCount(), qint64(0));
}

int runTestRpcManager(int argc, char **argv)
{
    TestRpcManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestRpcManager.moc"
