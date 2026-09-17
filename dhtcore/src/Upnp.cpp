#include "dhtcore/Upnp.h"

#include "dhtcore/Endpoint.h"

#include <QDeadlineTimer>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QXmlStreamReader>

#include <algorithm>
#include <utility>

namespace dht {

namespace upnp {

namespace {

constexpr const char *SsdpGroup = "239.255.255.250";

int serviceRank(const QByteArray &type)
{
    if (type == "urn:schemas-upnp-org:service:WANIPConnection:2")
        return 0;
    if (type == "urn:schemas-upnp-org:service:WANIPConnection:1")
        return 1;
    if (type == "urn:schemas-upnp-org:service:WANPPPConnection:1")
        return 2;
    return 3;
}

bool isWanConnection(const QByteArray &type)
{
    return type.startsWith("urn:schemas-upnp-org:service:WANIPConnection:")
           || type.startsWith("urn:schemas-upnp-org:service:WANPPPConnection:");
}

} // namespace

std::optional<SearchResponse> parseSearchResponse(const QByteArray &datagram)
{
    QList<QByteArray> lines = datagram.split('\n');
    if (lines.isEmpty())
        return std::nullopt;
    const QList<QByteArray> status = lines.takeFirst().trimmed().split(' ');
    if (status.size() < 2 || !status[0].toUpper().startsWith("HTTP/1.") || status[1] != "200")
        return std::nullopt;

    SearchResponse r;
    for (const QByteArray &raw : std::as_const(lines)) {
        const QByteArray line = raw.trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QByteArray name = line.left(colon).trimmed().toUpper();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "LOCATION")
            r.location = QUrl::fromEncoded(value);
        else if (name == "ST")
            r.searchTarget = value;
    }
    if (!r.location.isValid() || r.location.scheme() != QLatin1String("http") || r.location.host().isEmpty())
        return std::nullopt;
    return r;
}

std::vector<Service> parseDescription(const QByteArray &xml, const QUrl &location)
{
    QXmlStreamReader reader(xml);
    QUrl base;
    std::vector<std::pair<QByteArray, QByteArray>> found;  // type, control URL
    bool inService = false;
    QByteArray type;
    QByteArray control;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QStringView name = reader.name();
            if (name == QLatin1String("URLBase")) {
                base = QUrl(reader.readElementText().trimmed());
            } else if (name == QLatin1String("service")) {
                inService = true;
                type.clear();
                control.clear();
            } else if (inService && name == QLatin1String("serviceType")) {
                type = reader.readElementText().trimmed().toUtf8();
            } else if (inService && name == QLatin1String("controlURL")) {
                control = reader.readElementText().trimmed().toUtf8();
            }
        } else if (reader.isEndElement() && reader.name() == QLatin1String("service")) {
            inService = false;
            if (isWanConnection(type) && !control.isEmpty())
                found.emplace_back(type, control);
        }
    }

    const QUrl root = base.isValid() && !base.isRelative() ? base : location;
    std::vector<Service> services;
    for (const auto &[t, c] : found)
        services.push_back({t, root.resolved(QUrl::fromEncoded(c))});
    std::stable_sort(services.begin(), services.end(),
                     [](const Service &a, const Service &b) { return serviceRank(a.type) < serviceRank(b.type); });
    return services;
}

QByteArray soapEnvelope(const QByteArray &serviceType, const QByteArray &action,
                        const QList<QPair<QByteArray, QString>> &arguments)
{
    QByteArray body = "<?xml version=\"1.0\"?>\r\n"
                      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>";
    body += "<u:" + action + " xmlns:u=\"" + serviceType + "\">";
    for (const auto &[name, value] : arguments)
        body += "<" + name + ">" + value.toHtmlEscaped().toUtf8() + "</" + name + ">";
    body += "</u:" + action + "></s:Body></s:Envelope>\r\n";
    return body;
}

QString SoapResult::value(const QByteArray &element) const
{
    for (const auto &[name, text] : values) {
        if (name == element)
            return text;
    }
    return QString();
}

