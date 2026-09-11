#pragma once

#include "dhtcore/Endpoint.h"
#include "dhtcore/NodeId.h"

#include <optional>
#include <vector>

namespace dht {

struct RoutingNode
{
    enum class State { Good, Questionable, Bad };

    NodeId id;
    Endpoint endpoint;
    QByteArray version;
    qint64 firstSeen = 0;
    qint64 lastResponse = 0;  // last reply to one of our queries
    qint64 lastQuery = 0;     // last query it sent us
    qint64 lastPingSent = 0;
    int failCount = 0;        // consecutive timeouts
    int rttMs = -1;           // smoothed round-trip time
};

// BEP 5 routing table: k-buckets indexed by shared prefix length with our
// own ID. Only the bucket that covers our own ID is ever split.
class RoutingTable
{
public:
    static constexpr int K = 8;
    static constexpr qint64 GoodWindowMs = 15 * 60 * 1000;
    static constexpr int BadFailCount = 2;
    static constexpr int RemoveFailCount = 5;

    enum class InsertResult { Added, Updated, Replaced, Cached, Rejected };

    explicit RoutingTable(const NodeId &self, bool restrictOnePerIp = true);

    const NodeId &selfId() const { return m_self; }

    // A node answered one of our queries. The only way into the table.
    InsertResult heardFrom(const NodeId &id, const Endpoint &endpoint, const QByteArray &version,
                           int rttMs, qint64 now);

    // A node sent us a query. Returns true if it was already in the table.
    bool queriedBy(const NodeId &id, const Endpoint &endpoint, qint64 now);

    // A query to this endpoint timed out.
    void failed(const Endpoint &endpoint, qint64 now);

    void markPinged(const Endpoint &endpoint, qint64 now);

    const RoutingNode *find(const Endpoint &endpoint) const;
    bool contains(const Endpoint &endpoint) const { return find(endpoint) != nullptr; }

    std::vector<RoutingNode> closest(const NodeId &target, int count, bool includeBad = false) const;

    // Questionable nodes, least recently heard from first.
    std::vector<RoutingNode> nodesNeedingPing(qint64 now, int max, qint64 minPingIntervalMs) const;

    // The bucket unchanged for longest, if any exceeds staleMs.
    std::optional<int> staleBucket(qint64 now, qint64 staleMs) const;
    void touchBucket(int index, qint64 now);
    NodeId randomIdInBucket(int index) const;

    // Whether a node with this ID could enter the table without displacing
    // a node that is still good.
    bool wouldAccept(const NodeId &id) const;

    // Re-keys the table around a new own ID, keeping every node that fits.
    void rebuild(const NodeId &newSelf, qint64 now);

    int size() const;
    int bucketCount() const { return int(m_buckets.size()); }
    int bucketIndexFor(const NodeId &id) const;
    std::vector<RoutingNode> allNodes() const;

    static RoutingNode::State stateOf(const RoutingNode &node, qint64 now);

private:
    struct Bucket
    {
        std::vector<RoutingNode> nodes;
        std::vector<RoutingNode> replacements;
        qint64 lastChanged = 0;
    };

    InsertResult insert(RoutingNode node, qint64 now);
    bool canSplit(int index) const;
    void split();
    RoutingNode *locate(const Endpoint &endpoint, int *bucketIndex = nullptr);
    bool ipInUse(const QHostAddress &address) const;

    NodeId m_self;
    bool m_restrictOnePerIp;
    std::vector<Bucket> m_buckets;
};

} // namespace dht
