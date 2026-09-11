#include "dhtcore/Bep42.h"

#include "dhtcore/Crc32c.h"
#include "dhtcore/Endpoint.h"

#include <QRandomGenerator>

namespace dht::bep42 {

namespace {

// Top 21 bits of the CRC32C of the masked address prefix.
quint32 prefixCrc(const QHostAddress &raw, quint8 rand)
{
    const QHostAddress address = normalizeAddress(raw);
    quint8 ip[8] = {};
    int octets = 0;

    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        static constexpr quint8 mask[4] = {0x03, 0x0f, 0x3f, 0xff};
        const quint32 v4 = address.toIPv4Address();
        for (int i = 0; i < 4; ++i)
            ip[i] = quint8(v4 >> (24 - 8 * i)) & mask[i];
        octets = 4;
    } else {
        static constexpr quint8 mask[8] = {0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};
        const Q_IPV6ADDR v6 = address.toIPv6Address();
        for (int i = 0; i < 8; ++i)
            ip[i] = v6.c[i] & mask[i];
        octets = 8;
    }

    ip[0] |= quint8((rand & 0x7) << 5);
    return crc32c(QByteArrayView(reinterpret_cast<const char *>(ip), octets));
}

} // namespace

QString statusName(Status status)
{
    switch (status) {
    case Status::Unknown: return QStringLiteral("unknown");
    case Status::Compliant: return QStringLiteral("compliant");
    case Status::NonCompliant: return QStringLiteral("noncompliant");
    case Status::Exempt: return QStringLiteral("exempt");
    }
    return QStringLiteral("unknown");
}

bool isExempt(const QHostAddress &address)
{
    return isLocalAddress(address);
}

NodeId generate(const QHostAddress &address, quint8 rand)
{
    NodeId id = NodeId::random();
    const quint32 crc = prefixCrc(address, rand);
    const quint8 low = quint8(QRandomGenerator::system()->bounded(8));
    id[0] = quint8(crc >> 24);
    id[1] = quint8(crc >> 16);
    id[2] = quint8((crc >> 8) & 0xf8) | low;
    id[19] = rand;
    return id;
}

NodeId generate(const QHostAddress &address)
{
    return generate(address, quint8(QRandomGenerator::system()->bounded(256)));
}

bool isCompliant(const NodeId &id, const QHostAddress &address)
{
    const quint32 crc = prefixCrc(address, id[19]);
    return id[0] == quint8(crc >> 24)
        && id[1] == quint8(crc >> 16)
        && (id[2] & 0xf8) == quint8((crc >> 8) & 0xf8);
}

Status check(const NodeId &id, const QHostAddress &address)
{
    if (address.isNull())
        return Status::Unknown;
    if (isExempt(address))
        return Status::Exempt;
    return isCompliant(id, address) ? Status::Compliant : Status::NonCompliant;
}

} // namespace dht::bep42
