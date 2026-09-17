#include "dhtcore/Sybil.h"

#include "dhtcore/Bep42.h"

#include <QHashFunctions>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace dht {

namespace {

using AddressKey = std::array<quint8, 16>;

struct AddressKeyHash
{
    size_t operator()(const AddressKey &key) const noexcept { return qHashBits(key.data(), key.size(), 0); }
};

bool isMappedV4(const AddressKey &key)
{
    static constexpr std::array<quint8, 12> prefix = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    return std::memcmp(key.data(), prefix.data(), prefix.size()) == 0;
}

QHostAddress addressOf(const AddressKey &key)
{
    if (isMappedV4(key))
        return QHostAddress((quint32(key[12]) << 24) | (quint32(key[13]) << 16) | (quint32(key[14]) << 8) | key[15]);
    return QHostAddress(key.data());
}

// The /24 or /48 an address belongs to, as bytes of the packed key.
bool sameSubnet(const AddressKey &a, const AddressKey &b)
{
    const size_t bytes = isMappedV4(a) ? 15 : 6;
    return isMappedV4(a) == isMappedV4(b) && std::memcmp(a.data(), b.data(), bytes) == 0;
}

NodeId maskedTo(const NodeId &id, int bits)
{
    NodeId out;
    for (int i = 0; i < NodeId::Size; ++i) {
        const int start = i * 8;
        if (bits >= start + 8)
            out[i] = id[i];
        else if (bits > start)
            out[i] = quint8(id[i] & (0xff << (8 - (bits - start))));
    }
    return out;
}

quint32 prefix21(const NodeId &id)
{
    return ((quint32(id[0]) << 16) | (quint32(id[1]) << 8) | id[2]) >> 3;
}

struct Item
{
    AddressKey address;
    NodeId id;
    quint16 port;
    quint8 selfShare;
    bool compliant;
};

struct AddressInfo
{
    AddressKey address;
    int nodes = 0;
    int compliant = 0;
    int prefixes = 0;
};

} // namespace

quint8 AddressSignals::at(const Key &address) const
{
    const auto it = std::lower_bound(m_items.begin(), m_items.end(), address,
                                     [](const auto &item, const Key &k) { return item.first < k; });
    return it != m_items.end() && it->first == address ? it->second : 0;
}

bool matchesSignals(const CatalogEntry &entry, quint8 filter, const AddressSignals *suspicion)
{
    if (filter == 0)
        return true;
    if (filter & PointsToSelf) {
        if (entry.state == CatalogEntry::State::Responsive && entry.selfListShare != CatalogEntry::NoShare
            && entry.selfListShare >= SybilReport::PointsToSelfAt)
            return true;
    }
    const quint8 bits = suspicion ? suspicion->at(entry.address) : 0;
    if (filter & SeveralSignals && std::popcount(unsigned(bits)) >= 2)
        return true;
    return (bits & filter & ~(PointsToSelf | SeveralSignals)) != 0;
}

double poissonTail(double lambda, int k)
{
    if (k <= 0)
        return 1.0;
    if (!(lambda > 0))
        return 0.0;
    if (lambda < k) {
        // Sum the terms from k upwards; they shrink quickly past the mean.
        double term = std::exp(-lambda + k * std::log(lambda) - std::lgamma(k + 1.0));
        double sum = term;
        for (int j = k + 1; j < k + 10000; ++j) {
            term *= lambda / j;
            sum += term;
            if (term < sum * 1e-16)
                break;
        }
        return std::min(1.0, sum);
    }
    double term = std::exp(-lambda);
    double below = term;
    for (int j = 1; j < k; ++j) {
        term *= lambda / j;
        below += term;
    }
    return std::clamp(1.0 - below, 0.0, 1.0);
}

QHostAddress subnetOf(const QHostAddress &address, int *bits)
{
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        *bits = 24;
        return QHostAddress(address.toIPv4Address() & 0xffffff00u);
    }
    *bits = 48;
    Q_IPV6ADDR a = address.toIPv6Address();
    for (int i = 6; i < 16; ++i)
        a.c[i] = 0;
    return QHostAddress(a);
}

