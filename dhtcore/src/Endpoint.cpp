#include "dhtcore/Endpoint.h"

#include <QHashFunctions>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>

namespace dht {

QString familyName(Family family)
{
    return family == Family::IPv4 ? QStringLiteral("IPv4") : QStringLiteral("IPv6");
}

QHostAddress normalizeAddress(const QHostAddress &address)
{
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        bool ok = false;
        const quint32 v4 = address.toIPv4Address(&ok);
        if (ok)
            return QHostAddress(v4);
        if (!address.scopeId().isEmpty()) {
            QHostAddress copy(address);
            copy.setScopeId(QString());
            return copy;
        }
    }
    return address;
}

Family familyOf(const QHostAddress &address)
{
    return normalizeAddress(address).protocol() == QAbstractSocket::IPv6Protocol ? Family::IPv6
                                                                                 : Family::IPv4;
}

bool isLocalAddress(const QHostAddress &raw)
{
    const QHostAddress address = normalizeAddress(raw);
    if (address.isLoopback() || address.isLinkLocal())
        return true;
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        static const QList<QPair<QHostAddress, int>> subnets = {
            QHostAddress::parseSubnet(QStringLiteral("10.0.0.0/8")),
            QHostAddress::parseSubnet(QStringLiteral("172.16.0.0/12")),
            QHostAddress::parseSubnet(QStringLiteral("192.168.0.0/16")),
            QHostAddress::parseSubnet(QStringLiteral("169.254.0.0/16")),
            QHostAddress::parseSubnet(QStringLiteral("127.0.0.0/8")),
        };
        for (const auto &subnet : subnets) {
            if (address.isInSubnet(subnet))
                return true;
        }
        return false;
    }
    return address.isUniqueLocalUnicast();
}

QString Endpoint::toString() const
{
    if (address.protocol() == QAbstractSocket::IPv6Protocol)
        return QStringLiteral("[%1]:%2").arg(address.toString()).arg(port);
    return QStringLiteral("%1:%2").arg(address.toString()).arg(port);
}

QByteArray Endpoint::toCompact() const
{
    QByteArray out;
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        out.resize(6);
        qToBigEndian<quint32>(address.toIPv4Address(), out.data());
        qToBigEndian<quint16>(port, out.data() + 4);
    } else {
        out.resize(18);
        const Q_IPV6ADDR v6 = address.toIPv6Address();
        std::copy(std::begin(v6.c), std::end(v6.c), out.data());
        qToBigEndian<quint16>(port, out.data() + 16);
    }
    return out;
}

std::optional<Endpoint> Endpoint::fromCompact(QByteArrayView bytes)
{
    if (bytes.size() == 6) {
        const quint32 ip = qFromBigEndian<quint32>(bytes.data());
        const quint16 p = qFromBigEndian<quint16>(bytes.data() + 4);
        return Endpoint(QHostAddress(ip), p);
    }
    if (bytes.size() == 18) {
        Q_IPV6ADDR v6;
        std::copy(bytes.begin(), bytes.begin() + 16, std::begin(v6.c));
        const quint16 p = qFromBigEndian<quint16>(bytes.data() + 16);
        return Endpoint(QHostAddress(v6), p);
    }
    return std::nullopt;
}

size_t qHash(const Endpoint &endpoint, size_t seed) noexcept
{
    return qHashMulti(seed, endpoint.address, endpoint.port);
}

QString addressProblemName(AddressProblem problem)
{
    switch (problem) {
    case AddressProblem::None: return QStringLiteral("None");
    case AddressProblem::ZeroPort: return QStringLiteral("Port 0");
    case AddressProblem::Unspecified: return QStringLiteral("Unspecified");
    case AddressProblem::Local: return QStringLiteral("Private or local");
    case AddressProblem::Multicast: return QStringLiteral("Multicast or broadcast");
    case AddressProblem::Reserved: return QStringLiteral("Reserved");
    }
    return QString();
}

