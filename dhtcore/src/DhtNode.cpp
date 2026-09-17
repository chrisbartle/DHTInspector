#include "dhtcore/DhtNode.h"

#include "dhtcore/Inbound.h"

#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QVariant>

#include <memory>

namespace dht {

namespace {

constexpr int MaintenanceIntervalMs = 5000;
constexpr qint64 PingIntervalMs = 60 * 1000;
constexpr qint64 BucketRefreshMs = 15 * 60 * 1000;
constexpr qint64 VerificationWindowMs = 10 * 60 * 1000;
constexpr int MaxPendingVerifications = 2000;

QByteArray sortKeyFor(Family family, bool hasId, const NodeId &id, const NodeId &self, const Endpoint &endpoint)
{
    QByteArray key;
    key += char(family == Family::IPv4 ? 0 : 1);
    key += char(hasId ? 0 : 1);
    key += hasId ? (id ^ self).toBytes() : QByteArray(NodeId::Size, char(0xff));
    key += endpoint.toString().toUtf8();
    return key;
}

} // namespace

DhtNode::DhtNode(const NodeConfig &config, PeerStorage *storage, ItemStorage *items, SendBudget *budget,
                 QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_storage(storage)
    , m_items(items)
    , m_budget(budget)
    , m_id(config.nodeId ? *config.nodeId : NodeId::random())
    , m_table(m_id)
    , m_maintenance(this)
{
    connect(&m_maintenance, &QTimer::timeout, this, &DhtNode::onMaintenance);
}

DhtNode::~DhtNode()
{
    close();
}

bool DhtNode::bind(QString *error)
{
    QHostAddress address = m_config.bindAddress;
    if (address.isNull())
        address = QHostAddress(m_config.family == Family::IPv4 ? QHostAddress::AnyIPv4 : QHostAddress::AnyIPv6);

    m_socket = new QUdpSocket(this);
    // DontShareAddress makes a port clash with another client fail loudly
    // (on Windows the default would silently share the port).
    if (!m_socket->bind(address, m_config.port, QAbstractSocket::DontShareAddress)) {
        if (error) {
            *error = QStringLiteral("Could not bind %1 port %2: %3")
                         .arg(familyName(m_config.family))
                         .arg(m_config.port)
                         .arg(m_socket->errorString());
        }
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    // Large buffers, so a fast scan is limited by the network rather than by
    // datagrams dropped on this machine.
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, QVariant(8 << 20));
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, QVariant(4 << 20));
    connect(m_socket, &QUdpSocket::readyRead, this, &DhtNode::onReadyRead);

    m_rpc = new RpcManager([this](const QByteArray &data, const Endpoint &to) { return sendDatagram(data, to); },
                           this);
    m_rpc->setReadOnly(m_config.readOnly);
    m_rpc->setHostLimit(m_config.hostLimit);
    m_rpc->setBudget(m_budget);
    m_maintenance.start(MaintenanceIntervalMs);
    return true;
}

void DhtNode::close()
{
    m_maintenance.stop();
    if (m_rpc)
        m_rpc->cancelAll();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
    }
}

quint16 DhtNode::port() const
{
    return m_socket ? m_socket->localPort() : 0;
}

QHostAddress DhtNode::localAddress() const
{
    return m_socket ? m_socket->localAddress() : QHostAddress();
}

void DhtNode::setReadOnly(bool readOnly)
{
    if (m_config.readOnly == readOnly)
        return;
    m_config.readOnly = readOnly;
    if (m_rpc)
        m_rpc->setReadOnly(readOnly);
    emit changed();
}

void DhtNode::setId(const NodeId &id)
{
    m_id = id;
    m_table.rebuild(id, nowMs());
    emit changed();
}

// --- Outgoing ---------------------------------------------------------------

bool DhtNode::sendDatagram(const QByteArray &data, const Endpoint &to)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::BoundState)
        return false;
    if (m_socket->writeDatagram(data, to.address, to.port) < 0)
        return false;
    ++m_stats.packetsOut;
    m_stats.bytesOut += data.size();
    if (m_budget)
        m_budget->spend(data.size(), nowMs());
    return true;
}

