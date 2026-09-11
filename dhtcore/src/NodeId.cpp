#include "dhtcore/NodeId.h"

#include <QRandomGenerator>

#include <bit>

namespace dht {

NodeId NodeId::random()
{
    NodeId id;
    QRandomGenerator::system()->generate(id.m_bytes.begin(), id.m_bytes.end());
    return id;
}

std::optional<NodeId> NodeId::fromBytes(QByteArrayView bytes)
{
    if (bytes.size() != Size)
        return std::nullopt;
    NodeId id;
    std::copy(bytes.begin(), bytes.end(), id.m_bytes.begin());
    return id;
}

std::optional<NodeId> NodeId::fromHex(QStringView hex)
{
    if (hex.size() != Size * 2)
        return std::nullopt;
    const QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
    return fromBytes(bytes);
}

QByteArray NodeId::toBytes() const
{
    return QByteArray(reinterpret_cast<const char *>(m_bytes.data()), Size);
}

QString NodeId::toHex() const
{
    return QString::fromLatin1(toBytes().toHex());
}

NodeId NodeId::operator^(const NodeId &other) const
{
    NodeId out;
    for (int i = 0; i < Size; ++i)
        out.m_bytes[i] = m_bytes[i] ^ other.m_bytes[i];
    return out;
}

int NodeId::commonPrefixLength(const NodeId &a, const NodeId &b)
{
    for (int i = 0; i < Size; ++i) {
        const quint8 x = a.m_bytes[i] ^ b.m_bytes[i];
        if (x != 0)
            return i * 8 + std::countl_zero(x);
    }
    return Bits;
}

bool NodeId::closer(const NodeId &target, const NodeId &a, const NodeId &b)
{
    for (int i = 0; i < Size; ++i) {
        const quint8 da = a.m_bytes[i] ^ target.m_bytes[i];
        const quint8 db = b.m_bytes[i] ^ target.m_bytes[i];
        if (da != db)
            return da < db;
    }
    return false;
}

NodeId NodeId::randomWithPrefix(const NodeId &base, int prefixBits, bool exact)
{
    NodeId id = random();
    prefixBits = std::clamp(prefixBits, 0, Bits);
    for (int bit = 0; bit < prefixBits; ++bit) {
        const int byte = bit / 8;
        const quint8 mask = quint8(0x80 >> (bit % 8));
        id.m_bytes[byte] = (id.m_bytes[byte] & ~mask) | (base.m_bytes[byte] & mask);
    }
    if (exact && prefixBits < Bits) {
        const int byte = prefixBits / 8;
        const quint8 mask = quint8(0x80 >> (prefixBits % 8));
        id.m_bytes[byte] = (id.m_bytes[byte] & ~mask) | (~base.m_bytes[byte] & mask);
    }
    return id;
}

size_t qHash(const NodeId &id, size_t seed) noexcept
{
    return qHashBits(id.data(), NodeId::Size, seed);
}

} // namespace dht
