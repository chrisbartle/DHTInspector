#pragma once

#include "dhtcore/Bep42.h"
#include "dhtcore/ClientVersion.h"
#include "dhtcore/Endpoint.h"
#include "dhtcore/NodeCatalog.h"
#include "dhtcore/NodeId.h"

#include <QHash>
#include <QHostAddress>
#include <QMetaType>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace dht {

// Which nodes of the catalogue to show, in what order, and which page.
struct NodeQuery
{
    enum class Sort { Address, RoundTrip, LastAnswered, FirstSeen, Client, NodesAtAddress };

    static constexpr quint32 AllStates = 0x1f;  // one bit per CatalogEntry::State

    std::optional<Family> family;
    quint32 states = AllStates;
    // Client filters apply to nodes that have answered, since only they
    // have told us what they run. Matched against clientLabel() and
    // versionLabel(); empty means any.
    QString client;
    QString version;
    std::optional<bep42::Status> bep42;
    int minRttMs = -1;  // -1: no bound
    int maxRttMs = -1;
    // An address or subnet; a null address means any.
    QHostAddress subnet;
    int subnetBits = -1;
    quint16 port = 0;          // 0: any
    int minNodesAtAddress = 0; // nodes the catalogue holds at the same address

    Sort sort = Sort::Address;
    bool descending = false;
    int offset = 0;
    int limit = 100;
};

// One node as the list shows it.
struct NodeListRow
{
    Endpoint endpoint;
    NodeId id;
    CatalogEntry::State state = CatalogEntry::State::New;
    bool answered = false;  // at some point
    QString client;         // empty when it never answered
    QString version;
    QString clientKind;
    QByteArray rawVersion;
    int rttMs = -1;
    bep42::Status bep42 = bep42::Status::Unknown;
    int nodesAtAddress = 1;
    int failures = 0;
    int sightings = 0;
    qint64 firstSeenAgoMs = -1;
    qint64 lastAnsweredAgoMs = -1;  // -1: never
    qint64 lastQueriedAgoMs = -1;
};

struct NodeListPage
{
    quint64 requestId = 0;
    int matchedNodes = 0;
    int matchedAddresses = 0;
    int offset = 0;
    std::vector<NodeListRow> rows;
    int queryMs = 0;
};

// Everything matching a query, for export. Entries are copied as they are
// kept, so the file can be written away from the engine thread.
struct NodeExport
{
    std::vector<CatalogEntry> entries;
    std::vector<int> nodesAtAddress;  // parallel to entries
    quint32 nowStamp = 0;
    int tickMs = NodeCatalog::TickMs;
    NodeQuery query;
};

// Decodes each distinct version field once and remembers how it reads.
class ClientLabelCache
{
public:
    struct Labels
    {
        ClientInfo info;
        QString client;   // clientLabel()
        QString version;  // versionLabel()
        QString kind;     // clientKindName()
    };

    const Labels &labels(quint64 versionKey);

private:
    QHash<quint64, Labels> m_labels;
};

// Whether `address` lies within `subnet`/`bits` (bits < 0: any).
bool addressInSubnet(const std::array<quint8, 16> &address, const QHostAddress &subnet, int bits);

// Parses "203.0.113.7", "203.0.113.0/24" or "2001:db8::/32". An empty text
// means any address. Returns false, with a reason, when it cannot be read.
bool parseAddressFilter(const QString &text, QHostAddress *address, int *bits, QString *error = nullptr);

NodeListPage queryNodes(const NodeCatalog &catalog, const NodeQuery &query, qint64 nowMs, ClientLabelCache &labels);
NodeExport collectNodes(const NodeCatalog &catalog, const NodeQuery &query, qint64 nowMs, ClientLabelCache &labels);

// One row per node, with a header. Returns false, with a reason, on failure.
bool writeNodesCsv(const NodeExport &nodes, const QString &path, QString *error);

QString stateName(CatalogEntry::State state);

} // namespace dht

Q_DECLARE_METATYPE(dht::NodeQuery)
Q_DECLARE_METATYPE(dht::NodeListPage)
Q_DECLARE_METATYPE(std::shared_ptr<dht::NodeExport>)
