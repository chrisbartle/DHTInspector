#pragma once

#include "dhtcore/DhtNode.h"
#include "dhtcore/PortMapper.h"
#include "dhtcore/Snapshot.h"
#include "dhtcore/Support.h"

#include <QObject>
#include <QTimer>

#include <functional>

namespace dht {

struct EngineConfig
{
    quint16 port = 6881;
    bool enableIpv6 = false;
    bool portForwarding = false;
    // Initial node IDs, random when unset. BEP 42 may replace them later.
    std::optional<NodeId> nodeIdV4;
    std::optional<NodeId> nodeIdV6;
    // BEP 42: adopt node IDs derived from the agreed external address, and
    // tell every node in our replies which address we see it from.
    bool bep42 = true;
    bool allowLocalAddresses = false;  // for LAN and loopback testing
    QHostAddress bindAddressV4;        // null: any
    QHostAddress bindAddressV6;        // null: any
    int snapshotIntervalMs = 1000;
};

struct BootstrapRouter
{
    QString host;
    quint16 port;
};

const QList<BootstrapRouter> &defaultBootstrapRouters();

// Our KRPC "v" value: client code "DI" plus a two-byte version.
QByteArray clientVersion();

// The whole engine: one DhtNode per enabled family, shared peer storage and
// port mapping. Lives on a single thread; stopping it means destroying it.
class DhtEngine : public QObject
{
    Q_OBJECT

public:
    explicit DhtEngine(QObject *parent = nullptr);
    ~DhtEngine() override;

    // Fails only if the IPv4 socket cannot be bound. An IPv6 failure is
    // reported through notice() and the snapshot, and IPv4 keeps running.
    bool start(const EngineConfig &config, QString *error = nullptr);
    void shutdown();
    bool isRunning() const { return m_running; }

    // Contacts a user-supplied node; hostnames are resolved first.
    void addNode(const QString &host, quint16 port);
    // Contacts the well-known bootstrap routers.
    void bootstrap();

    void setPortForwarding(bool enabled);

    void getPeers(const NodeId &infohash, std::function<void(const std::vector<Endpoint> &peers)> done);
    void announce(const NodeId &infohash, quint16 port, bool impliedPort, std::function<void(int accepted)> done);

    DhtNode *node(Family family) const { return family == Family::IPv4 ? m_v4 : m_v6; }
    const PeerStorage &storage() const { return m_storage; }
    const ItemStorage &items() const { return m_items; }
    PortMapper *portMapper() const { return m_mapper; }

    EngineSnapshot snapshot() const;

    // Contents of the data store. Listing is capped; the snapshot says so.
    static constexpr int MaxListedInfohashes = 1000;
    StorageSnapshot storageSnapshot() const;
    void requestStorageSnapshot();

signals:
    void snapshotReady(const dht::EngineSnapshot &snapshot);
    void storageSnapshotReady(const dht::StorageSnapshot &snapshot);
    void notice(const QString &message, bool isError);

private:
    bool addSeed(const Endpoint &endpoint, DhtNode::SeedSource source);
    void scheduleSnapshot();
    void publishSnapshot();

    EngineConfig m_config;
    PeerStorage m_storage;
    ItemStorage m_items;
    DhtNode *m_v4 = nullptr;
    DhtNode *m_v6 = nullptr;
    QString m_v6Error;
    PortMapper *m_mapper = nullptr;
    QTimer m_snapshotTimer;
    QTimer m_coalesceTimer;
    QTimer m_expiryTimer;
    bool m_running = false;
};

} // namespace dht