void DhtNode::sendQuery(const Endpoint &to, const QByteArray &method, BValue::Dict arguments,
                        RpcManager::Callback callback, int timeoutMs, RpcManager::SentFn onSent)
{
    if (!m_rpc)
        return;
    arguments.insert_or_assign("id", BValue(m_id.toBytes()));
    ++m_stats.queriesOut;
    QPointer<DhtNode> self(this);
    m_rpc->query(to, method, std::move(arguments), m_config.version,
                 [self, callback = std::move(callback)](const RpcReply &reply) {
                     if (!self)
                         return;
                     self->onRpcReply(reply);
                     if (callback)
                         callback(reply);
                 },
                 timeoutMs, std::move(onSent));
}

bool DhtNode::budgetAllowsReply()
{
    // Over the send limit, incoming queries go unanswered, as libtorrent
    // does with dht_upload_rate_limit. Our own queries wait instead, since
    // dropping them would make a healthy node look dead.
    if (!m_budget || m_budget->available(nowMs()))
        return true;
    ++m_stats.repliesShed;
    return false;
}

void DhtNode::sendResponse(const krpc::Message &query, const Endpoint &to, BValue::Dict values)
{
    values.insert_or_assign("id", BValue(m_id.toBytes()));
    const Endpoint requester = m_config.bep42 ? to : Endpoint(); // BEP 42 "ip"
    sendDatagram(krpc::encodeResponse(query.transactionId, std::move(values), m_config.version, requester), to);
}

void DhtNode::sendError(const QByteArray &transactionId, const Endpoint &to, int code, const QByteArray &message)
{
    sendDatagram(krpc::encodeError(transactionId, code, message, m_config.version), to);
}

// --- Incoming ---------------------------------------------------------------

void DhtNode::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        if (!datagram.isValid())
            continue;
        const QByteArray data = datagram.data();
        ++m_stats.packetsIn;
        m_stats.bytesIn += data.size();
        handleDatagram(data, Endpoint(datagram.senderAddress(), quint16(datagram.senderPort())));
    }
}

void DhtNode::handleDatagram(const QByteArray &data, const Endpoint &from)
{
    const krpc::ParseResult parsed = krpc::parse(data);
    if (!parsed.message) {
        ++m_stats.malformedIn;
        return;
    }
    const krpc::Message &message = *parsed.message;
    if (message.type == krpc::MessageType::Query)
        handleQuery(message, from, data);
    else if (m_rpc)
        m_rpc->handleReply(message, data, from);
}

