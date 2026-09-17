#pragma once

#include "dhtcore/RpcManager.h"
#include "dhtcore/Snapshot.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>
#include <vector>

namespace dht {

// Iterative Kademlia lookup (find_node or get_peers) towards a target.
// Deletes itself after reporting its result.
class Lookup : public QObject
{
    Q_OBJECT

public:
    enum class Kind { FindNode, GetPeers, GetItem };

    struct Contact
    {
        NodeId id;
        Endpoint endpoint;
        QByteArray token;  // get_peers only
    };

    struct Result
    {
        Kind kind = Kind::FindNode;
        NodeId target;
        std::vector<Contact> closest;  // up to K responders, closest first
        std::vector<Endpoint> peers;   // get_peers only, unique
        std::vector<PeerSighting> sightings;  // get_peers only, with their source
        int queried = 0;
        int responded = 0;
        qint64 durationMs = 0;
        // Referrals from the starting nodes to the closest responder: 0 when
        // a starting node was itself the closest.
        int hops = -1;

        // get only. A mutable item is kept when its signature checks out and
        // its sequence number is the highest seen; an immutable one when its
        // value hashes to the target.
        bool itemFound = false;
        bool itemIsMutable = false;
        QByteArray itemValue;  // bencoded
        QByteArray itemPublicKey;
        QByteArray itemSignature;
        qint64 itemSequence = -1;
    };

    // Sends `method` with `arguments` (the caller adds our "id") and invokes
    // the callback exactly once, possibly synchronously. `timeoutMs` is how
    // long the reply is waited for.
    // `onSent` says when the query left, which is when its clock starts.
    using QueryFn = std::function<void(const Endpoint &to, const QByteArray &method, BValue::Dict arguments,
                                       RpcManager::Callback callback, int timeoutMs, RpcManager::SentFn onSent)>;
    using DoneFn = std::function<void(const Result &result)>;

    static constexpr int K = 8;
    static constexpr int Alpha = 3;
    static constexpr int MaxCandidates = 100;
    static constexpr int MaxQueries = 150;
    static constexpr int MaxSightings = 2000;
    // Half the nodes the network lists never answer, and waiting on them is
    // what makes a lookup slow. A query that has not answered by
    // SlowAfterMs stops holding one of the Alpha slots, so another goes out
    // in its place; it still counts if it answers before QueryTimeoutMs.
    // Even with slow queries set aside, no more than this many are ever
    // outstanding at once: a query to a busy host may be waiting its turn
    // under the per-host limit rather than being ignored, and piling more
    // on would only lengthen that queue.
    static constexpr int MaxOutstanding = 2 * Alpha;
    static constexpr int SlowAfterMs = 700;
    static constexpr int QueryTimeoutMs = 1500;
    static constexpr int SlowCheckMs = 100;

    Lookup(Kind kind, const NodeId &target, Family family, const NodeId &selfId, bool allowLocal,
           QueryFn query, DoneFn done, QObject *parent = nullptr);

    void addCandidate(const NodeId &id, const Endpoint &endpoint) { addCandidate(id, endpoint, 0); }
    // The salt a mutable item was published under; needed to check signatures.
    void setSalt(const QByteArray &salt) { m_salt = salt; }
    // Both are clamped to at least 100 ms, and slow is capped at the timeout.
    void setTimeouts(int slowAfterMs, int queryTimeoutMs);
    void start();

    Kind kind() const { return m_kind; }
    const NodeId &target() const { return m_target; }
    bool isDone() const { return m_done; }

private:
    // Slow: sent, past SlowAfterMs, no longer holding a slot.
    enum class State { New, InFlight, Slow, Responded, Failed };

    struct Candidate
    {
        NodeId id;
        Endpoint endpoint;
        State state = State::New;
        QByteArray token;
        int hops = 0;
        qint64 sentAtMs = 0;  // 0 until it goes out, which may be a while
    };

    void addCandidate(const NodeId &id, const Endpoint &endpoint, int hops);
    void step();
    // Whether the K closest have answered with nothing closer outstanding,
    // or there is simply nothing left to wait for.
    bool canFinish() const;
    void onSent(const Endpoint &endpoint);
    void checkSlow();
    void onReply(const Endpoint &endpoint, const RpcReply &reply);
    void collectItem(const RpcReply &reply);
    void finish();

    Kind m_kind;
    NodeId m_target;
    Family m_family;
    NodeId m_self;
    bool m_allowLocal;
    QueryFn m_query;
    DoneFn m_onDone;

    std::vector<Candidate> m_candidates;  // sorted by distance to target
    QSet<Endpoint> m_seen;
    QSet<Endpoint> m_peerSet;
    std::vector<Endpoint> m_peers;
    std::vector<PeerSighting> m_sightings;
    QByteArray m_salt;
    bool m_itemFound = false;
    bool m_itemIsMutable = false;
    QByteArray m_itemValue;
    QByteArray m_itemPublicKey;
    QByteArray m_itemSignature;
    qint64 m_itemSequence = -1;
    QElapsedTimer m_clock;
    QTimer m_slowTimer;
    int m_slowAfterMs = SlowAfterMs;
    int m_queryTimeoutMs = QueryTimeoutMs;
    int m_inFlight = 0;   // queries still holding a slot
    int m_outstanding = 0;  // those, plus the slow ones
    int m_queries = 0;
    int m_responded = 0;
    bool m_started = false;
    bool m_finished = false;
    bool m_done = false;
};

} // namespace dht
