#include "dhtcore/RoutingTable.h"

#include <algorithm>

namespace dht {

RoutingTable::RoutingTable(const NodeId &self, bool restrictOnePerIp)
    : m_self(self), m_restrictOnePerIp(restrictOnePerIp), m_buckets(1)
{
}

RoutingNode::State RoutingTable::stateOf(const RoutingNode &node, qint64 now)
{
    if (node.failCount >= BadFailCount)
        return RoutingNode::State::Bad;
    if (node.failCount == 0) {
        if (node.lastResponse > 0 && now - node.lastResponse < GoodWindowMs)
            return RoutingNode::State::Good;
        if (node.lastResponse > 0 && node.lastQuery > 0 && now - node.lastQuery < GoodWindowMs)
            return RoutingNode::State::Good;
    }
    return RoutingNode::State::Questionable;
}

int RoutingTable::bucketIndexFor(const NodeId &id) const
{
    return std::min(NodeId::commonPrefixLength(m_self, id), int(m_buckets.size()) - 1);
}

RoutingTable::InsertResult RoutingTable::heardFrom(const NodeId &id, const Endpoint &endpoint,
                                                   const QByteArray &version, int rttMs, qint64 now)
{
    if (id == m_self || !endpoint.isValid())
        return InsertResult::Rejected;

    int existingBucket = -1;
    if (RoutingNode *existing = locate(endpoint, &existingBucket)) {
        if (existing->id == id) {
            existing->lastResponse = now;
            existing->failCount = 0;
            if (!version.isEmpty())
                existing->version = version;
            if (rttMs >= 0)
                existing->rttMs = existing->rttMs < 0 ? rttMs : (existing->rttMs * 3 + rttMs) / 4;
            m_buckets[existingBucket].lastChanged = now;
            return InsertResult::Updated;
        }
        // Same endpoint, new ID: the node restarted. Forget the old identity.
        auto &nodes = m_buckets[existingBucket].nodes;
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                                   [&](const RoutingNode &n) { return n.endpoint == endpoint; }),
                    nodes.end());
    }

    // Same ID at a different endpoint: keep the established entry unless it
    // has stopped answering.
    Bucket &bucket = m_buckets[bucketIndexFor(id)];
    for (RoutingNode &n : bucket.nodes) {
        if (n.id != id)
            continue;
        if (stateOf(n, now) != RoutingNode::State::Bad)
            return InsertResult::Rejected;
        n.endpoint = endpoint;
        n.lastResponse = now;
        n.failCount = 0;
        n.rttMs = rttMs;
        n.version = version;
        bucket.lastChanged = now;
        return InsertResult::Updated;
    }

    // Drop any stale replacement-cache copy; insert() re-adds it if needed.
    for (Bucket &b : m_buckets) {
        b.replacements.erase(std::remove_if(b.replacements.begin(), b.replacements.end(),
                                            [&](const RoutingNode &n) {
                                                return n.endpoint == endpoint || n.id == id;
                                            }),
                             b.replacements.end());
    }

    RoutingNode node;
    node.id = id;
    node.endpoint = endpoint;
    node.version = version;
    node.firstSeen = now;
    node.lastResponse = now;
    node.rttMs = rttMs;
    return insert(std::move(node), now);
}

RoutingTable::InsertResult RoutingTable::insert(RoutingNode node, qint64 now)
{
    if (node.id == m_self)
        return InsertResult::Rejected;
    if (m_restrictOnePerIp && !isLocalAddress(node.endpoint.address) && ipInUse(node.endpoint.address))
        return InsertResult::Rejected;

    while (true) {
        const int index = bucketIndexFor(node.id);
        Bucket &bucket = m_buckets[index];

        if (int(bucket.nodes.size()) < K) {
            bucket.nodes.push_back(std::move(node));
            bucket.lastChanged = now;
            return InsertResult::Added;
        }

        if (canSplit(index)) {
            split();
            continue;
        }

        const auto bad = std::find_if(bucket.nodes.begin(), bucket.nodes.end(), [&](const RoutingNode &n) {
            return stateOf(n, now) == RoutingNode::State::Bad;
        });
        if (bad != bucket.nodes.end()) {
            *bad = std::move(node);
            bucket.lastChanged = now;
            return InsertResult::Replaced;
        }

        auto &cache = bucket.replacements;
        cache.push_back(std::move(node));
        if (int(cache.size()) > K) {
            const auto oldest = std::min_element(cache.begin(), cache.end(), [](const auto &a, const auto &b) {
                return a.lastResponse < b.lastResponse;
            });
            cache.erase(oldest);
        }
        return InsertResult::Cached;
    }
}

