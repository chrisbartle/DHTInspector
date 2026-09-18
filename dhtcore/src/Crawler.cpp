#include "dhtcore/Crawler.h"

#include "dhtcore/Bep42.h"
#include "dhtcore/DhtNode.h"
#include "dhtcore/Inbound.h"
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
        // Rates are measured from here; the first sample follows shortly.
        m_historyCounters = {now, m_queries, m_answers + m_errors, m_featureQueries,
                             m_inbound ? m_inbound->queries() : 0};
        m_nextHistoryMs = now + std::min<qint64>(m_config.historyIntervalMs, 5000);
    } else {
        m_timer.stop();
        m_monitoredMs += now - m_monitoringSinceMs;
        refreshStats(now);  // what was found stays on view, up to date
        sampleHistory(now);
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

    // Discovery and rechecks go first, keeping a share of the batch for
    // feature checks; those only go to hosts with room left, so where the
    // per-host limit holds things back, the scan itself keeps priority.
    const int scanShare = m_batch - std::max(1, m_batch / FeatureShareDivisor);
    int sent = backlogged() ? 0 : dispatchDeferred(scanShare, now);
    while (sent < scanShare && !backlogged() && dispatchNext(now))
        ++sent;
    if (!backlogged())
        sent += dispatchFeatures(m_batch - sent, now);
    while (sent < m_batch && !backlogged() && dispatchNext(now))
        ++sent;
    m_lastTickFull = sent >= m_batch;

    if (m_config.sizeEstimateIntervalMs > 0 && now >= m_nextEstimateMs) {
        m_nextEstimateMs = now + m_config.sizeEstimateIntervalMs;
        estimateSize();
    }
    if (now >= m_nextStatsMs)
        refreshStats(now);
    if (m_config.historyIntervalMs > 0 && now >= m_nextHistoryMs) {
        m_nextHistoryMs = now + m_config.historyIntervalMs;
        sampleHistory(now);
    }
}

