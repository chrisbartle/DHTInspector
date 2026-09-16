#include "dhtcore/DhtEngine.h"

#include "dhtcore/Bep44.h"

#include <QHostInfo>
#include <QPointer>

#include <algorithm>
#include <memory>

namespace dht {

const QList<BootstrapRouter> &defaultBootstrapRouters()
{
    static const QList<BootstrapRouter> routers = {
        {QStringLiteral("router.bittorrent.com"), 6881},
        {QStringLiteral("router.utorrent.com"), 6881},
        {QStringLiteral("dht.transmissionbt.com"), 6881},
        {QStringLiteral("dht.libtorrent.org"), 25401},
    };
    return routers;
}

QByteArray clientVersion()
{
    return QByteArray("DI\x00\x01", 4);
}

// Member timers are parented to the engine so moveToThread() takes them
// along; the application runs the engine on a worker thread.
DhtEngine::DhtEngine(QObject *parent)
    : QObject(parent)
    , m_snapshotTimer(this)
    , m_coalesceTimer(this)
    , m_expiryTimer(this)
{
    m_coalesceTimer.setSingleShot(true);
    m_coalesceTimer.setInterval(150);
    connect(&m_coalesceTimer, &QTimer::timeout, this, &DhtEngine::publishSnapshot);
    connect(&m_snapshotTimer, &QTimer::timeout, this, &DhtEngine::publishSnapshot);
    connect(&m_expiryTimer, &QTimer::timeout, this, [this] {
        const qint64 now = nowMs();
        m_storage.expire(now);
        m_items.expire(now);
    });
}

DhtEngine::~DhtEngine()
{
    shutdown();
}

bool DhtEngine::start(const EngineConfig &config, QString *error)
{
    if (m_running)
        return true;
    m_config = config;

    NodeConfig v4;
    v4.family = Family::IPv4;
    v4.bindAddress = config.bindAddressV4;
    v4.port = config.port;
    v4.allowLocalAddresses = config.allowLocalAddresses;
    v4.bep42 = config.bep42;
    v4.readOnly = config.readOnly;
    v4.nodeId = config.nodeIdV4;
    v4.version = clientVersion();

    m_v4 = new DhtNode(v4, &m_storage, &m_items, this);
    if (!m_v4->bind(error)) {
        delete m_v4;
        m_v4 = nullptr;
        return false;
    }
    connect(m_v4, &DhtNode::changed, this, &DhtEngine::scheduleSnapshot);

    if (config.enableIpv6) {
        NodeConfig v6 = v4;
        v6.family = Family::IPv6;
        v6.bindAddress = config.bindAddressV6;
        v6.nodeId = config.nodeIdV6;
        v6.port = m_v4->port(); // same port number on both families
        m_v6 = new DhtNode(v6, &m_storage, &m_items, this);
        if (m_v6->bind(&m_v6Error)) {
            connect(m_v6, &DhtNode::changed, this, &DhtEngine::scheduleSnapshot);
            m_v4->setSibling(m_v6);
            m_v6->setSibling(m_v4);
        } else {
            delete m_v6;
            m_v6 = nullptr;
            emit notice(m_v6Error, true);
        }
    }

    m_mapper = new PortMapper(this);
    connect(m_mapper, &PortMapper::changed, this, &DhtEngine::scheduleSnapshot);
    if (config.portForwarding)
        m_mapper->start(m_v4->port());

    m_running = true;
    m_snapshotTimer.start(std::max(100, config.snapshotIntervalMs));
    m_expiryTimer.start(60 * 1000);
    publishSnapshot();
    return true;
}

void DhtEngine::shutdown()
{
    if (!m_running)
        return;
    m_running = false;

    m_snapshotTimer.stop();
    m_coalesceTimer.stop();
    m_expiryTimer.stop();

    if (m_mapper) {
        m_mapper->disconnect(this);
        m_mapper->stop(); // releases the gateway mapping while the socket still exists
        delete m_mapper;
        m_mapper = nullptr;
    }
    const auto destroy = [this](DhtNode *&node) {
        if (!node)
            return;
        node->disconnect(this);
        node->close();
        delete node;
        node = nullptr;
    };
    destroy(m_v6);
    destroy(m_v4);
    m_storage.clear();
    m_items.clear();
    m_v6Error.clear();
}

bool DhtEngine::addSeed(const Endpoint &endpoint, DhtNode::SeedSource source)
{
    DhtNode *target = node(endpoint.family());
    if (!target)
        return false;
    target->addSeed(endpoint, source);
    return true;
}

void DhtEngine::addNode(const QString &host, quint16 port)
{
    if (!m_running)
        return;

    QHostAddress literal;
    if (literal.setAddress(host)) {
        const Endpoint endpoint(literal, port);
        if (addSeed(endpoint, DhtNode::SeedSource::Injected))
            emit notice(QStringLiteral("Contacting %1").arg(endpoint.toString()), false);
        else
            emit notice(QStringLiteral("%1 is disabled, cannot contact %2")
                            .arg(familyName(endpoint.family()), endpoint.toString()),
                        true);
        return;
    }

    emit notice(QStringLiteral("Resolving %1").arg(host), false);
    QPointer<DhtEngine> self(this);
    QHostInfo::lookupHost(host, this, [self, host, port](const QHostInfo &info) {
        if (!self || !self->m_running)
            return;
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            emit self->notice(QStringLiteral("Could not resolve %1: %2").arg(host, info.errorString()), true);
            return;
        }
        int added = 0;
        for (const QHostAddress &address : info.addresses())
            added += self->addSeed(Endpoint(address, port), DhtNode::SeedSource::Injected) ? 1 : 0;
        if (added == 0)
            emit self->notice(QStringLiteral("%1 has no addresses in an enabled address family").arg(host), true);
        else
            emit self->notice(QStringLiteral("Contacting %1 (%2 address%3)").arg(host).arg(added).arg(added == 1 ? "" : "es"), false);
    });
}