void DhtNode::handleQuery(const krpc::Message &message, const Endpoint &from, const QByteArray &datagram)
{
    const qint64 now = nowMs();
    // Every query counts towards who talks to us, whether or not we answer.
    if (m_inbound)
        m_inbound->record(from, message);
    if (!m_limiter.allow(from.address, now)) {
        ++m_stats.rateLimited;
        return;
    }
    ++m_stats.queriesIn;

    // BEP 43: a read-only node "no longer responds to query messages that it
    // receives". Dropping them here also means nothing is learned from the
    // sender and nothing is stored, since announce_peer and put never get
    // any further. Our own queries still go out, so lookups keep working.
    if (m_config.readOnly) {
        ++m_stats.readOnlyDropped;
        return;
    }

    // Over the send limit we could not answer, so the query is dropped
    // before it changes anything.
    if (!budgetAllowsReply())
        return;

    const auto senderId = message.senderId();
    if (!senderId) {
        sendError(message.transactionId, from, krpc::ProtocolError, "missing or invalid id");
        return;
    }

    // BEP 43: read-only nodes must not be added to routing tables.
    if (!message.readOnly && *senderId != m_id && !m_table.queriedBy(*senderId, from, now))
        maybeVerify(*senderId, from, now);

    const QByteArray &method = message.method;
    const BValue &args = message.body;

    if (method == "ping") {
        sendResponse(message, from, {});
        return;
    }

    if (method == "find_node") {
        const auto target = NodeId::fromBytes(args.stringAt("target").value_or(QByteArray()));
        if (!target) {
            sendError(message.transactionId, from, krpc::ProtocolError, "missing or invalid target");
            return;
        }
        sendResponse(message, from, nodesFor(*target, args));
        return;
    }

    if (method == "get_peers") {
        const auto infohash = NodeId::fromBytes(args.stringAt("info_hash").value_or(QByteArray()));
        if (!infohash) {
            sendError(message.transactionId, from, krpc::ProtocolError, "missing or invalid info_hash");
            return;
        }
        BValue::Dict values = nodesFor(*infohash, args);
        values.insert_or_assign("token", BValue(m_tokens.generate(from.address, now)));
        const int maxPeers = m_config.family == Family::IPv4 ? 100 : 50;
        const auto peers = m_storage->peers(*infohash, m_config.family, maxPeers);
        if (!peers.empty()) {
            BValue::List list;
            for (const Endpoint &peer : peers)
                list.push_back(BValue(peer.toCompact()));
            values.insert_or_assign("values", BValue(std::move(list)));
        }
        sendResponse(message, from, std::move(values));
        return;
    }

    if (method == "announce_peer") {
        const auto infohash = NodeId::fromBytes(args.stringAt("info_hash").value_or(QByteArray()));
        if (!infohash) {
            sendError(message.transactionId, from, krpc::ProtocolError, "missing or invalid info_hash");
            return;
        }
        const auto token = args.stringAt("token");
        if (!token || !m_tokens.validate(*token, from.address, now)) {
            sendError(message.transactionId, from, krpc::ProtocolError, "invalid token");
            return;
        }
        const bool implied = args.integerAt("implied_port").value_or(0) == 1;
        const qint64 port = implied ? from.port : args.integerAt("port").value_or(0);
        if (port < 1 || port > 65535) {
            sendError(message.transactionId, from, krpc::ProtocolError, "invalid port");
            return;
        }
        m_storage->announce(*infohash, Endpoint(from.address, quint16(port)), now);
        sendResponse(message, from, {});
        emit changed();
        return;
    }

    if (method == "get") {
        handleGet(message, from, now);
        return;
    }

    if (method == "sample_infohashes") {
        // BEP 51: a random sample of the infohashes we hold, how many there
        // are in all, and nodes near the target like find_node.
        const auto target = NodeId::fromBytes(args.stringAt("target").value_or(QByteArray()));
        if (!target) {
            sendError(message.transactionId, from, krpc::ProtocolError, "missing or invalid target");
            return;
        }
        BValue::Dict values = nodesFor(*target, args);
        QByteArray samples;
        for (const NodeId &infohash : m_storage->sampleInfohashes(MaxSamples))
            samples += infohash.toBytes();
        values.insert_or_assign("samples", BValue(samples));
        values.insert_or_assign("num", BValue(qint64(m_storage->infohashCount())));
        values.insert_or_assign("interval", BValue(qint64(SampleIntervalSeconds)));
        sendResponse(message, from, std::move(values));
        return;
    }

    if (method == "put") {
        handlePut(message, from, datagram, now);
        return;
    }

    sendError(message.transactionId, from, krpc::MethodUnknown, "Method Unknown");
}

void DhtNode::handleGet(const krpc::Message &message, const Endpoint &from, qint64 now)
{
    const BValue &args = message.body;
    const auto target = NodeId::fromBytes(args.stringAt("target").value_or(QByteArray()));
    if (!target) {
        sendError(message.transactionId, from, krpc::ProtocolError, "missing or invalid target");
        return;
    }

    BValue::Dict values = nodesFor(*target, args);
    values.insert_or_assign("token", BValue(m_tokens.generate(from.address, now)));

    if (const ImmutableItem *item = m_items->immutableItem(*target)) {
        values.insert_or_assign("v", BValue::preEncoded(item->value));
    } else if (const MutableItem *item = m_items->mutableItem(*target)) {
        values.insert_or_assign("k", BValue(item->publicKey));
        values.insert_or_assign("seq", BValue(item->sequence));
        // A getter that already holds this sequence number only wants to know
        // whether a newer one exists.
        const auto knownSeq = args.integerAt("seq");
        if (!knownSeq || item->sequence > *knownSeq) {
            values.insert_or_assign("v", BValue::preEncoded(item->value));
            values.insert_or_assign("sig", BValue(item->signature));
        }
    }
    sendResponse(message, from, std::move(values));
}

