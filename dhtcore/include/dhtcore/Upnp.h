#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <optional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QUdpSocket;

namespace dht {

namespace upnp {

// What an SSDP search response says, if it is one.
struct SearchResponse
{
    QUrl location;
    QByteArray searchTarget;
};
std::optional<SearchResponse> parseSearchResponse(const QByteArray &datagram);

struct Service
{
    QByteArray type;  // e.g. urn:schemas-upnp-org:service:WANIPConnection:1
    QUrl controlUrl;  // absolute
};
// The WAN connection services in a device description, best first
// (WANIPConnection:2, WANIPConnection:1, WANPPPConnection:1). Relative
// control URLs are resolved against URLBase, or else `location`.
std::vector<Service> parseDescription(const QByteArray &xml, const QUrl &location);

QByteArray soapEnvelope(const QByteArray &serviceType, const QByteArray &action,
                        const QList<QPair<QByteArray, QString>> &arguments);

// A SOAP reply: the text of each element in the body, and for a fault
// the UPnP error code and description.
struct SoapResult
{
    bool ok = false;
    int errorCode = 0;
    QString errorDescription;
    QString value(const QByteArray &element) const;
    QList<QPair<QByteArray, QString>> values;
};
SoapResult parseSoapResponse(int httpStatus, const QByteArray &body);

} // namespace upnp

// Maps a UDP port on an Internet Gateway Device (UPnP IGD): finds it with
// SSDP, reads its description, and calls AddPortMapping on its WAN
// connection service. Used by PortMapper after PCP and NAT-PMP.
class UpnpIgd : public QObject
{
    Q_OBJECT

public:
    static constexpr quint16 SsdpPort = 1900;
    static constexpr int HttpTimeoutMs = 5000;
    static constexpr int MaxConflictRetries = 3;
    // Leases are renewed by PortMapper; a gateway that only allows
    // permanent mappings is refreshed this often instead.
    static constexpr quint32 PermanentRefreshSeconds = 30 * 60;

    explicit UpnpIgd(QObject *parent = nullptr);
    ~UpnpIgd() override;

    // Test hooks: search a given address instead of the SSDP multicast
    // group, and shorten the search schedule.
    void setSearchTarget(const QHostAddress &address, quint16 port);
    void setSearchDelays(const QList<int> &delaysMs);

    void start(const QHostAddress &gateway, const QHostAddress &localAddress, quint16 internalPort,
               quint16 suggestedExternalPort, quint32 leaseSeconds);
    // Adds the same mapping again, extending its lease.
    void renew();
    // Removes the mapping, waiting at most `timeoutMs`. Blocking, so it
    // can run while its owner is being torn down.
    void releaseBlocking(int timeoutMs);
    void cancel();

    QString deviceDescription() const { return m_location.toString(); }

signals:
    void progress(const QString &message);
    void mapped(quint16 externalPort, quint32 leaseSeconds, const QHostAddress &externalAddress);
    void failed(const QString &message);

private:
    enum class Stage { Idle, Searching, Describing, Mapping, Renewing, AskingAddress, Mapped };

    void search();
    void onSearchTimeout();
    void onDatagrams();
    void describe(const QUrl &location);
    void onDescription(QNetworkReply *reply, quint64 generation);
    void addMapping();
    void onAddMapping(QNetworkReply *reply, quint64 generation);
    void askAddress();
    void onAddress(QNetworkReply *reply, quint64 generation);
    void tryNextLocation(const QString &why);
    void fail(const QString &message);
    QNetworkReply *post(const QByteArray &action, const QList<QPair<QByteArray, QString>> &arguments);
    QList<QPair<QByteArray, QString>> addArguments() const;

    QNetworkAccessManager *m_http = nullptr;
    QUdpSocket *m_socket = nullptr;
    QTimer m_searchTimer;
    QList<int> m_searchDelays{1000, 1000, 1500};
    int m_searchAttempt = 0;
    QHostAddress m_searchOverride;
    quint16 m_searchOverridePort = SsdpPort;

    QHostAddress m_gateway;
    QHostAddress m_localAddress;
    quint16 m_internalPort = 0;
    quint16 m_externalPort = 0;
    quint32 m_lease = 0;
    int m_conflicts = 0;

    std::vector<QUrl> m_locations;  // found by the search, gateway first
    size_t m_nextLocation = 0;
    QUrl m_location;
    std::vector<upnp::Service> m_services;
    size_t m_serviceIndex = 0;
    QStringList m_problems;

    Stage m_stage = Stage::Idle;
    quint64 m_generation = 0;
    QPointer<QNetworkReply> m_reply;
};

} // namespace dht
