#include "dhtcore/RpcManager.h"

#include "dhtcore/Support.h"

#include <QRandomGenerator>

namespace dht {

RpcManager::RpcManager(SendFn send, QObject *parent)
    : QObject(parent)
    , m_send(std::move(send))
    , m_timer(this)
    , m_nextId(quint16(QRandomGenerator::global()->generate()))
{
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &RpcManager::expire);
    m_timer.start();
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
    const QByteArray tid = nextTransactionId();
    const qint64 now = nowMs();
    const QByteArray datagram = krpc::encodeQuery(tid, method, std::move(arguments), version, m_readOnly);
    m_pending.insert(tid, Pending{to, datagram, now, now + timeoutMs, std::move(callback)});
    m_send(datagram, to);
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
    if (m_pending.isEmpty())
        return;

    const qint64 now = nowMs();
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
}

} // namespace dht