void DhtNode::handlePut(const krpc::Message &message, const Endpoint &from, const QByteArray &datagram, qint64 now)
{
    const BValue &args = message.body;

    const auto token = args.stringAt("token");
    if (!token || !m_tokens.validate(*token, from.address, now)) {
        sendError(message.transactionId, from, krpc::ProtocolError, "invalid token");
        return;
    }

    // The target is a hash of the value's bytes as they were sent, so use
    // those rather than a re-encoding.
    const BValue *value = args.find("v");
    const QByteArray raw = value ? value->rawSpan(datagram) : QByteArray();
    if (raw.isEmpty()) {
        sendError(message.transactionId, from, krpc::ProtocolError, "missing or unreadable v");
        return;
    }
    if (raw.size() > bep44::MaxValueBytes) {
        sendError(message.transactionId, from, bep44::MessageTooBig, "message (v field) too big");
        return;
    }

    const auto key = args.stringAt("k");
    if (!key) {
        m_items->putImmutable(bep44::immutableTarget(raw), raw, now);
        sendResponse(message, from, {});
        emit changed();
        return;
    }

    if (key->size() != bep44::PublicKeyBytes) {
        sendError(message.transactionId, from, krpc::ProtocolError, "invalid k");
        return;
    }
    const QByteArray salt = args.stringAt("salt").value_or(QByteArray());
    if (salt.size() > bep44::MaxSaltBytes) {
        sendError(message.transactionId, from, bep44::SaltTooLong, "salt (salt field) too long");
        return;
    }
    const auto sequence = args.integerAt("seq");
    const auto signature = args.stringAt("sig");
    if (!sequence || !signature || signature->size() != bep44::SignatureBytes) {
        sendError(message.transactionId, from, krpc::ProtocolError, "mutable put needs seq and sig");
        return;
    }

    const auto verified = ed25519::verify(*signature, bep44::signingBuffer(salt, *sequence, raw), *key);
    if (!verified) {
        // Storing a mutable item without checking its signature would let
        // anyone overwrite anyone else's data.
        sendError(message.transactionId, from, krpc::ProtocolError,
                  "mutable items are not supported by this node yet");
        return;
    }
    if (!*verified) {
        sendError(message.transactionId, from, bep44::InvalidSignature, "invalid signature");
        return;
    }

    MutableItem item;
    item.target = bep44::mutableTarget(*key, salt);
    item.publicKey = *key;
    item.salt = salt;
    item.signature = *signature;
    item.value = raw;
    item.sequence = *sequence;

    switch (m_items->putMutable(item, args.integerAt("cas"), now)) {
    case ItemStorage::PutResult::CasMismatch:
        sendError(message.transactionId, from, bep44::CasMismatch, "CAS hash mismatch, re-read value and try again");
        return;
    case ItemStorage::PutResult::SequenceTooLow:
        sendError(message.transactionId, from, bep44::SequenceTooLow, "sequence number less than current");
        return;
    case ItemStorage::PutResult::TooBig:
        sendError(message.transactionId, from, bep44::MessageTooBig, "message (v field) too big");
        return;
    case ItemStorage::PutResult::SaltTooLong:
        sendError(message.transactionId, from, bep44::SaltTooLong, "salt (salt field) too long");
        return;
    case ItemStorage::PutResult::Stored:
    case ItemStorage::PutResult::Refreshed:
        break;
    }

    sendResponse(message, from, {});
    emit changed();
}

