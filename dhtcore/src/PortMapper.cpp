#include "dhtcore/PortMapper.h"

#include "dhtcore/Endpoint.h"
#include "dhtcore/Gateway.h"
#include "dhtcore/Upnp.h"

#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QUdpSocket>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace dht {

PortMapper::PortMapper(QObject *parent)
    : QObject(parent)
    , m_retryTimer(this)
    , m_renewTimer(this)
{
    m_retryTimer.setSingleShot(true);
    m_renewTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &PortMapper::onRetryTimeout);
    connect(&m_renewTimer, &QTimer::timeout, this, [this] {
        if (m_protocol == Protocol::Upnp) {
            m_upnp->renew();
            return;
        }
        m_attempt = 0;
        sendRequest();
    });

    m_upnp = new UpnpIgd(this);
    connect(m_upnp, &UpnpIgd::progress, this, [this](const QString &message) {
        if (m_protocol != Protocol::Upnp || m_snapshot.state == PortMappingSnapshot::State::Mapped)
            return;
        m_snapshot.message = message;
        emit changed();
    });
    connect(m_upnp, &UpnpIgd::mapped, this, [this](quint16 port, quint32 lease, const QHostAddress &external) {
        if (m_protocol == Protocol::Upnp)
            mapped(port, lease, external);
    });
    connect(m_upnp, &UpnpIgd::failed, this, [this](const QString &message) {
        if (m_protocol == Protocol::Upnp)
            fallBack(message);
    });
}

PortMapper::~PortMapper()
{
    stopInternal(false);
}

void PortMapper::setGatewayOverride(const QHostAddress &address, quint16 port)
{
    m_gatewayOverride = normalizeAddress(address);
    m_gatewayOverridePort = port;
}

void PortMapper::setRetryDelays(const QList<int> &delaysMs)
{
    if (!delaysMs.isEmpty())
        m_retryDelays = delaysMs;
}

void PortMapper::setUpnpSearchTarget(const QHostAddress &address, quint16 port)
{
    m_upnp->setSearchTarget(address, port);
}

void PortMapper::setUpnpSearchDelays(const QList<int> &delaysMs)
{
    m_upnp->setSearchDelays(delaysMs);
}

QString PortMapper::protocolName() const
{
    switch (m_protocol) {
    case Protocol::Pcp: return QStringLiteral("PCP");
    case Protocol::NatPmp: return QStringLiteral("NAT-PMP");
    case Protocol::Upnp: return QStringLiteral("UPnP IGD");
    }
    return QString();
}

void PortMapper::start(quint16 internalPort)
{
    stopInternal(false);
    m_active = true;
    m_snapshot = PortMappingSnapshot{};
    m_snapshot.internalPort = internalPort;
    m_reasons.clear();

    const bool overridden = !m_gatewayOverride.isNull();
    m_gateway = overridden ? m_gatewayOverride : defaultGatewayIpv4();
    m_gatewayPort = overridden ? m_gatewayOverridePort : GatewayPort;
    m_snapshot.gateway = m_gateway;

    if (m_gateway.isNull()) {
        fail(QStringLiteral("No IPv4 default gateway found"));
        return;
    }

    // PCP requests carry the client address as the gateway sees it.
    {
        QUdpSocket probe;
        probe.connectToHost(m_gateway, m_gatewayPort);
        probe.waitForConnected(500);
        m_localAddress = normalizeAddress(probe.localAddress());
    }

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        fail(QStringLiteral("Could not open a socket for port mapping: %1").arg(m_socket->errorString()));
        return;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &PortMapper::onReadyRead);

    m_nonce = QByteArray(12, Qt::Uninitialized);
    QRandomGenerator::system()->generate(m_nonce.begin(), m_nonce.end());

    beginProtocol(Protocol::Pcp);
}

void PortMapper::stop()
{
    stopInternal(true);
}

void PortMapper::stopInternal(bool notify)
{
    // Best effort: ask the gateway to drop the mapping now rather than
    // leaving it to expire.
    if (m_snapshot.state == PortMappingSnapshot::State::Mapped) {
        if (m_protocol == Protocol::Upnp) {
            m_upnp->releaseBlocking(UpnpReleaseTimeoutMs);
        } else if (m_socket) {
            const QByteArray release = m_protocol == Protocol::Pcp ? buildPcpMap(0) : buildNatPmpMap(0);
            m_socket->writeDatagram(release, m_gateway, m_gatewayPort);
        }
    }
    m_upnp->cancel();

    m_retryTimer.stop();
    m_renewTimer.stop();

    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    const bool wasActive = m_active;
    m_active = false;
    m_snapshot = PortMappingSnapshot{};
    if (notify && wasActive)
        emit changed();
}