void Crawler::sampleHistory(qint64 now)
{
    if (m_config.historyIntervalMs <= 0 || now - m_historyCounters.atMs <= 0)
        return;
    if (!m_stats || now - m_stats->computedAtMs > m_config.historyIntervalMs)
        refreshStats(now);
    const NetworkStatsSet &st = *m_stats;
    const NetworkStats &all = st.all;
    using M = Metric;

    HistorySample s;
    s.atMs = now;
    s.spanMs = now - m_historyCounters.atMs;
    // A pause (or the very first sample) leaves a gap on the charts.
    s.gapBefore = m_lastHistoryMs < 0 || m_historyCounters.atMs - m_lastHistoryMs > m_config.historyIntervalMs;

    s.set(M::HeardIps, all.heardIps);
    s.set(M::ConnectedIps, all.connectedIps);
    s.set(M::AnsweringIps, all.answeringIps);

    double size = 0;
    bool anySize = false;
    for (int f = 0; f < 2; ++f) {
        const SizeEstimate e = m_size[f].summary();
        const NetworkStats &fs = f == 0 ? st.ipv4 : st.ipv6;
        if (e.samples > 0 && e.median > 0) {
            size += e.median / fs.nodesPerIp();
            anySize = true;
        }
    }
    if (anySize)
        s.set(M::SizeEstimate, size);

    const double seconds = double(s.spanMs) / 1000.0;
    const qint64 inbound = m_inbound ? m_inbound->queries() : 0;
    s.set(M::QueriesPerSecond, double(m_queries - m_historyCounters.queries) / seconds);
    s.set(M::AnswersPerSecond, double(m_answers + m_errors - m_historyCounters.answers) / seconds);
    s.set(M::FeatureChecksPerSecond, double(m_featureQueries - m_historyCounters.features) / seconds);
    s.set(M::InboundPerSecond, double(inbound - m_historyCounters.inbound) / seconds);
    m_historyCounters = {now, m_queries, m_answers + m_errors, m_featureQueries, inbound};

    if (all.rttMedianMs >= 0)
        s.set(M::RttMedianMs, all.rttMedianMs);
    if (all.answeringIps > 0)
        s.set(M::Bep42Share, all.bep42[int(bep42::Status::Compliant)] / all.answeringIps);
    const auto share = [&](M metric, const FeatureTally &t) {
        if (t.tested > 0)
            s.set(metric, t.yes / t.tested);
    };
    share(M::Bep51Share, all.bep51);
    share(M::Bep44Share, all.bep44);
    share(M::SendsIpShare, all.sendsIp);
    share(M::InventsPeersShare, all.inventsPeers);

    s.set(M::Flagged, st.suspicious.flaggedCount);
    s.set(M::ManyNodes, st.suspicious.manyNodesCount);
    s.set(M::DenseSubnets, st.suspicious.denseSubnetCount);
    s.set(M::SharedIds, st.suspicious.sharedIdCount);
    s.set(M::DepartedPerHour, all.churn.departedLastHour);
    s.set(M::ReturnedPerHour, all.churn.returnedLastHour);

    // Lookups: both families' latest, weighted by how many each has.
    LookupPerformance p4 = m_lookups[0].summary();
    const LookupPerformance p6 = m_lookups[1].summary();
    const LookupPerformance &p = p6.samples > p4.samples ? p6 : p4;
    if (p.samples > 0) {
        s.set(M::LookupMedianMs, p.medianMs);
        s.set(M::LookupMedianQueries, p.medianQueries);
        s.set(M::LookupResponseRate, p.responseRate);
    }

    constexpr size_t ClientsKept = 10;
    for (size_t i = 0; i < all.clients.size() && i < ClientsKept && all.answeringIps > 0; ++i)
        s.clientShares.emplace_back(all.clients[i].name, all.clients[i].count / all.answeringIps);

    m_lastHistoryMs = now;
    m_history.add(std::move(s));
    m_historyView = std::make_shared<const std::vector<HistorySample>>(m_history.samples());
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
        const NodeId target = NodeId::random();
        node->findNode(target, [self, family](const Lookup::Result &result) {
            if (!self)
                return;
            self->m_widening[int(family)] = false;
            self->recordLookup(family, result);
        });
    }
}

void Crawler::estimateSize()
{
    for (DhtNode *node : {m_v4.data(), m_v6.data()}) {
        if (!node || m_estimating[int(node->family())] >= MaxEstimates)
            continue;
        const Family family = node->family();
        ++m_estimating[int(family)];
        const NodeId target = NodeId::random();
        QPointer<Crawler> self(this);
        // get_peers rather than find_node: it returns the same nodes, so the
        // size estimate is unchanged, and the target was invented here, so
        // any peer that comes back is invented too. That catches the nodes
        // which answer a bare check honestly and only invent peers for
        // hashes they have seen a lookup ask about.
        node->getPeers(target, [self, family](const Lookup::Result &result) {
            if (!self)
                return;
            --self->m_estimating[int(family)];
            self->recordLookup(family, result);
        });
    }
}

void Crawler::recordLookup(Family family, const Lookup::Result &result)
{
    // Every random-target lookup is a size sample and a measure of how
    // lookups perform, and its nodes are worth knowing about.
    const std::vector<Lookup::Contact> &closest = result.closest;
    const NodeId &target = result.target;
    if (result.queried > 0) {
        m_lookups[int(family)].add(result.durationMs, result.queried, result.responded, result.hops,
                                   int(closest.size()) >= Lookup::K);
    }
    std::vector<NodeId> ids;
    ids.reserve(closest.size());
    for (const Lookup::Contact &contact : closest)
        ids.push_back(contact.id);
    if (int(ids.size()) >= Lookup::K)
        m_size[int(family)].add(estimateNetworkSize(target, ids));

    // Every target here is invented by this crawler, so a node that answered
    // with peers made them up. The nodes that answered honestly are not
    // marked as checked: the denominator stays the feature check, which
    // reaches every answering node evenly, and this only ever adds to the
    // numerator. The share is therefore a floor.
    if (result.kind == Lookup::Kind::GetPeers) {
        ++m_randomLookups;
        m_lookupsWithInventedPeers += result.sightings.empty() ? 0 : 1;
    }
    for (const PeerSighting &sighting : result.sightings) {
        const Slot slot = m_catalog->find(sighting.source);
        if (slot != NodeCatalog::NoSlot)
            m_catalog->at(slot).set(CatalogEntry::InventsPeers);
    }

    const qint64 now = nowMs();
    for (const Lookup::Contact &contact : closest)
        learn({contact.id, contact.endpoint}, now);
}

