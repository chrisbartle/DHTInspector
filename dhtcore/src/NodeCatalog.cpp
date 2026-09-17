#include "dhtcore/NodeCatalog.h"

#include "dhtcore/Support.h"

#include <QHashFunctions>
#include <QRandomGenerator>

#include <algorithm>
#include <cstring>
#include <limits>

namespace dht {

namespace {

constexpr size_t npos = std::numeric_limits<size_t>::max();
constexpr size_t InitialTableSize = 1024;
constexpr int VictimSample = 16;

// Which entries to give up first when full: unreachable addresses, then
// nodes that never or no longer answer, then ones not yet asked, and
// responsive nodes last.
int evictionRank(CatalogEntry::State state)
{
    switch (state) {
    case CatalogEntry::State::Unroutable: return 4;
    case CatalogEntry::State::Silent: return 3;
    case CatalogEntry::State::Gone: return 3;
    case CatalogEntry::State::New: return 2;
    case CatalogEntry::State::Responsive: return 1;
    }
    return 0;
}

quint32 lastSign(const CatalogEntry &e)
{
    return std::max({e.lastAnswered, e.lastListed, e.firstSeen});
}

} // namespace

// --- CatalogEntry ------------------------------------------------------------

bool CatalogEntry::isIPv4() const
{
    static constexpr std::array<quint8, 12> mappedPrefix = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    return std::memcmp(address.data(), mappedPrefix.data(), mappedPrefix.size()) == 0;
}

QHostAddress CatalogEntry::hostAddress() const
{
    if (isIPv4()) {
        return QHostAddress((quint32(address[12]) << 24) | (quint32(address[13]) << 16)
                            | (quint32(address[14]) << 8) | quint32(address[15]));
    }
    return QHostAddress(address.data());
}

Endpoint CatalogEntry::endpoint() const
{
    return Endpoint(hostAddress(), port);
}

QByteArray CatalogEntry::versionBytes() const
{
    QByteArray out(versionLength, '\0');
    std::memcpy(out.data(), version.data(), std::min<size_t>(versionLength, version.size()));
    return out;
}

quint64 CatalogEntry::versionKey() const
{
    return (quint64(versionLength) << 32) | (quint64(version[0]) << 24) | (quint64(version[1]) << 16)
           | (quint64(version[2]) << 8) | quint64(version[3]);
}

QByteArray CatalogEntry::versionBytesForKey(quint64 key)
{
    const int length = int(key >> 32);
    QByteArray out(length, '\0');
    for (int i = 0; i < std::min(length, 4); ++i)
        out[i] = char((key >> (24 - 8 * i)) & 0xff);
    return out;
}

void CatalogEntry::setVersion(const QByteArray &v)
{
    versionLength = quint8(std::min<qsizetype>(v.size(), 254));
    version.fill(0);
    std::memcpy(version.data(), v.constData(), std::min<size_t>(size_t(v.size()), version.size()));
}

// --- NodeCatalog -------------------------------------------------------------

NodeCatalog::NodeCatalog(int cap)
    : m_cap(std::clamp(cap, 0, MaxCap))
    , m_epochMs(nowMs())
    , m_seed(QRandomGenerator::global()->generate())
{
}

void NodeCatalog::clear()
{
    // Swap with empties so the memory is actually returned.
    std::vector<CatalogEntry>().swap(m_entries);
    std::vector<Slot>().swap(m_free);
    std::vector<Slot>().swap(m_table);
    m_size = 0;
    m_evicted = 0;
    m_counts.fill(0);
    m_epochMs = nowMs();
}

void NodeCatalog::setCap(int cap)
{
    m_cap = std::clamp(cap, 0, MaxCap);
}

int NodeCatalog::trim(int maxEvictions)
{
    int evicted = 0;
    while (m_size > m_cap && evicted < maxEvictions) {
        release(pickVictim());
        ++m_evicted;
        ++evicted;
    }
    return evicted;
}

quint32 NodeCatalog::stamp(qint64 nowMs) const
{
    return quint32(std::max<qint64>(0, (nowMs - m_epochMs) / TickMs) + 1);
}

qint64 NodeCatalog::ageMs(quint32 then, qint64 nowMs) const
{
    if (then == 0)
        return std::numeric_limits<qint64>::max();
    return (qint64(stamp(nowMs)) - qint64(then)) * TickMs;
}

bool NodeCatalog::isCurrent(Ref ref) const
{
    return ref.slot < m_entries.size() && m_entries[ref.slot].inUse
           && m_entries[ref.slot].generation == ref.generation;
}

void NodeCatalog::setState(Slot slot, CatalogEntry::State state)
{
    CatalogEntry &e = m_entries[slot];
    if (e.state == state)
        return;
    --m_counts[int(e.state)];
    ++m_counts[int(state)];
    e.state = state;
}

qint64 NodeCatalog::memoryBytes() const
{
    return qint64(m_entries.capacity() * sizeof(CatalogEntry) + m_table.capacity() * sizeof(Slot)
                  + m_free.capacity() * sizeof(Slot));
}

qint64 NodeCatalog::bytesPerEntry()
{
    // The entry, plus its share of an index kept between two and four
    // times the entry count.
    return qint64(sizeof(CatalogEntry) + 4 * sizeof(Slot));
}

NodeCatalog::Key NodeCatalog::keyOf(const Endpoint &endpoint)
{
    Key key{};
    const QHostAddress &a = endpoint.address;
    if (a.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v4 = a.toIPv4Address();
        key[10] = 0xff;
        key[11] = 0xff;
        key[12] = quint8(v4 >> 24);
        key[13] = quint8(v4 >> 16);
        key[14] = quint8(v4 >> 8);
        key[15] = quint8(v4);
    } else {
        const Q_IPV6ADDR v6 = a.toIPv6Address();
        std::memcpy(key.data(), v6.c, 16);
    }
    key[16] = quint8(endpoint.port >> 8);
    key[17] = quint8(endpoint.port);
    return key;
}

NodeCatalog::Key NodeCatalog::keyOf(const CatalogEntry &entry)
{
    Key key{};
    std::memcpy(key.data(), entry.address.data(), 16);
    key[16] = quint8(entry.port >> 8);
    key[17] = quint8(entry.port);
    return key;
}

size_t NodeCatalog::hashOf(const Key &key) const
{
    return qHashBits(key.data(), key.size(), m_seed);
}

size_t NodeCatalog::findIndex(const Key &key) const
{
    if (m_table.empty())
        return npos;
    const size_t mask = m_table.size() - 1;
    for (size_t i = hashOf(key) & mask; m_table[i] != NoSlot; i = (i + 1) & mask) {
        if (keyOf(m_entries[m_table[i]]) == key)
            return i;
    }
    return npos;
}

NodeCatalog::Slot NodeCatalog::find(const Endpoint &endpoint) const
{
    const size_t i = findIndex(keyOf(endpoint));
    return i == npos ? NoSlot : m_table[i];
}

void NodeCatalog::insertIndex(Slot slot)
{
    const size_t mask = m_table.size() - 1;
    size_t i = hashOf(keyOf(m_entries[slot])) & mask;
    while (m_table[i] != NoSlot)
        i = (i + 1) & mask;
    m_table[i] = slot;
}

void NodeCatalog::eraseIndex(Slot slot)
{
    size_t i = findIndex(keyOf(m_entries[slot]));
    if (i == npos)
        return;
    // Backward-shift deletion keeps every remaining entry reachable from its
    // home position without tombstones.
    const size_t mask = m_table.size() - 1;
    size_t j = i;
    for (;;) {
        j = (j + 1) & mask;
        if (m_table[j] == NoSlot)
            break;
        const size_t home = hashOf(keyOf(m_entries[m_table[j]])) & mask;
        const bool staysPut = i <= j ? (i < home && home <= j) : (i < home || home <= j);
        if (!staysPut) {
            m_table[i] = m_table[j];
            i = j;
        }
    }
    m_table[i] = NoSlot;
}

void NodeCatalog::growTable()
{
    const size_t size = m_table.empty() ? InitialTableSize : m_table.size() * 2;
    std::vector<Slot>(size, NoSlot).swap(m_table);
    for (Slot s = 0; s < Slot(m_entries.size()); ++s) {
        if (m_entries[s].inUse)
            insertIndex(s);
    }
}

NodeCatalog::Slot NodeCatalog::allocate()
{
    if (!m_free.empty()) {
        const Slot s = m_free.back();
        m_free.pop_back();
        return s;
    }
    if (m_entries.size() == m_entries.capacity()) {
        // Grow gently and never past the cap, so a full catalogue does not
        // hold twice the memory it needs.
        const size_t wanted = std::max(m_entries.size() + m_entries.size() / 2, size_t(4096));
        m_entries.reserve(std::max(m_entries.size() + 1, std::min(wanted, size_t(m_cap))));
    }
    m_entries.emplace_back();
    return Slot(m_entries.size() - 1);
}

NodeCatalog::Slot NodeCatalog::pickVictim()
{
    Slot best = NoSlot;
    int bestRank = -1;
    quint32 bestSign = 0;
    const auto consider = [&](Slot s) {
        const CatalogEntry &e = m_entries[s];
        if (!e.inUse)
            return;
        const int rank = evictionRank(e.state);
        const quint32 sign = lastSign(e);
        if (rank > bestRank || (rank == bestRank && sign < bestSign)) {
            best = s;
            bestRank = rank;
            bestSign = sign;
        }
    };

    // Small catalogues are simply scanned; sampling is for large ones.
    if (m_entries.size() <= size_t(VictimSample) * 4) {
        for (Slot s = 0; s < Slot(m_entries.size()); ++s)
            consider(s);
        return best;
    }

    auto *rng = QRandomGenerator::global();
    int found = 0;
    for (int draws = 0; draws < VictimSample * 8 && found < VictimSample; ++draws) {
        const Slot s = Slot(rng->bounded(quint32(m_entries.size())));
        if (m_entries[s].inUse) {
            consider(s);
            ++found;
        }
    }
    if (best == NoSlot) {  // mostly free slots: fall back to a scan
        for (Slot s = 0; s < Slot(m_entries.size()); ++s)
            consider(s);
    }
    return best;
}

void NodeCatalog::release(Slot slot)
{
    CatalogEntry &e = m_entries[slot];
    eraseIndex(slot);
    --m_counts[int(e.state)];
    --m_size;
    e.inUse = 0;
    m_free.push_back(slot);
}

NodeCatalog::Slot NodeCatalog::upsert(const Endpoint &endpoint, qint64 nowMs, bool *added)
{
    if (added)
        *added = false;
    const Key key = keyOf(endpoint);
    if (const size_t i = findIndex(key); i != npos)
        return m_table[i];
    if (m_cap <= 0)
        return NoSlot;

    if (m_size >= m_cap) {
        release(pickVictim());
        ++m_evicted;
    }
    if (size_t(m_size + 1) * 2 > m_table.size())
        growTable();

    const Slot slot = allocate();
    CatalogEntry &e = m_entries[slot];
    e = CatalogEntry{};
    std::memcpy(e.address.data(), key.data(), 16);
    e.port = endpoint.port;
    e.inUse = 1;
    e.generation = m_nextGeneration++;
    if (m_nextGeneration == 0)
        m_nextGeneration = 1;
    e.firstSeen = stamp(nowMs);
    ++m_counts[int(CatalogEntry::State::New)];
    ++m_size;
    insertIndex(slot);
    if (added)
        *added = true;
    return slot;
}

} // namespace dht