void PortMapper::beginProtocol(Protocol protocol)
{
    m_protocol = protocol;
    m_attempt = 0;
    m_snapshot.state = PortMappingSnapshot::State::Discovering;
    m_snapshot.protocol = protocolName();
    m_snapshot.message = QStringLiteral("Asking gateway %1 via %2").arg(m_gateway.toString(), protocolName());
    emit changed();
    if (protocol == Protocol::Upnp) {
        m_retryTimer.stop();
        m_upnp->start(m_gateway, m_localAddress, m_snapshot.internalPort, m_snapshot.externalPort, RequestedLifetime);
        return;
    }
    sendRequest();
}

void PortMapper::fallBack(const QString &reason)
{
    m_reasons << reason;
    if (m_protocol == Protocol::Pcp) {
        beginProtocol(Protocol::NatPmp);
        return;
    }
    if (m_protocol == Protocol::NatPmp) {
        beginProtocol(Protocol::Upnp);
        return;
    }
    fail(m_reasons.join(QLatin1Char(' ')));
}

void PortMapper::sendRequest()
{
    if (!m_socket)
        return;
    const QByteArray request = m_protocol == Protocol::Pcp ? buildPcpMap(RequestedLifetime)
                                                           : buildNatPmpMap(RequestedLifetime);
    m_socket->writeDatagram(request, m_gateway, m_gatewayPort);
    m_retryTimer.start(m_retryDelays.value(m_attempt, m_retryDelays.last()));
}

void PortMapper::onRetryTimeout()
{
    ++m_attempt;
    if (m_attempt < m_retryDelays.size()) {
        sendRequest();
        return;
    }
    if (m_protocol == Protocol::Pcp) {
        beginProtocol(Protocol::NatPmp);
        return;
    }
    fallBack(QStringLiteral("Gateway %1 did not answer PCP or NAT-PMP.").arg(m_gateway.toString()));
}

void PortMapper::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        if (normalizeAddress(datagram.senderAddress()) != m_gateway || datagram.senderPort() != m_gatewayPort)
            continue;
        const QByteArray data = datagram.data();
        if (data.size() < 2)
            continue;
        if (quint8(data[0]) == 2)
            handlePcp(data);
        else if (quint8(data[0]) == 0)
            handleNatPmp(data);
    }
}

void PortMapper::handlePcp(const QByteArray &data)
{
    if (m_protocol != Protocol::Pcp || data.size() < 24)
        return;
    const quint8 opcode = quint8(data[1]);
    if (!(opcode & 0x80) || (opcode & 0x7f) != 1)
        return;

    const int result = quint8(data[3]);
    if (result == 1) { // UNSUPP_VERSION
        beginProtocol(Protocol::NatPmp);
        return;
    }
    if (result != 0) {
        fallBack(QStringLiteral("Gateway refused the PCP mapping: %1.").arg(pcpResultName(result)));
        return;
    }
    if (data.size() < 60 || data.mid(24, 12) != m_nonce)
        return;

    const quint32 lifetime = qFromBigEndian<quint32>(data.constData() + 4);
    const quint16 externalPort = qFromBigEndian<quint16>(data.constData() + 42);
    Q_IPV6ADDR v6;
    std::memcpy(v6.c, data.constData() + 44, 16);
    mapped(externalPort, lifetime, normalizeAddress(QHostAddress(v6)));
}

void PortMapper::handleNatPmp(const QByteArray &data)
{
    if (m_protocol == Protocol::Pcp) {
        // A NAT-PMP-only gateway answers a PCP request with a version 0
        // "unsupported version" reply.
        beginProtocol(Protocol::NatPmp);
        return;
    }
    if (m_protocol != Protocol::NatPmp || data.size() < 4)
        return;

    const quint8 opcode = quint8(data[1]);
    const quint16 result = qFromBigEndian<quint16>(data.constData() + 2);

    if (opcode == 129) { // UDP mapping response
        if (result != 0) {
            fallBack(QStringLiteral("Gateway refused the NAT-PMP mapping: %1.").arg(natPmpResultName(result)));
            return;
        }
        if (data.size() < 16)
            return;
        const quint16 externalPort = qFromBigEndian<quint16>(data.constData() + 10);
        const quint32 lifetime = qFromBigEndian<quint32>(data.constData() + 12);
        mapped(externalPort, lifetime, QHostAddress());

        const char request[2] = {0, 0}; // external address request
        m_socket->writeDatagram(request, 2, m_gateway, m_gatewayPort);
    } else if (opcode == 128) { // external address response
        if (result != 0 || data.size() < 12)
            return;
        m_snapshot.externalAddress = QHostAddress(qFromBigEndian<quint32>(data.constData() + 8));
        emit changed();
    }
}