SoapResult parseSoapResponse(int httpStatus, const QByteArray &body)
{
    SoapResult r;
    bool fault = false;
    struct Open
    {
        QByteArray name;
        QString text;
        bool hasChildren = false;
    };
    std::vector<Open> stack;
    QXmlStreamReader reader(body);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (!stack.empty())
                stack.back().hasChildren = true;
            stack.push_back({reader.name().toUtf8(), QString(), false});
            fault = fault || stack.back().name == "Fault";
        } else if (reader.isCharacters() && !stack.empty()) {
            stack.back().text += reader.text();
        } else if (reader.isEndElement() && !stack.empty()) {
            // Leaf elements only: the values, not their containers.
            const Open e = stack.back();
            stack.pop_back();
            if (e.hasChildren)
                continue;
            const QString text = e.text.trimmed();
            r.values.append({e.name, text});
            if (e.name == "errorCode")
                r.errorCode = text.toInt();
            else if (e.name == "errorDescription")
                r.errorDescription = text;
        }
    }
    r.ok = httpStatus == 200 && !fault && !reader.hasError();
    return r;
}

} // namespace upnp

UpnpIgd::UpnpIgd(QObject *parent)
    : QObject(parent)
    , m_searchTimer(this)
{
    m_searchTimer.setSingleShot(true);
    connect(&m_searchTimer, &QTimer::timeout, this, &UpnpIgd::onSearchTimeout);
}

UpnpIgd::~UpnpIgd()
{
    cancel();
}

void UpnpIgd::setSearchTarget(const QHostAddress &address, quint16 port)
{
    m_searchOverride = address;
    m_searchOverridePort = port;
}

void UpnpIgd::setSearchDelays(const QList<int> &delaysMs)
{
    if (!delaysMs.isEmpty())
        m_searchDelays = delaysMs;
}

void UpnpIgd::cancel()
{
    ++m_generation;
    m_searchTimer.stop();
    if (m_reply)
        m_reply->abort();
    m_reply = nullptr;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_stage = Stage::Idle;
}

void UpnpIgd::start(const QHostAddress &gateway, const QHostAddress &localAddress, quint16 internalPort,
                    quint16 suggestedExternalPort, quint32 leaseSeconds)
{
    cancel();
    m_gateway = normalizeAddress(gateway);
    m_localAddress = normalizeAddress(localAddress);
    m_internalPort = internalPort;
    m_externalPort = suggestedExternalPort ? suggestedExternalPort : internalPort;
    m_lease = leaseSeconds;
    m_conflicts = 0;
    m_locations.clear();
    m_nextLocation = 0;
    m_location.clear();
    m_services.clear();
    m_serviceIndex = 0;
    m_problems.clear();

    if (m_localAddress.isNull() || m_localAddress.protocol() != QAbstractSocket::IPv4Protocol) {
        fail(QStringLiteral("UPnP: no local IPv4 address towards the gateway"));
        return;
    }
    if (!m_http) {
        m_http = new QNetworkAccessManager(this);
        m_http->setProxy(QNetworkProxy::NoProxy);  // the gateway is on the local network
    }

    m_socket = new QUdpSocket(this);
    // Bound to the address facing the gateway, so the search goes out on
    // that interface.
    if (!m_socket->bind(m_localAddress, 0) && !m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        fail(QStringLiteral("UPnP: could not open a socket: %1").arg(m_socket->errorString()));
        return;
    }
    m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 2);
    connect(m_socket, &QUdpSocket::readyRead, this, &UpnpIgd::onDatagrams);

    m_stage = Stage::Searching;
    m_searchAttempt = 0;
    emit progress(QStringLiteral("Searching for a UPnP gateway"));
    search();
}

void UpnpIgd::search()
{
    const QHostAddress target = m_searchOverride.isNull() ? QHostAddress(QString::fromLatin1(upnp::SsdpGroup))
                                                         : m_searchOverride;
    const quint16 port = m_searchOverride.isNull() ? SsdpPort : m_searchOverridePort;
    for (const char *st : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                           "urn:schemas-upnp-org:device:InternetGatewayDevice:2"}) {
        const QByteArray request = QByteArray("M-SEARCH * HTTP/1.1\r\n"
                                              "HOST: 239.255.255.250:1900\r\n"
                                              "MAN: \"ssdp:discover\"\r\n"
                                              "MX: 1\r\n"
                                              "ST: ") + st + "\r\n\r\n";
        m_socket->writeDatagram(request, target, port);
    }
    m_searchTimer.start(m_searchDelays.value(m_searchAttempt, m_searchDelays.last()));
}

