#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QString>

#include <optional>

namespace dht {

enum class Family { IPv4, IPv6 };

QString familyName(Family family);

// Strips IPv4-mapped IPv6 encoding and scope ids so the same host always
// compares and hashes equal.
QHostAddress normalizeAddress(const QHostAddress &address);

Family familyOf(const QHostAddress &address);

// Loopback, RFC 1918, link-local and IPv6 unique-local addresses.
bool isLocalAddress(const QHostAddress &address);

struct Endpoint
{
    QHostAddress address;
    quint16 port = 0;

    Endpoint() = default;
    Endpoint(const QHostAddress &addr, quint16 p) : address(normalizeAddress(addr)), port(p) {}

    bool isValid() const { return !address.isNull() && port != 0; }
    Family family() const { return familyOf(address); }

    QString toString() const;

    // BEP 5 compact peer info: 4+2 bytes for IPv4, 16+2 for IPv6.
    QByteArray toCompact() const;
    static std::optional<Endpoint> fromCompact(QByteArrayView bytes);

    friend bool operator==(const Endpoint &a, const Endpoint &b)
    {
        return a.port == b.port && a.address == b.address;
    }
};

size_t qHash(const Endpoint &endpoint, size_t seed = 0) noexcept;

// Why an endpoint handed to us by the network cannot be contacted.
enum class AddressProblem : quint8 {
    None,
    ZeroPort,     // port 0
    Unspecified,  // 0.0.0.0/8 or ::
    Local,        // loopback, private, link-local, unique-local
    Multicast,    // multicast or broadcast
    Reserved,     // documentation, benchmarking, shared (CGNAT), 240/4, IPv6 outside 2000::/3
};
constexpr int AddressProblemCount = 6;

QString addressProblemName(AddressProblem problem);
AddressProblem addressProblem(const Endpoint &endpoint, bool allowLocal);

// Whether an endpoint handed to us by the network is worth contacting:
// it has no AddressProblem. Local addresses are fine only when explicitly
// allowed (LAN testing).
bool isUsableRemote(const Endpoint &endpoint, bool allowLocal);

struct HostPort
{
    QString host;          // hostname or address literal, without brackets
    quint16 port = 0;
    QHostAddress literal;  // set when host is an address literal
};

// Parses "1.2.3.4:6881", "[2001:db8::1]:6881" or "host.example:6881".
std::optional<HostPort> parseHostPort(QStringView text, QString *error = nullptr);

} // namespace dht