void PortMapper::mapped(quint16 externalPort, quint32 lifetime, const QHostAddress &externalAddress)
{
    m_retryTimer.stop();
    m_snapshot.state = PortMappingSnapshot::State::Mapped;
    m_snapshot.protocol = protocolName();
    m_snapshot.externalPort = externalPort;
    m_snapshot.lifetimeSeconds = lifetime;
    if (!externalAddress.isNull() && externalAddress != QHostAddress(QHostAddress::AnyIPv4))
        m_snapshot.externalAddress = externalAddress;
    m_snapshot.message = QStringLiteral("External port %1 mapped via %2").arg(externalPort).arg(protocolName());
    // A permanent UPnP mapping (lifetime 0) is still refreshed now and then,
    // in case the gateway restarts.
    const qint64 renewSeconds = lifetime == 0 ? UpnpIgd::PermanentRefreshSeconds : std::max<quint32>(30, lifetime / 2);
    m_renewTimer.start(int(renewSeconds * 1000));
    emit changed();
}

void PortMapper::fail(const QString &message)
{
    m_retryTimer.stop();
    m_renewTimer.stop();
    m_snapshot.state = PortMappingSnapshot::State::Failed;
    m_snapshot.message = message;
    emit changed();
}

QByteArray PortMapper::buildPcpMap(quint32 lifetime) const
{
    QByteArray d(60, '\0');
    d[0] = 2;  // version
    d[1] = 1;  // MAP request
    qToBigEndian<quint32>(lifetime, d.data() + 4);
    d[18] = char(0xff); // client address, IPv4-mapped
    d[19] = char(0xff);
    qToBigEndian<quint32>(m_localAddress.toIPv4Address(), d.data() + 20);
    std::memcpy(d.data() + 24, m_nonce.constData(), 12);
    d[36] = 17; // UDP
    qToBigEndian<quint16>(m_snapshot.internalPort, d.data() + 40);
    const quint16 suggested = m_snapshot.externalPort ? m_snapshot.externalPort : m_snapshot.internalPort;
    qToBigEndian<quint16>(suggested, d.data() + 42);
    d[54] = char(0xff); // suggested external address ::ffff:0.0.0.0 = no preference
    d[55] = char(0xff);
    return d;
}

QByteArray PortMapper::buildNatPmpMap(quint32 lifetime) const
{
    QByteArray d(12, '\0');
    d[1] = 1; // map UDP
    qToBigEndian<quint16>(m_snapshot.internalPort, d.data() + 4);
    const quint16 suggested = m_snapshot.externalPort ? m_snapshot.externalPort : m_snapshot.internalPort;
    qToBigEndian<quint16>(lifetime ? suggested : quint16(0), d.data() + 6);
    qToBigEndian<quint32>(lifetime, d.data() + 8);
    return d;
}

QString PortMapper::pcpResultName(int code)
{
    switch (code) {
    case 2: return QStringLiteral("not authorized");
    case 3: return QStringLiteral("malformed request");
    case 4: return QStringLiteral("unsupported opcode");
    case 5: return QStringLiteral("unsupported option");
    case 6: return QStringLiteral("malformed option");
    case 7: return QStringLiteral("network failure");
    case 8: return QStringLiteral("no resources");
    case 9: return QStringLiteral("unsupported protocol");
    case 10: return QStringLiteral("user quota exceeded");
    case 11: return QStringLiteral("cannot provide external address");
    case 12: return QStringLiteral("address mismatch");
    case 13: return QStringLiteral("excessive remote peers");
    default: return QStringLiteral("result code %1").arg(code);
    }
}

QString PortMapper::natPmpResultName(int code)
{
    switch (code) {
    case 2: return QStringLiteral("not authorized or refused");
    case 3: return QStringLiteral("network failure");
    case 4: return QStringLiteral("out of resources");
    case 5: return QStringLiteral("unsupported opcode");
    default: return QStringLiteral("result code %1").arg(code);
    }
}

} // namespace dht