void Crawler::refreshStats(qint64 now)
{
    QElapsedTimer timer;
    timer.start();
    auto stats = std::make_shared<NetworkStatsSet>(computeNetworkStats(*m_catalog, now));
    stats->suspicious = computeSybilReport(*m_catalog);
    if (m_inbound)
        stats->inbound = m_inbound->summary();
    stats->computeMs = int(timer.elapsed());
    m_nextStatsMs = now + std::max<qint64>(StatsIntervalMs, qint64(StatsCostFactor) * stats->computeMs);
    m_stats = std::move(stats);
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
            const bool wasGone = entry.state == CatalogEntry::State::Gone;
            entry.failures = 0;
            entry.lastAnswered = m_catalog->stamp(now);
            if (!wasAnswering) {
                entry.answeringSince = entry.lastAnswered;
                entry.set(CatalogEntry::Rejoined, wasGone);
            }
            entry.rttMs = quint16(std::clamp(reply.rttMs, 0, int(CatalogEntry::NoRtt) - 1));
            entry.setVersion(reply.message.version);
            if (const auto id = reply.message.senderId()) {
                entry.id = *id;
                entry.bep42 = quint8(bep42::check(*id, entry.hostAddress()));
            }
            if (reply.status == RpcReply::Status::Response)
                noteResponse(entry, reply);
            m_catalog->setState(slot, CatalogEntry::State::Responsive);
            m_revisit.push_back(ref);
            queueFeatures(ref);
        }
    } else {
        ++(reply.status == RpcReply::Status::Timeout ? m_timeouts
           : reply.status == RpcReply::Status::Response ? m_answers : m_errors);
    }

    // After the entry is updated: learning can grow the catalogue, which
    // invalidates references into it.
    if (reply.status == RpcReply::Status::Response)
        learnFrom(ref, reply, now);
}

void Crawler::noteResponse(CatalogEntry &entry, const RpcReply &reply)
{
    entry.set(CatalogEntry::TestedIp);
    entry.set(CatalogEntry::SendsIp, reply.message.reportedAddress.has_value());
}

void Crawler::learnFrom(Ref ref, const RpcReply &reply, qint64 now)
{
    const BValue &body = reply.message.body;
    std::vector<krpc::CompactNode> listed;
    for (Family family : {Family::IPv4, Family::IPv6}) {
        const QByteArray key = family == Family::IPv4 ? QByteArray("nodes") : QByteArray("nodes6");
        if (const auto bytes = body.stringAt(key)) {
            const auto nodes = krpc::decodeNodes(*bytes, family).nodes;
            listed.insert(listed.end(), nodes.begin(), nodes.end());
        }
    }

    // What the responder lists says something about it: a node listing
    // mostly its own address or subnet may be part of a group pushing its
    // own nodes, and one listing unreachable addresses is passing on junk.
    if (!listed.empty() && m_catalog->isCurrent(ref)) {
        CatalogEntry &entry = m_catalog->at(ref.slot);
        int bits = 0;
        const QHostAddress subnet = subnetOf(entry.hostAddress(), &bits);
        // The share is over the responder's own family, which is all a
        // subnet can match.
        const Family family = entry.family();
        int sameFamily = 0;
        int own = 0;
        bool bogons = false;
        for (const krpc::CompactNode &n : listed) {
            if (n.endpoint.family() == family) {
                ++sameFamily;
                own += n.endpoint.address.isInSubnet(subnet, bits) ? 1 : 0;
            }
            if (!isUsableRemote(n.endpoint, m_config.allowLocalAddresses))
                bogons = true;
        }
        if (sameFamily > 0)
            entry.selfListShare = quint8((own * 100 + sameFamily / 2) / sameFamily);
        entry.set(CatalogEntry::ListsBogons, bogons || entry.has(CatalogEntry::ListsBogons));
    }

    // Learning can grow the catalogue, so the entry is not touched after this.
    for (const krpc::CompactNode &n : listed)
        learn(n, now);
}

