#pragma once

#include "dhtcore/Bep42.h"
#include "dhtcore/Endpoint.h"
#include "dhtcore/NetworkStats.h"
#include "dhtcore/NodeId.h"

#include <QHostAddress>
#include <QMetaType>
#include <QString>

#include <memory>
#include <vector>

namespace dht {

// Plain value types describing engine state. Produced on the engine thread
// and copied to whoever displays them.

struct NodeRow
{
    enum class Source { Routing, Injected, Bootstrap };
    enum class Status { Good, Questionable, Bad, Querying, Responded, NoResponse };

    Family family = Family::IPv4;
    Endpoint endpoint;
    NodeId id;
    bool hasId = false;
    Source source = Source::Routing;
    Status status = Status::Querying;
    int rttMs = -1;
    qint64 lastSeenAgoMs = -1;
    bep42::Status bep42 = bep42::Status::Unknown;
    QByteArray version;
    int bucket = -1;
    // Stable ordering key: family, then XOR distance from our own ID, then
    // endpoint. Lets a view merge successive snapshots without resetting.
    QByteArray sortKey;
};

struct FamilySnapshot
{
    bool enabled = false;
    bool bound = false;
    quint16 port = 0;
    QString error;
    NodeId id;
    QHostAddress externalAddress;
    bep42::Status bep42 = bep42::Status::Unknown;
    int nodeCount = 0;
    int bucketCount = 0;
};

struct PortMappingSnapshot
{
    enum class State { Disabled, Discovering, Mapped, Failed };

    State state = State::Disabled;
    QString protocol;
    QHostAddress gateway;
    QHostAddress externalAddress;
    quint16 internalPort = 0;
    quint16 externalPort = 0;
    quint32 lifetimeSeconds = 0;
    QString message;
};

struct EngineStats
{
    qint64 packetsIn = 0;
    qint64 packetsOut = 0;
    qint64 bytesIn = 0;
    qint64 bytesOut = 0;
    qint64 queriesIn = 0;
    qint64 queriesOut = 0;
    qint64 responsesIn = 0;
    qint64 errorsIn = 0;
    qint64 timeouts = 0;
    qint64 malformedIn = 0;
    qint64 rateLimited = 0;
    qint64 readOnlyDropped = 0;  // queries ignored because BEP 43 read-only is on
    qint64 queriesDelayed = 0;   // our queries that waited for a host's allowance
    qint64 queriesRefused = 0;   // our queries never sent: too many already waiting
    int queriesWaiting = 0;      // waiting right now
    qint64 repliesShed = 0;      // queries left unanswered because of the send limit
    qint64 sendFailures = 0;     // datagrams the OS would not take (buffer full)
    int storedInfohashes = 0;
    int storedPeers = 0;
    int activeLookups = 0;

    void add(const EngineStats &o)
    {
        packetsIn += o.packetsIn;
        packetsOut += o.packetsOut;
        bytesIn += o.bytesIn;
        bytesOut += o.bytesOut;
        queriesIn += o.queriesIn;
        queriesOut += o.queriesOut;
        responsesIn += o.responsesIn;
        errorsIn += o.errorsIn;
        timeouts += o.timeouts;
        malformedIn += o.malformedIn;
        rateLimited += o.rateLimited;
        readOnlyDropped += o.readOnlyDropped;
        queriesDelayed += o.queriesDelayed;
        queriesRefused += o.queriesRefused;
        queriesWaiting += o.queriesWaiting;
        repliesShed += o.repliesShed;
        sendFailures += o.sendFailures;
        activeLookups += o.activeLookups;
    }
};

// The network scan: where it is and what the catalogue holds.
struct CrawlSnapshot
{
    enum class Phase {
        Off,
        WaitingForNodes,  // monitoring, but nothing to start from yet
        Discovering,      // asking nodes not asked before
        Rechecking,       // everything known has been asked; going round again
        UpToDate,         // nothing due right now
    };

    Phase phase = Phase::Off;
    int known = 0;
    int notAsked = 0;     // not answered yet, and not yet given up on
    int responsive = 0;
    int silent = 0;
    int gone = 0;
    int unroutable = 0;

    int cap = 0;
    qint64 evicted = 0;
    qint64 memoryBytes = 0;
    qint64 bytesPerEntry = 0;

    qint64 queries = 0;
    qint64 answers = 0;   // responses
    qint64 errors = 0;    // error replies, which still show the node is alive
    qint64 timeouts = 0;
    qint64 notSent = 0;   // held back by limits or refused by the OS
    int outstanding = 0;  // asked, not yet answered or timed out
    int waiting = 0;      // queued to be asked
    int batch = 0;        // queries per tick the crawler currently allows itself
    qint64 monitoredMs = 0;

