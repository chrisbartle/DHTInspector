#include "dhtcore/Support.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <utility>

namespace dht {

qint64 nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

namespace {

QByteArray randomBytes(int size)
{
    QByteArray bytes(size, Qt::Uninitialized);
    QRandomGenerator::system()->generate(bytes.begin(), bytes.end());
    return bytes;
}

QByteArray addressBytes(const QHostAddress &raw)
{
    const QHostAddress address = normalizeAddress(raw);
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v4 = address.toIPv4Address();
        const char b[4] = {char(v4 >> 24), char(v4 >> 16), char(v4 >> 8), char(v4)};
        return QByteArray(b, 4);
    }
    const Q_IPV6ADDR v6 = address.toIPv6Address();
    return QByteArray(reinterpret_cast<const char *>(v6.c), 16);
}

} // namespace

// --- TokenManager ----------------------------------------------------------

void TokenManager::rotateIfNeeded(qint64 now)
{
    if (m_rotatedAt < 0 || now - m_rotatedAt >= 2 * RotationMs) {
        m_current = randomBytes(8);
        m_previous = randomBytes(8);
        m_rotatedAt = now;
    } else if (now - m_rotatedAt >= RotationMs) {
        m_previous = m_current;
        m_current = randomBytes(8);
        m_rotatedAt = now;
    }
}

QByteArray TokenManager::tokenFor(const QByteArray &secret, const QHostAddress &address)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(secret);
    hash.addData(addressBytes(address));
    return hash.result().left(8);
}

QByteArray TokenManager::generate(const QHostAddress &address, qint64 now)
{
    rotateIfNeeded(now);
    return tokenFor(m_current, address);
}

bool TokenManager::validate(const QByteArray &token, const QHostAddress &address, qint64 now)
{
    rotateIfNeeded(now);
    return token == tokenFor(m_current, address) || token == tokenFor(m_previous, address);
}

// --- PeerStorage -----------------------------------------------------------

void PeerStorage::announce(const NodeId &infohash, const Endpoint &peer, qint64 now)
{
    auto it = m_entries.find(infohash);
    if (it == m_entries.end()) {
        if (int(m_entries.size()) >= MaxInfohashes) {
            auto oldest = m_entries.begin();
            for (auto e = m_entries.begin(); e != m_entries.end(); ++e) {
                if (e->lastAnnounce < oldest->lastAnnounce)
                    oldest = e;
            }
            m_peerCount -= int(oldest->peers.size());
            m_entries.erase(oldest);
        }
        it = m_entries.insert(infohash, Entry{});
    }

    Entry &entry = *it;
    entry.lastAnnounce = now;
    if (!entry.peers.contains(peer)) {
        if (int(entry.peers.size()) >= MaxPeersPerInfohash) {
            auto oldest = entry.peers.begin();
            for (auto p = entry.peers.begin(); p != entry.peers.end(); ++p) {
                if (p.value() < oldest.value())
                    oldest = p;
            }
            entry.peers.erase(oldest);
            --m_peerCount;
        }
        ++m_peerCount;
    }
    entry.peers.insert(peer, now);
}

std::vector<Endpoint> PeerStorage::peers(const NodeId &infohash, Family family, int max) const
{
    std::vector<Endpoint> out;
    const auto it = m_entries.constFind(infohash);
    if (it == m_entries.constEnd())
        return out;
    for (auto p = it->peers.constBegin(); p != it->peers.constEnd(); ++p) {
        if (p.key().family() == family)
            out.push_back(p.key());
    }
    if (int(out.size()) > max) {
        auto *rng = QRandomGenerator::global();
        for (int i = 0; i < max; ++i) {
            const int j = i + int(rng->bounded(quint32(out.size() - i)));
            std::swap(out[i], out[j]);
        }
        out.resize(max);
    }
    return out;
}

void PeerStorage::expire(qint64 now)
{
    for (auto e = m_entries.begin(); e != m_entries.end();) {
        for (auto p = e->peers.begin(); p != e->peers.end();) {
            if (now - p.value() >= PeerTtlMs) {
                p = e->peers.erase(p);
                --m_peerCount;
            } else {
                ++p;
            }
        }
        e = e->peers.isEmpty() ? m_entries.erase(e) : std::next(e);
    }
}