void DhtEngine::bootstrap()
{
    if (!m_running)
        return;

    const auto &routers = defaultBootstrapRouters();
    emit notice(QStringLiteral("Resolving %1 bootstrap routers").arg(routers.size()), false);

    QPointer<DhtEngine> self(this);
    for (const BootstrapRouter &router : routers) {
        QHostInfo::lookupHost(router.host, this, [self, router](const QHostInfo &info) {
            if (!self || !self->m_running)
                return;
            int added = 0;
            if (info.error() == QHostInfo::NoError) {
                for (const QHostAddress &address : info.addresses())
                    added += self->addSeed(Endpoint(address, router.port), DhtNode::SeedSource::Bootstrap) ? 1 : 0;
            }
            if (added == 0)
                emit self->notice(QStringLiteral("Could not resolve bootstrap router %1").arg(router.host), true);
        });
    }
}

void DhtEngine::setReadOnly(bool enabled)
{
    m_config.readOnly = enabled;
    if (m_v4)
        m_v4->setReadOnly(enabled);
    if (m_v6)
        m_v6->setReadOnly(enabled);
    scheduleSnapshot();
}

void DhtEngine::setPortForwarding(bool enabled)
{
    m_config.portForwarding = enabled;
    if (!m_running || !m_mapper)
        return;
    if (enabled)
        m_mapper->start(m_v4->port());
    else
        m_mapper->stop();
    scheduleSnapshot();
}

void DhtEngine::getPeers(const NodeId &infohash, std::function<void(const std::vector<Endpoint> &)> done)
{
    struct State
    {
        int remaining = 0;
        std::vector<Endpoint> peers;
    };
    auto state = std::make_shared<State>();
    const auto finishOne = [state, done](const Lookup::Result &result) {
        state->peers.insert(state->peers.end(), result.peers.begin(), result.peers.end());
        if (--state->remaining == 0 && done)
            done(state->peers);
    };

    const DhtNode *nodes[] = {m_v4, m_v6};
    for (const DhtNode *n : nodes)
        state->remaining += n ? 1 : 0;
    if (state->remaining == 0) {
        if (done)
            done({});
        return;
    }
    if (m_v4)
        m_v4->getPeers(infohash, finishOne);
    if (m_v6)
        m_v6->getPeers(infohash, finishOne);
}

