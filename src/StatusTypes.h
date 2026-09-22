#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Read-only value types the controller hands to QML.

class FamilyStatus
{
    Q_GADGET
    QML_VALUE_TYPE(familyStatus)
    Q_PROPERTY(bool enabled MEMBER enabled)
    Q_PROPERTY(bool bound MEMBER bound)
    Q_PROPERTY(int port MEMBER port)
    Q_PROPERTY(QString nodeId MEMBER nodeId)
    Q_PROPERTY(QString externalAddress MEMBER externalAddress)
    Q_PROPERTY(QString bep42 MEMBER bep42)
    Q_PROPERTY(QString error MEMBER error)
    Q_PROPERTY(int nodeCount MEMBER nodeCount)
    Q_PROPERTY(int bucketCount MEMBER bucketCount)

public:
    bool enabled = false;
    bool bound = false;
    int port = 0;
    QString nodeId;
    QString externalAddress;
    QString bep42 = QStringLiteral("unknown");
    QString error;
    int nodeCount = 0;
    int bucketCount = 0;

    friend bool operator==(const FamilyStatus &, const FamilyStatus &) = default;
};

class PortMappingStatus
{
    Q_GADGET
    QML_VALUE_TYPE(portMappingStatus)
    Q_PROPERTY(QString state MEMBER state)
    Q_PROPERTY(QString protocol MEMBER protocol)
    Q_PROPERTY(QString gateway MEMBER gateway)
    Q_PROPERTY(QString externalAddress MEMBER externalAddress)
    Q_PROPERTY(int externalPort MEMBER externalPort)
    Q_PROPERTY(QString message MEMBER message)

public:
    QString state = QStringLiteral("disabled");
    QString protocol;
    QString gateway;
    QString externalAddress;
    int externalPort = 0;
    QString message;

    friend bool operator==(const PortMappingStatus &, const PortMappingStatus &) = default;
};

class EngineStatistics
{
    Q_GADGET
    QML_VALUE_TYPE(engineStatistics)
    Q_PROPERTY(qint64 packetsIn MEMBER packetsIn)
    Q_PROPERTY(qint64 packetsOut MEMBER packetsOut)
    Q_PROPERTY(qint64 bytesIn MEMBER bytesIn)
    Q_PROPERTY(qint64 bytesOut MEMBER bytesOut)
    Q_PROPERTY(qint64 queriesIn MEMBER queriesIn)
    Q_PROPERTY(qint64 queriesOut MEMBER queriesOut)
    Q_PROPERTY(qint64 responsesIn MEMBER responsesIn)
    Q_PROPERTY(qint64 errorsIn MEMBER errorsIn)
    Q_PROPERTY(qint64 timeouts MEMBER timeouts)
    Q_PROPERTY(qint64 malformedIn MEMBER malformedIn)
    Q_PROPERTY(qint64 rateLimited MEMBER rateLimited)
    Q_PROPERTY(qint64 readOnlyDropped MEMBER readOnlyDropped)
    Q_PROPERTY(qint64 queriesDelayed MEMBER queriesDelayed)
    Q_PROPERTY(qint64 queriesRefused MEMBER queriesRefused)
    Q_PROPERTY(int queriesWaiting MEMBER queriesWaiting)
    Q_PROPERTY(qint64 newContacts MEMBER newContacts)
    Q_PROPERTY(int trackedContacts MEMBER trackedContacts)
    Q_PROPERTY(double newContactsPerSecond MEMBER newContactsPerSecond)
    Q_PROPERTY(qint64 sendFailures MEMBER sendFailures)
    Q_PROPERTY(double bytesInPerSecond MEMBER bytesInPerSecond)
    Q_PROPERTY(double bytesOutPerSecond MEMBER bytesOutPerSecond)
    Q_PROPERTY(int storedInfohashes MEMBER storedInfohashes)
    Q_PROPERTY(int storedPeers MEMBER storedPeers)
    Q_PROPERTY(int activeLookups MEMBER activeLookups)

public:
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
    qint64 readOnlyDropped = 0;
    qint64 queriesDelayed = 0;
    qint64 queriesRefused = 0;
    int queriesWaiting = 0;
    qint64 newContacts = 0;
    int trackedContacts = 0;
    double newContactsPerSecond = 0;
    qint64 sendFailures = 0;
    // Averaged over the last few seconds of snapshots.
    double bytesInPerSecond = 0;
    double bytesOutPerSecond = 0;
    int storedInfohashes = 0;
    int storedPeers = 0;
    int activeLookups = 0;
};

