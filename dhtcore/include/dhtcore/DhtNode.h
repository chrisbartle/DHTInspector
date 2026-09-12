#pragma once

#include "dhtcore/Lookup.h"
#include "dhtcore/RoutingTable.h"
#include "dhtcore/RpcManager.h"
#include "dhtcore/Snapshot.h"
#include "dhtcore/Support.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

class QUdpSocket;

namespace dht {

struct NodeConfig
{
    Family family = Family::IPv4;
    QHostAddress bindAddress;          // null: any address of the family
    quint16 port = 0;                  // 0: let the OS choose
    bool allowLocalAddresses = false;  // accept private/loopback endpoints from the network
    bool bep42 = true;                 // derived node IDs and "ip" in replies
    std::optional<NodeId> nodeId;      // random when unset
    QByteArray version;                // KRPC "v" field
};

// A DHT node on one address family. BEP 32 dual-stack operation runs one of
// these per family, each with its own socket, routing table and node ID.
class DhtNode : public QObject
{
    Q_OBJECT

public:
    enum class SeedSource { Injected, Bootstrap };

    DhtNode(const NodeConfig &config, PeerStorage *storage, ItemStorage *items, QObject *parent = nullptr);
    ~DhtNode() override;

    bool bind(QString *error);
    void close();

    Family family() const { return m_config.family; }
    quint16 port() const;
    const NodeId &id() const { return m_id; }
    QHostAddress externalAddress() const { return m_voter.consensus(); }

    // The node for the other family, used to answer BEP 32 "want" requests.
    void setSibling(DhtNode *sibling) { m_sibling = sibling; }

    // Contacts an endpoint whose ID we do not know yet and joins through it.
    // Bootstrap routers are used for joining but never enter the table.
    void addSeed(const Endpoint &endpoint, SeedSource source);

    void findNode(const NodeId &target, Lookup::DoneFn done);
    void getPeers(const NodeId &infohash, Lookup::DoneFn done);
    void announce(const NodeId &infohash, quint16 port, bool impliedPort,
                  std::function<void(int accepted)> done);

    std::vector<krpc::CompactNode> closestNodes(const NodeId &target, int count) const;

    FamilySnapshot familySnapshot() const;
    void appendNodeRows(std::vector<NodeRow> &rows, qint64 now) const;
    EngineStats stats() const;

    // Replaces the node ID and re-keys the routing table.
    void setId(const NodeId &id);

signals:
    void changed();

private:
    struct Seed
    {
        enum class State { Querying, Responded, NoResponse };

        SeedSource source = SeedSource::Injected;
        State state = State::Querying;
        qint64 lastAttempt = 0;
        int rttMs = -1;
        NodeId id;
        bool hasId = false;
        QByteArray version;
    };

    void onReadyRead();
    void onMaintenance();
    void handleDatagram(const QByteArray &data, const Endpoint &from);
    void handleQuery(const krpc::Message &message, const Endpoint &from, const QByteArray &datagram);
    void handlePut(const krpc::Message &message, const Endpoint &from, const QByteArray &datagram, qint64 now);
    void handleGet(const krpc::Message &message, const Endpoint &from, qint64 now);

    void sendDatagram(const QByteArray &data, const Endpoint &to);
    void sendQuery(const Endpoint &to, const QByteArray &method, BValue::Dict arguments,
                   RpcManager::Callback callback);
    void sendResponse(const krpc::Message &query, const Endpoint &to, BValue::Dict values);
    void sendError(const QByteArray &transactionId, const Endpoint &to, int code, const QByteArray &message);

    void onRpcReply(const RpcReply &reply);
    bool isRouter(const Endpoint &endpoint) const;
    void maybeVerify(const NodeId &id, const Endpoint &from, qint64 now);
    void adoptExternalAddress(const QHostAddress &address);
    BValue::Dict nodesFor(const NodeId &target, const BValue &arguments) const;

    Lookup *startLookup(Lookup::Kind kind, const NodeId &target, Lookup::DoneFn done,
                        const std::vector<krpc::CompactNode> &extraCandidates = {});

    NodeConfig m_config;
    PeerStorage *m_storage;
    ItemStorage *m_items;
    QUdpSocket *m_socket = nullptr;
    RpcManager *m_rpc = nullptr;
    NodeId m_id;
    RoutingTable m_table;
    TokenManager m_tokens;
    ExternalIpVoter m_voter;
    RateLimiter m_limiter{20.0, 40.0};
    QHash<Endpoint, Seed> m_seeds;
    QHash<Endpoint, qint64> m_recentVerifications;
    QPointer<Lookup> m_selfLookup;
    QPointer<Lookup> m_refreshLookup;
    QList<QPointer<Lookup>> m_lookups;
    QPointer<DhtNode> m_sibling;
    QTimer m_maintenance;
    EngineStats m_stats;
};

} // namespace dht
