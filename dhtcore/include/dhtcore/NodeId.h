#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QHashFunctions>
#include <QString>

#include <array>
#include <compare>
#include <optional>

namespace dht {

// A 160-bit Kademlia identifier. Used for node IDs and info-hashes alike.
class NodeId
{
public:
    static constexpr int Size = 20;
    static constexpr int Bits = Size * 8;

    NodeId() = default;

    static NodeId random();
    static std::optional<NodeId> fromBytes(QByteArrayView bytes);
    static std::optional<NodeId> fromHex(QStringView hex);

    QByteArray toBytes() const;
    QString toHex() const;

    const quint8 *data() const { return m_bytes.data(); }
    quint8 *data() { return m_bytes.data(); }
    quint8 operator[](int i) const { return m_bytes[i]; }
    quint8 &operator[](int i) { return m_bytes[i]; }

    NodeId operator^(const NodeId &other) const;

    // Number of leading bits a and b share.
    static int commonPrefixLength(const NodeId &a, const NodeId &b);

    // True if a is strictly closer to target than b under the XOR metric.
    static bool closer(const NodeId &target, const NodeId &a, const NodeId &b);

    // A random ID sharing exactly `prefixBits` leading bits with `base`
    // (bit `prefixBits` is flipped). With exact=false the remaining bits,
    // including bit `prefixBits`, are random.
    static NodeId randomWithPrefix(const NodeId &base, int prefixBits, bool exact);

    friend bool operator==(const NodeId &, const NodeId &) = default;
    friend std::strong_ordering operator<=>(const NodeId &, const NodeId &) = default;

private:
    std::array<quint8, Size> m_bytes{};
};

size_t qHash(const NodeId &id, size_t seed = 0) noexcept;

} // namespace dht