void DhtEngine::announce(const NodeId &infohash, quint16 port, bool impliedPort, std::function<void(int)> done)
{
    auto remaining = std::make_shared<int>((m_v4 ? 1 : 0) + (m_v6 ? 1 : 0));
    auto accepted = std::make_shared<int>(0);
    if (*remaining == 0) {
        if (done)
            done(0);
        return;
    }
    const auto finishOne = [remaining, accepted, done](int count) {
        *accepted += count;
        if (--*remaining == 0 && done)
            done(*accepted);
    };
    if (m_v4)
        m_v4->announce(infohash, port, impliedPort, finishOne);
    if (m_v6)
        m_v6->announce(infohash, port, impliedPort, finishOne);
}

namespace {

ProbeResult resultFrom(const Endpoint &endpoint, const QByteArray &method, const RpcReply &reply)
{
    ProbeResult out;
    out.endpoint = endpoint;
    out.method = method;
    out.request = reply.request;
    out.rttMs = reply.rttMs;

    switch (reply.status) {
    case RpcReply::Status::Timeout:
        out.timedOut = true;
        out.summary = QStringLiteral("no reply within the timeout");
        return out;
    case RpcReply::Status::Error:
        out.isError = true;
        out.errorCode = reply.message.errorCode;
        out.errorMessage = QString::fromUtf8(reply.message.errorMessage);
        break;
    case RpcReply::Status::Response:
        break;
    }

    out.response = reply.datagram;
    out.decoded = krpc::describe(reply.message);
    out.token = reply.message.body.stringAt("token").value_or(QByteArray());

    if (out.isError) {
        out.summary = QStringLiteral("error %1: %2").arg(out.errorCode).arg(out.errorMessage);
        return out;
    }

    QStringList parts;
    parts << QStringLiteral("%1 bytes in %2 ms").arg(out.response.size()).arg(out.rttMs);
    if (const auto id = reply.message.senderId())
        parts << QStringLiteral("id %1").arg(id->toHex().left(12) + QChar(0x2026));
    if (!reply.message.version.isEmpty())
        parts << QStringLiteral("version %1").arg(krpc::escapeBytes(reply.message.version));
    const int v4 = int(krpc::decodeNodes(reply.message.body.stringAt("nodes").value_or(QByteArray()),
                                         Family::IPv4).nodes.size());
    const int v6 = int(krpc::decodeNodes(reply.message.body.stringAt("nodes6").value_or(QByteArray()),
                                         Family::IPv6).nodes.size());
    if (v4 + v6 > 0)
        parts << QStringLiteral("%1 node(s)").arg(v4 + v6);
    if (const BValue *values = reply.message.body.listAt("values"))
        parts << QStringLiteral("%1 peer(s)").arg(values->toList().size());
    if (!out.token.isEmpty())
        parts << QStringLiteral("token %1").arg(QString::fromLatin1(out.token.toHex()));
    out.summary = parts.join(QStringLiteral(", "));
    return out;
}

} // namespace

void DhtEngine::probe(const Endpoint &endpoint, const QByteArray &method, BValue::Dict arguments)
{
    DhtNode *node = this->node(endpoint.family());
    if (!node) {
        ProbeResult out;
        out.endpoint = endpoint;
        out.method = method;
        out.errorMessage = QStringLiteral("%1 is not enabled, so %2 cannot be reached")
                               .arg(familyName(endpoint.family()), endpoint.toString());
        out.summary = out.errorMessage;
        emit probeFinished(out);
        return;
    }

    QPointer<DhtEngine> self(this);
    node->probe(endpoint, method, std::move(arguments), [self, endpoint, method](const RpcReply &reply) {
        if (self)
            emit self->probeFinished(resultFrom(endpoint, method, reply));
    });
}

