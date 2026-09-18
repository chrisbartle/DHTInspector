#pragma once

#include "dhtcore/Bep42.h"
#include "dhtcore/ClientVersion.h"
#include "dhtcore/Endpoint.h"
#include "dhtcore/NodeCatalog.h"
#include "dhtcore/NodeId.h"
#include "dhtcore/Sybil.h"

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
    // Node IDs starting with these bits (bits < 0: any).
    NodeId idPrefix;
    int idPrefixBits = -1;
    // CatalogEntry::Flag bits that must all be set, and that must all be clear.
    quint32 flagsSet = 0;
    quint32 flagsClear = 0;
    // Suspicious-group filter: SybilSignal bits or SeveralSignals, 0 for any.
    quint8 suspicion = 0;

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
    quint32 flags = 0;              // CatalogEntry::Flag bits
    int selfListSharePercent = -1;  // -1: not known
    quint32 bep51Samples = 0;       // "num" from sample_infohashes
    quint8 suspicion = 0;           // SybilSignal bits for its address
    AddressProblem problem = AddressProblem::None;  // why it is unreachable
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
    std::vector<quint8> suspicion;    // parallel to entries
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

// Parses a node ID prefix given in hex, optionally with a bit count:
// "a1b2", "a1b2/13". Without a count, the prefix is 4 bits per digit.
bool parseIdPrefixFilter(const QString &text, NodeId *prefix, int *bits, QString *error = nullptr);

// Whether `address` lies within `subnet`/`bits` (bits < 0: any).
bool addressInSubnet(const std::array<quint8, 16> &address, const QHostAddress &subnet, int bits);

// Parses "203.0.113.7", "203.0.113.0/24" or "2001:db8::/32". An empty text
// means any address. Returns false, with a reason, when it cannot be read.
bool parseAddressFilter(const QString &text, QHostAddress *address, int *bits, QString *error = nullptr);

// `suspicion` is the latest suspicious-address map; without it, signal
// filters match only on what a node records itself.
NodeListPage queryNodes(const NodeCatalog &catalog, const NodeQuery &query, qint64 nowMs, ClientLabelCache &labels,
                        const AddressSignals *suspicion = nullptr);
NodeExport collectNodes(const NodeCatalog &catalog, const NodeQuery &query, qint64 nowMs, ClientLabelCache &labels,
                        const AddressSignals *suspicion = nullptr);

// Short text for a feature check: "yes", "no", or empty when not checked.
QString featureAnswer(quint32 flags, CatalogEntry::Flag tested, CatalogEntry::Flag has);
// The unknown-query check: "204", "other error", "reply", "no answer", or empty.
QString unknownQueryAnswer(quint32 flags);

// Whether the node has answered with peers for an infohash invented here:
// empty until it has been asked or caught, since a lookup can catch one
// before its own check runs.
QString inventsPeersAnswer(quint32 flags);
// Signal bits as text, e.g. "many nodes, dense subnet".
QString signalNames(quint8 suspicion);

// One row per node, with a header. Returns false, with a reason, on failure.
bool writeNodesCsv(const NodeExport &nodes, const QString &path, QString *error);

QString stateName(CatalogEntry::State state);

} // namespace dht

Q_DECLARE_METATYPE(dht::NodeQuery)
Q_DECLARE_METATYPE(dht::NodeListPage)
Q_DECLARE_METATYPE(std::shared_ptr<dht::NodeExport>)