SybilReport computeSybilReport(const NodeCatalog &catalog)
{
    SybilReport report;

    report.addressSignals = std::make_shared<const AddressSignals>(std::vector<std::pair<AddressKey, quint8>>{});
    std::vector<Item> items;
    catalog.forEach([&](NodeCatalog::Slot, const CatalogEntry &e) {
        if (e.state != CatalogEntry::State::Responsive)
            return;
        items.push_back({e.address, e.id, e.port, e.selfListShare,
                         bep42::Status(e.bep42) == bep42::Status::Compliant});
    });
    report.answeringNodes = int(items.size());
    if (items.empty())
        return report;

    std::unordered_map<AddressKey, quint8, AddressKeyHash> suspicion;

    // --- by address: many nodes, and the subnets they sit in -----------------
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.address < b.address; });
    std::vector<AddressInfo> addresses;
    for (size_t begin = 0; begin < items.size();) {
        size_t end = begin + 1;
        while (end < items.size() && items[end].address == items[begin].address)
            ++end;
        AddressInfo info;
        info.address = items[begin].address;
        std::vector<quint32> prefixes;
        for (size_t i = begin; i < end; ++i) {
            ++info.nodes;
            info.compliant += items[i].compliant ? 1 : 0;
            prefixes.push_back(prefix21(items[i].id));
            if (items[i].selfShare != CatalogEntry::NoShare && items[i].selfShare >= SybilReport::PointsToSelfAt) {
                report.selfPointers.push_back({addressOf(items[i].address), items[i].port, items[i].selfShare});
                suspicion[info.address] |= PointsToSelf;
            }
        }
        std::sort(prefixes.begin(), prefixes.end());
        info.prefixes = int(std::unique(prefixes.begin(), prefixes.end()) - prefixes.begin());
        if (info.nodes >= SybilReport::ManyNodesAt)
            suspicion[info.address] |= ManyNodes;
        addresses.push_back(info);
        begin = end;
    }
    report.selfPointerCount = int(report.selfPointers.size());

    // Addresses are sorted, so a subnet's addresses are consecutive.
    for (size_t begin = 0; begin < addresses.size();) {
        size_t end = begin + 1;
        int nodes = addresses[begin].nodes;
        while (end < addresses.size() && sameSubnet(addresses[end].address, addresses[begin].address)) {
            nodes += addresses[end].nodes;
            ++end;
        }
        const int count = int(end - begin);
        if (count >= SybilReport::DenseSubnetAt) {
            int bits = 0;
            const QHostAddress base = subnetOf(addressOf(addresses[begin].address), &bits);
            report.denseSubnets.push_back({base, bits, count, nodes});
            for (size_t i = begin; i < end; ++i)
                suspicion[addresses[i].address] |= DenseSubnet;
        }
        begin = end;
    }
    report.denseSubnetCount = int(report.denseSubnets.size());

    // --- by ID: shared IDs and dense windows ----------------------------------
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.id != b.id ? a.id < b.id : a.address < b.address;
    });
    std::vector<NodeId> distinctIds;
    for (size_t begin = 0; begin < items.size();) {
        size_t end = begin + 1;
        while (end < items.size() && items[end].id == items[begin].id)
            ++end;
        distinctIds.push_back(items[begin].id);
        int distinctAddresses = 0;
        SharedIdGroup group;
        group.id = items[begin].id;
        for (size_t i = begin; i < end; ++i) {
            if (i == begin || items[i].address != items[i - 1].address) {
                ++distinctAddresses;
                if (group.sample.size() < 5)
                    group.sample.push_back(addressOf(items[i].address));
            }
        }
        if (distinctAddresses >= 2) {
            group.addresses = distinctAddresses;
            report.sharedIds.push_back(group);
            for (size_t i = begin; i < end; ++i)
                suspicion[items[i].address] |= SharedId;
        }
        begin = end;
    }
    report.sharedIdCount = int(report.sharedIds.size());

    // Windows are ID prefixes; at depth L a random network puts about n/2^L
    // nodes in each. A window holding far more than that, allowing for how
    // many windows there are, is flagged. Several depths catch both broad
    // and tight clusters. Distinct IDs are counted, so shared IDs (above)
    // do not show up here as well.
    const size_t n = distinctIds.size();
    if (n >= 16) {
        const int base = int(std::floor(std::log2(double(n))));
        for (int depth : {base, base + 4, base + 8, base + 12}) {
            if (depth > NodeId::Bits)
                break;
            const double windows = std::ldexp(1.0, depth);
            const double expected = double(n) / windows;
            for (size_t begin = 0; begin < n;) {
                size_t end = begin + 1;
                while (end < n && NodeId::commonPrefixLength(distinctIds[end], distinctIds[begin]) >= depth)
                    ++end;
                const int count = int(end - begin);
                if (count >= 3) {
                    const double chance = std::min(1.0, poissonTail(expected, count) * windows);
                    if (chance < SybilReport::DenseWindowChance) {
                        DenseIdWindow w;
                        w.prefix = maskedTo(distinctIds[begin], depth);
                        w.bits = depth;
                        w.nodes = count;
                        w.expected = expected;
                        w.chance = chance;
                        // The nodes behind these IDs.
                        std::vector<AddressKey> keys;
                        const auto first = std::lower_bound(items.begin(), items.end(), distinctIds[begin],
                                                            [](const Item &it, const NodeId &id) { return it.id < id; });
                        for (auto it = first; it != items.end()
                                              && NodeId::commonPrefixLength(it->id, distinctIds[begin]) >= depth;
                             ++it) {
                            keys.push_back(it->address);
                            suspicion[it->address] |= DenseIds;
                        }
                        std::sort(keys.begin(), keys.end());
                        w.addresses = int(std::unique(keys.begin(), keys.end()) - keys.begin());
                        report.denseWindows.push_back(w);
                    }
                }
                begin = end;
            }
        }
    }

    // A cluster shows up at every depth that holds it. Keep the tightest
    // window for it, dropping wider ones that add little beyond it.
    std::sort(report.denseWindows.begin(), report.denseWindows.end(),
              [](const DenseIdWindow &a, const DenseIdWindow &b) { return a.bits > b.bits; });
    std::vector<DenseIdWindow> kept;
    for (const DenseIdWindow &w : report.denseWindows) {
        const bool covered = std::any_of(kept.begin(), kept.end(), [&](const DenseIdWindow &inner) {
            return NodeId::commonPrefixLength(inner.prefix, w.prefix) >= w.bits && w.nodes * 10 <= inner.nodes * 11;
        });
        if (!covered)
            kept.push_back(w);
    }
    report.denseWindows = std::move(kept);

    // --- everything about the addresses with signals ---------------------------
    const auto infoFor = [&](const AddressKey &key) -> const AddressInfo * {
        const auto it = std::lower_bound(addresses.begin(), addresses.end(), key,
                                         [](const AddressInfo &a, const AddressKey &k) { return a.address < k; });
        return it != addresses.end() && it->address == key ? &*it : nullptr;
    };
    for (const auto &[key, bits] : suspicion) {
        const AddressInfo *info = infoFor(key);
        if (!info)
            continue;
        SuspectAddress s{addressOf(key), info->nodes, info->compliant, info->prefixes, bits};
        if (bits & ManyNodes)
            report.manyNodes.push_back(s);
        if (std::popcount(unsigned(bits)) >= 2)
            report.flagged.push_back(s);
    }
    std::vector<std::pair<AddressKey, quint8>> sorted(suspicion.begin(), suspicion.end());
    std::sort(sorted.begin(), sorted.end());
    report.addressSignals = std::make_shared<const AddressSignals>(std::move(sorted));
    report.manyNodesCount = int(report.manyNodes.size());
    report.denseWindowCount = int(report.denseWindows.size());
    report.flaggedCount = int(report.flagged.size());

    // Order and trim the lists.
    const auto byNodes = [](const SuspectAddress &a, const SuspectAddress &b) {
        if (a.answeringNodes != b.answeringNodes)
            return a.answeringNodes > b.answeringNodes;
        return a.address.toString() < b.address.toString();
    };
    std::sort(report.manyNodes.begin(), report.manyNodes.end(), byNodes);
    std::sort(report.flagged.begin(), report.flagged.end(), [&](const SuspectAddress &a, const SuspectAddress &b) {
        const int sa = std::popcount(unsigned(a.suspicion));
        const int sb = std::popcount(unsigned(b.suspicion));
        return sa != sb ? sa > sb : byNodes(a, b);
    });
    std::sort(report.denseSubnets.begin(), report.denseSubnets.end(), [](const SuspectSubnet &a, const SuspectSubnet &b) {
        return a.addresses != b.addresses ? a.addresses > b.addresses : a.answeringNodes > b.answeringNodes;
    });
    std::sort(report.sharedIds.begin(), report.sharedIds.end(),
              [](const SharedIdGroup &a, const SharedIdGroup &b) { return a.addresses > b.addresses; });
    std::sort(report.denseWindows.begin(), report.denseWindows.end(),
              [](const DenseIdWindow &a, const DenseIdWindow &b) { return a.chance < b.chance; });
    std::sort(report.selfPointers.begin(), report.selfPointers.end(), [](const SelfPointer &a, const SelfPointer &b) {
        return a.sharePercent > b.sharePercent;
    });
    const auto trim = [](auto &list) {
        if (list.size() > size_t(SybilReport::MaxListed))
            list.resize(SybilReport::MaxListed);
    };
    trim(report.manyNodes);
    trim(report.flagged);
    trim(report.denseSubnets);
    trim(report.sharedIds);
    trim(report.denseWindows);
    trim(report.selfPointers);
    return report;
}

} // namespace dht