void PeerStorage::clear()
{
    m_entries.clear();
    m_peerCount = 0;
}

std::vector<StoredInfohash> PeerStorage::snapshot() const
{
    std::vector<StoredInfohash> out;
    out.reserve(m_entries.size());
    for (auto e = m_entries.constBegin(); e != m_entries.constEnd(); ++e) {
        StoredInfohash item;
        item.infohash = e.key();
        item.lastAnnounce = e->lastAnnounce;
        for (auto p = e->peers.constBegin(); p != e->peers.constEnd(); ++p)
            item.peers.push_back({p.key(), p.value()});
        out.push_back(std::move(item));
    }
    return out;
}

// --- ItemStorage -----------------------------------------------------------

void ItemStorage::makeRoom()
{
    if (immutableCount() + mutableCount() < MaxItems)
        return;
    // Drop whichever stored item is oldest.
    qint64 oldest = std::numeric_limits<qint64>::max();
    NodeId victim;
    bool victimIsMutable = false;
    for (auto it = m_immutable.constBegin(); it != m_immutable.constEnd(); ++it) {
        if (it->storedAt < oldest) {
            oldest = it->storedAt;
            victim = it.key();
            victimIsMutable = false;
        }
    }
    for (auto it = m_mutable.constBegin(); it != m_mutable.constEnd(); ++it) {
        if (it->storedAt < oldest) {
            oldest = it->storedAt;
            victim = it.key();
            victimIsMutable = true;
        }
    }
    if (victimIsMutable)
        m_mutable.remove(victim);
    else
        m_immutable.remove(victim);
}

ItemStorage::PutResult ItemStorage::putImmutable(const NodeId &target, const QByteArray &bencodedValue, qint64 now)
{
    if (bencodedValue.size() > bep44::MaxValueBytes)
        return PutResult::TooBig;

    const auto it = m_immutable.find(target);
    if (it != m_immutable.end()) {
        it->storedAt = now;
        return PutResult::Refreshed;
    }

    makeRoom();
    ImmutableItem item;
    item.target = target;
    item.value = bencodedValue;
    item.storedAt = now;
    m_immutable.insert(target, item);
    return PutResult::Stored;
}

ItemStorage::PutResult ItemStorage::putMutable(const MutableItem &item, std::optional<qint64> cas, qint64 now)
{
    if (item.value.size() > bep44::MaxValueBytes)
        return PutResult::TooBig;
    if (item.salt.size() > bep44::MaxSaltBytes)
        return PutResult::SaltTooLong;

    const auto it = m_mutable.find(item.target);
    if (it != m_mutable.end()) {
        // compare-and-swap: refuse if someone else has written since the
        // putter last read the item
        if (cas && it->sequence != *cas)
            return PutResult::CasMismatch;
        if (item.sequence < it->sequence)
            return PutResult::SequenceTooLow;
        if (item.sequence == it->sequence) {
            it->storedAt = now;
            return PutResult::Refreshed;
        }
        MutableItem stored = item;
        stored.storedAt = now;
        *it = stored;
        return PutResult::Stored;
    }

    if (cas)
        return PutResult::CasMismatch;

    makeRoom();
    MutableItem stored = item;
    stored.storedAt = now;
    m_mutable.insert(item.target, stored);
    return PutResult::Stored;
}

const ImmutableItem *ItemStorage::immutableItem(const NodeId &target) const
{
    const auto it = m_immutable.constFind(target);
    return it == m_immutable.constEnd() ? nullptr : &it.value();
}

const MutableItem *ItemStorage::mutableItem(const NodeId &target) const
{
    const auto it = m_mutable.constFind(target);
    return it == m_mutable.constEnd() ? nullptr : &it.value();
}

void ItemStorage::expire(qint64 now)
{
    for (auto it = m_immutable.begin(); it != m_immutable.end();)
        it = (now - it->storedAt >= bep44::ItemTtlMs) ? m_immutable.erase(it) : std::next(it);
    for (auto it = m_mutable.begin(); it != m_mutable.end();)
        it = (now - it->storedAt >= bep44::ItemTtlMs) ? m_mutable.erase(it) : std::next(it);
}