void DhtEngine::probeAnnounce(const Endpoint &endpoint, const NodeId &infohash, quint16 port, bool impliedPort)
{
    DhtNode *node = this->node(endpoint.family());
    if (!node) {
        probe(endpoint, "announce_peer", {});  // reports the same "not enabled" result
        return;
    }

    BValue::Dict getArgs;
    getArgs.emplace("info_hash", BValue(infohash.toBytes()));

    QPointer<DhtEngine> self(this);
    node->probe(endpoint, "get_peers", std::move(getArgs),
                [self, node, endpoint, infohash, port, impliedPort](const RpcReply &reply) {
                    if (!self)
                        return;
                    const ProbeResult first = resultFrom(endpoint, "get_peers", reply);
                    emit self->probeFinished(first);
                    if (first.token.isEmpty()) {
                        ProbeResult blocked;
                        blocked.endpoint = endpoint;
                        blocked.method = "announce_peer";
                        blocked.errorMessage = first.timedOut
                                                   ? QStringLiteral("no token: the node did not answer get_peers")
                                                   : QStringLiteral("no token in the get_peers reply, cannot announce");
                        blocked.summary = blocked.errorMessage;
                        emit self->probeFinished(blocked);
                        return;
                    }

                    BValue::Dict announceArgs;
                    announceArgs.emplace("info_hash", BValue(infohash.toBytes()));
                    announceArgs.emplace("port", BValue(qint64(port)));
                    announceArgs.emplace("token", BValue(first.token));
                    if (impliedPort)
                        announceArgs.emplace("implied_port", BValue(1));
                    node->probe(endpoint, "announce_peer", std::move(announceArgs),
                                [self, endpoint](const RpcReply &announceReply) {
                                    if (self)
                                        emit self->probeFinished(resultFrom(endpoint, "announce_peer", announceReply));
                                });
                });
}

void DhtEngine::searchPeers(const NodeId &infohash)
{
    struct State
    {
        int remaining = 0;
        PeerSearchResult result;
    };
    auto state = std::make_shared<State>();
    state->result.infohash = infohash;
    state->remaining = (m_v4 ? 1 : 0) + (m_v6 ? 1 : 0);
    if (state->remaining == 0) {
        emit peerSearchFinished(state->result);
        return;
    }

    QPointer<DhtEngine> self(this);
    const auto collect = [self, state](const Lookup::Result &result) {
        state->result.peers.insert(state->result.peers.end(), result.peers.begin(), result.peers.end());
        state->result.sightings.insert(state->result.sightings.end(), result.sightings.begin(),
                                       result.sightings.end());
        state->result.queried += result.queried;
        state->result.responded += result.responded;
        if (--state->remaining == 0 && self)
            emit self->peerSearchFinished(state->result);
    };
    if (m_v4)
        m_v4->getPeers(infohash, collect);
    if (m_v6)
        m_v6->getPeers(infohash, collect);
}

void DhtEngine::searchItem(const NodeId &target, const QByteArray &salt)
{
    struct State
    {
        int remaining = 0;
        ItemSearchResult result;
    };
    auto state = std::make_shared<State>();
    state->result.target = target;
    state->result.salt = salt;
    state->remaining = (m_v4 ? 1 : 0) + (m_v6 ? 1 : 0);
    if (state->remaining == 0) {
        emit itemSearchFinished(state->result);
        return;
    }

    QPointer<DhtEngine> self(this);
    const auto collect = [self, state](const Lookup::Result &result) {
        state->result.queried += result.queried;
        state->result.responded += result.responded;
        // Keep the newest mutable item, or any immutable one. Immutable items
        // have no sequence number, so never compare on it alone.
        if (result.itemFound && (!state->result.found || result.itemSequence > state->result.sequence)) {
            state->result.found = true;
            state->result.isMutable = result.itemIsMutable;
            state->result.value = result.itemValue;
            state->result.publicKey = result.itemPublicKey;
            state->result.signature = result.itemSignature;
            state->result.sequence = result.itemSequence;
        }
        if (--state->remaining == 0 && self)
            emit self->itemSearchFinished(state->result);
    };
    if (m_v4)
        m_v4->getItem(target, salt, collect);
    if (m_v6)
        m_v6->getItem(target, salt, collect);
}