void UpnpIgd::onSearchTimeout()
{
    if (m_stage != Stage::Searching)
        return;
    ++m_searchAttempt;
    if (!m_locations.empty()) {
        // No answer from the gateway itself, but other devices answered.
        describe(m_locations.front());
        return;
    }
    if (m_searchAttempt < m_searchDelays.size()) {
        search();
        return;
    }
    fail(QStringLiteral("No UPnP gateway answered"));
}

void UpnpIgd::onDatagrams()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        const auto response = upnp::parseSearchResponse(datagram.data());
        if (!response || m_stage != Stage::Searching)
            continue;
        if (std::find(m_locations.begin(), m_locations.end(), response->location) != m_locations.end())
            continue;
        const bool fromGateway = QHostAddress(response->location.host()) == m_gateway
                                 || normalizeAddress(datagram.senderAddress()) == m_gateway;
        if (fromGateway) {
            m_locations.insert(m_locations.begin(), response->location);
            m_searchTimer.stop();
            describe(response->location);
            return;
        }
        m_locations.push_back(response->location);
    }
}

void UpnpIgd::describe(const QUrl &location)
{
    m_searchTimer.stop();
    m_stage = Stage::Describing;
    m_location = location;
    m_nextLocation = size_t(std::find(m_locations.begin(), m_locations.end(), location) - m_locations.begin());
    emit progress(QStringLiteral("Reading the UPnP description at %1").arg(location.toString()));

    QNetworkRequest request(location);
    request.setTransferTimeout(HttpTimeoutMs);
    QNetworkReply *reply = m_http->get(request);
    m_reply = reply;
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation == m_generation)
            onDescription(reply, generation);
    });
}

void UpnpIgd::onDescription(QNetworkReply *reply, quint64)
{
    m_reply = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        tryNextLocation(QStringLiteral("%1: %2").arg(m_location.toString(), reply->errorString()));
        return;
    }
    m_services = upnp::parseDescription(reply->readAll(), m_location);
    if (m_services.empty()) {
        tryNextLocation(QStringLiteral("%1 offers no WAN connection service").arg(m_location.toString()));
        return;
    }
    m_serviceIndex = 0;
    m_stage = Stage::Mapping;
    addMapping();
}

QList<QPair<QByteArray, QString>> UpnpIgd::addArguments() const
{
    // In the order the specification lists them; some gateways insist.
    return {
        {"NewRemoteHost", QString()},
        {"NewExternalPort", QString::number(m_externalPort)},
        {"NewProtocol", QStringLiteral("UDP")},
        {"NewInternalPort", QString::number(m_internalPort)},
        {"NewInternalClient", m_localAddress.toString()},
        {"NewEnabled", QStringLiteral("1")},
        {"NewPortMappingDescription", QStringLiteral("DHT Inspector")},
        {"NewLeaseDuration", QString::number(m_lease)},
    };
}

QNetworkReply *UpnpIgd::post(const QByteArray &action, const QList<QPair<QByteArray, QString>> &arguments)
{
    const upnp::Service &service = m_services[m_serviceIndex];
    QNetworkRequest request(service.controlUrl);
    request.setTransferTimeout(HttpTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("text/xml; charset=\"utf-8\""));
    request.setRawHeader("SOAPAction", "\"" + service.type + "#" + action + "\"");
    QNetworkReply *reply = m_http->post(request, upnp::soapEnvelope(service.type, action, arguments));
    m_reply = reply;
    return reply;
}

void UpnpIgd::addMapping()
{
    emit progress(QStringLiteral("Asking %1 for port %2 via UPnP").arg(m_location.host()).arg(m_externalPort));
    QNetworkReply *reply = post("AddPortMapping", addArguments());
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation == m_generation)
            onAddMapping(reply, generation);
    });
}

void UpnpIgd::renew()
{
    if (m_stage != Stage::Mapped || m_services.empty())
        return;
    m_stage = Stage::Renewing;
    addMapping();
}