bool RoutingTable::canSplit(int index) const
{
    return index == int(m_buckets.size()) - 1 && int(m_buckets.size()) < NodeId::Bits;
}

void RoutingTable::split()
{
    const int last = int(m_buckets.size()) - 1;
    Bucket fresh;
    {
        Bucket &old = m_buckets[last];
        fresh.lastChanged = old.lastChanged;
        auto movesOut = [&](const RoutingNode &n) { return NodeId::commonPrefixLength(m_self, n.id) > last; };

        auto keepNodes = std::stable_partition(old.nodes.begin(), old.nodes.end(),
                                               [&](const RoutingNode &n) { return !movesOut(n); });
        std::move(keepNodes, old.nodes.end(), std::back_inserter(fresh.nodes));
        old.nodes.erase(keepNodes, old.nodes.end());

        auto keepCache = std::stable_partition(old.replacements.begin(), old.replacements.end(),
                                               [&](const RoutingNode &n) { return !movesOut(n); });
        std::move(keepCache, old.replacements.end(), std::back_inserter(fresh.replacements));
        old.replacements.erase(keepCache, old.replacements.end());
    }
    m_buckets.push_back(std::move(fresh));
}

bool RoutingTable::queriedBy(const NodeId &id, const Endpoint &endpoint, qint64 now)
{
    RoutingNode *node = locate(endpoint);
    if (!node || node->id != id)
        return false;
    node->lastQuery = now;
    return true;
}

void RoutingTable::failed(const Endpoint &endpoint, qint64 now)
{
    int index = -1;
    RoutingNode *node = locate(endpoint, &index);
    if (!node) {
        for (Bucket &b : m_buckets) {
            b.replacements.erase(std::remove_if(b.replacements.begin(), b.replacements.end(),
                                                [&](const RoutingNode &n) { return n.endpoint == endpoint; }),
                                 b.replacements.end());
        }
        return;
    }

    ++node->failCount;
    if (stateOf(*node, now) != RoutingNode::State::Bad)
        return;

    Bucket &bucket = m_buckets[index];
    if (!bucket.replacements.empty()) {
        const auto best = std::max_element(bucket.replacements.begin(), bucket.replacements.end(),
                                           [](const auto &a, const auto &b) { return a.lastResponse < b.lastResponse; });
        *node = std::move(*best);
        bucket.replacements.erase(best);
        bucket.lastChanged = now;
        return;
    }

    if (node->failCount >= RemoveFailCount) {
        bucket.nodes.erase(std::remove_if(bucket.nodes.begin(), bucket.nodes.end(),
                                          [&](const RoutingNode &n) { return n.endpoint == endpoint; }),
                           bucket.nodes.end());
    }
}

void RoutingTable::markPinged(const Endpoint &endpoint, qint64 now)
{
    if (RoutingNode *node = locate(endpoint))
        node->lastPingSent = now;
}

const RoutingNode *RoutingTable::find(const Endpoint &endpoint) const
{
    return const_cast<RoutingTable *>(this)->locate(endpoint);
}

RoutingNode *RoutingTable::locate(const Endpoint &endpoint, int *bucketIndex)
{
    for (int i = 0; i < int(m_buckets.size()); ++i) {
        for (RoutingNode &n : m_buckets[i].nodes) {
            if (n.endpoint == endpoint) {
                if (bucketIndex)
                    *bucketIndex = i;
                return &n;
            }
        }
    }
    return nullptr;
}

bool RoutingTable::ipInUse(const QHostAddress &address) const
{
    for (const Bucket &b : m_buckets) {
        for (const RoutingNode &n : b.nodes) {
            if (n.endpoint.address == address)
                return true;
        }
    }
    return false;
}