    // Recomputed every few seconds while scanning; shared, not copied.
    std::shared_ptr<const NetworkStatsSet> stats;
    SizeEstimate sizeV4;
    SizeEstimate sizeV6;
};

// --- precise count (Census) -------------------------------------------------

// One slice, counted.
struct CensusSlice
{
    Family family = Family::IPv4;
    int bits = 0;
    NodeId prefix;
    int nodesHeard = 0;      // address-and-port entries listed inside the slice
    int nodesAnswered = 0;   // of those, answering with an ID inside the slice
    int ipsHeard = 0;
    int ipsAnswered = 0;
    double heardEstimate = 0;      // addresses in the whole network, scaled from this slice
    double connectedEstimate = 0;
    int rounds = 0;
    qint64 queries = 0;
    qint64 durationMs = 0;
};

struct CensusTotal
{
    int slices = 0;
    double heard = 0;       // mean over slices
    double heardLow = 0;    // 95% confidence interval
    double heardHigh = 0;
    double connected = 0;
    double connectedLow = 0;
    double connectedHigh = 0;
};

struct CensusSnapshot
{
    enum class State { Idle, Running, Done, Cancelled };

    State state = State::Idle;
    int slicesTotal = 0;
    int slicesDone = 0;
    // The slice in progress.
    Family family = Family::IPv4;
    int bits = 0;
    int round = 0;
    int nodesFound = 0;
    int nodesAnswered = 0;
    int outstanding = 0;
    qint64 queries = 0;
    qint64 elapsedMs = 0;

    std::vector<CensusSlice> slices;  // finished ones
    CensusTotal ipv4;
    CensusTotal ipv6;
};

struct EngineSnapshot
{
    bool running = false;
    FamilySnapshot ipv4;
    FamilySnapshot ipv6;
    PortMappingSnapshot portMapping;
    EngineStats stats;
    std::vector<NodeRow> nodes;
    CrawlSnapshot crawl;
    CensusSnapshot census;
};

// --- data store -------------------------------------------------------------

struct StoredPeerRow
{
    Endpoint endpoint;
    qint64 ageMs = 0;        // since this peer last announced
    qint64 expiresInMs = 0;  // until it is dropped
};

struct StoredInfohashRow
{
    NodeId infohash;
    int peerCount = 0;
    qint64 lastAnnounceAgoMs = 0;
    qint64 expiresInMs = 0;  // when the longest-lived peer expires
    std::vector<StoredPeerRow> peers;
};

// A BEP 44 item: immutable (a value keyed by its own hash) or mutable (signed
// by a public key, with a sequence number).
struct StoredItemRow
{
    bool isMutable = false;
    NodeId target;
    QByteArray value;      // bencoded, as it arrived
    QByteArray publicKey;  // mutable only
    QByteArray salt;       // mutable only
    qint64 sequence = 0;   // mutable only
    qint64 ageMs = 0;
    qint64 expiresInMs = 0;
};

// Everything this node holds for other people. Built on demand: it can be far
// larger than the routine engine snapshot.
struct StorageSnapshot
{
    bool running = false;
    qint64 ttlMs = 0;
    int maxInfohashes = 0;
    int maxPeersPerInfohash = 0;
    int infohashCount = 0;
    int peerCount = 0;
    bool truncated = false;  // more infohashes than the listing limit
    std::vector<StoredInfohashRow> infohashes;  // most recent announce first

    qint64 itemTtlMs = 0;
    int maxItems = 0;
    int immutableCount = 0;
    int mutableCount = 0;
    std::vector<StoredItemRow> items;  // most recently stored first
};

// --- search and publish results ---------------------------------------------

// Which node handed back which peer. A peer list is only a claim by the node
// that returned it, so the source is part of the finding.
struct PeerSighting
{
    Endpoint peer;
    Endpoint source;
};

struct PeerSearchResult
{
    NodeId infohash;
    std::vector<Endpoint> peers;  // unique
    std::vector<PeerSighting> sightings;
    int queried = 0;
    int responded = 0;
};

struct ItemSearchResult
{
    NodeId target;
    bool found = false;
    bool isMutable = false;
    QByteArray value;  // bencoded, as stored in the DHT
    QByteArray publicKey;
    QByteArray salt;
    QByteArray signature;
    qint64 sequence = -1;
    int queried = 0;
    int responded = 0;
};

// One question put to one node, and everything that came back.
struct ProbeResult
{
    Endpoint endpoint;
    QByteArray method;
    QByteArray request;   // datagram sent
    QByteArray response;  // datagram received, empty when nothing came back
    bool timedOut = false;
    bool isError = false;  // the node answered with an error
    qint64 errorCode = 0;
    QString errorMessage;  // the node's error text, or why we could not ask
    int rttMs = -1;
    QString decoded;  // the whole response, decoded
    QString summary;  // one line
    QByteArray token;  // carried over for announce and put
    QByteArray version;  // the node's "v" field, empty if it sent none
};

// Announce and put both write to the nodes closest to a target.
struct PublishResult
{
    enum class Kind { Announce, Immutable, Mutable };

    Kind kind = Kind::Announce;
    NodeId target;
    int accepted = 0;   // nodes that stored it
    int attempted = 0;  // nodes we asked
    qint64 sequence = -1;
    QString error;
};

} // namespace dht

Q_DECLARE_METATYPE(dht::EngineSnapshot)
Q_DECLARE_METATYPE(dht::PeerSearchResult)
Q_DECLARE_METATYPE(dht::ItemSearchResult)
Q_DECLARE_METATYPE(dht::PublishResult)
Q_DECLARE_METATYPE(dht::ProbeResult)
Q_DECLARE_METATYPE(dht::StorageSnapshot)
