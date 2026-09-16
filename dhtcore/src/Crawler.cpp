#include "dhtcore/Crawler.h"

#include "dhtcore/Bep42.h"
#include "dhtcore/DhtNode.h"
#include "dhtcore/Support.h"

#include <algorithm>

namespace dht {

namespace {

// Beyond this much waiting in the RPC layer, a limit is doing the pacing
// and adding more would only lengthen queues.
constexpr int BacklogQueued = 2000;
// Leave room under RpcManager's pending cap for everything else.
constexpr int BacklogPending = RpcManager::DefaultMaxPending - 5000;
// With nothing new to ask, look again this often: at our routing tables,
// and with a lookup for a random ID, which reaches well beyond them.
constexpr qint64 SeedIntervalMs = 5 * 1000;
constexpr int TrimPerTick = 20000;
// How many set-aside nodes to look at per tick, at most.
constexpr int DeferredScanPerTick = 2000;

} // namespace

Crawler::Crawler(NodeCatalog *catalog, const CrawlConfig &config, QObject *parent)
    : QObject(parent)
    , m_catalog(catalog)
    , m_config(config)
    , m_timer(this)
{
    m_timer.setInterval(TickMs);
    connect(&m_timer, &QTimer::timeout, this, &Crawler::tick);
}

void Crawler::setNodes(DhtNode *v4, DhtNode *v6)
{
    m_v4 = v4;
    m_v6 = v6;
}

void Crawler::setMonitoring(bool on)
{
    if (on == m_monitoring)
        return;
    m_monitoring = on;
    const qint64 now = nowMs();
    if (on) {
        m_monitoringSinceMs = now;
        m_batch = 16;
        m_lastSendFailures = sendFailures();
        m_tickClock.start();
        seed(now);
        m_timer.start();
    } else {
        m_timer.stop();
        m_monitoredMs += now - m_monitoringSinceMs;
    }
}

DhtNode *Crawler::nodeFor(Family family) const
{
    return family == Family::IPv4 ? m_v4.data() : m_v6.data();
}

bool Crawler::isOwnId(const NodeId &id) const
{
    return (m_v4 && m_v4->id() == id) || (m_v6 && m_v6->id() == id);
}

bool Crawler::backlogged() const
{
    for (DhtNode *node : {m_v4.data(), m_v6.data()}) {
        if (node && (node->rpcQueued() > BacklogQueued || node->rpcPending() > BacklogPending))
            return true;
    }
    return false;
}

qint64 Crawler::sendFailures() const
{
    return (m_v4 ? m_v4->rpcSendFailures() : 0) + (m_v6 ? m_v6->rpcSendFailures() : 0);
}

void Crawler::tick()
{
    const qint64 now = nowMs();

    // Pace to what this machine manages: a late timer means replies are
    // queuing up unread, a refused datagram means the socket is full.
    const qint64 elapsed = m_tickClock.restart();
    const qint64 failures = sendFailures();
    if (elapsed > TickMs * 3 || failures > m_lastSendFailures)
        m_batch = std::max(MinBatch, m_batch / 2);
    else if (m_lastTickFull)
        m_batch = std::min(MaxBatch, m_batch + m_batch / 4 + 1);
    m_lastSendFailures = failures;

    m_catalog->trim(TrimPerTick);

    if (m_fresh.empty() && m_deferred.empty() && m_outstanding == 0 && now - m_lastSeedMs >= SeedIntervalMs) {
        seed(now);
        widen();
    }

    int sent = backlogged() ? 0 : dispatchDeferred(m_batch, now);
    while (sent < m_batch && !backlogged() && dispatchNext(now))
        ++sent;
    m_lastTickFull = sent >= m_batch;
}

void Crawler::seed(qint64 now)
{
    m_lastSeedMs = now;
    // Whatever our own routing tables hold is the starting point; after
    // that the scan feeds itself.
    for (DhtNode *node : {m_v4.data(), m_v6.data()}) {
        if (!node)
            continue;
        std::vector<krpc::CompactNode> start = node->closestNodes(node->id(), 8);
        for (int i = 0; i < 8; ++i) {
            const auto more = node->closestNodes(NodeId::random(), 8);
            start.insert(start.end(), more.begin(), more.end());
        }
        for (const krpc::CompactNode &n : start)
            learn(n, now);
    }
}

void Crawler::widen()
{
    for (DhtNode *node : {m_v4.data(), m_v6.data()}) {
        if (!node || m_widening[int(node->family())])
            continue;
        m_widening[int(node->family())] = true;
        const Family family = node->family();
        QPointer<Crawler> self(this);
        node->findNode(NodeId::random(), [self, family](const Lookup::Result &result) {
            if (!self)
                return;
            self->m_widening[int(family)] = false;
            const qint64 now = nowMs();
            for (const Lookup::Contact &contact : result.closest)
                self->learn({contact.id, contact.endpoint}, now);
        });
    }
}

NodeCatalog::Slot Crawler::takeDue(std::deque<Ref> &queue, qint64 minAgeMs, qint64 now)
{
    while (!queue.empty()) {
        const Ref ref = queue.front();
        if (!m_catalog->isCurrent(ref)) {
            queue.pop_front();
            continue;
        }
        if (minAgeMs > 0 && m_catalog->ageMs(m_catalog->at(ref.slot).lastQueried, now) < minAgeMs)
            return NodeCatalog::NoSlot;  // queues are in asking order, so nothing behind is due either
        queue.pop_front();
        return ref.slot;
    }
    return NodeCatalog::NoSlot;
}

bool Crawler::anyDue(qint64 now) const
{
    const auto due = [&](const std::deque<Ref> &queue, qint64 minAgeMs) {
        for (const Ref &ref : queue) {
            if (m_catalog->isCurrent(ref))
                return m_catalog->ageMs(m_catalog->at(ref.slot).lastQueried, now) >= minAgeMs;
        }
        return false;
    };
    return due(m_retry, m_config.retryDelayMs) || due(m_revisit, m_config.revisitIntervalMs)
           || due(m_revisitSilent, m_config.silentRevisitIntervalMs);
}

bool Crawler::hostBusy(const CatalogEntry &entry) const
{
    const DhtNode *node = nodeFor(entry.family());
    return node && node->rpcQueuedFor(entry.hostAddress()) >= MaxQueuedPerHost;
}

int Crawler::dispatchDeferred(int budget, qint64 now)
{
    int sent = 0;
    const int scan = std::min(int(m_deferred.size()), DeferredScanPerTick);
    for (int i = 0; i < scan && sent < budget; ++i) {
        const Deferred d = m_deferred.front();
        m_deferred.pop_front();
        if (!m_catalog->isCurrent(d.ref))
            continue;
        if (hostBusy(m_catalog->at(d.ref.slot))) {
            m_deferred.push_back(d);
            continue;
        }
        ask(d.ref.slot, d.firstContact, now);
        ++sent;
    }
    return sent;
}

bool Crawler::dispatchNext(qint64 now)
{
    bool first = true;
    Slot slot = takeDue(m_fresh, 0, now);
    if (slot == NodeCatalog::NoSlot) {
        first = false;
        slot = takeDue(m_retry, m_config.retryDelayMs, now);
    }
    if (slot == NodeCatalog::NoSlot)
        slot = takeDue(m_revisit, m_config.revisitIntervalMs, now);
    if (slot == NodeCatalog::NoSlot)
        slot = takeDue(m_revisitSilent, m_config.silentRevisitIntervalMs, now);
    if (slot == NodeCatalog::NoSlot)
        return false;

    const CatalogEntry &entry = m_catalog->at(slot);
    if (entry.state == CatalogEntry::State::Unroutable || !nodeFor(entry.family()))
        return true;  // dropped from the rounds; still counted as known
    if (hostBusy(entry)) {
        m_deferred.push_back({m_catalog->ref(slot), first});
        return true;
    }
    ask(slot, first, now);
    return true;
}

void Crawler::ask(Slot slot, bool firstContact, qint64 now)
{
    CatalogEntry &entry = m_catalog->at(slot);
    entry.lastQueried = m_catalog->stamp(now);
    const Endpoint endpoint = entry.endpoint();
    DhtNode *node = nodeFor(entry.family());

    // First contact asks for the node's own neighbourhood, which in a dense
    // network is mostly nodes we have not met; later rounds ask about
    // random parts of the ID space.
    const NodeId target = firstContact ? NodeId::randomWithPrefix(entry.id, NeighbourhoodBits, false)
                                       : NodeId::random();
    BValue::Dict args;
    args.emplace("target", BValue(target.toBytes()));

    const Ref ref = m_catalog->ref(slot);
    ++m_outstanding;
    ++m_queries;
    QPointer<Crawler> self(this);
    node->probe(
        endpoint, "find_node", std::move(args),
        [self, ref](const RpcReply &reply) {
            if (self)
                self->onReply(ref, reply);
        },
        m_config.queryTimeoutMs);
}

void Crawler::onReply(Ref ref, const RpcReply &reply)
{
    --m_outstanding;
    const qint64 now = nowMs();

    if (reply.status == RpcReply::Status::Throttled) {
        // Never sent: try again later without holding it against the node.
        ++m_notSent;
        if (m_catalog->isCurrent(ref))
            m_retry.push_back(ref);
        return;
    }

    if (m_catalog->isCurrent(ref)) {
        const Slot slot = ref.slot;
        CatalogEntry &entry = m_catalog->at(slot);
        if (reply.status == RpcReply::Status::Timeout) {
            ++m_timeouts;
            if (entry.failures < 255)
                ++entry.failures;
            if (entry.failures >= m_config.failuresToGiveUp) {
                m_catalog->setState(slot, entry.lastAnswered ? CatalogEntry::State::Gone
                                                             : CatalogEntry::State::Silent);
                m_revisitSilent.push_back(ref);
            } else {
                m_retry.push_back(ref);
            }
        } else {
            // A response or an error: either way the node is alive.
            ++(reply.status == RpcReply::Status::Response ? m_answers : m_errors);
            const bool wasAnswering = entry.state == CatalogEntry::State::Responsive;
            entry.failures = 0;
            entry.lastAnswered = m_catalog->stamp(now);
            if (!wasAnswering)
                entry.answeringSince = entry.lastAnswered;
            entry.rttMs = quint16(std::clamp(reply.rttMs, 0, int(CatalogEntry::NoRtt) - 1));
            entry.setVersion(reply.message.version);
            if (const auto id = reply.message.senderId()) {
                entry.id = *id;
                entry.bep42 = quint8(bep42::check(*id, entry.hostAddress()));
            }
            m_catalog->setState(slot, CatalogEntry::State::Responsive);
            m_revisit.push_back(ref);
        }
    } else {
        ++(reply.status == RpcReply::Status::Timeout ? m_timeouts
           : reply.status == RpcReply::Status::Response ? m_answers : m_errors);
    }

    // After the entry is updated: learning can grow the catalogue, which
    // invalidates references into it.
    if (reply.status == RpcReply::Status::Response)
        learnFrom(reply, now);
}

void Crawler::learnFrom(const RpcReply &reply, qint64 now)
{
    const BValue &body = reply.message.body;
    for (Family family : {Family::IPv4, Family::IPv6}) {
        const QByteArray key = family == Family::IPv4 ? QByteArray("nodes") : QByteArray("nodes6");
        if (const auto bytes = body.stringAt(key)) {
            for (const krpc::CompactNode &n : krpc::decodeNodes(*bytes, family).nodes)
                learn(n, now);
        }
    }
}

void Crawler::learn(const krpc::CompactNode &listed, qint64 now)
{
    if (!nodeFor(listed.endpoint.family()) || isOwnId(listed.id))
        return;
    bool added = false;
    const Slot slot = m_catalog->upsert(listed.endpoint, now, &added);
    if (slot == NodeCatalog::NoSlot)
        return;
    CatalogEntry &entry = m_catalog->at(slot);
    entry.lastListed = m_catalog->stamp(now);
    if (entry.sightings < 0xffff)
        ++entry.sightings;
    if (!added)
        return;

    entry.id = listed.id;
    if (!isUsableRemote(listed.endpoint, m_config.allowLocalAddresses)) {
        m_catalog->setState(slot, CatalogEntry::State::Unroutable);
        return;
    }
    m_fresh.push_back(m_catalog->ref(slot));
}

CrawlSnapshot Crawler::snapshot() const
{
    const qint64 now = nowMs();
    CrawlSnapshot s;
    s.known = m_catalog->size();
    s.notAsked = m_catalog->count(CatalogEntry::State::New);
    s.responsive = m_catalog->count(CatalogEntry::State::Responsive);
    s.silent = m_catalog->count(CatalogEntry::State::Silent);
    s.gone = m_catalog->count(CatalogEntry::State::Gone);
    s.unroutable = m_catalog->count(CatalogEntry::State::Unroutable);
    s.cap = m_catalog->cap();
    s.evicted = m_catalog->evicted();
    s.memoryBytes = m_catalog->memoryBytes();
    s.bytesPerEntry = NodeCatalog::bytesPerEntry();
    s.queries = m_queries;
    s.answers = m_answers;
    s.errors = m_errors;
    s.timeouts = m_timeouts;
    s.notSent = m_notSent;
    s.outstanding = m_outstanding;
    s.waiting = int(m_fresh.size() + m_deferred.size());
    s.batch = m_monitoring ? m_batch : 0;
    s.monitoredMs = m_monitoredMs + (m_monitoring ? now - m_monitoringSinceMs : 0);

    // Discovering while any node has yet to answer or run out of tries.
    if (!m_monitoring)
        s.phase = CrawlSnapshot::Phase::Off;
    else if (!m_fresh.empty() || !m_deferred.empty() || s.notAsked > 0)
        s.phase = CrawlSnapshot::Phase::Discovering;
    else if (m_outstanding > 0 || anyDue(now))
        s.phase = CrawlSnapshot::Phase::Rechecking;
    else if (s.known == 0)
        s.phase = CrawlSnapshot::Phase::WaitingForNodes;
    else
        s.phase = CrawlSnapshot::Phase::UpToDate;
    return s;
}

} // namespace dht