void UpnpIgd::onAddMapping(QNetworkReply *reply, quint64)
{
    m_reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const upnp::SoapResult result = upnp::parseSoapResponse(status, reply->readAll());
    const bool renewing = m_stage == Stage::Renewing;

    if (result.ok) {
        if (renewing) {
            m_stage = Stage::Mapped;
            emit mapped(m_externalPort, m_lease, QHostAddress());
            return;
        }
        askAddress();
        return;
    }

    if (result.errorCode == 718 && m_conflicts < MaxConflictRetries) {
        // Someone else holds the port on the gateway: pick another.
        ++m_conflicts;
        m_externalPort = quint16(QRandomGenerator::global()->bounded(49152, 65536));
        addMapping();
        return;
    }
    if (result.errorCode == 725 && m_lease != 0) {
        // OnlyPermanentLeasesSupported.
        m_lease = 0;
        addMapping();
        return;
    }

    const QString reason = result.errorCode != 0
                               ? QStringLiteral("error %1 %2").arg(result.errorCode).arg(result.errorDescription)
                               : status != 0 ? QStringLiteral("HTTP status %1").arg(status)
                                             : reply->errorString();
    if (renewing) {
        fail(QStringLiteral("UPnP gateway %1 would not renew the mapping: %2").arg(m_location.host(), reason));
        return;
    }
    if (result.errorCode == 606 || result.errorCode == 401 || result.errorCode == 0) {
        // Not allowed or not understood here: another service or device
        // may do better.
        if (m_serviceIndex + 1 < m_services.size()) {
            ++m_serviceIndex;
            addMapping();
            return;
        }
        tryNextLocation(QStringLiteral("%1 refused the mapping (%2)").arg(m_location.host(), reason));
        return;
    }
    fail(QStringLiteral("UPnP gateway %1 refused the mapping: %2").arg(m_location.host(), reason));
}

void UpnpIgd::askAddress()
{
    m_stage = Stage::AskingAddress;
    QNetworkReply *reply = post("GetExternalIPAddress", {});
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation == m_generation)
            onAddress(reply, generation);
    });
}

void UpnpIgd::onAddress(QNetworkReply *reply, quint64)
{
    m_reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const upnp::SoapResult result = upnp::parseSoapResponse(status, reply->readAll());
    QHostAddress external;
    if (result.ok)
        external = QHostAddress(result.value("NewExternalIPAddress"));
    m_stage = Stage::Mapped;
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    // The mapping stands whether or not the address came back.
    emit mapped(m_externalPort, m_lease, external);
}

void UpnpIgd::tryNextLocation(const QString &why)
{
    m_problems << why;
    if (m_nextLocation + 1 < m_locations.size()) {
        describe(m_locations[m_nextLocation + 1]);
        return;
    }
    fail(QStringLiteral("UPnP failed: %1").arg(m_problems.join(QStringLiteral("; "))));
}

void UpnpIgd::fail(const QString &message)
{
    cancel();
    emit failed(message);
}

void UpnpIgd::releaseBlocking(int timeoutMs)
{
    if ((m_stage != Stage::Mapped && m_stage != Stage::Renewing) || m_services.empty())
        return;
    const upnp::Service service = m_services[m_serviceIndex];
    cancel();

    const QByteArray body = upnp::soapEnvelope(service.type, "DeletePortMapping",
                                               {{"NewRemoteHost", QString()},
                                                {"NewExternalPort", QString::number(m_externalPort)},
                                                {"NewProtocol", QStringLiteral("UDP")}});
    const QUrl &url = service.controlUrl;
    QByteArray path = url.toEncoded(QUrl::RemoveScheme | QUrl::RemoveAuthority | QUrl::RemoveFragment);
    if (path.isEmpty())
        path = "/";
    const quint16 port = quint16(url.port(80));
    QByteArray request = "POST " + path + " HTTP/1.1\r\n"
                         "Host: " + url.host().toUtf8() + ":" + QByteArray::number(port) + "\r\n"
                         "Content-Type: text/xml; charset=\"utf-8\"\r\n"
                         "SOAPAction: \"" + service.type + "#DeletePortMapping\"\r\n"
                         "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                         "Connection: close\r\n\r\n" + body;

    QDeadlineTimer deadline(timeoutMs);
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.connectToHost(url.host(), port);
    if (!socket.waitForConnected(int(deadline.remainingTime())))
        return;
    socket.write(request);
    while (socket.bytesToWrite() > 0 && !deadline.hasExpired()) {
        if (!socket.waitForBytesWritten(int(deadline.remainingTime())))
            break;
    }
    // Give the gateway a moment to act before the connection goes.
    socket.waitForReadyRead(int(std::max<qint64>(0, deadline.remainingTime())));
    socket.disconnectFromHost();
}

} // namespace dht