void Crawler::queueFeatures(Ref ref)
{
    CatalogEntry &entry = m_catalog->at(ref.slot);
    if (entry.featuresDone() || entry.has(CatalogEntry::FeatureQueued))
        return;
    entry.set(CatalogEntry::FeatureQueued);
    m_features.push_back(ref);
}

int Crawler::dispatchFeatures(int budget, qint64 now)
{
    Q_UNUSED(now);
    int sent = 0;
    const int scan = std::min(int(m_features.size()), DeferredScanPerTick);
    for (int i = 0; i < scan && sent < budget; ++i) {
        const Ref ref = m_features.front();
        m_features.pop_front();
        if (!m_catalog->isCurrent(ref))
            continue;
        CatalogEntry &entry = m_catalog->at(ref.slot);
        // Only nodes that are answering are worth the queries; the flag is
        // cleared so they are queued again when they next answer.
        if (entry.state != CatalogEntry::State::Responsive || entry.featuresDone()
            || !nodeFor(entry.family())) {
            entry.set(CatalogEntry::FeatureQueued, false);
            continue;
        }
        if (hostBusy(entry)) {
            m_features.push_back(ref);
            continue;
        }
        askFeature(ref.slot);
        ++sent;
    }
    return sent;
}

void Crawler::askFeature(Slot slot)
{
    CatalogEntry &entry = m_catalog->at(slot);
    const Endpoint endpoint = entry.endpoint();
    DhtNode *node = nodeFor(entry.family());

    BValue::Dict args;
    QByteArray method;
    CatalogEntry::Flag check;
    // Asking for both families' nodes doubles as the BEP 32 check.
    const auto withTarget = [&args] {
        args.emplace("target", BValue(NodeId::random().toBytes()));
        args.emplace("want", BValue(BValue::List{BValue("n4"), BValue("n6")}));
    };
    if (!entry.has(CatalogEntry::Tested51)) {
        method = "sample_infohashes";
        check = CatalogEntry::Tested51;
        withTarget();
    } else if (!entry.has(CatalogEntry::Tested44)) {
        method = "get";
        check = CatalogEntry::Tested44;
        withTarget();
    } else if (!entry.has(CatalogEntry::TestedPeers)) {
        // An infohash invented here and asked about at once: nobody can have
        // announced it, so peers in the reply are made up.
        method = "get_peers";
        check = CatalogEntry::TestedPeers;
        args.emplace("info_hash", BValue(NodeId::random().toBytes()));
        args.emplace("want", BValue(BValue::List{BValue("n4"), BValue("n6")}));
    } else {
        method = UnknownMethod;
        check = CatalogEntry::TestedUnknown;
    }

    const Ref ref = m_catalog->ref(slot);
    ++m_outstanding;
    ++m_featureQueries;
    QPointer<Crawler> self(this);
    node->probe(
        endpoint, method, std::move(args),
        [self, ref, check](const RpcReply &reply) {
            if (self)
                self->onFeatureReply(ref, check, reply);
        },
        m_config.queryTimeoutMs);
}

