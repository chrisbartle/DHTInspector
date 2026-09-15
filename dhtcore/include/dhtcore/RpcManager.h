#pragma once

#include "dhtcore/Krpc.h"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <functional>

namespace dht {

struct RpcReply
{
    enum class Status { Response, Error, Timeout };

    Status status = Status::Timeout;
    krpc::Message message;  // empty on timeout
    QByteArray datagram;    // the bytes the message was parsed from
    Endpoint from;
    int rttMs = -1;
};

// Outstanding KRPC transactions: ids, matching and timeouts.
class RpcManager : public QObject
{
    Q_OBJECT

public:
    using SendFn = std::function<void(const QByteArray &datagram, const Endpoint &to)>;
    using Callback = std::function<void(const RpcReply &reply)>;

    static constexpr int DefaultTimeoutMs = 3000;

    explicit RpcManager(SendFn send, QObject *parent = nullptr);

    void query(const Endpoint &to, const QByteArray &method, BValue::Dict arguments,
               const QByteArray &version, Callback callback, int timeoutMs = DefaultTimeoutMs);

    // Returns true if the response or error matched an outstanding query.
    // A reply only matches if it comes from the endpoint we queried.
    bool handleReply(const krpc::Message &message, const QByteArray &datagram, const Endpoint &from);

    int pendingCount() const { return int(m_pending.size()); }
    void cancelAll();

private:
    void expire();
    QByteArray nextTransactionId();

    struct Pending
    {
        Endpoint to;
        qint64 sentAt = 0;
        qint64 deadline = 0;
        Callback callback;
    };

    SendFn m_send;
    QHash<QByteArray, Pending> m_pending;
    QTimer m_timer;
    quint16 m_nextId = 0;
};

} // namespace dht
