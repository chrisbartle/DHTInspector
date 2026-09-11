#include "dhtcore/Support.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <algorithm>
#include <chrono>

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

void RateLimiter::prune(qint64 now)
{
    const qint64 refillMs = qint64(m_burst / m_rate * 1000.0);
    for (auto it = m_buckets.begin(); it != m_buckets.end();)
        it = (now - it->last > refillMs) ? m_buckets.erase(it) : std::next(it);
}

} // namespace dht
