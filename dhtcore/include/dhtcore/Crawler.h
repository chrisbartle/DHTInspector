#pragma once

#include "dhtcore/Krpc.h"
#include "dhtcore/Lookup.h"
#include "dhtcore/NetworkStats.h"
#include "dhtcore/NodeCatalog.h"
#include "dhtcore/RpcManager.h"
#include "dhtcore/Snapshot.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <array>
#include <deque>
#include <memory>

namespace dht {

class DhtNode;

struct CrawlConfig
{
    // How long after a node was last asked before it is asked again.
    qint64 revisitIntervalMs = 10 * 60 * 1000;
    // The same for nodes that have stopped or never answered: they are
    // mostly unreachable, so they are rechecked less often.
    qint64 silentRevisitIntervalMs = 60 * 60 * 1000;
    // How long a node that did not answer waits for its second chance.
    qint64 retryDelayMs = 30 * 1000;
    // Unanswered queries in a row before a node counts as silent or gone.
    int failuresToGiveUp = 2;
    int queryTimeoutMs = RpcManager::DefaultTimeoutMs;
    // Start a size-estimating lookup per family this often, keeping up to
    // MaxEstimates running; 0 turns them off. Lookups on the real network
    // take seconds, so several run at once to gather samples quickly.
    qint64 sizeEstimateIntervalMs = 1000;
    bool allowLocalAddresses = false;
};

// Scans the whole network while monitoring is on: asks every node it hears
// of for its neighbours, gives silent ones a second try, then keeps
// rechecking what it knows, longest-unchecked first. Every query goes
// through the node's RpcManager, so the per-host limit and the send limit
// always apply; beyond those it goes as fast as this machine keeps up,
// backing off when the event loop falls behind or the OS refuses datagrams,
// since either would record healthy nodes as silent.
class Crawler : public QObject
{
    Q_OBJECT

public:
    static constexpr int TickMs = 20;
    static constexpr int MinBatch = 4;
    static constexpr int MaxBatch = 4000;
    static constexpr int NeighbourhoodBits = 24;
    // Queries the crawler lets wait behind any one IP address. Hosts running
    // many nodes would otherwise build long queues under the per-host limit,
    // stalling the scan and leaving traffic behind when it is paused.
    static constexpr int MaxQueuedPerHost = 2;
    // Statistics are recomputed at most this often, and less often when a
    // pass takes long, so they never cost more than a few percent.
    static constexpr qint64 StatsIntervalMs = 2000;
    static constexpr int StatsCostFactor = 20;
    static constexpr int MaxEstimates = 6;


    Crawler(NodeCatalog *catalog, const CrawlConfig &config, QObject *parent = nullptr);

    void setNodes(DhtNode *v4, DhtNode *v6);

    // Off pauses: what has been learned is kept.
    void setMonitoring(bool on);
    bool isMonitoring() const { return m_monitoring; }

    CrawlSnapshot snapshot() const;

private:
    using Ref = NodeCatalog::Ref;
    using Slot = NodeCatalog::Slot;

    void tick();
    bool dispatchNext(qint64 now);
    int dispatchDeferred(int budget, qint64 now);
    bool hostBusy(const CatalogEntry &entry) const;
    Slot takeDue(std::deque<Ref> &queue, qint64 minAgeMs, qint64 now);
    void ask(Slot slot, bool firstContact, qint64 now);
    void onReply(Ref ref, const RpcReply &reply);
    void learn(const krpc::CompactNode &listed, qint64 now);
    void learnFrom(const RpcReply &reply, qint64 now);
    void seed(qint64 now);
    void widen();
    void refreshStats(qint64 now);
    void estimateSize();
    void recordLookup(Family family, const NodeId &target, const std::vector<Lookup::Contact> &closest);
    DhtNode *nodeFor(Family family) const;
    bool isOwnId(const NodeId &id) const;
    bool backlogged() const;
    qint64 sendFailures() const;
    bool anyDue(qint64 now) const;

    NodeCatalog *m_catalog;
    CrawlConfig m_config;
    QPointer<DhtNode> m_v4;
    QPointer<DhtNode> m_v6;
    QTimer m_timer;
    QElapsedTimer m_tickClock;

    std::deque<Ref> m_fresh;          // never asked
    std::deque<Ref> m_retry;          // one unanswered query
    std::deque<Ref> m_revisit;        // answered last time
    std::deque<Ref> m_revisitSilent;  // silent or gone
    struct Deferred
    {
        Ref ref;
        bool firstContact;
    };
    std::deque<Deferred> m_deferred;  // due, but their host is busy
    std::array<bool, 2> m_widening{};  // per family: a widening lookup is running
    std::array<int, 2> m_estimating{};  // per family: size lookups running
    std::array<SizeEstimator, 2> m_size;
    std::shared_ptr<const NetworkStatsSet> m_stats;
    qint64 m_nextStatsMs = 0;
    qint64 m_nextEstimateMs = 0;

    bool m_monitoring = false;
    int m_batch = 16;
    bool m_lastTickFull = false;
    qint64 m_lastSendFailures = 0;
    qint64 m_lastSeedMs = 0;
    qint64 m_monitoringSinceMs = 0;
    qint64 m_monitoredMs = 0;
    int m_outstanding = 0;

    qint64 m_queries = 0;
    qint64 m_answers = 0;
    qint64 m_errors = 0;
    qint64 m_timeouts = 0;
    qint64 m_notSent = 0;
};

} // namespace dht