BValue::Dict DhtNode::nodesFor(const NodeId &target, const BValue &arguments) const
{
    bool want4 = m_config.family == Family::IPv4;
    bool want6 = m_config.family == Family::IPv6;
    if (const BValue *want = arguments.listAt("want")) {
        bool any4 = false;
        bool any6 = false;
        for (const BValue &item : want->toList()) {
            any4 = any4 || item.toString() == "n4";
            any6 = any6 || item.toString() == "n6";
        }
        if (any4 || any6) {
            want4 = any4;
            want6 = any6;
        }
    }

    BValue::Dict out;
    const auto fill = [&](const DhtNode *node, Family family, const char *key) {
        if (!node)
            return;
        const auto nodes = node->closestNodes(target, RoutingTable::K);
        if (!nodes.empty())
            out.insert_or_assign(key, BValue(krpc::encodeNodes(nodes, family)));
    };
    if (want4)
        fill(m_config.family == Family::IPv4 ? this : m_sibling.data(), Family::IPv4, "nodes");
    if (want6)
        fill(m_config.family == Family::IPv6 ? this : m_sibling.data(), Family::IPv6, "nodes6");
    return out;
}

// --- Replies ---------------------------------------------------------------

bool DhtNode::isRouter(const Endpoint &endpoint) const
{
    const auto it = m_seeds.constFind(endpoint);
    return it != m_seeds.constEnd() && it->source == SeedSource::Bootstrap;
}

int DhtNode::replyRttQuantile(double quantile) const
{
    constexpr size_t MinSamples = 32;
    if (m_replyRtts.size() < MinSamples)
        return -1;
    std::vector<int> sorted(m_replyRtts.begin(), m_replyRtts.end());
    const size_t index = std::min(sorted.size() - 1, size_t(quantile * double(sorted.size())));
    std::nth_element(sorted.begin(), sorted.begin() + qsizetype(index), sorted.end());
    return sorted[index];
}

void DhtNode::onRpcReply(const RpcReply &reply)
{
    constexpr size_t MaxRttSamples = 256;
    if (reply.status == RpcReply::Status::Response && reply.rttMs >= 0) {
        m_replyRtts.push_back(reply.rttMs);
        while (m_replyRtts.size() > MaxRttSamples)
            m_replyRtts.pop_front();
    }
    const qint64 now = nowMs();
    switch (reply.status) {
    case RpcReply::Status::Timeout:
        ++m_stats.timeouts;
        m_table.failed(reply.from, now);
        return;
    case RpcReply::Status::Error:
        ++m_stats.errorsIn;
        return;
    case RpcReply::Status::Throttled:
        return;  // never sent, so nothing to hold against the node
    case RpcReply::Status::Response:
        break;
    }

    ++m_stats.responsesIn;
    const krpc::Message &message = reply.message;

    if (message.reportedAddress && message.reportedAddress->family() == m_config.family) {
        if (m_voter.addVote(reply.from.address, message.reportedAddress->address, now))
            adoptExternalAddress(m_voter.consensus());
    }

    const auto id = message.senderId();
    if (!id) {
        ++m_stats.malformedIn;
        return;
    }
    if (!isRouter(reply.from) && !message.readOnly)
        m_table.heardFrom(*id, reply.from, message.version, reply.rttMs, now);
}

void DhtNode::maybeVerify(const NodeId &id, const Endpoint &from, qint64 now)
{
    // Nodes that query us only enter the table after answering a ping, so a
    // spoofed source address cannot plant entries.
    if (!isUsableRemote(from, m_config.allowLocalAddresses) || !m_table.wouldAccept(id))
        return;
    const auto it = m_recentVerifications.constFind(from);
    if (it != m_recentVerifications.constEnd() && now - *it < VerificationWindowMs)
        return;
    if (m_recentVerifications.size() >= MaxPendingVerifications)
        return;
    m_recentVerifications.insert(from, now);
    sendQuery(from, "ping", {}, nullptr);
}

void DhtNode::adoptExternalAddress(const QHostAddress &address)
{
    emit changed();
    // The address is still recorded and reported with BEP 42 off; only the
    // switch to a derived ID is skipped.
    if (!m_config.bep42 || bep42::isExempt(address) || bep42::isCompliant(m_id, address))
        return;

    // BEP 42: take an ID derived from our external address, then rejoin
    // around it.
    setId(bep42::generate(address));
    startLookup(Lookup::Kind::FindNode, m_id, nullptr);
}

// --- Seeds and lookups -----------------------------------------------------

