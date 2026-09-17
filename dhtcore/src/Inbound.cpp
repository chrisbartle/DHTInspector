#include "dhtcore/Inbound.h"

#include "dhtcore/NetworkStats.h"
#include "dhtcore/NodeList.h"

#include <QHashFunctions>

#include <algorithm>
#include <cstring>

namespace dht {

namespace {

// Methods are counted by name only when they are ones we know, so a node
// sending made-up names cannot grow the table.
QString methodLabel(const QByteArray &method)
{
    static const QList<QByteArray> known = {"ping", "find_node", "get_peers", "announce_peer",
                                            "get", "put", "sample_infohashes"};
    return known.contains(method) ? QString::fromLatin1(method) : QStringLiteral("other");
}

} // namespace

size_t InboundTally::KeyHash::operator()(const AddressKey &key) const noexcept
{
    return qHashBits(key.data(), key.size(), 0);
}

void InboundTally::record(const Endpoint &from, const krpc::Message &query)
{
    ++m_queries;
    ++m_methods[methodLabel(query.method)];

    AddressKey key{};
    const bool v4 = from.address.protocol() == QAbstractSocket::IPv4Protocol;
    if (v4) {
        const quint32 a = from.address.toIPv4Address();
        key[10] = 0xff;
        key[11] = 0xff;
        key[12] = quint8(a >> 24);
        key[13] = quint8(a >> 16);
        key[14] = quint8(a >> 8);
        key[15] = quint8(a);
    } else {
        const Q_IPV6ADDR a = from.address.toIPv6Address();
        std::memcpy(key.data(), a.c, 16);
    }

    auto it = m_queriers.find(key);
    if (it == m_queriers.end()) {
        if (int(m_queriers.size()) >= MaxAddresses) {
            ++m_untrackedQueries;
            return;
        }
        it = m_queriers.emplace(key, Querier{}).first;
        it->second.ipv4 = v4;
    }
    Querier &q = it->second;
    q.readOnly = q.readOnly || query.readOnly;
    if (!query.version.isEmpty() || q.versionKey == 0)
        q.versionKey = CatalogEntry::versionKeyFor(query.version);
}

void InboundTally::clear()
{
    m_queriers.clear();
    m_methods.clear();
    m_queries = 0;
    m_untrackedQueries = 0;
}

InboundSummary InboundTally::summary() const
{
    InboundSummary s;
    s.queries = m_queries;
    s.untrackedQueries = m_untrackedQueries;
    s.addresses = int(m_queriers.size());

    ClientLabelCache labels;
    QHash<QString, ClientTally> clients;
    for (const auto &[key, q] : m_queriers) {
        (q.ipv4 ? s.ipv4Addresses : s.ipv6Addresses) += 1;
        s.readOnlyAddresses += q.readOnly ? 1 : 0;
        const ClientLabelCache::Labels &l = labels.labels(q.versionKey);
        ClientTally &t = clients[l.client];
        t.name = l.client;
        t.kind = l.kind;
        t.count += 1;
    }
    for (const ClientTally &t : std::as_const(clients))
        s.clients.push_back(t);
    std::sort(s.clients.begin(), s.clients.end(), [](const ClientTally &a, const ClientTally &b) {
        return a.count != b.count ? a.count > b.count : a.name < b.name;
    });

    for (auto it = m_methods.cbegin(); it != m_methods.cend(); ++it)
        s.methods.emplace_back(it.key(), it.value());
    std::sort(s.methods.begin(), s.methods.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    return s;
}

} // namespace dht