void ItemStorage::clear()
{
    m_immutable.clear();
    m_mutable.clear();
}

std::vector<ImmutableItem> ItemStorage::immutableSnapshot() const
{
    std::vector<ImmutableItem> out;
    out.reserve(m_immutable.size());
    for (auto it = m_immutable.constBegin(); it != m_immutable.constEnd(); ++it)
        out.push_back(it.value());
    return out;
}

std::vector<MutableItem> ItemStorage::mutableSnapshot() const
{
    std::vector<MutableItem> out;
    out.reserve(m_mutable.size());
    for (auto it = m_mutable.constBegin(); it != m_mutable.constEnd(); ++it)
        out.push_back(it.value());
    return out;
}

// --- ExternalIpVoter -------------------------------------------------------

bool ExternalIpVoter::addVote(const QHostAddress &rawVoter, const QHostAddress &rawReported, qint64 now)
{
    const QHostAddress voter = normalizeAddress(rawVoter);
    const QHostAddress reported = normalizeAddress(rawReported);
    if (voter.isNull() || reported.isNull())
        return false;

    m_votes.insert(voter, Vote{reported, now});
    if (int(m_votes.size()) > MaxVoters) {
        auto oldest = m_votes.begin();
        for (auto v = m_votes.begin(); v != m_votes.end(); ++v) {
            if (v->at < oldest->at)
                oldest = v;
        }
        m_votes.erase(oldest);
    }

    QHash<QHostAddress, int> tally;
    for (const Vote &v : std::as_const(m_votes))
        ++tally[v.reported];

    QHostAddress winner;
    int top = 0;
    int second = 0;
    for (auto t = tally.constBegin(); t != tally.constEnd(); ++t) {
        if (t.value() > top) {
            second = top;
            top = t.value();
            winner = t.key();
        } else if (t.value() > second) {
            second = t.value();
        }
    }

    if (top < MinVotes || top < 2 * second || winner == m_consensus)
        return false;
    m_consensus = winner;
    return true;
}

// --- RateLimiter -----------------------------------------------------------

bool RateLimiter::allow(const QHostAddress &address, qint64 now)
{
    auto it = m_buckets.find(address);
    if (it == m_buckets.end())
        it = m_buckets.insert(address, Bucket{m_burst, now});

    Bucket &b = *it;
    b.tokens = std::min(m_burst, b.tokens + double(now - b.last) * m_rate / 1000.0);
    b.last = now;
    if (b.tokens < 1.0)
        return false;
    b.tokens -= 1.0;
    return true;
}

std::vector<NodeId> PeerStorage::sampleInfohashes(int max) const
{
    std::vector<NodeId> all;
    all.reserve(m_entries.size());
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        all.push_back(it.key());
    std::shuffle(all.begin(), all.end(), std::mt19937(QRandomGenerator::global()->generate()));
    if (int(all.size()) > max)
        all.resize(size_t(std::max(0, max)));
    return all;
}

void SendBudget::setLimit(qint64 bytesPerSecond, qint64 now)
{
    bytesPerSecond = std::max<qint64>(0, bytesPerSecond);
    if (bytesPerSecond == m_limit)
        return;
    const bool wasLimited = isLimited();
    m_limit = bytesPerSecond;
    if (!wasLimited)
        m_balance = double(m_limit);  // start with a full second
    else
        m_balance = std::min(m_balance, double(m_limit));
    m_last = now;
}

void SendBudget::refill(qint64 now)
{
    if (now > m_last) {
        m_balance = std::min(double(m_limit), m_balance + double(now - m_last) * double(m_limit) / 1000.0);
        m_last = now;
    }
}

bool SendBudget::available(qint64 now)
{
    if (!isLimited())
        return true;
    refill(now);
    return m_balance > 0;
}

void SendBudget::spend(qint64 bytes, qint64 now)
{
    if (!isLimited())
        return;
    refill(now);
    m_balance -= double(bytes);
}

void RateLimiter::prune(qint64 now)
{
    const qint64 refillMs = qint64(m_burst / m_rate * 1000.0);
    for (auto it = m_buckets.begin(); it != m_buckets.end();)
        it = (now - it->last > refillMs) ? m_buckets.erase(it) : std::next(it);
}

} // namespace dht
