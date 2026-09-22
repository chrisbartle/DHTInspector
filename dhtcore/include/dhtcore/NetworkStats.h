#pragma once

#include "dhtcore/ClientVersion.h"
#include "dhtcore/Endpoint.h"
#include "dhtcore/Inbound.h"
#include "dhtcore/NodeCatalog.h"
#include "dhtcore/NodeId.h"
#include "dhtcore/Sybil.h"

#include <QString>

#include <array>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

namespace dht {

// Statistics for one address family, or both, counted by IP address: node
// IDs can be changed at will and one host can run many nodes, while an
// address is comparatively stable. Everything is as seen from this machine.
// A yes/no property over answering addresses: how many were checked, and
// how many of those had it.
struct FeatureTally
{
    double tested = 0;
    double yes = 0;
};

// How addresses come and go, from the scan's timestamps. An address is up
// while any node there answers. Departures are seen only when a node is
// rechecked, so they lag by up to the recheck interval.
struct ChurnStats
{
    static constexpr qint64 WindowMs = 60 * 60 * 1000;
    static constexpr std::array<qint64, 3> SurvivalMs{60 * 60 * 1000, 6 * 60 * 60 * 1000, 24 * 60 * 60 * 1000};
    // Upper bounds of the time bins, the last one open-ended.
    static constexpr std::array<qint64, 8> BinEndsMs{10 * 60 * 1000,      30 * 60 * 1000,       60 * 60 * 1000,
                                                    3 * 60 * 60 * 1000,  6 * 60 * 60 * 1000,   12 * 60 * 60 * 1000,
                                                    24 * 60 * 60 * 1000, std::numeric_limits<qint64>::max()};

    int departedLastHour = 0;  // stopped answering within the window
    int returnedLastHour = 0;  // answering again, after having stopped, since within the window
    int arrivedLastHour = 0;   // answering, first heard of within the window
    // Of the addresses up at the start of each window, how many still are.
    std::array<int, 3> survivalBase{};
    std::array<int, 3> survivalKept{};
    std::array<int, 8> uptimeBins{};   // answering addresses, by how long they have been up
    std::array<int, 8> sessionBins{};  // addresses gone, by how long their last spell lasted
    qint64 medianUptimeMs = -1;
    qint64 medianSessionMs = -1;
};

struct NetworkStats
{
    static constexpr int MaxRttMs = 3000;

    int heardIps = 0;        // listed to us by any node (reachable addresses)
    int connectedIps = 0;    // a node there has answered us at some point
    int answeringIps = 0;    // a node there answered its latest query
    int unroutableIps = 0;   // listed, but not an address that can be reached
    int multiNodeIps = 0;    // answering addresses with more than one answering node
    int maxNodesPerIp = 0;
    int answeringNodes = 0;  // address-and-port entries behind answeringIps

    // The rest is over answering addresses, each counting once in total.
    std::vector<ClientTally> clients;   // by name, most common first
    std::vector<ClientTally> versions;  // by name and version, most common first
    std::array<double, 4> bep42{};      // indexed by bep42::Status

    std::vector<double> rttHistogram;   // addresses per millisecond, 0..MaxRttMs (the last bin holds anything slower)
    double rttWeight = 0;               // addresses with a measured round trip
    double rttMeanMs = 0;
    int rttMedianMs = -1;
    int rttP90Ms = -1;
    int rttP99Ms = -1;

    std::vector<std::pair<quint16, double>> topPorts;  // most common first
    int distinctPorts = 0;
    double defaultPortCount = 0;                        // on 6881

