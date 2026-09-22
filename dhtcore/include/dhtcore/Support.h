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
    // Up to `max` stored infohashes, chosen at random (BEP 51).
    std::vector<NodeId> sampleInfohashes(int max) const;
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

// How many endpoints a second the engine may contact afresh, both address
// families together.
//
// What a home router struggles with is not traffic but the number of UDP
// conversations it has to keep track of: one entry per remote endpoint,
// held for its timeout, in a table only a few thousand entries deep. A
// query to an endpoint we have written to within the window reuses that
// entry and so costs nothing here, and replies cost nothing either, since
// the query being answered made an entry of its own on the way in.
//
// A limit of 0 means unlimited; endpoints are tracked either way, so the
// load can be shown. Holds at most one second's worth, so an idle spell
// does not save up a large burst.
class ContactBudget
{
public:
    // How long a router is taken to keep an entry. Longer than the usual 30
    // to 120 seconds, so neither the charging nor the count shown is
    // optimistic.
    static constexpr qint64 WindowMs = 180 * 1000;

    void setLimit(int contactsPerSecond, qint64 now);
    int limit() const { return m_limit; }
    bool isLimited() const { return m_limit > 0; }

    // Whether a datagram may go to `to` now.
    bool allows(const Endpoint &to, qint64 now);
    // Records one actually sent, which is what charges for a new endpoint.
    void record(const Endpoint &to, qint64 now);
    void prune(qint64 now);

    qint64 contacts() const { return m_contacts; }              // new endpoints, in total
    int tracked() const { return int(m_recent.size()); }        // entries a router would hold now

private:
    void refill(qint64 now);
    bool isLive(const Endpoint &to, qint64 now) const;

    QHash<Endpoint, qint64> m_recent;  // endpoint -> when we last wrote to it
    qint64 m_contacts = 0;
    int m_limit = 0;
    double m_balance = 0;
    qint64 m_last = 0;
};

} // namespace dht
