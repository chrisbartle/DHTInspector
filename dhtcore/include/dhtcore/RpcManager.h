#pragma once

#include "dhtcore/Krpc.h"
#include "dhtcore/Support.h"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <deque>
#include <functional>

namespace dht {

struct RpcReply
{
    // Throttled: never sent, because too many queries were already waiting
    // for that host. It says nothing about the node, so it must not count
    // against it.
    enum class Status { Response, Error, Timeout, Throttled };

    Status status = Status::Timeout;
    krpc::Message message;  // empty on timeout
    QByteArray datagram;    // the bytes the message was parsed from
    QByteArray request;     // the bytes we sent
    Endpoint from;
    int rttMs = -1;
};

// How hard we may lean on any one host. Every outgoing query passes through
// this, whatever asked for it: lookups, maintenance, probes, crawls.
//
// Keyed by IP address, not endpoint, because that is how nodes judge us:
// libtorrent bans an address for five minutes once it averages more than
// 5 packets a second over 10 seconds (dht_block_ratelimit). Two a second
// with a burst of two is at most 4 in any second and 22 in any ten.
struct HostLimit
{
    double ratePerSecond = 2.0;
    double burst = 2.0;
    int maxQueued = 64;  // per host; beyond this a query is refused
};

// Outstanding KRPC transactions: ids, matching, timeouts, and the per-host
// send limit. A query over the limit waits its turn rather than being
// dropped, since a dropped query would look like an unresponsive node.
class RpcManager : public QObject
{
    Q_OBJECT

public:
    using SendFn = std::function<void(const QByteArray &datagram, const Endpoint &to)>;
    using Callback = std::function<void(const RpcReply &reply)>;

    static constexpr int DefaultTimeoutMs = 3000;
    static constexpr int MaxQueuedTotal = 20000;

    explicit RpcManager(SendFn send, QObject *parent = nullptr);

    // Sends now if the host has allowance, otherwise queues the query behind
    // any others for that host. The timeout starts when it is actually sent.
    // A refused query is reported as Throttled, never from inside this call.
    void query(const Endpoint &to, const QByteArray &method, BValue::Dict arguments,
               const QByteArray &version, Callback callback, int timeoutMs = DefaultTimeoutMs);

    // BEP 43: mark every outgoing query read-only. Queries already sent keep
    // whatever flag they carried.
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly; }

    void setHostLimit(const HostLimit &limit);
    const HostLimit &hostLimit() const { return m_hostLimit; }

    // The engine-wide byte budget; null means unlimited. Only consulted here:
    // whoever actually sends the datagram spends from it.
    void setBudget(SendBudget *budget) { m_budget = budget; }

    // Returns true if the response or error matched an outstanding query.
    // A reply only matches if it comes from the endpoint we queried.
    bool handleReply(const krpc::Message &message, const QByteArray &datagram, const Endpoint &from);

    int pendingCount() const { return int(m_pending.size()); }
    int queuedCount() const { return m_queuedTotal; }
    qint64 delayedCount() const { return m_delayed; }    // queries that had to wait
    qint64 refusedCount() const { return m_refused; }    // queries never sent
    void cancelAll();

private:
    struct Queued
    {
        Endpoint to;
        QByteArray method;
        BValue::Dict arguments;
        QByteArray version;
        Callback callback;
        int timeoutMs = DefaultTimeoutMs;
    };

    void send(Queued query);
    void refuse(Queued query);
    void drain();
    void expire();
    QByteArray nextTransactionId();

    struct Pending
    {
        Endpoint to;
        QByteArray request;
        qint64 sentAt = 0;
        qint64 deadline = 0;
        Callback callback;
    };

    SendFn m_send;
    QHash<QByteArray, Pending> m_pending;
    QHash<QHostAddress, std::deque<Queued>> m_queues;
    std::deque<QHostAddress> m_order;  // hosts with waiting queries, served in turn
    SendBudget *m_budget = nullptr;
    bool m_budgetExhausted = false;    // hosts are waiting on the budget
    HostLimit m_hostLimit;
    RateLimiter m_limiter;
    QTimer m_timer;
    QTimer m_drainTimer;
    qint64 m_lastPrune = 0;
    int m_queuedTotal = 0;
    qint64 m_delayed = 0;
    qint64 m_refused = 0;
    quint16 m_nextId = 0;
    bool m_readOnly = false;
};

} // namespace dht
