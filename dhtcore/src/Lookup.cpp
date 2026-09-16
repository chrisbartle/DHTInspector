#include "dhtcore/Lookup.h"

#include "dhtcore/Bep44.h"

#include <QPointer>

#include <algorithm>

namespace dht {

Lookup::Lookup(Kind kind, const NodeId &target, Family family, const NodeId &selfId, bool allowLocal,
               QueryFn query, DoneFn done, QObject *parent)
    : QObject(parent)
    , m_kind(kind)
    , m_target(target)
    , m_family(family)
    , m_self(selfId)
    , m_allowLocal(allowLocal)
    , m_query(std::move(query))
    , m_onDone(std::move(done))
{
}

void Lookup::addCandidate(const NodeId &id, const Endpoint &endpoint)
{
    if (m_finished || id == m_self)
        return;
    if (endpoint.family() != m_family || !isUsableRemote(endpoint, m_allowLocal))
        return;
    if (m_seen.contains(endpoint))
        return;
    m_seen.insert(endpoint);

    Candidate candidate{id, endpoint, State::New, {}};
    const auto pos = std::lower_bound(m_candidates.begin(), m_candidates.end(), candidate,
                                      [this](const Candidate &a, const Candidate &b) {
                                          return NodeId::closer(m_target, a.id, b.id);
                                      });
    m_candidates.insert(pos, std::move(candidate));

    // Trim the far end, but never drop a node we are waiting on.
    for (int i = int(m_candidates.size()) - 1; i >= 0 && int(m_candidates.size()) > MaxCandidates; --i) {
        if (m_candidates[i].state == State::New)
            m_candidates.erase(m_candidates.begin() + i);
    }

    if (m_started)
        step();
}

void Lookup::start()
{
    m_started = true;
    step();
}

void Lookup::step()
{
    if (m_finished)
        return;

    const QByteArray method = m_kind == Kind::FindNode ? QByteArray("find_node")
                              : m_kind == Kind::GetPeers ? QByteArray("get_peers")
                                                         : QByteArray("get");
    const QByteArray key = m_kind == Kind::GetPeers ? QByteArray("info_hash") : QByteArray("target");

    // Pick what to send first; sending can call back synchronously and
    // mutate m_candidates.
    std::vector<Endpoint> toSend;
    int budget = Alpha - m_inFlight;
    int respondedAhead = 0;
    for (Candidate &c : m_candidates) {
        if (budget <= 0 || respondedAhead >= K)
            break;
        if (c.state == State::Responded) {
            ++respondedAhead;
        } else if (c.state == State::New && m_queries < MaxQueries) {
            c.state = State::InFlight;
            ++m_inFlight;
            ++m_queries;
            --budget;
            toSend.push_back(c.endpoint);
        }
    }

    for (const Endpoint &endpoint : toSend) {
        if (m_finished)
            return;
        BValue::Dict args;
        args.emplace(key, m_target.toBytes());
        QPointer<Lookup> self(this);
        m_query(endpoint, method, std::move(args), [self, endpoint](const RpcReply &reply) {
            if (self)
                self->onReply(endpoint, reply);
        });
    }

    if (!m_finished && m_inFlight == 0)
        finish();
}

void Lookup::onReply(const Endpoint &endpoint, const RpcReply &reply)
{
    if (m_finished)
        return;

    const auto it = std::find_if(m_candidates.begin(), m_candidates.end(),
                                 [&](const Candidate &c) { return c.endpoint == endpoint; });
    if (it == m_candidates.end() || it->state != State::InFlight)
        return;
    --m_inFlight;

    if (reply.status != RpcReply::Status::Response) {
        it->state = State::Failed;
        step();
        return;
    }

    it->state = State::Responded;
    ++m_responded;
    const BValue &body = reply.message.body;
    if (const auto token = body.stringAt("token"))
        it->token = *token;

    const QByteArray nodesKey = m_family == Family::IPv4 ? QByteArray("nodes") : QByteArray("nodes6");
    std::vector<krpc::CompactNode> found;
    if (const auto nodes = body.stringAt(nodesKey))
        found = krpc::decodeNodes(*nodes, m_family).nodes;

    if (m_kind == Kind::GetItem)
        collectItem(reply);

    if (m_kind == Kind::GetPeers) {
        for (const Endpoint &peer : krpc::decodePeers(body.listAt("values"), m_family)) {
            if (int(m_sightings.size()) < MaxSightings)
                m_sightings.push_back({peer, endpoint});
            if (!m_peerSet.contains(peer)) {
                m_peerSet.insert(peer);
                m_peers.push_back(peer);
            }
        }
    }

    // addCandidate() steps on its own once started; batch the inserts first.
    const bool wasStarted = m_started;
    m_started = false;
    for (const krpc::CompactNode &node : found)
        addCandidate(node.id, node.endpoint);
    m_started = wasStarted;

    step();
}

void Lookup::collectItem(const RpcReply &reply)
{
    const BValue &body = reply.message.body;
    const BValue *value = body.find("v");
    if (!value)
        return;
    // BEP 44 hashes and signs the value's bytes as sent, so take them from the
    // datagram rather than re-encoding.
    const QByteArray raw = value->rawSpan(reply.datagram);
    if (raw.isEmpty())
        return;

    const auto key = body.stringAt("k");
    const auto signature = body.stringAt("sig");
    const auto sequence = body.integerAt("seq");

    if (key && signature && sequence) {
        if (key->size() != bep44::PublicKeyBytes || signature->size() != bep44::SignatureBytes)
            return;
        if (bep44::mutableTarget(*key, m_salt) != m_target)
            return;
        if (*sequence <= m_itemSequence)
            return;
        if (!ed25519::verify(*signature, bep44::signingBuffer(m_salt, *sequence, raw), *key).value_or(false))
            return;
        m_itemFound = true;
        m_itemIsMutable = true;
        m_itemValue = raw;
        m_itemPublicKey = *key;
        m_itemSignature = *signature;
        m_itemSequence = *sequence;
        return;
    }

    if (!m_itemFound && bep44::immutableTarget(raw) == m_target) {
        m_itemFound = true;
        m_itemIsMutable = false;
        m_itemValue = raw;
    }
}

void Lookup::finish()
{
    m_finished = true;

    Result result;
    result.kind = m_kind;
    result.target = m_target;
    result.peers = m_peers;
    result.sightings = m_sightings;
    result.queried = m_queries;
    result.responded = m_responded;
    result.itemFound = m_itemFound;
    result.itemIsMutable = m_itemIsMutable;
    result.itemValue = m_itemValue;
    result.itemPublicKey = m_itemPublicKey;
    result.itemSignature = m_itemSignature;
    result.itemSequence = m_itemSequence;
    for (const Candidate &c : m_candidates) {
        if (int(result.closest.size()) >= K)
            break;
        if (c.state == State::Responded)
            result.closest.push_back({c.id, c.endpoint, c.token});
    }

    m_done = true;
    if (m_onDone)
        m_onDone(result);
    deleteLater();
}

} // namespace dht
