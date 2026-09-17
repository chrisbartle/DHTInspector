#pragma once

#include "dhtcore/ClientVersion.h"
#include "dhtcore/Endpoint.h"
#include "dhtcore/Krpc.h"

#include <QHash>
#include <QString>

#include <array>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dht {

// What the nodes that query us look like, counted by address. Read-only
// nodes (BEP 43) never answer and are never listed by others, so queries
// sent to us are the only place they show up.
struct InboundSummary
{
    qint64 queries = 0;
    int addresses = 0;           // distinct addresses that queried us (tracked)
    int ipv4Addresses = 0;
    int ipv6Addresses = 0;
    int readOnlyAddresses = 0;   // sent at least one query flagged "ro"
    qint64 untrackedQueries = 0; // from addresses beyond the tracking cap
    std::vector<ClientTally> clients;               // by address, latest "v" seen
    std::vector<std::pair<QString, qint64>> methods; // queries per method, most first
};

class InboundTally
{
public:
    static constexpr int MaxAddresses = 500'000;

    void record(const Endpoint &from, const krpc::Message &query);
    void clear();
    InboundSummary summary() const;
    int trackedAddresses() const { return int(m_queriers.size()); }
    qint64 queries() const { return m_queries; }

private:
    using AddressKey = std::array<quint8, 16>;
    struct KeyHash
    {
        size_t operator()(const AddressKey &key) const noexcept;
    };
    struct Querier
    {
        quint64 versionKey = 0;
        bool readOnly = false;
        bool ipv4 = true;
    };

    std::unordered_map<AddressKey, Querier, KeyHash> m_queriers;
    QHash<QString, qint64> m_methods;
    qint64 m_queries = 0;
    qint64 m_untrackedQueries = 0;
};

} // namespace dht
