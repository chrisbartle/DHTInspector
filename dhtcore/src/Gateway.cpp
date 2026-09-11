#include "dhtcore/Gateway.h"

#if defined(Q_OS_WIN)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#elif defined(Q_OS_LINUX)
#  include <QFile>
#  include <QtEndian>
#endif

namespace dht {

#if defined(Q_OS_WIN)

QHostAddress defaultGatewayIpv4()
{
    // Ask for the route to an arbitrary public address. No packet is sent.
    MIB_IPFORWARDROW row{};
    if (GetBestRoute(htonl(0x08080808), 0, &row) != NO_ERROR)
        return {};
    const quint32 nextHop = ntohl(row.dwForwardNextHop);
    return nextHop == 0 ? QHostAddress() : QHostAddress(nextHop);
}

#elif defined(Q_OS_LINUX)

QHostAddress defaultGatewayIpv4()
{
    // /proc files report a size of zero, so read to EOF rather than
    // trusting atEnd().
    QFile file(QStringLiteral("/proc/net/route"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QList<QByteArray> lines = file.readAll().split('\n');

    static constexpr quint32 RtfUp = 0x1;
    static constexpr quint32 RtfGateway = 0x2;

    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> fields = lines[i].simplified().split(' ');
        if (fields.size() < 4)
            continue;
        bool okDestination = false;
        bool okGateway = false;
        bool okFlags = false;
        const quint32 destination = fields[1].toUInt(&okDestination, 16);
        const quint32 gateway = fields[2].toUInt(&okGateway, 16);
        const quint32 flags = fields[3].toUInt(&okFlags, 16);
        if (!okDestination || !okGateway || !okFlags)
            continue;
        if (destination == 0 && gateway != 0 && (flags & RtfUp) && (flags & RtfGateway)) {
            // The kernel prints the network-order value as a host-order
            // integer, so undo that to get the address.
            return QHostAddress(qFromBigEndian<quint32>(gateway));
        }
    }
    return {};
}

#else

QHostAddress defaultGatewayIpv4()
{
    return {};
}

#endif

} // namespace dht