void Crawler::onFeatureReply(Ref ref, CatalogEntry::Flag check, const RpcReply &reply)
{
    --m_outstanding;
    if (!m_catalog->isCurrent(ref))
        return;
    using F = CatalogEntry;
    const qint64 now = nowMs();
    CatalogEntry &entry = m_catalog->at(ref.slot);
    const krpc::Message &m = reply.message;

    switch (reply.status) {
    case RpcReply::Status::Throttled:
        break;  // never sent: ask again later
    case RpcReply::Status::Timeout:
        // Nodes drop the odd datagram; a check only counts as unanswered
        // after a second try. Liveness is left to the regular rounds.
        entry.setFeatureTries(entry.featureTries() + 1);
        if (entry.featureTries() >= FeatureTries) {
            entry.set(check);
            entry.setFeatureTries(0);
        }
        break;
    case RpcReply::Status::Response:
    case RpcReply::Status::Error: {
        const bool response = reply.status == RpcReply::Status::Response;
        entry.set(check);
        entry.setFeatureTries(0);
        if (response) {
            noteResponse(entry, reply);
            if (check != F::TestedUnknown) {
                // Asked for both families: listing the other one is BEP 32.
                const bool v4 = entry.isIPv4();
                entry.set(F::Tested32);
                entry.set(F::Has32, m.body.stringAt(v4 ? "nodes6" : "nodes").has_value()
                                        || entry.has(F::Has32));
            }
        }
        if (check == F::Tested51) {
            // Nodes that do not know the method may still answer as if it
            // were find_node; only the BEP 51 fields count.
            const bool has = response && (m.body.stringAt("samples") || m.body.integerAt("num"));
            entry.set(F::Has51, has);
            if (has)
                entry.setSampleCount(m.body.integerAt("num").value_or(0));
        } else if (check == F::Tested44) {
            // A get for a missing item still returns a write token.
            entry.set(F::Has44, response && m.body.stringAt("token").has_value());
        } else if (check == F::TestedPeers) {
            // Only ever set: see CatalogEntry::InventsPeers.
            if (response && !krpc::decodePeers(m.body.listAt("values"), entry.family()).empty())
                entry.set(F::InventsPeers);
        } else {
            const bool is204 = !response && m.errorCode == krpc::MethodUnknown;
            entry.set(F::Answers204, is204);
            entry.set(F::AnswersOther, !is204);
            entry.set(F::AnswersError, !is204 && !response);
        }
        break;
    }
    }

    if (entry.featuresDone())
        entry.set(F::FeatureQueued, false);
    else
        m_features.push_back(ref);

    if (reply.status == RpcReply::Status::Response)
        learnFrom(ref, reply, now);
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
    if (const AddressProblem problem = addressProblem(listed.endpoint, m_config.allowLocalAddresses);
        problem != AddressProblem::None) {
        entry.failures = quint8(problem);
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
    s.featureQueries = m_featureQueries;
    s.featureWaiting = int(m_features.size());
    s.randomLookups = m_randomLookups;
    s.lookupsWithInventedPeers = m_lookupsWithInventedPeers;
    s.waiting = int(m_fresh.size() + m_deferred.size());
    s.batch = m_monitoring ? m_batch : 0;
    s.monitoredMs = m_monitoredMs + (m_monitoring ? now - m_monitoringSinceMs : 0);
    s.stats = m_stats;
    s.sizeV4 = m_size[0].summary();
    s.sizeV6 = m_size[1].summary();
    s.lookupV4 = m_lookups[0].summary();
    s.lookupV6 = m_lookups[1].summary();
    s.history = m_historyView;

    // Discovering while any node has yet to answer or run out of tries.
    if (!m_monitoring)
        s.phase = CrawlSnapshot::Phase::Off;
    else if (!m_fresh.empty() || !m_deferred.empty() || s.notAsked > 0)
        s.phase = CrawlSnapshot::Phase::Discovering;
    else if (m_outstanding > 0 || !m_features.empty() || anyDue(now))
        s.phase = CrawlSnapshot::Phase::Rechecking;
    else if (s.known == 0)
        s.phase = CrawlSnapshot::Phase::WaitingForNodes;
    else
        s.phase = CrawlSnapshot::Phase::UpToDate;
    return s;
}

} // namespace dht