AddressProblem addressProblem(const Endpoint &endpoint, bool allowLocal)
{
    const QHostAddress &a = endpoint.address;
    if (a.isNull() || a == QHostAddress(QHostAddress::AnyIPv4) || a == QHostAddress(QHostAddress::AnyIPv6))
        return AddressProblem::Unspecified;
    if (a.protocol() == QAbstractSocket::IPv4Protocol && (a.toIPv4Address() >> 24) == 0)
        return AddressProblem::Unspecified;
    if (a.isMulticast() || a.isBroadcast())
        return AddressProblem::Multicast;
    if (isLocalAddress(a)) {
        if (!allowLocal)
            return AddressProblem::Local;
    } else if (a.protocol() == QAbstractSocket::IPv4Protocol) {
        static const QList<QPair<QHostAddress, int>> reserved = {
            QHostAddress::parseSubnet(QStringLiteral("100.64.0.0/10")),
            QHostAddress::parseSubnet(QStringLiteral("192.0.0.0/24")),
            QHostAddress::parseSubnet(QStringLiteral("192.0.2.0/24")),
            QHostAddress::parseSubnet(QStringLiteral("198.18.0.0/15")),
            QHostAddress::parseSubnet(QStringLiteral("198.51.100.0/24")),
            QHostAddress::parseSubnet(QStringLiteral("203.0.113.0/24")),
            QHostAddress::parseSubnet(QStringLiteral("240.0.0.0/4")),
        };
        for (const auto &subnet : reserved) {
            if (a.isInSubnet(subnet))
                return AddressProblem::Reserved;
        }
    } else {
        // Every globally routed IPv6 address is in 2000::/3.
        const Q_IPV6ADDR b = a.toIPv6Address();
        const bool global = (b.c[0] & 0xe0) == 0x20;
        const bool documentation = b.c[0] == 0x20 && b.c[1] == 0x01 && b.c[2] == 0x0d && b.c[3] == 0xb8;
        if (!global || documentation)
            return AddressProblem::Reserved;
    }
    if (endpoint.port == 0)
        return AddressProblem::ZeroPort;
    return AddressProblem::None;
}

bool isUsableRemote(const Endpoint &endpoint, bool allowLocal)
{
    return addressProblem(endpoint, allowLocal) == AddressProblem::None;
}

std::optional<HostPort> parseHostPort(QStringView input, QString *error)
{
    auto fail = [error](const QString &message) -> std::optional<HostPort> {
        if (error)
            *error = message;
        return std::nullopt;
    };

    const QString text = input.trimmed().toString();
    if (text.isEmpty())
        return fail(QStringLiteral("Enter an address and port"));

    const bool bracketed = text.startsWith(u'[');
    QString host;
    QString portText;

    if (bracketed) {
        const qsizetype close = text.indexOf(u']');
        if (close < 0)
            return fail(QStringLiteral("Missing closing ']'"));
        host = text.mid(1, close - 1);
        const QString rest = text.mid(close + 1);
        if (!rest.startsWith(u':') || rest.size() == 1)
            return fail(QStringLiteral("Missing port"));
        portText = rest.mid(1);
    } else {
        const qsizetype colons = text.count(u':');
        if (colons == 0)
            return fail(QStringLiteral("Missing port"));
        if (colons > 1)
            return fail(QStringLiteral("IPv6 addresses need brackets, e.g. [2001:db8::1]:6881"));
        const qsizetype colon = text.indexOf(u':');
        host = text.left(colon);
        portText = text.mid(colon + 1);
    }

    if (host.isEmpty())
        return fail(QStringLiteral("Missing address"));

    static const QRegularExpression digits(QStringLiteral("^[0-9]{1,5}$"));
    if (!digits.match(portText).hasMatch())
        return fail(QStringLiteral("Port must be a number"));
    const int port = portText.toInt();
    if (port < 1 || port > 65535)
        return fail(QStringLiteral("Port must be between 1 and 65535"));

    HostPort out;
    out.host = host;
    out.port = quint16(port);

    static const QRegularExpression dottedQuad(QStringLiteral(
        "^(25[0-5]|2[0-4][0-9]|1?[0-9]?[0-9])(\\.(25[0-5]|2[0-4][0-9]|1?[0-9]?[0-9])){3}$"));

    if (bracketed) {
        QHostAddress address;
        if (!address.setAddress(host) || address.protocol() != QAbstractSocket::IPv6Protocol)
            return fail(QStringLiteral("Not a valid IPv6 address"));
        out.literal = normalizeAddress(address);
    } else if (dottedQuad.match(host).hasMatch()) {
        out.literal = QHostAddress(host);
    } else {
        static const QRegularExpression hostname(QStringLiteral(
            "^(?=.{1,253}$)[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"));
        static const QRegularExpression numeric(QStringLiteral("^[0-9.]+$"));
        if (numeric.match(host).hasMatch() || !hostname.match(host).hasMatch())
            return fail(QStringLiteral("Not a valid address or hostname"));
    }

    if (!out.literal.isNull()) {
        if (out.literal.isMulticast() || out.literal.isBroadcast()
            || out.literal == QHostAddress(QHostAddress::AnyIPv4)
            || out.literal == QHostAddress(QHostAddress::AnyIPv6)) {
            return fail(QStringLiteral("That address cannot be a DHT node"));
        }
    }

    return out;
}

} // namespace dht
