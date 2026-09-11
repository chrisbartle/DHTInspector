#pragma once

#include "dhtcore/RpcManager.h"

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
    enum class Kind { FindNode, GetPeers };

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
        std::vector<Endpoint> peers;   // get_peers only
        int queried = 0;
        int responded = 0;
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

    Lookup(Kind kind, const NodeId &target, Family family, const NodeId &selfId, bool allowLocal,
           QueryFn query, DoneFn done, QObject *parent = nullptr);

    void addCandidate(const NodeId &id, const Endpoint &endpoint);
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
    };

    void step();
    void onReply(const Endpoint &endpoint, const RpcReply &reply);
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
    int m_inFlight = 0;
    int m_queries = 0;
    int m_responded = 0;
    bool m_started = false;
    bool m_finished = false;
    bool m_done = false;
};

} // namespace dht
