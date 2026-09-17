#include "dhtcore/NetworkStats.h"

#include "dhtcore/Bep42.h"
#include "dhtcore/ClientVersion.h"

#include <QElapsedTimer>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace dht {

namespace {

constexpr quint16 DefaultPort = 6881;

// Everything gathered for one family before it is summarised. Weights
// are in addresses: each answering address contributes one in total,
// split evenly across its answering nodes.
struct Accumulator
{
    int heardIps = 0;
    int connectedIps = 0;
    int answeringIps = 0;
    int unroutableIps = 0;
    int multiNodeIps = 0;
    int maxNodesPerIp = 0;
    int answeringNodes = 0;
    QHash<quint64, double> versions;  // keyed by versionKey()
    std::array<double, 4> bep42{};
    std::vector<double> rtt = std::vector<double>(NetworkStats::MaxRttMs + 1, 0.0);
    double rttWeight = 0;
    double rttSumMs = 0;
    std::vector<double> ports = std::vector<double>(65536, 0.0);

    void add(const Accumulator &o)
    {
        heardIps += o.heardIps;
        connectedIps += o.connectedIps;
        answeringIps += o.answeringIps;
        unroutableIps += o.unroutableIps;
        multiNodeIps += o.multiNodeIps;
        maxNodesPerIp = std::max(maxNodesPerIp, o.maxNodesPerIp);
        answeringNodes += o.answeringNodes;
        for (auto it = o.versions.cbegin(); it != o.versions.cend(); ++it)
            versions[it.key()] += it.value();
        for (size_t i = 0; i < bep42.size(); ++i)
            bep42[i] += o.bep42[i];
        for (size_t i = 0; i < rtt.size(); ++i)
            rtt[i] += o.rtt[i];
        rttWeight += o.rttWeight;
        rttSumMs += o.rttSumMs;
        for (size_t i = 0; i < ports.size(); ++i)
            ports[i] += o.ports[i];
    }
};

// The "v" field as it is kept in the catalogue, packed into one number:
// length in the high half, the first four bytes in the low half.
quint64 versionKey(const CatalogEntry &e)
{
    return (quint64(e.versionLength) << 32) | (quint64(e.version[0]) << 24) | (quint64(e.version[1]) << 16)
           | (quint64(e.version[2]) << 8) | quint64(e.version[3]);
}

QByteArray versionBytes(quint64 key)
{
    const int length = int(key >> 32);
    QByteArray out(length, '\0');
    for (int i = 0; i < std::min(length, 4); ++i)
        out[i] = char((key >> (24 - 8 * i)) & 0xff);
    return out;
}

void sortTallies(std::vector<ClientTally> &tallies)
{
    std::sort(tallies.begin(), tallies.end(), [](const ClientTally &a, const ClientTally &b) {
        if (a.count != b.count)
            return a.count > b.count;
        if (a.name != b.name)
            return a.name < b.name;
        return a.version < b.version;
    });
}

NetworkStats summarise(const Accumulator &a, const QHash<quint64, ClientInfo> &decoded, int topPorts)
{
    NetworkStats s;
    s.heardIps = a.heardIps;
    s.connectedIps = a.connectedIps;
    s.answeringIps = a.answeringIps;
    s.unroutableIps = a.unroutableIps;
    s.multiNodeIps = a.multiNodeIps;
    s.maxNodesPerIp = a.maxNodesPerIp;
    s.answeringNodes = a.answeringNodes;
    s.bep42 = a.bep42;

    // Clients: by name, and by name and version.
    QHash<QString, double> byName;
    QHash<QString, double> byVersion;
    QHash<QString, ClientTally> versionInfo;
    QHash<QString, QString> nameKind;
    for (auto it = a.versions.cbegin(); it != a.versions.cend(); ++it) {
        const ClientInfo &info = decoded[it.key()];
        const bool known = info.kind == ClientInfo::Kind::Known;
        const QString name = known ? info.name : info.display();
        QString version;
        if (known) {
            version = !info.version.isEmpty()
                          ? info.version
                          : QStringLiteral("bytes %1").arg(QString::fromLatin1(info.raw.mid(2).toHex(' ')));
        }
        byName[name] += it.value();
        nameKind.insert(name, clientKindName(info.kind));
        const QString key = name + QChar(0) + version;
        byVersion[key] += it.value();
        versionInfo.insert(key, ClientTally{name, version, clientKindName(info.kind), 0});
    }
    for (auto it = byName.cbegin(); it != byName.cend(); ++it)
        s.clients.push_back(ClientTally{it.key(), QString(), nameKind.value(it.key()), it.value()});
    for (auto it = byVersion.cbegin(); it != byVersion.cend(); ++it) {
        ClientTally tally = versionInfo.value(it.key());
        tally.count = it.value();
        s.versions.push_back(tally);
    }
    sortTallies(s.clients);
    sortTallies(s.versions);

    // Round trips.
    s.rttHistogram = a.rtt;
    s.rttWeight = a.rttWeight;
    if (a.rttWeight > 0) {
        s.rttMeanMs = a.rttSumMs / a.rttWeight;
        s.rttMedianMs = histogramQuantile(a.rtt, a.rttWeight, 0.5);
        s.rttP90Ms = histogramQuantile(a.rtt, a.rttWeight, 0.9);
        s.rttP99Ms = histogramQuantile(a.rtt, a.rttWeight, 0.99);
    }

    // Ports.
    std::vector<std::pair<quint16, double>> ports;
    for (size_t p = 0; p < a.ports.size(); ++p) {
        if (a.ports[p] > 0)
            ports.emplace_back(quint16(p), a.ports[p]);
    }
    s.distinctPorts = int(ports.size());
    s.defaultPortCount = a.ports[DefaultPort];
    const auto byCount = [](const auto &x, const auto &y) {
        return x.second != y.second ? x.second > y.second : x.first < y.first;
    };
    const size_t keep = std::min(ports.size(), size_t(std::max(0, topPorts)));
    std::partial_sort(ports.begin(), ports.begin() + keep, ports.end(), byCount);
    ports.resize(keep);
    s.topPorts = std::move(ports);
    return s;
}

} // namespace

