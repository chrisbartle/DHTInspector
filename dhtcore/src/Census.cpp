#include "dhtcore/Census.h"

#include "dhtcore/DhtNode.h"
#include "dhtcore/Support.h"

#include <QHashFunctions>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace dht {

namespace {

constexpr int SeedLookups = 3;
constexpr int MaxQueuedPerHost = 2;
constexpr int MaxSliceBits = 24;
// First contact asks for a node's own surroundings inside the slice.
constexpr int NeighbourhoodExtraBits = 8;

using AddressKey = std::array<quint8, 16>;

struct AddressKeyHash
{
    size_t operator()(const AddressKey &key) const noexcept { return qHashBits(key.data(), key.size(), 0); }
};

AddressKey keyOf(const QHostAddress &address)
{
    AddressKey key{};
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v4 = address.toIPv4Address();
        key[10] = 0xff;
        key[11] = 0xff;
        key[12] = quint8(v4 >> 24);
        key[13] = quint8(v4 >> 16);
        key[14] = quint8(v4 >> 8);
        key[15] = quint8(v4);
    } else {
        const Q_IPV6ADDR v6 = address.toIPv6Address();
        std::memcpy(key.data(), v6.c, 16);
    }
    return key;
}

// Two-sided 97.5% points of Student's t for 1 to 30 degrees of freedom.
double tValue(int degreesOfFreedom)
{
    static constexpr double table[] = {
        12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
        2.201,  2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
        2.080,  2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042,
    };
    if (degreesOfFreedom < 1)
        return 0;
    return degreesOfFreedom <= 30 ? table[degreesOfFreedom - 1] : 1.96;
}

} // namespace

double sliceInclusionProbability(int ids, int bits)
{
    if (ids <= 0)
        return 0;
    if (bits <= 0)
        return 1;
    const double share = std::ldexp(1.0, -bits);
    return -std::expm1(double(ids) * std::log1p(-share));
}

void summariseSlices(const std::vector<double> &estimates, double *mean, double *low, double *high)
{
    const int n = int(estimates.size());
    double m = 0;
    for (double e : estimates)
        m += e;
    m = n > 0 ? m / n : 0;
    double half = 0;
    if (n > 1) {
        double squares = 0;
        for (double e : estimates)
            squares += (e - m) * (e - m);
        half = tValue(n - 1) * std::sqrt(squares / (n - 1)) / std::sqrt(double(n));
    }
    *mean = m;
    *low = std::max(0.0, m - half);
    *high = m + half;
}

Census::Census(NodeCatalog *catalog, const CensusConfig &config, QObject *parent)
    : QObject(parent)
    , m_catalog(catalog)
    , m_config(config)
    , m_timer(this)
{
    m_timer.setInterval(TickMs);
    connect(&m_timer, &QTimer::timeout, this, &Census::tick);
}

void Census::setNodes(DhtNode *v4, DhtNode *v6)
{
    m_v4 = v4;
    m_v6 = v6;
}

DhtNode *Census::currentNode() const
{
    return m_family == Family::IPv4 ? m_v4.data() : m_v6.data();
}

void Census::start(double nodesV4, double nodesV6)
{
    if (isRunning())
        return;
    m_plan.clear();
    for (Family family : {Family::IPv4, Family::IPv6}) {
        const bool present = family == Family::IPv4 ? bool(m_v4) : bool(m_v6);
        for (int i = 0; present && i < m_config.slices; ++i)
            m_plan.push_back(family);
    }
    m_sizeHint = {nodesV4, nodesV6};
    m_done.clear();
    m_queries = 0;
    m_elapsedMs = 0;
    m_state = CensusSnapshot::State::Running;
    m_clock.start();
    startSlice();
    if (isRunning())
        m_timer.start();
}

void Census::cancel()
{
    if (isRunning())
        finishAll(CensusSnapshot::State::Cancelled);
}

void Census::finishAll(CensusSnapshot::State state)
{
    m_state = state;
    m_timer.stop();
    m_plan.clear();
    m_queue.clear();
    m_nodes.clear();
    m_outstanding = 0;
    m_pendingLookups = 0;
    ++m_generation;  // replies still on their way are ignored
    m_elapsedMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    emit finished();
}

int Census::chooseBits(Family family) const
{
    if (m_config.sliceBits >= 0)
        return std::min(m_config.sliceBits, MaxSliceBits);
    const double nodes = m_sizeHint[int(family)];
    if (!(nodes > 0))
        return m_config.defaultSliceBits;
    const double bits = std::round(std::log2(nodes / std::max(1, m_config.targetPerSlice)));
    return std::clamp(int(bits), 0, MaxSliceBits);
}