void DhtNode::addSeed(const Endpoint &endpoint, SeedSource source)
{
    if (!endpoint.isValid() || endpoint.family() != m_config.family)
        return;

    Seed &seed = m_seeds[endpoint];
    if (source == SeedSource::Injected)
        seed.source = SeedSource::Injected;
    else if (seed.state == Seed::State::Querying && seed.lastAttempt == 0)
        seed.source = source;
    seed.state = Seed::State::Querying;
    seed.lastAttempt = nowMs();
    emit changed();

    BValue::Dict args;
    args.emplace("target", BValue(m_id.toBytes()));
    QPointer<DhtNode> self(this);
    sendQuery(endpoint, "find_node", std::move(args), [self, endpoint](const RpcReply &reply) {
        if (!self)
            return;
        const auto it = self->m_seeds.find(endpoint);
        if (it == self->m_seeds.end())
            return;

        if (reply.status == RpcReply::Status::Timeout || reply.status == RpcReply::Status::Throttled) {
            it->state = Seed::State::NoResponse;
            emit self->changed();
            return;
        }

        it->state = Seed::State::Responded;
        it->rttMs = reply.rttMs;
        it->version = reply.message.version;
        if (const auto id = reply.message.senderId()) {
            it->id = *id;
            it->hasId = true;
        }

        if (reply.status == RpcReply::Status::Response) {
            const QByteArray key = self->m_config.family == Family::IPv4 ? QByteArray("nodes") : QByteArray("nodes6");
            std::vector<krpc::CompactNode> nodes;
            if (const auto bytes = reply.message.body.stringAt(key))
                nodes = krpc::decodeNodes(*bytes, self->m_config.family).nodes;

            if (self->m_selfLookup && !self->m_selfLookup->isDone()) {
                for (const auto &node : nodes)
                    self->m_selfLookup->addCandidate(node.id, node.endpoint);
            } else {
                self->m_selfLookup = self->startLookup(Lookup::Kind::FindNode, self->m_id, nullptr, nodes);
            }
        }
        emit self->changed();
    });
}

Lookup *DhtNode::startLookup(Lookup::Kind kind, const NodeId &target, Lookup::DoneFn done,
                             const std::vector<krpc::CompactNode> &extraCandidates, const QByteArray &salt)
{
    QPointer<DhtNode> self(this);
    auto *lookup = new Lookup(
        kind, target, m_config.family, m_id, m_config.allowLocalAddresses,
        [self](const Endpoint &to, const QByteArray &method, BValue::Dict args, RpcManager::Callback callback,
               int timeoutMs, RpcManager::SentFn onSent) {
            if (self)
                self->sendQuery(to, method, std::move(args), std::move(callback), timeoutMs, std::move(onSent));
        },
        [self, done = std::move(done)](const Lookup::Result &result) {
            if (done)
                done(result);
            if (self)
                emit self->changed();
        },
        this);
    lookup->setSalt(salt);
    // Nodes that answer at all nearly always answer quickly, so a lookup
    // need not wait the full query timeout: twice the round trip that 95%
    // of answers beat is enough, within bounds.
    const int p95 = replyRttQuantile(0.95);
    const int timeout = m_config.lookupTimeoutMs > 0    ? m_config.lookupTimeoutMs
                        : p95 > 0                       ? std::clamp(2 * p95, 800, RpcManager::DefaultTimeoutMs)
                                                        : Lookup::QueryTimeoutMs;
    const int slow = m_config.lookupSlowAfterMs > 0 ? m_config.lookupSlowAfterMs
                                                    : std::clamp(timeout / 2, 300, Lookup::SlowAfterMs);
    lookup->setTimeouts(slow, timeout);

    for (const RoutingNode &node : m_table.closest(target, Lookup::K * 2))
        lookup->addCandidate(node.id, node.endpoint);
    for (const krpc::CompactNode &node : extraCandidates)
        lookup->addCandidate(node.id, node.endpoint);

    m_lookups.append(lookup);
    lookup->start();
    return lookup;
}

void DhtNode::probe(const Endpoint &endpoint, const QByteArray &method, BValue::Dict arguments,
                    std::function<void(const RpcReply &)> done, int timeoutMs)
{
    sendQuery(endpoint, method, std::move(arguments), std::move(done), timeoutMs);
}

