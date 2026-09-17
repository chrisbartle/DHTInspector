#pragma once

#include "dhtcore/RpcManager.h"
#include "dhtcore/Snapshot.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSet>

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
    // the callback exactly once, possibly synchronously.
    using QueryFn = std::function<void(const Endpoint &to, const QByteArray &method,
                                       BValue::Dict arguments, RpcManager::Callback callback)>;
    using DoneFn = std::function<void(const Result &result)>;

    static constexpr int K = 8;
    static constexpr int Alpha = 3;
    static constexpr int MaxCandidates = 100;
    static constexpr int MaxQueries = 150;
    static constexpr int MaxSightings = 2000;

    Lookup(Kind kind, const NodeId &target, Family family, const NodeId &selfId, bool allowLocal,
           QueryFn query, DoneFn done, QObject *parent = nullptr);

    void addCandidate(const NodeId &id, const Endpoint &endpoint) { addCandidate(id, endpoint, 0); }
    // The salt a mutable item was published under; needed to check signatures.
    void setSalt(const QByteArray &salt) { m_salt = salt; }
    void start();

    Kind kind() const { return m_kind; }
    const NodeId &target() const { return m_target; }
    bool isDone() const { return m_done; }

private:
    enum class State { New, InFlight, Responded, Failed };

    struct Candidate
    {
        NodeId id;
        Endpoint endpoint;
        State state = State::New;
        QByteArray token;
        int hops = 0;
    };

    void addCandidate(const NodeId &id, const Endpoint &endpoint, int hops);
    void step();
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
    int m_inFlight = 0;
    int m_queries = 0;
    int m_responded = 0;
    bool m_started = false;
    bool m_finished = false;
    bool m_done = false;
};

} // namespace dht