void Census::startSlice()
{
    bool found = false;
    while (!found && !m_plan.empty()) {
        m_family = m_plan.front();
        m_plan.erase(m_plan.begin());
        found = currentNode() != nullptr;
    }
    if (!found) {
        finishAll(CensusSnapshot::State::Done);
        return;
    }

    ++m_generation;
    m_bits = chooseBits(m_family);
    m_prefix = NodeId::random();
    m_round = 0;
    m_quietRounds = 0;
    m_newThisRound = 0;
    m_pendingLookups = 0;
    m_sliceQueries = 0;
    m_outstanding = 0;
    m_nodes.clear();
    m_queue.clear();
    m_sliceClock.start();

    beginRound();
    seedSlice();
}

bool Census::inSlice(const NodeId &id) const
{
    return NodeId::commonPrefixLength(id, m_prefix) >= m_bits;
}

void Census::seedSlice()
{
    DhtNode *node = currentNode();

    // What the scan already knows about the slice.
    m_catalog->forEach([&](NodeCatalog::Slot, const CatalogEntry &e) {
        if (e.family() == m_family && e.state != CatalogEntry::State::Unroutable && inSlice(e.id))
            consider(e.id, e.endpoint());
    });

    // Our routing table, and lookups into the slice to reach it from outside.
    for (const krpc::CompactNode &n : node->closestNodes(NodeId::randomWithPrefix(m_prefix, m_bits, false), 8))
        consider(n.id, n.endpoint);
    const quint64 generation = m_generation;
    QPointer<Census> self(this);
    for (int i = 0; i < SeedLookups; ++i) {
        ++m_pendingLookups;
        node->findNode(NodeId::randomWithPrefix(m_prefix, m_bits, false),
                       [self, generation](const Lookup::Result &result) {
                           if (self)
                               self->onSeedLookup(generation, result);
                       });
    }
}

void Census::onSeedLookup(quint64 generation, const Lookup::Result &result)
{
    if (generation != m_generation)
        return;
    --m_pendingLookups;
    for (const Lookup::Contact &contact : result.closest)
        consider(contact.id, contact.endpoint);
}

void Census::consider(const NodeId &id, const Endpoint &endpoint)
{
    if (endpoint.family() != m_family || !inSlice(id))
        return;
    if (!isUsableRemote(endpoint, m_config.allowLocalAddresses))
        return;
    if (DhtNode *node = currentNode(); node && node->id() == id)
        return;
    if (m_nodes.contains(endpoint))
        return;
    m_nodes.insert(endpoint, SliceNode{id});
    ++m_newThisRound;
    m_queue.push_back(endpoint);
}

void Census::beginRound()
{
    ++m_round;
    m_newThisRound = 0;
    // Everything that answered is asked again, about another part of the
    // slice; its table holds more of it than one reply shows.
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        if (it->state == NodeState::Answered)
            m_queue.push_back(it.key());
    }
}

void Census::tick()
{
    if (!isRunning())
        return;

    DhtNode *node = currentNode();
    if (!node) {
        finishAll(CensusSnapshot::State::Cancelled);
        return;
    }

    // Ask as many as the window allows, setting aside busy hosts.
    size_t scanned = 0;
    const size_t limit = m_queue.size();
    while (m_outstanding < m_config.maxOutstanding && !m_queue.empty() && scanned++ < limit) {
        const Endpoint endpoint = m_queue.front();
        m_queue.pop_front();
        if (node->rpcQueuedFor(endpoint.address) >= MaxQueuedPerHost) {
            m_queue.push_back(endpoint);
            continue;
        }
        ask(endpoint);
    }

    if (!m_queue.empty() || m_outstanding > 0 || m_pendingLookups > 0)
        return;

    // A round is over.
    m_quietRounds = m_newThisRound == 0 ? m_quietRounds + 1 : 0;
    if (m_quietRounds >= m_config.quietRoundsToFinish || m_round >= m_config.maxRounds) {
        finishSlice();
        startSlice();
    } else {
        beginRound();
    }
}

void Census::ask(const Endpoint &endpoint)
{
    const auto it = m_nodes.find(endpoint);
    if (it == m_nodes.end())
        return;
    const bool firstContact = it->state == NodeState::Waiting && it->askedInRound < 0;
    it->state = it->state == NodeState::Answered ? NodeState::Answered : NodeState::Asked;
    it->askedInRound = m_round;

    const NodeId target = firstContact
                              ? NodeId::randomWithPrefix(it->listedId, std::min(m_bits + NeighbourhoodExtraBits, NodeId::Bits), false)
                              : NodeId::randomWithPrefix(m_prefix, m_bits, false);
    BValue::Dict args;
    args.emplace("target", BValue(target.toBytes()));

    ++m_outstanding;
    ++m_queries;
    ++m_sliceQueries;
    const quint64 generation = m_generation;
    QPointer<Census> self(this);
    currentNode()->probe(
        endpoint, "find_node", std::move(args),
        [self, generation, endpoint](const RpcReply &reply) {
            if (self)
                self->onReply(generation, endpoint, reply);
        },
        m_config.queryTimeoutMs);
}

