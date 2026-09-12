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
    int storedInfohashes = 0;
    int storedPeers = 0;
    int activeLookups = 0;
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
