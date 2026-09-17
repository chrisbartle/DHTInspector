#include "dhtcore/Lookup.h"

#include "dhtcore/Bep44.h"
#include "dhtcore/Support.h"

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
    m_slowTimer.setInterval(SlowCheckMs);
    connect(&m_slowTimer, &QTimer::timeout, this, &Lookup::checkSlow);
}

void Lookup::setTimeouts(int slowAfterMs, int queryTimeoutMs)
{
    m_queryTimeoutMs = std::max(100, queryTimeoutMs);
    m_slowAfterMs = std::clamp(slowAfterMs, 100, m_queryTimeoutMs);
}

void Lookup::addCandidate(const NodeId &id, const Endpoint &endpoint, int hops)
{
    if (m_finished || id == m_self)
        return;
    if (endpoint.family() != m_family || !isUsableRemote(endpoint, m_allowLocal))
        return;
    if (m_seen.contains(endpoint))
        return;
    m_seen.insert(endpoint);

    Candidate candidate{id, endpoint, State::New, {}, hops};
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
    m_clock.start();
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
    int budget = std::min(Alpha - m_inFlight, MaxOutstanding - m_outstanding);
    int respondedAhead = 0;
    for (Candidate &c : m_candidates) {
        if (budget <= 0 || respondedAhead >= K)
            break;
        if (c.state == State::Responded) {
            ++respondedAhead;
        } else if (c.state == State::New && m_queries < MaxQueries) {
            c.state = State::InFlight;
            c.sentAtMs = 0;  // its clock starts when it actually goes out
            ++m_inFlight;
            ++m_outstanding;
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
        m_query(
            endpoint, method, std::move(args),
            [self, endpoint](const RpcReply &reply) {
                if (self)
                    self->onReply(endpoint, reply);
            },
            m_queryTimeoutMs, [self, endpoint] {
                if (self)
                    self->onSent(endpoint);
            });
    }

    if (m_finished)
        return;
    if (m_outstanding > 0 && !m_slowTimer.isActive())
        m_slowTimer.start();
    if (canFinish())
        finish();
}

bool Lookup::canFinish() const
{
    // Candidates are sorted by distance, so the first K responders are the
    // closest ones found. Anything still outstanding ahead of them could
    // yet turn out closer.
    int responded = 0;
    for (const Candidate &c : m_candidates) {
        if (responded >= K)
            break;
        if (c.state == State::Responded)
            ++responded;
        else if (c.state == State::InFlight || c.state == State::Slow)
            return false;
    }
    if (responded >= K)
        return true;
    if (m_outstanding > 0)
        return false;
    // Fewer than K answered: only done once there is nothing left to ask.
    if (m_queries >= MaxQueries)
        return true;
    return std::none_of(m_candidates.begin(), m_candidates.end(),
                        [](const Candidate &c) { return c.state == State::New; });
}

void Lookup::onSent(const Endpoint &endpoint)
{
    const auto it = std::find_if(m_candidates.begin(), m_candidates.end(),
                                 [&](const Candidate &c) { return c.endpoint == endpoint; });
    if (it != m_candidates.end() && it->state == State::InFlight)
        it->sentAtMs = nowMs();
}

void Lookup::checkSlow()
{
    if (m_finished)
        return;
    const qint64 now = nowMs();
    bool promoted = false;
    for (Candidate &c : m_candidates) {
        // A query still waiting its turn under the per-host limit has not
        // had its chance yet, so it is never counted slow.
        if (c.state == State::InFlight && c.sentAtMs > 0 && now - c.sentAtMs >= m_slowAfterMs) {
            c.state = State::Slow;
            --m_inFlight;
            promoted = true;
        }
    }
    if (m_inFlight == 0)
        m_slowTimer.stop();
    if (promoted)
        step();
}

void Lookup::onReply(const Endpoint &endpoint, const RpcReply &reply)
{
    if (m_finished)
        return;

    const auto it = std::find_if(m_candidates.begin(), m_candidates.end(),
                                 [&](const Candidate &c) { return c.endpoint == endpoint; });
    if (it == m_candidates.end() || (it->state != State::InFlight && it->state != State::Slow))
        return;
    if (it->state == State::InFlight)
        --m_inFlight;
    --m_outstanding;

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
    const int hops = it->hops + 1;  // `it` is invalidated by the inserts
    m_started = false;
    for (const krpc::CompactNode &node : found)
        addCandidate(node.id, node.endpoint, hops);
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
    m_slowTimer.stop();

    Result result;
    result.kind = m_kind;
    result.target = m_target;
    result.peers = m_peers;
    result.sightings = m_sightings;
    result.queried = m_queries;
    result.responded = m_responded;
    result.durationMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    result.itemFound = m_itemFound;
    result.itemIsMutable = m_itemIsMutable;
    result.itemValue = m_itemValue;
    result.itemPublicKey = m_itemPublicKey;
    result.itemSignature = m_itemSignature;
    result.itemSequence = m_itemSequence;
    for (const Candidate &c : m_candidates) {
        if (int(result.closest.size()) >= K)
            break;
        if (c.state != State::Responded)
            continue;
        if (result.closest.empty())
            result.hops = c.hops;
        result.closest.push_back({c.id, c.endpoint, c.token});
    }

    m_done = true;
    if (m_onDone)
        m_onDone(result);
    deleteLater();
}

} // namespace dht