void DhtNode::findNode(const NodeId &target, Lookup::DoneFn done)
{
    startLookup(Lookup::Kind::FindNode, target, std::move(done));
}

void DhtNode::getPeers(const NodeId &infohash, Lookup::DoneFn done)
{
    startLookup(Lookup::Kind::GetPeers, infohash, std::move(done));
}

void DhtNode::getItem(const NodeId &target, const QByteArray &salt, Lookup::DoneFn done)
{
    startLookup(Lookup::Kind::GetItem, target, std::move(done), {}, salt);
}

void DhtNode::put(const PutRequest &request, std::function<void(int, int)> done)
{
    QPointer<DhtNode> self(this);
    // The same lookup that finds the closest nodes collects their write tokens.
    startLookup(
        Lookup::Kind::GetItem, request.target,
        [self, request, done](const Lookup::Result &result) {
            if (!self) {
                if (done)
                    done(0, 0);
                return;
            }
            auto pending = std::make_shared<int>(0);
            auto accepted = std::make_shared<int>(0);
            auto attempted = std::make_shared<int>(0);
            for (const Lookup::Contact &contact : result.closest) {
                if (contact.token.isEmpty())
                    continue;
                BValue::Dict args;
                args.emplace("token", BValue(contact.token));
                args.emplace("v", BValue::preEncoded(request.value));
                if (request.isMutable) {
                    args.emplace("k", BValue(request.publicKey));
                    args.emplace("seq", BValue(request.sequence));
                    args.emplace("sig", BValue(request.signature));
                    if (!request.salt.isEmpty())
                        args.emplace("salt", BValue(request.salt));
                    if (request.cas)
                        args.emplace("cas", BValue(*request.cas));
                }
                ++*pending;
                ++*attempted;
                self->sendQuery(contact.endpoint, "put", std::move(args),
                                [pending, accepted, attempted, done](const RpcReply &reply) {
                                    if (reply.status == RpcReply::Status::Response)
                                        ++*accepted;
                                    if (--*pending == 0 && done)
                                        done(*accepted, *attempted);
                                });
            }
            if (*pending == 0 && done)
                done(0, *attempted);
        },
        {}, request.salt);
}

void DhtNode::announce(const NodeId &infohash, quint16 port, bool impliedPort, std::function<void(int)> done)
{
    QPointer<DhtNode> self(this);
    getPeers(infohash, [self, infohash, port, impliedPort, done](const Lookup::Result &result) {
        if (!self) {
            if (done)
                done(0);
            return;
        }
        auto remaining = std::make_shared<int>(0);
        auto accepted = std::make_shared<int>(0);
        for (const Lookup::Contact &contact : result.closest) {
            if (contact.token.isEmpty())
                continue;
            BValue::Dict args;
            args.emplace("info_hash", BValue(infohash.toBytes()));
            args.emplace("port", BValue(qint64(port)));
            args.emplace("token", BValue(contact.token));
            if (impliedPort)
                args.emplace("implied_port", BValue(1));
            ++*remaining;
            self->sendQuery(contact.endpoint, "announce_peer", std::move(args),
                            [remaining, accepted, done](const RpcReply &reply) {
                                if (reply.status == RpcReply::Status::Response)
                                    ++*accepted;
                                if (--*remaining == 0 && done)
                                    done(*accepted);
                            });
        }
        if (*remaining == 0 && done)
            done(0);
    });
}

std::vector<krpc::CompactNode> DhtNode::closestNodes(const NodeId &target, int count) const
{
    std::vector<krpc::CompactNode> out;
    for (const RoutingNode &node : m_table.closest(target, count))
        out.push_back({node.id, node.endpoint});
    return out;
}

// --- Maintenance -----------------------------------------------------------