// The network scan, for the Global Health tab.
class CrawlStatus
{
    Q_GADGET
    QML_VALUE_TYPE(crawlStatus)
    Q_PROPERTY(QString phase MEMBER phase)
    Q_PROPERTY(int known MEMBER known)
    Q_PROPERTY(int notAsked MEMBER notAsked)
    Q_PROPERTY(int responsive MEMBER responsive)
    Q_PROPERTY(int silent MEMBER silent)
    Q_PROPERTY(int gone MEMBER gone)
    Q_PROPERTY(int unroutable MEMBER unroutable)
    Q_PROPERTY(int cap MEMBER cap)
    Q_PROPERTY(qint64 evicted MEMBER evicted)
    Q_PROPERTY(double memoryBytes MEMBER memoryBytes)
    Q_PROPERTY(double bytesPerEntry MEMBER bytesPerEntry)
    Q_PROPERTY(qint64 queries MEMBER queries)
    Q_PROPERTY(qint64 answers MEMBER answers)
    Q_PROPERTY(qint64 errors MEMBER errors)
    Q_PROPERTY(qint64 timeouts MEMBER timeouts)
    Q_PROPERTY(qint64 notSent MEMBER notSent)
    Q_PROPERTY(int outstanding MEMBER outstanding)
    Q_PROPERTY(int waiting MEMBER waiting)
    Q_PROPERTY(int batch MEMBER batch)
    Q_PROPERTY(double monitoredSeconds MEMBER monitoredSeconds)
    Q_PROPERTY(double queriesPerSecond MEMBER queriesPerSecond)
    Q_PROPERTY(double answersPerSecond MEMBER answersPerSecond)
    Q_PROPERTY(qint64 featureQueries MEMBER featureQueries)
    Q_PROPERTY(int featureWaiting MEMBER featureWaiting)
    Q_PROPERTY(qint64 randomLookups MEMBER randomLookups)
    Q_PROPERTY(qint64 lookupsWithInventedPeers MEMBER lookupsWithInventedPeers)

public:
    // One of: off, waiting, discovering, rechecking, up to date.
    QString phase = QStringLiteral("off");
    int known = 0;
    int notAsked = 0;
    int responsive = 0;
    int silent = 0;
    int gone = 0;
    int unroutable = 0;
    int cap = 0;
    qint64 evicted = 0;
    double memoryBytes = 0;
    double bytesPerEntry = 0;
    qint64 queries = 0;
    qint64 answers = 0;
    qint64 errors = 0;
    qint64 timeouts = 0;
    qint64 notSent = 0;
    int outstanding = 0;
    int waiting = 0;
    int batch = 0;
    qint64 featureQueries = 0;
    int featureWaiting = 0;
    qint64 randomLookups = 0;
    qint64 lookupsWithInventedPeers = 0;
    double monitoredSeconds = 0;
    double queriesPerSecond = 0;
    double answersPerSecond = 0;
};

class ProbeStatus
{
    Q_GADGET
    QML_VALUE_TYPE(probeStatus)
    Q_PROPERTY(bool valid MEMBER valid)
    Q_PROPERTY(QString method MEMBER method)
    Q_PROPERTY(QString endpoint MEMBER endpoint)
    Q_PROPERTY(QString outcome MEMBER outcome)
    Q_PROPERTY(QString summary MEMBER summary)
    Q_PROPERTY(QString decoded MEMBER decoded)
    Q_PROPERTY(QString request MEMBER request)
    Q_PROPERTY(QString response MEMBER response)
    Q_PROPERTY(QString rtt MEMBER rtt)
    Q_PROPERTY(QString errorMessage MEMBER errorMessage)
    Q_PROPERTY(QString time MEMBER time)
    Q_PROPERTY(QString client MEMBER client)
    Q_PROPERTY(QString clientKind MEMBER clientKind)
    Q_PROPERTY(QString clientNote MEMBER clientNote)
    Q_PROPERTY(QString clientRaw MEMBER clientRaw)

public:
    bool valid = false;
    QString method;
    QString endpoint;
    QString outcome;
    QString summary;
    QString decoded;
    QString request;
    QString response;
    QString rtt;
    QString errorMessage;
    QString time;
    // From the node's "v" field, only when it replied.
    QString client;      // e.g. "libtorrent (Rasterbar) 2.0.11"
    QString clientKind;  // known, unknown, nonstandard or absent
    QString clientNote;
    QString clientRaw;   // the field as sent, escaped and in hex
};

