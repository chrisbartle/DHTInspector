#pragma once

#include "dhtcore/Snapshot.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QTimer>

class QUdpSocket;

namespace dht {

class UpnpIgd;

// Maps the DHT's UDP port on the IPv4 gateway. Tries PCP (RFC 6887) first,
// then NAT-PMP (RFC 6886), then UPnP IGD, and renews at half the granted
// lifetime.
class PortMapper : public QObject
{
    Q_OBJECT

public:
    static constexpr quint32 RequestedLifetime = 3600;
    static constexpr quint16 GatewayPort = 5351;
    // How long stopping may wait for a UPnP gateway to drop the mapping.
    static constexpr int UpnpReleaseTimeoutMs = 1500;

    explicit PortMapper(QObject *parent = nullptr);
    ~PortMapper() override;

    void start(quint16 internalPort);
    void stop();

    bool isActive() const { return m_active; }
    PortMappingSnapshot snapshot() const { return m_snapshot; }

    // Test hooks: talk to a fake gateway and shorten the retry schedule.
    void setGatewayOverride(const QHostAddress &address, quint16 port);
    void setRetryDelays(const QList<int> &delaysMs);
    void setUpnpSearchTarget(const QHostAddress &address, quint16 port);
    void setUpnpSearchDelays(const QList<int> &delaysMs);

signals:
    void changed();

private:
    enum class Protocol { Pcp, NatPmp, Upnp };

    void stopInternal(bool notify);
    void beginProtocol(Protocol protocol);
    // Moves on to the next protocol, or fails with every reason so far.
    void fallBack(const QString &reason);
    void sendRequest();
    void onRetryTimeout();
    void onReadyRead();
    void handlePcp(const QByteArray &data);
    void handleNatPmp(const QByteArray &data);
    void mapped(quint16 externalPort, quint32 lifetime, const QHostAddress &externalAddress);
    void fail(const QString &message);

    QByteArray buildPcpMap(quint32 lifetime) const;
    QByteArray buildNatPmpMap(quint32 lifetime) const;
    QString protocolName() const;

    static QString pcpResultName(int code);
    static QString natPmpResultName(int code);

    QUdpSocket *m_socket = nullptr;
    QTimer m_retryTimer;
    QTimer m_renewTimer;
    PortMappingSnapshot m_snapshot;
    Protocol m_protocol = Protocol::Pcp;
    QList<int> m_retryDelays{250, 500, 1000, 2000};
    int m_attempt = 0;
    QHostAddress m_gatewayOverride;
    quint16 m_gatewayOverridePort = GatewayPort;
    QHostAddress m_gateway;
    quint16 m_gatewayPort = GatewayPort;
    QHostAddress m_localAddress;
    QByteArray m_nonce;
    UpnpIgd *m_upnp = nullptr;
    QStringList m_reasons;
    bool m_active = false;
};

} // namespace dht
