#pragma once

#include "dhtcore/NodeCatalog.h"
#include "dhtcore/NodeId.h"

#include <QHostAddress>

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace dht {

// Signals that a group of nodes may be run by one party to crowd the
// network (a Sybil attack). None is proof on its own; addresses that trip
// several at once are the ones worth a closer look. Everything here is
// over nodes that answered their latest query.
enum SybilSignal : quint8 {
    ManyNodes = 1 << 0,     // several answering nodes on one address
    DenseSubnet = 1 << 1,   // many answering addresses in one /24 or /48
    SharedId = 1 << 2,      // one node ID answering from several addresses
    DenseIds = 1 << 3,      // IDs packed closer than chance allows
    PointsToSelf = 1 << 4,  // lists mostly its own address or subnet
};

// Pseudo-signal for filtering: two or more signals at once.
constexpr quint8 SeveralSignals = 1 << 7;

// Every address with at least one signal, sorted by address, for filtering
// the node list.
class AddressSignals
{
public:
    using Key = std::array<quint8, 16>;
    explicit AddressSignals(std::vector<std::pair<Key, quint8>> sorted) : m_items(std::move(sorted)) {}
    quint8 at(const Key &address) const;
    int size() const { return int(m_items.size()); }

private:
    std::vector<std::pair<Key, quint8>> m_items;
};

// Whether a node matches a signal filter (SybilSignal bits, or
// SeveralSignals). Points-to-self is judged per node, the rest per address.
bool matchesSignals(const CatalogEntry &entry, quint8 filter, const AddressSignals *suspicion);

struct SuspectAddress
{
    QHostAddress address;
    int answeringNodes = 0;
    int compliantNodes = 0;    // BEP 42 compliant among them
    int distinctPrefixes = 0;  // distinct 21-bit ID prefixes; BEP 42 allows at most 8 per address
    quint8 suspicion = 0;
};

struct SuspectSubnet
{
    QHostAddress base;
    int bits = 0;
    int addresses = 0;
    int answeringNodes = 0;
};

struct SharedIdGroup
{
    NodeId id;
    int addresses = 0;
    std::vector<QHostAddress> sample;  // a few of them
};

struct DenseIdWindow
{
    NodeId prefix;
    int bits = 0;
    int nodes = 0;
    double expected = 0;     // nodes a window this size holds by chance
    double chance = 0;       // probability of any such window by chance, all windows considered
    int addresses = 0;
};

struct SelfPointer
{
    QHostAddress address;
    quint16 port = 0;
    int sharePercent = 0;
};

struct SybilReport
{
    // Thresholds, reported so the page can say what was looked for.
    static constexpr int ManyNodesAt = 5;
    static constexpr int DenseSubnetAt = 8;
    static constexpr int PointsToSelfAt = 50;
    static constexpr double DenseWindowChance = 1e-3;
    static constexpr int MaxListed = 50;

    int answeringNodes = 0;
    std::vector<SuspectAddress> manyNodes;     // most nodes first
    std::vector<SuspectSubnet> denseSubnets;   // most addresses first
    std::vector<SharedIdGroup> sharedIds;      // most addresses first
    std::vector<DenseIdWindow> denseWindows;   // least likely first
    std::vector<SelfPointer> selfPointers;     // highest share first
    std::vector<SuspectAddress> flagged;       // two or more signals, most first
    int flaggedCount = 0;
    int manyNodesCount = 0;
    int denseSubnetCount = 0;
    int sharedIdCount = 0;
    int selfPointerCount = 0;
    int denseWindowCount = 0;
    std::shared_ptr<const AddressSignals> addressSignals;
};

// Probability that a Poisson variable with mean `lambda` is at least `k`.
double poissonTail(double lambda, int k);

// The subnet an address is judged in: /24 for IPv4, /48 for IPv6.
QHostAddress subnetOf(const QHostAddress &address, int *bits);

SybilReport computeSybilReport(const NodeCatalog &catalog);

} // namespace dht