void DhtEngine::announcePeer(const NodeId &infohash, quint16 port, bool impliedPort)
{
    PublishResult empty;
    empty.kind = PublishResult::Kind::Announce;
    empty.target = infohash;
    if (!m_v4 && !m_v6) {
        emit publishFinished(empty);
        return;
    }
    QPointer<DhtEngine> self(this);
    announce(infohash, port, impliedPort, [self, infohash](int accepted) {
        if (!self)
            return;
        PublishResult result;
        result.kind = PublishResult::Kind::Announce;
        result.target = infohash;
        result.accepted = accepted;
        result.attempted = accepted;  // announce only reports acceptances
        emit self->publishFinished(result);
    });
}

void DhtEngine::publishImmutable(const QByteArray &bencodedValue)
{
    PublishResult result;
    result.kind = PublishResult::Kind::Immutable;
    result.target = bep44::immutableTarget(bencodedValue);
    if (bencodedValue.size() > bep44::MaxValueBytes) {
        result.error = QStringLiteral("value is larger than the %1 byte limit").arg(bep44::MaxValueBytes);
        emit publishFinished(result);
        return;
    }

    DhtNode::PutRequest request;
    request.target = result.target;
    request.value = bencodedValue;

    auto state = std::make_shared<PublishResult>(result);
    auto remaining = std::make_shared<int>((m_v4 ? 1 : 0) + (m_v6 ? 1 : 0));
    if (*remaining == 0) {
        emit publishFinished(*state);
        return;
    }
    QPointer<DhtEngine> self(this);
    const auto collect = [self, state, remaining](int accepted, int attempted) {
        state->accepted += accepted;
        state->attempted += attempted;
        if (--*remaining == 0 && self)
            emit self->publishFinished(*state);
    };
    if (m_v4)
        m_v4->put(request, collect);
    if (m_v6)
        m_v6->put(request, collect);
}

void DhtEngine::publishMutable(const QByteArray &publicKey, const QByteArray &secretKey, const QByteArray &salt,
                               qint64 sequence, const QByteArray &bencodedValue, std::optional<qint64> cas)
{
    PublishResult result;
    result.kind = PublishResult::Kind::Mutable;
    result.sequence = sequence;
    result.target = bep44::mutableTarget(publicKey, salt);

    const auto fail = [&](const QString &message) {
        result.error = message;
        emit publishFinished(result);
    };
    if (publicKey.size() != bep44::PublicKeyBytes || secretKey.size() != 64)
        return fail(QStringLiteral("the key pair is not a valid Ed25519 key"));
    if (bencodedValue.size() > bep44::MaxValueBytes)
        return fail(QStringLiteral("value is larger than the %1 byte limit").arg(bep44::MaxValueBytes));
    if (salt.size() > bep44::MaxSaltBytes)
        return fail(QStringLiteral("salt is longer than %1 bytes").arg(bep44::MaxSaltBytes));

    DhtNode::PutRequest request;
    request.target = result.target;
    request.value = bencodedValue;
    request.isMutable = true;
    request.publicKey = publicKey;
    request.salt = salt;
    request.sequence = sequence;
    request.cas = cas;
    request.signature = ed25519::sign(bep44::signingBuffer(salt, sequence, bencodedValue), secretKey);
    if (request.signature.isEmpty())
        return fail(QStringLiteral("could not sign the item"));

    auto state = std::make_shared<PublishResult>(result);
    auto remaining = std::make_shared<int>((m_v4 ? 1 : 0) + (m_v6 ? 1 : 0));
    if (*remaining == 0) {
        emit publishFinished(*state);
        return;
    }
    QPointer<DhtEngine> self(this);
    const auto collect = [self, state, remaining](int accepted, int attempted) {
        state->accepted += accepted;
        state->attempted += attempted;
        if (--*remaining == 0 && self)
            emit self->publishFinished(*state);
    };
    if (m_v4)
        m_v4->put(request, collect);
    if (m_v6)
        m_v6->put(request, collect);
}