void Census::onReply(quint64 generation, const Endpoint &endpoint, const RpcReply &reply)
{
    if (generation != m_generation)
        return;
    --m_outstanding;
    const auto it = m_nodes.find(endpoint);
    if (it == m_nodes.end())
        return;

    switch (reply.status) {
    case RpcReply::Status::Throttled:
        // Never sent; ask again without counting it against the node.
        if (it->state != NodeState::Answered)
            it->state = NodeState::Waiting;
        m_queue.push_back(endpoint);
        return;
    case RpcReply::Status::Timeout:
        if (it->state == NodeState::Answered)
            return;  // it answered in an earlier round; that stands
        if (++it->failures >= m_config.failuresToGiveUp) {
            it->state = NodeState::Silent;
        } else {
            it->state = NodeState::Waiting;
            m_queue.push_back(endpoint);
        }
        return;
    case RpcReply::Status::Error:
    case RpcReply::Status::Response:
        break;
    }

    it->state = NodeState::Answered;
    it->failures = 0;
    if (const auto id = reply.message.senderId()) {
        it->answeredId = *id;
        it->answeredInSlice = inSlice(*id);
    } else {
        it->answeredInSlice = inSlice(it->listedId);
    }

    // After the entry is updated: considering new nodes can rehash m_nodes.
    if (reply.status != RpcReply::Status::Response)
        return;
    const QByteArray key = m_family == Family::IPv4 ? QByteArray("nodes") : QByteArray("nodes6");
    if (const auto bytes = reply.message.body.stringAt(key)) {
        for (const krpc::CompactNode &n : krpc::decodeNodes(*bytes, m_family).nodes)
            consider(n.id, n.endpoint);
    }
}

void Census::finishSlice()
{
    m_done.push_back(countSlice());
}

CensusSlice Census::countSlice() const
{
    CensusSlice slice;
    slice.family = m_family;
    slice.bits = m_bits;
    slice.prefix = m_prefix;
    slice.rounds = m_round;
    slice.queries = m_sliceQueries;
    slice.durationMs = m_sliceClock.elapsed();

    struct Address
    {
        std::vector<NodeId> ids;  // distinct IDs seen inside the slice
        bool answered = false;
        int catalogIds = 0;       // distinct IDs the scan has seen at this address
    };
    std::unordered_map<AddressKey, Address, AddressKeyHash> addresses;
    const auto addId = [](std::vector<NodeId> &ids, const NodeId &id) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    };

    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        Address &a = addresses[keyOf(it.key().address)];
        ++slice.nodesHeard;
        addId(a.ids, it->listedId);
        if (it->state == NodeState::Answered && it->answeredInSlice) {
            ++slice.nodesAnswered;
            a.answered = true;
            addId(a.ids, it->answeredId);
        }
    }

    // An address running several nodes is more likely to land in a slice,
    // so it is scaled up less. How many it runs comes from the scan when
    // that has seen more of them than the slice did.
    std::unordered_map<AddressKey, std::vector<NodeId>, AddressKeyHash> known;
    m_catalog->forEach([&](NodeCatalog::Slot, const CatalogEntry &e) {
        if (addresses.count(e.address))
            addId(known[e.address], e.id);
    });

    for (auto &[key, a] : addresses) {
        const auto k = known.find(key);
        const int ids = std::max(int(a.ids.size()), k == known.end() ? 0 : int(k->second.size()));
        const double weight = 1.0 / sliceInclusionProbability(std::max(1, ids), m_bits);
        ++slice.ipsHeard;
        slice.heardEstimate += weight;
        if (a.answered) {
            ++slice.ipsAnswered;
            slice.connectedEstimate += weight;
        }
    }
    return slice;
}

CensusSnapshot Census::snapshot() const
{
    CensusSnapshot s;
    s.state = m_state;
    s.slicesDone = int(m_done.size());
    s.slicesTotal = s.slicesDone + int(m_plan.size()) + (isRunning() ? 1 : 0);
    s.queries = m_queries;
    s.elapsedMs = isRunning() ? m_clock.elapsed() : m_elapsedMs;
    s.slices = m_done;
    if (isRunning()) {
        s.family = m_family;
        s.bits = m_bits;
        s.round = m_round;
        s.nodesFound = int(m_nodes.size());
        s.outstanding = m_outstanding;
        for (const SliceNode &n : m_nodes)
            s.nodesAnswered += n.state == NodeState::Answered && n.answeredInSlice ? 1 : 0;
    }

    for (Family family : {Family::IPv4, Family::IPv6}) {
        std::vector<double> heard;
        std::vector<double> connected;
        for (const CensusSlice &slice : m_done) {
            if (slice.family == family) {
                heard.push_back(slice.heardEstimate);
                connected.push_back(slice.connectedEstimate);
            }
        }
        CensusTotal &total = family == Family::IPv4 ? s.ipv4 : s.ipv6;
        total.slices = int(heard.size());
        summariseSlices(heard, &total.heard, &total.heardLow, &total.heardHigh);
        summariseSlices(connected, &total.connected, &total.connectedLow, &total.connectedHigh);
    }
    return s;
}

} // namespace dht
