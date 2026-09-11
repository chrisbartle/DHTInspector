#pragma once

#include "dhtcore/Endpoint.h"
#include "dhtcore/NodeId.h"

#include <QByteArray>
#include <QHash>
#include <QHostAddress>

#include <vector>

namespace dht {

// Monotonic milliseconds. All engine timestamps use this clock.
qint64 nowMs();

// BEP 5 announce tokens: a hash of a rotating secret and the requester's
// address. Tokens from the current and previous secret are accepted, which
// gives the ten-minute validity window the spec calls for.
class TokenManager
{
public:
    static constexpr qint64 RotationMs = 5 * 60 * 1000;

    QByteArray generate(const QHostAddress &address, qint64 now);
    bool validate(const QByteArray &token, const QHostAddress &address, qint64 now);

private:
    void rotateIfNeeded(qint64 now);
    static QByteArray tokenFor(const QByteArray &secret, const QHostAddress &address);

    QByteArray m_current;
    QByteArray m_previous;
    qint64 m_rotatedAt = -1;
};

struct StoredPeer
{
    Endpoint endpoint;
    qint64 announcedAt = 0;
};

struct StoredInfohash
{
    NodeId infohash;
    qint64 lastAnnounce = 0;
    std::vector<StoredPeer> peers;
};

// Peers announced to this node via announce_peer.
class PeerStorage
{
public:
    static constexpr qint64 PeerTtlMs = 30 * 60 * 1000;
    static constexpr int MaxInfohashes = 5000;
    static constexpr int MaxPeersPerInfohash = 500;

    void announce(const NodeId &infohash, const Endpoint &peer, qint64 now);

    // A random selection of up to `max` peers of the given family.
    std::vector<Endpoint> peers(const NodeId &infohash, Family family, int max) const;

    void expire(qint64 now);
    void clear();

    int infohashCount() const { return int(m_entries.size()); }
    int peerCount() const { return m_peerCount; }
    std::vector<StoredInfohash> snapshot() const;

private:
    struct Entry
    {
        QHash<Endpoint, qint64> peers;
        qint64 lastAnnounce = 0;
    };

    QHash<NodeId, Entry> m_entries;
    int m_peerCount = 0;
};

// Establishes our external address from the "ip" field other nodes put in
// their responses (BEP 42). One vote per remote address, so a single node
// cannot swing the result.
class ExternalIpVoter
{
public:
    static constexpr int MaxVoters = 50;
    static constexpr int MinVotes = 3;

    // Returns true when the consensus address changes.
    bool addVote(const QHostAddress &voter, const QHostAddress &reported, qint64 now);

    QHostAddress consensus() const { return m_consensus; }
    int voterCount() const { return int(m_votes.size()); }

private:
    struct Vote
    {
        QHostAddress reported;
        qint64 at = 0;
    };

    QHash<QHostAddress, Vote> m_votes;
    QHostAddress m_consensus;
};

// Token bucket per remote address.
class RateLimiter
{
public:
    RateLimiter(double ratePerSecond, double burst) : m_rate(ratePerSecond), m_burst(burst) {}

    bool allow(const QHostAddress &address, qint64 now);
    void prune(qint64 now);

private:
    struct Bucket
    {
        double tokens = 0;
        qint64 last = 0;
    };

    QHash<QHostAddress, Bucket> m_buckets;
    double m_rate;
    double m_burst;
};

} // namespace dht