EngineSnapshot DhtEngine::snapshot() const
{
    EngineSnapshot s;
    s.running = m_running;
    if (!m_running)
        return s;

    const qint64 now = nowMs();
    if (m_v4) {
        s.ipv4 = m_v4->familySnapshot();
        s.stats.add(m_v4->stats());
        m_v4->appendNodeRows(s.nodes, now);
    }
    if (m_v6) {
        s.ipv6 = m_v6->familySnapshot();
        s.stats.add(m_v6->stats());
        m_v6->appendNodeRows(s.nodes, now);
    } else {
        s.ipv6.enabled = m_config.enableIpv6;
        s.ipv6.error = m_v6Error;
    }
    if (m_mapper)
        s.portMapping = m_mapper->snapshot();

    s.stats.storedInfohashes = m_storage.infohashCount();
    s.stats.storedPeers = m_storage.peerCount();

    std::sort(s.nodes.begin(), s.nodes.end(),
              [](const NodeRow &a, const NodeRow &b) { return a.sortKey < b.sortKey; });
    return s;
}

StorageSnapshot DhtEngine::storageSnapshot() const
{
    StorageSnapshot out;
    out.running = m_running;
    out.ttlMs = PeerStorage::PeerTtlMs;
    out.maxInfohashes = PeerStorage::MaxInfohashes;
    out.maxPeersPerInfohash = PeerStorage::MaxPeersPerInfohash;
    out.itemTtlMs = bep44::ItemTtlMs;
    out.maxItems = ItemStorage::MaxItems;
    if (!m_running)
        return out;

    out.immutableCount = m_items.immutableCount();
    out.mutableCount = m_items.mutableCount();

    out.infohashCount = m_storage.infohashCount();
    out.peerCount = m_storage.peerCount();

    const qint64 now = nowMs();
    std::vector<StoredInfohash> stored = m_storage.snapshot();
    std::sort(stored.begin(), stored.end(), [](const StoredInfohash &a, const StoredInfohash &b) {
        return a.lastAnnounce > b.lastAnnounce;
    });
    if (int(stored.size()) > MaxListedInfohashes) {
        stored.resize(MaxListedInfohashes);
        out.truncated = true;
    }

    for (const StoredInfohash &entry : stored) {
        StoredInfohashRow row;
        row.infohash = entry.infohash;
        row.peerCount = int(entry.peers.size());
        row.lastAnnounceAgoMs = now - entry.lastAnnounce;
        for (const StoredPeer &peer : entry.peers) {
            StoredPeerRow out_peer;
            out_peer.endpoint = peer.endpoint;
            out_peer.ageMs = now - peer.announcedAt;
            out_peer.expiresInMs = PeerStorage::PeerTtlMs - out_peer.ageMs;
            row.expiresInMs = std::max(row.expiresInMs, out_peer.expiresInMs);
            row.peers.push_back(out_peer);
        }
        std::sort(row.peers.begin(), row.peers.end(),
                  [](const StoredPeerRow &a, const StoredPeerRow &b) { return a.ageMs < b.ageMs; });
        out.infohashes.push_back(std::move(row));
    }

    for (const ImmutableItem &item : m_items.immutableSnapshot()) {
        StoredItemRow row;
        row.target = item.target;
        row.value = item.value;
        row.ageMs = now - item.storedAt;
        row.expiresInMs = bep44::ItemTtlMs - row.ageMs;
        out.items.push_back(std::move(row));
    }
    for (const MutableItem &item : m_items.mutableSnapshot()) {
        StoredItemRow row;
        row.isMutable = true;
        row.target = item.target;
        row.value = item.value;
        row.publicKey = item.publicKey;
        row.salt = item.salt;
        row.sequence = item.sequence;
        row.ageMs = now - item.storedAt;
        row.expiresInMs = bep44::ItemTtlMs - row.ageMs;
        out.items.push_back(std::move(row));
    }
    std::sort(out.items.begin(), out.items.end(),
              [](const StoredItemRow &a, const StoredItemRow &b) { return a.ageMs < b.ageMs; });
    return out;
}

void DhtEngine::requestStorageSnapshot()
{
    emit storageSnapshotReady(storageSnapshot());
}

void DhtEngine::scheduleSnapshot()
{
    if (m_running && !m_coalesceTimer.isActive())
        m_coalesceTimer.start();
}

void DhtEngine::publishSnapshot()
{
    if (m_running)
        emit snapshotReady(snapshot());
}

} // namespace dht