std::vector<RoutingNode> RoutingTable::closest(const NodeId &target, int count, bool includeBad) const
{
    std::vector<RoutingNode> all;
    const qint64 now = 0; // state below only distinguishes Bad, which is time-independent
    for (const Bucket &b : m_buckets) {
        for (const RoutingNode &n : b.nodes) {
            if (includeBad || stateOf(n, now) != RoutingNode::State::Bad)
                all.push_back(n);
        }
    }
    const auto byDistance = [&](const RoutingNode &a, const RoutingNode &b) {
        return NodeId::closer(target, a.id, b.id);
    };
    if (int(all.size()) > count) {
        std::partial_sort(all.begin(), all.begin() + count, all.end(), byDistance);
        all.resize(count);
    } else {
        std::sort(all.begin(), all.end(), byDistance);
    }
    return all;
}

std::vector<RoutingNode> RoutingTable::nodesNeedingPing(qint64 now, int max, qint64 minPingIntervalMs) const
{
    std::vector<RoutingNode> out;
    for (const Bucket &b : m_buckets) {
        for (const RoutingNode &n : b.nodes) {
            if (stateOf(n, now) != RoutingNode::State::Questionable)
                continue;
            if (n.lastPingSent > 0 && now - n.lastPingSent < minPingIntervalMs)
                continue;
            out.push_back(n);
        }
    }
    std::sort(out.begin(), out.end(), [](const RoutingNode &a, const RoutingNode &b) {
        return std::max(a.lastResponse, a.lastQuery) < std::max(b.lastResponse, b.lastQuery);
    });
    if (int(out.size()) > max)
        out.resize(max);
    return out;
}

std::optional<int> RoutingTable::staleBucket(qint64 now, qint64 staleMs) const
{
    std::optional<int> best;
    for (int i = 0; i < int(m_buckets.size()); ++i) {
        const qint64 changed = m_buckets[i].lastChanged;
        if (now - changed < staleMs)
            continue;
        if (!best || changed < m_buckets[*best].lastChanged)
            best = i;
    }
    return best;
}

void RoutingTable::touchBucket(int index, qint64 now)
{
    if (index >= 0 && index < int(m_buckets.size()))
        m_buckets[index].lastChanged = now;
}

NodeId RoutingTable::randomIdInBucket(int index) const
{
    const bool last = index == int(m_buckets.size()) - 1;
    return NodeId::randomWithPrefix(m_self, index, !last);
}

bool RoutingTable::wouldAccept(const NodeId &id) const
{
    if (id == m_self)
        return false;
    const int index = bucketIndexFor(id);
    const Bucket &bucket = m_buckets[index];
    for (const RoutingNode &n : bucket.nodes) {
        if (n.id == id)
            return false;
    }
    if (int(bucket.nodes.size()) < K || canSplit(index))
        return true;
    return std::any_of(bucket.nodes.begin(), bucket.nodes.end(), [](const RoutingNode &n) {
        return n.failCount >= BadFailCount;
    });
}

void RoutingTable::rebuild(const NodeId &newSelf, qint64 now)
{
    std::vector<RoutingNode> nodes;
    std::vector<RoutingNode> cached;
    for (Bucket &b : m_buckets) {
        std::move(b.nodes.begin(), b.nodes.end(), std::back_inserter(nodes));
        std::move(b.replacements.begin(), b.replacements.end(), std::back_inserter(cached));
    }
    m_self = newSelf;
    m_buckets.assign(1, Bucket{});
    m_buckets[0].lastChanged = now;
    for (RoutingNode &n : nodes)
        insert(std::move(n), now);
    for (RoutingNode &n : cached)
        insert(std::move(n), now);
}

int RoutingTable::size() const
{
    int total = 0;
    for (const Bucket &b : m_buckets)
        total += int(b.nodes.size());
    return total;
}

std::vector<RoutingNode> RoutingTable::allNodes() const
{
    std::vector<RoutingNode> out;
    for (const Bucket &b : m_buckets)
        out.insert(out.end(), b.nodes.begin(), b.nodes.end());
    return out;
}

} // namespace dht