class PeerSearchStatus
{
    Q_GADGET
    QML_VALUE_TYPE(peerSearchStatus)
    Q_PROPERTY(QString infohash MEMBER infohash)
    Q_PROPERTY(int queried MEMBER queried)
    Q_PROPERTY(int responded MEMBER responded)
    Q_PROPERTY(bool done MEMBER done)

public:
    QString infohash;
    int queried = 0;
    int responded = 0;
    bool done = false;
};

class ItemSearchStatus
{
    Q_GADGET
    QML_VALUE_TYPE(itemSearchStatus)
    Q_PROPERTY(bool done MEMBER done)
    Q_PROPERTY(bool found MEMBER found)
    Q_PROPERTY(bool isMutable MEMBER isMutable)
    Q_PROPERTY(QString target MEMBER target)
    Q_PROPERTY(QString value MEMBER value)
    Q_PROPERTY(QString rawValue MEMBER rawValue)
    Q_PROPERTY(QString publicKey MEMBER publicKey)
    Q_PROPERTY(QString salt MEMBER salt)
    Q_PROPERTY(QString signature MEMBER signature)
    Q_PROPERTY(QString sequence MEMBER sequence)
    Q_PROPERTY(int queried MEMBER queried)
    Q_PROPERTY(int responded MEMBER responded)

public:
    bool done = false;
    bool found = false;
    bool isMutable = false;
    QString target;
    QString value;
    QString rawValue;
    QString publicKey;
    QString salt;
    QString signature;
    QString sequence;
    int queried = 0;
    int responded = 0;
};

class PublishStatus
{
    Q_GADGET
    QML_VALUE_TYPE(publishStatus)
    Q_PROPERTY(QString kind MEMBER kind)
    Q_PROPERTY(QString kindId MEMBER kindId)  // announce, immutable, mutable
    Q_PROPERTY(QString target MEMBER target)
    Q_PROPERTY(int accepted MEMBER accepted)
    Q_PROPERTY(int attempted MEMBER attempted)
    Q_PROPERTY(QString error MEMBER error)
    Q_PROPERTY(bool done MEMBER done)

public:
    QString kind;
    QString kindId;
    QString target;
    int accepted = 0;
    int attempted = 0;
    QString error;
    bool done = false;
};

class DataStoreSummary
{
    Q_GADGET
    QML_VALUE_TYPE(dataStoreSummary)
    Q_PROPERTY(int infohashCount MEMBER infohashCount)
    Q_PROPERTY(int peerCount MEMBER peerCount)
    Q_PROPERTY(int listed MEMBER listed)
    Q_PROPERTY(int maxInfohashes MEMBER maxInfohashes)
    Q_PROPERTY(int maxPeersPerInfohash MEMBER maxPeersPerInfohash)
    Q_PROPERTY(int ttlMinutes MEMBER ttlMinutes)
    Q_PROPERTY(bool truncated MEMBER truncated)
    Q_PROPERTY(int immutableCount MEMBER immutableCount)
    Q_PROPERTY(int mutableCount MEMBER mutableCount)
    Q_PROPERTY(int maxItems MEMBER maxItems)
    Q_PROPERTY(int itemTtlMinutes MEMBER itemTtlMinutes)

public:
    int infohashCount = 0;
    int peerCount = 0;
    int listed = 0;
    int maxInfohashes = 0;
    int maxPeersPerInfohash = 0;
    int ttlMinutes = 0;
    bool truncated = false;
    int immutableCount = 0;
    int mutableCount = 0;
    int maxItems = 0;
    int itemTtlMinutes = 0;
};
