#pragma once

#include "dhtcore/Endpoint.h"
#include "dhtcore/Lookup.h"
#include "dhtcore/NodeCatalog.h"
#include "dhtcore/NodeId.h"
#include "dhtcore/RpcManager.h"
#include "dhtcore/Snapshot.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <array>
#include <deque>
#include <vector>

namespace dht {

class DhtNode;

struct CensusConfig
{
    int slices = 8;             // per address family
    int sliceBits = -1;         // slice = 1/2^bits of the ID space; -1 chooses from the network size
    int targetPerSlice = 2000;  // nodes a slice should hold when bits are chosen
    int defaultSliceBits = 12;  // when there is no size estimate to choose from
    int maxOutstanding = 256;   // queries awaiting replies at once
    int queryTimeoutMs = RpcManager::DefaultTimeoutMs;
    int failuresToGiveUp = 2;
    int quietRoundsToFinish = 2;  // rounds without a new node before a slice is done
    int maxRounds = 10;
    bool allowLocalAddresses = false;
};

// Probability that at least one of an address's `ids` nodes lies in a slice
// of 1/2^bits of the ID space. Scaling by its inverse (Horvitz-Thompson)
// keeps an address with many nodes from being counted many times over.
double sliceInclusionProbability(int ids, int bits);

// Mean and 95% interval (Student's t) of per-slice estimates.
void summariseSlices(const std::vector<double> &estimates, double *mean, double *low, double *high);

// The precise count: takes random slices of the ID space and asks the nodes
// inside each, over and over with different targets, until rounds stop
// turning up new ones, which finds nearly every reachable node there. The
// addresses heard of and the addresses that answered are then scaled up by
// the slice's share of the ID space. Node IDs are used only to find nodes
// and decide slice membership; everything is counted by IP address.
class Census : public QObject
{
    Q_OBJECT

public:
    static constexpr int TickMs = 20;

    Census(NodeCatalog *catalog, const CensusConfig &config, QObject *parent = nullptr);

    void setNodes(DhtNode *v4, DhtNode *v6);

    // Size estimates (in nodes) choose the slice width; 0 when unknown.
    void start(double nodesV4, double nodesV6);
    void cancel();
    bool isRunning() const { return m_state == CensusSnapshot::State::Running; }

    CensusSnapshot snapshot() const;

signals:
    void finished();

private:
    enum class NodeState : quint8 { Waiting, Asked, Answered, Silent };

    struct SliceNode
    {
        NodeId listedId;
        NodeId answeredId;
        NodeState state = NodeState::Waiting;
        quint8 failures = 0;
        bool answeredInSlice = false;
        int askedInRound = -1;
    };

    void tick();
    void startSlice();
    void finishSlice();
    void finishAll(CensusSnapshot::State state);
    void beginRound();
    void seedSlice();
    bool inSlice(const NodeId &id) const;
    void consider(const NodeId &id, const Endpoint &endpoint);
    void ask(const Endpoint &endpoint);
    void onReply(quint64 generation, const Endpoint &endpoint, const RpcReply &reply);
    void onSeedLookup(quint64 generation, const Lookup::Result &result);
    DhtNode *currentNode() const;
    int chooseBits(Family family) const;
    CensusSlice countSlice() const;

    NodeCatalog *m_catalog;
    CensusConfig m_config;
    QPointer<DhtNode> m_v4;
    QPointer<DhtNode> m_v6;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QElapsedTimer m_sliceClock;

    CensusSnapshot::State m_state = CensusSnapshot::State::Idle;
    std::vector<Family> m_plan;  // one entry per slice still to run
    std::array<double, 2> m_sizeHint{};
    quint64 m_generation = 0;    // bumped per slice, so stale replies are ignored

    // The slice in progress.
    Family m_family = Family::IPv4;
    int m_bits = 0;
    NodeId m_prefix;
    int m_round = 0;
    int m_quietRounds = 0;
    int m_newThisRound = 0;
    int m_pendingLookups = 0;
    qint64 m_sliceQueries = 0;
    QHash<Endpoint, SliceNode> m_nodes;
    std::deque<Endpoint> m_queue;
    int m_outstanding = 0;

    qint64 m_queries = 0;
    qint64 m_elapsedMs = 0;
    std::vector<CensusSlice> m_done;
};

} // namespace dht
