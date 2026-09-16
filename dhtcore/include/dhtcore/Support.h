#pragma once

#include "dhtcore/Bep44.h"
#include "dhtcore/Endpoint.h"
#include "dhtcore/NodeId.h"

#include <QByteArray>
#include <QHash>
#include <QHostAddress>

#include <optional>
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

struct ImmutableItem
{
    NodeId target;
    QByteArray value;  // bencoded, exactly as it arrived
    qint64 storedAt = 0;
};

struct MutableItem
{
    NodeId target;
    QByteArray publicKey;
    QByteArray salt;
    QByteArray signature;
    QByteArray value;  // bencoded, exactly as it arrived
    qint64 sequence = 0;
    qint64 storedAt = 0;
};

// BEP 44 items held for other people. Signatures are checked by the caller
// before anything reaches this class.
class ItemStorage
{
public:
    static constexpr int MaxItems = 1000;

    enum class PutResult { Stored, Refreshed, SequenceTooLow, CasMismatch, TooBig, SaltTooLong };

    PutResult putImmutable(const NodeId &target, const QByteArray &bencodedValue, qint64 now);
    PutResult putMutable(const MutableItem &item, std::optional<qint64> cas, qint64 now);

    const ImmutableItem *immutableItem(const NodeId &target) const;
    const MutableItem *mutableItem(const NodeId &target) const;

    void expire(qint64 now);
    void clear();

    int immutableCount() const { return int(m_immutable.size()); }
    int mutableCount() const { return int(m_mutable.size()); }
    std::vector<ImmutableItem> immutableSnapshot() const;
    std::vector<MutableItem> mutableSnapshot() const;

private:
    void makeRoom();

    QHash<NodeId, ImmutableItem> m_immutable;
    QHash<NodeId, MutableItem> m_mutable;
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

// Everything the engine may send, in bytes per second, shared by both
// address families. Sending may overdraw it by one datagram; the next send
// then waits until the balance is positive again. A limit of 0 means
// unlimited. Holds at most one second's worth, so an idle spell does not
// save up a large burst.
class SendBudget
{
public:
    void setLimit(qint64 bytesPerSecond, qint64 now);
    qint64 limit() const { return m_limit; }
    bool isLimited() const { return m_limit > 0; }

    bool available(qint64 now);
    void spend(qint64 bytes, qint64 now);

private:
    void refill(qint64 now);

    qint64 m_limit = 0;
    double m_balance = 0;
    qint64 m_last = 0;
};

} // namespace dht
