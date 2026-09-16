#include "dhtcore/RpcManager.h"

#include "dhtcore/Support.h"

#include <QRandomGenerator>

namespace dht {

namespace {

constexpr int DrainIntervalMs = 50;
constexpr qint64 PruneIntervalMs = 5000;

} // namespace

RpcManager::RpcManager(SendFn send, QObject *parent)
    : QObject(parent)
    , m_send(std::move(send))
    , m_limiter(m_hostLimit.ratePerSecond, m_hostLimit.burst)
    , m_timer(this)
    , m_drainTimer(this)
    , m_nextId(quint16(QRandomGenerator::global()->generate()))
{
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &RpcManager::expire);
    m_timer.start();

    // Runs only while something is waiting.
    m_drainTimer.setInterval(DrainIntervalMs);
    connect(&m_drainTimer, &QTimer::timeout, this, &RpcManager::drain);
}

void RpcManager::setHostLimit(const HostLimit &limit)
{
    m_hostLimit = limit;
    m_limiter = RateLimiter(limit.ratePerSecond, limit.burst);
}

QByteArray RpcManager::nextTransactionId()
{
    QByteArray id(2, Qt::Uninitialized);
    do {
        id[0] = char(m_nextId >> 8);
        id[1] = char(m_nextId & 0xff);
        ++m_nextId;
    } while (m_pending.contains(id));
    return id;
}

void RpcManager::query(const Endpoint &to, const QByteArray &method, BValue::Dict arguments,
                       const QByteArray &version, Callback callback, int timeoutMs)
{
    Queued q{to, method, std::move(arguments), version, std::move(callback), timeoutMs};
    const qint64 now = nowMs();

    // Anything already waiting for this host goes first, so order is kept,
    // and nothing jumps ahead of hosts that are waiting for the budget.
    auto queue = m_queues.find(to.address);
    if (queue == m_queues.end() && !m_budgetExhausted && canSendNow(now) && m_limiter.allow(to.address, now)) {
        send(std::move(q));
        return;
    }

    if (queue == m_queues.end()) {
        if (m_queuedTotal >= MaxQueuedTotal) {
            refuse(std::move(q));
            return;
        }
        queue = m_queues.insert(to.address, {});
        m_order.push_back(to.address);
    } else if (int(queue->size()) >= m_hostLimit.maxQueued || m_queuedTotal >= MaxQueuedTotal) {
        refuse(std::move(q));
        return;
    }

    queue->push_back(std::move(q));
    ++m_queuedTotal;
    ++m_delayed;
    if (!m_drainTimer.isActive())
        m_drainTimer.start();
}

int RpcManager::queuedFor(const QHostAddress &address) const
{
    const auto it = m_queues.constFind(address);
    return it == m_queues.cend() ? 0 : int(it->size());
}

bool RpcManager::canSendNow(qint64 now) const
{
    return int(m_pending.size()) < m_maxPending && (!m_budget || m_budget->available(now));
}

void RpcManager::send(Queued q)
{
    const QByteArray tid = nextTransactionId();
    const qint64 now = nowMs();
    const QByteArray datagram = krpc::encodeQuery(tid, q.method, std::move(q.arguments), q.version, m_readOnly);
    m_pending.insert(tid, Pending{q.to, datagram, now, now + q.timeoutMs, std::move(q.callback)});
    if (m_send(datagram, q.to))
        return;

    // Never left the machine (typically a full socket buffer): waiting for
    // the timeout would record a healthy node as silent.
    ++m_sendFailures;
    const auto it = m_pending.find(tid);
    Callback callback = std::move(it->callback);
    m_pending.erase(it);
    reportNotSent(std::move(callback), q.to);
}

void RpcManager::refuse(Queued q)
{
    ++m_refused;
    reportNotSent(std::move(q.callback), q.to);
}

void RpcManager::reportNotSent(Callback callback, const Endpoint &to)
{
    if (!callback)
        return;
    // Reported later, so callers never see their callback run from inside
    // their own query() call.
    RpcReply reply;
    reply.status = RpcReply::Status::Throttled;
    reply.from = to;
    QMetaObject::invokeMethod(
        this, [callback = std::move(callback), reply] { callback(reply); }, Qt::QueuedConnection);
}

void RpcManager::drain()
{
    const qint64 now = nowMs();
    // One query per host per pass, taking hosts in turn, so a tight budget
    // is shared rather than spent on whichever host happens to come first.
    // Sending only writes a datagram (callbacks run on replies and expiry),
    // so each send is charged before the next budget check.
    bool exhausted = false;
    const size_t hosts = m_order.size();
    for (size_t i = 0; i < hosts && !m_order.empty(); ++i) {
        if (!canSendNow(now)) {
            exhausted = true;
            break;
        }
        const QHostAddress host = m_order.front();
        m_order.pop_front();
        const auto queue = m_queues.find(host);
        if (queue == m_queues.end())
            continue;
        if (!queue->empty() && m_limiter.allow(host, now)) {
            Queued q = std::move(queue->front());
            queue->pop_front();
            --m_queuedTotal;
            send(std::move(q));
        }
        if (queue->empty())
            m_queues.erase(queue);
        else
            m_order.push_back(host);
    }
    m_budgetExhausted = exhausted;
    if (m_queues.isEmpty()) {
        m_drainTimer.stop();
        m_budgetExhausted = false;
    }
}

bool RpcManager::handleReply(const krpc::Message &message, const QByteArray &datagram, const Endpoint &from)
{
    const auto it = m_pending.find(message.transactionId);
    if (it == m_pending.end() || !(it->to == from))
        return false;

    Pending pending = std::move(*it);
    m_pending.erase(it);

    RpcReply reply;
    reply.status = message.type == krpc::MessageType::Error ? RpcReply::Status::Error
                                                            : RpcReply::Status::Response;
    reply.message = message;
    reply.datagram = datagram;
    reply.request = pending.request;
    reply.from = from;
    reply.rttMs = int(nowMs() - pending.sentAt);
    if (pending.callback)
        pending.callback(reply);
    return true;
}

void RpcManager::expire()
{
    const qint64 now = nowMs();
    if (now - m_lastPrune >= PruneIntervalMs) {
        m_limiter.prune(now);
        m_lastPrune = now;
    }
    if (m_pending.isEmpty())
        return;

    QList<QByteArray> expired;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        if (it->deadline <= now)
            expired.append(it.key());
    }

    // Callbacks may issue new queries, so detach each entry before calling it.
    for (const QByteArray &tid : std::as_const(expired)) {
        const auto it = m_pending.find(tid);
        if (it == m_pending.end())
            continue;
        Pending pending = std::move(*it);
        m_pending.erase(it);

        RpcReply reply;
        reply.status = RpcReply::Status::Timeout;
        reply.request = pending.request;
        reply.from = pending.to;
        if (pending.callback)
            pending.callback(reply);
    }
}

void RpcManager::cancelAll()
{
    m_pending.clear();
    m_queues.clear();
    m_order.clear();
    m_queuedTotal = 0;
    m_budgetExhausted = false;
    m_drainTimer.stop();
}

} // namespace dht
