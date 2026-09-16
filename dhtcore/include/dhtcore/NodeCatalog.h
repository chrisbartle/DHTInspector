#pragma once

#include "dhtcore/Endpoint.h"
#include "dhtcore/NodeId.h"

#include <QtGlobal>

#include <array>
#include <vector>

namespace dht {

// One node the network scan knows about. Kept to 80 bytes because the
// catalogue may hold millions of them.
struct CatalogEntry
{
    enum class State : quint8 {
        New,         // listed by another node, has not answered yet (asked at most once)
        Responsive,  // answered its most recent query
        Silent,      // never answered
        Gone,        // answered before, has stopped
        Unroutable,  // an address that cannot be reached, never asked
    };

    std::array<quint8, 16> address{};  // IPv4 stored as v4-mapped IPv6
    quint16 port = 0;
    State state = State::New;
    quint8 failures = 0;       // unanswered queries in a row
    NodeId id;
    std::array<quint8, 4> version{};
    quint8 versionLength = 0;  // length of "v" (up to 254); bytes past four are not kept
    quint8 bep42 = 0;          // bep42::Status, from the last answer
    quint16 rttMs = NoRtt;
    quint16 sightings = 0;     // times other nodes listed it, saturating
    quint16 inUse = 0;         // 0 for a free slot
    quint32 generation = 0;    // changes whenever the slot is reused
    // Timestamps in NodeCatalog ticks (see stamp()); 0 means never.
    quint32 firstSeen = 0;
    quint32 lastListed = 0;
    quint32 lastQueried = 0;
    quint32 lastAnswered = 0;
    quint32 answeringSince = 0;  // start of the current responsive spell
    quint32 flags = 0;           // reserved for feature results

    static constexpr quint16 NoRtt = 0xffff;

    bool isIPv4() const;
    Family family() const { return isIPv4() ? Family::IPv4 : Family::IPv6; }
    Endpoint endpoint() const;
    QHostAddress hostAddress() const;
    // The field as received, except that bytes past the fourth read as
    // zero; the length is kept, so an overlong field still shows as one.
    QByteArray versionBytes() const;
    void setVersion(const QByteArray &v);
};

// Every node the scan has come across, indexed by address and port. Slots
// are stable, so schedulers can hold a Ref and check it is still current.
// When full, a new node replaces an existing one, chosen from a random
// sample to favour the longest silent; that keeps eviction cheap at any
// size, at the cost of not always picking the single worst entry.
class NodeCatalog
{
public:
    using Slot = quint32;
    static constexpr Slot NoSlot = 0xffffffff;
    static constexpr int DefaultCap = 2'000'000;
    static constexpr int MaxCap = 50'000'000;
    static constexpr int TickMs = 100;

    struct Ref
    {
        Slot slot = NoSlot;
        quint32 generation = 0;
    };

    explicit NodeCatalog(int cap = DefaultCap);

    void clear();

    int size() const { return m_size; }
    int cap() const { return m_cap; }
    // Lowering the cap does not evict at once; trim() does, in batches.
    void setCap(int cap);
    // Evicts up to `maxEvictions` entries while over the cap.
    int trim(int maxEvictions);
    qint64 evicted() const { return m_evicted; }

    // Finds the entry for `endpoint`, adding it if new (evicting if full).
    // Returns NoSlot only when the cap is zero.
    Slot upsert(const Endpoint &endpoint, qint64 nowMs, bool *added = nullptr);
    Slot find(const Endpoint &endpoint) const;

    // References are invalidated by upsert(), which may grow storage.
    CatalogEntry &at(Slot slot) { return m_entries[slot]; }
    const CatalogEntry &at(Slot slot) const { return m_entries[slot]; }

    Ref ref(Slot slot) const { return {slot, m_entries[slot].generation}; }
    bool isCurrent(Ref ref) const;

    // Keep the per-state counts right: always change state through here.
    void setState(Slot slot, CatalogEntry::State state);
    int count(CatalogEntry::State state) const { return m_counts[int(state)]; }

    // Timestamps: ticks of TickMs since the catalogue was created, plus one
    // so that 0 can mean "never".
    quint32 stamp(qint64 nowMs) const;
    qint64 ageMs(quint32 stamp, qint64 nowMs) const;

    // What the catalogue occupies, and would at its cap.
    qint64 memoryBytes() const;
    static qint64 bytesPerEntry();

    template<typename F>
    void forEach(F f) const
    {
        for (Slot s = 0; s < Slot(m_entries.size()); ++s) {
            if (m_entries[s].inUse)
                f(s, m_entries[s]);
        }
    }

private:
    using Key = std::array<quint8, 18>;

    static Key keyOf(const Endpoint &endpoint);
    static Key keyOf(const CatalogEntry &entry);
    size_t hashOf(const Key &key) const;
    size_t findIndex(const Key &key) const;  // position in m_table, or npos
    void insertIndex(Slot slot);
    void eraseIndex(Slot slot);
    void growTable();
    Slot allocate();
    Slot pickVictim();
    void release(Slot slot);

    std::vector<CatalogEntry> m_entries;
    std::vector<Slot> m_free;
    std::vector<Slot> m_table;  // open addressing, linear probing
    int m_size = 0;
    int m_cap;
    qint64 m_evicted = 0;
    qint64 m_epochMs;
    size_t m_seed;
    quint32 m_nextGeneration = 1;
    std::array<int, 5> m_counts{};
};

} // namespace dht
