#pragma once

#include "dhtcore/Bep42.h"
#include "dhtcore/Endpoint.h"
#include "dhtcore/NodeId.h"

#include <QHostAddress>
#include <QMetaType>
#include <QString>

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
        activeLookups += o.activeLookups;
    }
};

struct EngineSnapshot
{
    bool running = false;
    FamilySnapshot ipv4;
    FamilySnapshot ipv6;
    PortMappingSnapshot portMapping;
    EngineStats stats;
    std::vector<NodeRow> nodes;
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

struct PeerSearchResult
{
    NodeId infohash;
    std::vector<Endpoint> peers;
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
Q_DECLARE_METATYPE(dht::StorageSnapshot)