    // Optional features, over answering addresses.
    FeatureTally bep51;         // sample_infohashes
    FeatureTally bep44;         // get
    FeatureTally bep32;         // lists the other family when asked
    FeatureTally sendsIp;       // replies carry "ip"
    FeatureTally answers204;    // tested: asked an unknown query; yes: error 204
    double unknownOther = 0;    // answered the unknown query some other way
    double unknownOtherError = 0;  // of which with another error code
    FeatureTally listsBogons;   // tested: has listed nodes; yes: some unreachable
    // tested: asked for an infohash invented here; yes: answered with peers
    // for one. A floor: see CatalogEntry::InventsPeers.
    FeatureTally inventsPeers;
    double bep51SamplesMedian = -1;  // stored infohashes, over BEP 51 addresses
    double bep51SamplesMean = -1;    // the same, averaged
    double bep51SamplesMeanError = 0;  // standard error of that average

    // Listed addresses that cannot be contacted, by reason. Port 0 counts
    // addresses listed with that port, even if listed with others too.
    std::array<int, AddressProblemCount> unroutableByProblem{};

    ChurnStats churn;

    // Nodes per answering address, to turn node counts into address counts.
    double nodesPerIp() const { return answeringIps > 0 ? double(answeringNodes) / answeringIps : 1.0; }
};

struct NetworkStatsSet
{
    NetworkStats ipv4;
    NetworkStats ipv6;
    NetworkStats all;
    SybilReport suspicious;  // both families
    InboundSummary inbound;  // queries sent to us
    qint64 computedAtMs = 0;
    int computeMs = 0;  // how long the pass took
};

// One pass over the catalogue.
NetworkStatsSet computeNetworkStats(const NodeCatalog &catalog, qint64 nowMs, int topPorts = 20);

// The smallest value v such that at least `quantile` of the weight lies at
// or below it; -1 when there is no weight.
int histogramQuantile(const std::vector<double> &histogram, double total, double quantile);

// Network size from one lookup: in a network of N nodes with uniformly
// random IDs, the i-th closest node to a random target sits at a distance
// of about i/N of the ID space. A least-squares fit of the k closest
// distances gives N = sum(i^2) / sum(i * d_i), scaled slightly so that the
// median of many such estimates is unbiased. This counts nodes, not
// addresses, and only nodes that answer. It assumes uniformly spread IDs
// and complete lookups, which the real network does not guarantee, so it
// is a rough figure. Returns 0 with fewer than four nodes.
double estimateNetworkSize(const NodeId &target, const std::vector<NodeId> &closest);

// How many infohashes the network holds, from the "num" that BEP 51 nodes
// report. An announcement goes to the K nodes closest to the infohash, so
// each active infohash is stored about K times over, and the network holds
// about (nodes x mean stored per node) / K. Announcements expire after 30
// minutes, so this counts infohashes announced within about that long:
// torrents with an active peer, not every torrent there is.
//
// Only BEP 51 nodes say how much they store, and the rest can hold
// anything from nothing to as much, so the answer is a range: low assumes
// only the BEP 51 share of nodes stores anything, high that every node
// stores as much as the BEP 51 nodes do. The bounds widen that range by
// the 95% intervals on the network size and on the mean.
struct StoredInfohashEstimate
{
    static constexpr int Replicas = 8;

    bool valid = false;
    double low = 0;
    double high = 0;
    double lowBound = 0;
    double highBound = 0;
};

// nodes, nodesLow and nodesHigh: the network size in nodes, with its 95%
// interval; stats: the same family's statistics.
StoredInfohashEstimate estimateStoredInfohashes(const NetworkStats &stats, double nodes, double nodesLow,
                                                double nodesHigh);

struct SizeEstimate
{
    int samples = 0;
    double median = 0;
    double low = 0;   // 95% confidence interval for the median
    double high = 0;
};

// Rolling summary of per-lookup estimates. Single lookups scatter widely,
// so the median of many is reported, with a distribution-free interval
// from the order statistics.
class SizeEstimator
{
public:
    static constexpr int MaxSamples = 400;

    void add(double estimate);
    void clear() { m_samples.clear(); }
    SizeEstimate summary() const;

private:
    std::deque<double> m_samples;
};

} // namespace dht