void DhtNode::onMaintenance()
{
    const qint64 now = nowMs();

    for (const RoutingNode &node : m_table.nodesNeedingPing(now, 3, PingIntervalMs)) {
        m_table.markPinged(node.endpoint, now);
        sendQuery(node.endpoint, "ping", {}, nullptr);
    }

    if (m_table.size() > 0 && !m_refreshLookup) {
        if (const auto bucket = m_table.staleBucket(now, BucketRefreshMs)) {
            m_table.touchBucket(*bucket, now);
            m_refreshLookup = startLookup(Lookup::Kind::FindNode, m_table.randomIdInBucket(*bucket), nullptr);
        }
    }

    m_limiter.prune(now);
    for (auto it = m_recentVerifications.begin(); it != m_recentVerifications.end();)
        it = (now - *it >= VerificationWindowMs) ? m_recentVerifications.erase(it) : std::next(it);
    m_lookups.removeIf([](const QPointer<Lookup> &l) { return l.isNull(); });
}

// --- Snapshots -------------------------------------------------------------

FamilySnapshot DhtNode::familySnapshot() const
{
    FamilySnapshot s;
    s.enabled = true;
    s.bound = m_socket && m_socket->state() == QAbstractSocket::BoundState;
    s.port = port();
    s.id = m_id;
    s.externalAddress = m_voter.consensus();
    s.bep42 = bep42::check(m_id, s.externalAddress);
    s.nodeCount = m_table.size();
    s.bucketCount = m_table.bucketCount();
    return s;
}

void DhtNode::appendNodeRows(std::vector<NodeRow> &rows, qint64 now) const
{
    for (const RoutingNode &node : m_table.allNodes()) {
        NodeRow row;
        row.family = m_config.family;
        row.endpoint = node.endpoint;
        row.id = node.id;
        row.hasId = true;
        row.source = NodeRow::Source::Routing;
        switch (RoutingTable::stateOf(node, now)) {
        case RoutingNode::State::Good: row.status = NodeRow::Status::Good; break;
        case RoutingNode::State::Questionable: row.status = NodeRow::Status::Questionable; break;
        case RoutingNode::State::Bad: row.status = NodeRow::Status::Bad; break;
        }
        row.rttMs = node.rttMs;
        row.lastSeenAgoMs = now - std::max(node.lastResponse, node.lastQuery);
        row.bep42 = bep42::check(node.id, node.endpoint.address);
        row.version = node.version;
        row.bucket = m_table.bucketIndexFor(node.id);
        row.sortKey = sortKeyFor(row.family, true, node.id, m_id, node.endpoint);
        rows.push_back(std::move(row));
    }

    for (auto it = m_seeds.constBegin(); it != m_seeds.constEnd(); ++it) {
        if (m_table.contains(it.key()))
            continue;
        const Seed &seed = it.value();
        NodeRow row;
        row.family = m_config.family;
        row.endpoint = it.key();
        row.id = seed.id;
        row.hasId = seed.hasId;
        row.source = seed.source == SeedSource::Bootstrap ? NodeRow::Source::Bootstrap : NodeRow::Source::Injected;
        switch (seed.state) {
        case Seed::State::Querying: row.status = NodeRow::Status::Querying; break;
        case Seed::State::Responded: row.status = NodeRow::Status::Responded; break;
        case Seed::State::NoResponse: row.status = NodeRow::Status::NoResponse; break;
        }
        row.rttMs = seed.rttMs;
        row.lastSeenAgoMs = now - seed.lastAttempt;
        row.bep42 = seed.hasId ? bep42::check(seed.id, it.key().address) : bep42::Status::Unknown;
        row.version = seed.version;
        row.sortKey = sortKeyFor(row.family, seed.hasId, seed.id, m_id, it.key());
        rows.push_back(std::move(row));
    }
}

EngineStats DhtNode::stats() const
{
    EngineStats s = m_stats;
    if (m_rpc) {
        s.queriesDelayed = m_rpc->delayedCount();
        s.queriesRefused = m_rpc->refusedCount();
        s.queriesWaiting = m_rpc->queuedCount();
        s.sendFailures = m_rpc->sendFailures();
    }
    s.activeLookups = 0;
    for (const QPointer<Lookup> &lookup : m_lookups) {
        if (lookup && !lookup->isDone())
            ++s.activeLookups;
    }
    return s;
}

} // namespace dht
