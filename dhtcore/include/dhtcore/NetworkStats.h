#pragma once

#include "dhtcore/NodeCatalog.h"
#include "dhtcore/NodeId.h"

#include <QString>

#include <array>
#include <deque>
#include <utility>
#include <vector>

namespace dht {

// How many answering addresses report one client (and, in the detailed
// list, one version of it). Fractional: an address running several nodes
// is split evenly between what they report.
struct ClientTally
{
    QString name;     // "libtorrent (Rasterbar)", "no version sent", "unknown client ZZ", ...
    QString version;  // "2.0.11"; "bytes ab cd" when the layout is unpublished; empty in the by-name list
    QString kind;     // known, unknown, nonstandard, absent
    double count = 0;
};

// Statistics for one address family, or both, counted by IP address: node
// IDs can be changed at will and one host can run many nodes, while an
// address is comparatively stable. Everything is as seen from this machine.
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

    // Nodes per answering address, to turn node counts into address counts.
    double nodesPerIp() const { return answeringIps > 0 ? double(answeringNodes) / answeringIps : 1.0; }
};

struct NetworkStatsSet
{
    NetworkStats ipv4;
    NetworkStats ipv6;
    NetworkStats all;
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