int histogramQuantile(const std::vector<double> &histogram, double total, double quantile)
{
    if (!(total > 0))
        return -1;
    // A little slack, so that fractional weights summing to a whole number
    // are not missed by rounding.
    const double wanted = std::max(quantile * total, 1e-9) - 1e-9;
    double seen = 0;
    for (size_t i = 0; i < histogram.size(); ++i) {
        seen += histogram[i];
        if (histogram[i] > 0 && seen >= wanted)
            return int(i);
    }
    return int(histogram.size()) - 1;
}

namespace {

using AddressKey = std::array<quint8, 16>;

} // namespace

NetworkStatsSet computeNetworkStats(const NodeCatalog &catalog, qint64 nowMs, int topPorts)
{
    QElapsedTimer timer;
    timer.start();

    // Group entries by address: sort (address, slot) pairs, then walk the runs.
    std::vector<std::pair<AddressKey, NodeCatalog::Slot>> entries;
    entries.reserve(size_t(catalog.size()));
    catalog.forEach([&](NodeCatalog::Slot slot, const CatalogEntry &e) { entries.emplace_back(e.address, slot); });
    std::sort(entries.begin(), entries.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    std::array<Accumulator, 2> families;
    for (size_t begin = 0; begin < entries.size();) {
        size_t end = begin + 1;
        while (end < entries.size() && entries[end].first == entries[begin].first)
            ++end;

        const CatalogEntry &first = catalog.at(entries[begin].second);
        Accumulator &a = families[first.isIPv4() ? 0 : 1];
        if (first.state == CatalogEntry::State::Unroutable) {
            // Unroutability is a property of the address, so the whole run is.
            ++a.unroutableIps;
            begin = end;
            continue;
        }

        ++a.heardIps;
        bool connected = false;
        int answering = 0;
        for (size_t i = begin; i < end; ++i) {
            const CatalogEntry &e = catalog.at(entries[i].second);
            connected = connected || e.lastAnswered > 0;
            answering += e.state == CatalogEntry::State::Responsive ? 1 : 0;
        }
        if (connected)
            ++a.connectedIps;
        if (answering > 0) {
            ++a.answeringIps;
            a.answeringNodes += answering;
            a.multiNodeIps += answering > 1 ? 1 : 0;
            a.maxNodesPerIp = std::max(a.maxNodesPerIp, answering);

            const double weight = 1.0 / answering;
            for (size_t i = begin; i < end; ++i) {
                const CatalogEntry &e = catalog.at(entries[i].second);
                if (e.state != CatalogEntry::State::Responsive)
                    continue;
                a.versions[versionKey(e)] += weight;
                a.bep42[std::min<int>(e.bep42, int(a.bep42.size()) - 1)] += weight;
                if (e.rttMs != CatalogEntry::NoRtt) {
                    a.rtt[std::min<int>(e.rttMs, NetworkStats::MaxRttMs)] += weight;
                    a.rttWeight += weight;
                    a.rttSumMs += weight * e.rttMs;
                }
                a.ports[e.port] += weight;
            }
        }
        begin = end;
    }

    Accumulator all;
    all.add(families[0]);
    all.add(families[1]);

    // Distinct version fields are few, so each is decoded once.
    QHash<quint64, ClientInfo> decoded;
    for (auto it = all.versions.cbegin(); it != all.versions.cend(); ++it)
        decoded.insert(it.key(), decodeClientVersion(versionBytes(it.key())));

    NetworkStatsSet set;
    set.ipv4 = summarise(families[0], decoded, topPorts);
    set.ipv6 = summarise(families[1], decoded, topPorts);
    set.all = summarise(all, decoded, topPorts);
    set.computedAtMs = nowMs;
    set.computeMs = int(timer.elapsed());
    return set;
}

double estimateNetworkSize(const NodeId &target, const std::vector<NodeId> &closest)
{
    if (closest.size() < 4)
        return 0;

    std::vector<double> distances;
    distances.reserve(closest.size());
    for (const NodeId &id : closest) {
        const NodeId x = id ^ target;
        quint64 top = 0;
        for (int i = 0; i < 8; ++i)
            top = (top << 8) | x[i];
        distances.push_back(std::ldexp(double(top), -64));
    }
    std::sort(distances.begin(), distances.end());

    double sumSquares = 0;
    double sumWeighted = 0;
    for (size_t i = 0; i < distances.size(); ++i) {
        const double rank = double(i + 1);
        sumSquares += rank * rank;
        sumWeighted += rank * distances[i];
    }
    if (!(sumWeighted > 0))
        return 0;

    // The fitted sum is a weighted sum of exponential gaps: gap j carries
    // the ranks j..k. Its distribution is skewed, so the median of many
    // estimates would read high; treating it as a gamma distribution with
    // the same mean and variance, whose median sits at about 1 - 1/(3a) of
    // its mean, corrects for that.
    const int k = int(distances.size());
    double variance = 0;
    for (int j = 1; j <= k; ++j) {
        const double weight = double(k * (k + 1) - (j - 1) * j) / 2.0;  // j + (j+1) + ... + k
        variance += weight * weight;
    }
    const double shape = sumSquares * sumSquares / variance;
    return sumSquares / sumWeighted * (1.0 - 1.0 / (3.0 * shape));
}

void SizeEstimator::add(double estimate)
{
    if (!(estimate > 0) || !std::isfinite(estimate))
        return;
    m_samples.push_back(estimate);
    while (int(m_samples.size()) > MaxSamples)
        m_samples.pop_front();
}

SizeEstimate SizeEstimator::summary() const
{
    SizeEstimate out;
    const int n = int(m_samples.size());
    out.samples = n;
    if (n == 0)
        return out;

    std::vector<double> sorted(m_samples.begin(), m_samples.end());
    std::sort(sorted.begin(), sorted.end());
    out.median = n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;

    // Ranks around the middle that bracket the true median 95% of the time.
    const double spread = 0.98 * std::sqrt(double(n));
    const int lowRank = std::clamp(int(std::floor(n / 2.0 - spread)), 1, n);
    const int highRank = std::clamp(int(std::ceil(n / 2.0 + spread)) + 1, 1, n);
    out.low = sorted[lowRank - 1];
    out.high = sorted[highRank - 1];
    return out;
}

} // namespace dht
