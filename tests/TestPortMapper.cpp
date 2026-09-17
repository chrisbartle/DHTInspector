#include "dhtcore/PortMapper.h"
#include "dhtcore/Upnp.h"

#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUdpSocket>
#include <QtEndian>

#include <cstring>
#include <functional>
#include <memory>

using namespace dht;

namespace {

// Waits for the next datagram on a fake gateway socket.
QNetworkDatagram nextDatagram(QUdpSocket &socket, int timeoutMs = 3000)
{
    if (!QTest::qWaitFor([&] { return socket.hasPendingDatagrams(); }, timeoutMs))
        return {};
    return socket.receiveDatagram();
}

QByteArray pcpMapResponse(const QByteArray &request, int result, quint16 externalPort, quint32 externalIp)
{
    QByteArray r(60, '\0');
    r[0] = 2;
    r[1] = char(0x81);
    r[3] = char(result);
    qToBigEndian<quint32>(7200, r.data() + 4);
    std::memcpy(r.data() + 24, request.constData() + 24, 12); // nonce
    r[36] = 17;
    std::memcpy(r.data() + 40, request.constData() + 40, 2); // internal port
    qToBigEndian<quint16>(externalPort, r.data() + 42);
    r[54] = char(0xff);
    r[55] = char(0xff);
    qToBigEndian<quint32>(externalIp, r.data() + 56);
    return r;
}

QByteArray soapOk(const QByteArray &action, const QByteArray &inner = {})
{
    return "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
           "<u:" + action + "Response xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">" + inner
           + "</u:" + action + "Response></s:Body></s:Envelope>";
}

QByteArray soapFault(int code, const QByteArray &description)
{
    return "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
           "<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>"
           "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>" + QByteArray::number(code)
           + "</errorCode><errorDescription>" + description + "</errorDescription></UPnPError>"
             "</detail></s:Fault></s:Body></s:Envelope>";
}

// A description with the IP connection under a relative control URL and a
// PPP one listed first, which should lose to it.
const QByteArray Description = R"(<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
 <device>
  <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
  <serviceList><service>
   <serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>
   <controlURL>/l3f</controlURL>
  </service></serviceList>
  <deviceList><device>
   <deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>
   <deviceList><device>
    <deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>
    <serviceList>
     <service>
      <serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>
      <controlURL>/ppp</controlURL>
     </service>
     <service>
      <serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>
      <controlURL>ctl/IPConn</controlURL>
     </service>
    </serviceList>
   </device></deviceList>
  </device></deviceList>
 </device>
</root>)";

// An Internet Gateway Device: answers SSDP searches and serves its
// description and SOAP control over HTTP.
class FakeIgd
{
public:
    struct Request
    {
        QByteArray method;
        QByteArray path;
        QByteArray soapAction;
        QByteArray body;
    };
    using Handler = std::function<std::pair<int, QByteArray>(const Request &)>;

    bool start()
    {
        if (!ssdp.bind(QHostAddress::LocalHost, 0) || !http.listen(QHostAddress::LocalHost, 0))
            return false;
        QObject::connect(&ssdp, &QUdpSocket::readyRead, [this] {
            while (ssdp.hasPendingDatagrams()) {
                const QNetworkDatagram d = ssdp.receiveDatagram();
                searches.append(d.data());
                if (!answerSearches || !d.data().startsWith("M-SEARCH"))
                    continue;
                ssdp.writeDatagram(d.makeReply(
                    "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\n"
                    "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
                    "location: http://127.0.0.1:" + QByteArray::number(http.serverPort()) + "/igd/desc.xml\r\n"
                    "SERVER: Fake/1.0 UPnP/1.0\r\n\r\n"));
            }
        });
        QObject::connect(&http, &QTcpServer::newConnection, [this] {
            while (QTcpSocket *s = http.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                auto done = std::make_shared<bool>(false);
                const auto take = [this, s, buffer, done] {
                    *buffer += s->readAll();
                    if (*done)
                        return;
                    const qsizetype end = buffer->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    Request r;
                    qsizetype length = 0;
                    const QList<QByteArray> lines = buffer->left(end).split('\n');
                    const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
                    r.method = first.value(0);
                    r.path = first.value(1);
                    for (const QByteArray &line : lines.mid(1)) {
                        const qsizetype colon = line.indexOf(':');
                        const QByteArray name = line.left(colon).trimmed().toLower();
                        const QByteArray value = line.mid(colon + 1).trimmed();
                        if (name == "content-length")
                            length = value.toLongLong();
                        else if (name == "soapaction")
                            r.soapAction = value;
                    }
                    if (buffer->size() < end + 4 + length)
                        return;
                    *done = true;
                    r.body = buffer->mid(end + 4, length);
                    requests.append(r);
                    const auto [status, body] = r.path.endsWith("desc.xml")
                                                    ? std::pair<int, QByteArray>{200, Description}
                                                    : handler(r);
                    s->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Internal Server Error")
                             + "\r\nContent-Type: text/xml\r\nContent-Length: " + QByteArray::number(body.size())
                             + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                };
                QObject::connect(s, &QTcpSocket::readyRead, take);
                QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
                take();
            }
        });
        return true;
    }

    QList<Request> calls(const QByteArray &action) const
    {
        QList<Request> out;
        for (const Request &r : requests) {
            if (r.soapAction.contains("#" + action))
                out.append(r);
        }
        return out;
    }

    static QByteArray argument(const QByteArray &body, const QByteArray &name)
    {
        const qsizetype from = body.indexOf("<" + name + ">");
        const qsizetype to = body.indexOf("</" + name + ">");
        return from < 0 || to < 0 ? QByteArray() : body.mid(from + name.size() + 2, to - from - name.size() - 2);
    }

    QUdpSocket ssdp;
    QTcpServer http;
    bool answerSearches = true;
    QList<QByteArray> searches;
    QList<Request> requests;
    Handler handler = [](const Request &r) -> std::pair<int, QByteArray> {
        if (r.soapAction.contains("#GetExternalIPAddress"))
            return {200, soapOk("GetExternalIPAddress", "<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>")};
        if (r.soapAction.contains("#AddPortMapping"))
            return {200, soapOk("AddPortMapping")};
        return {200, soapOk("DeletePortMapping")};
    };
};

} // namespace

class TestPortMapper : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void pcpMapsAndReleases();
    void fallsBackToNatPmp();
    void reportsRefusal();
    void failsWhenGatewaySilent();
    void parsesSearchResponses();
    void parsesDescriptions();
    void parsesSoap();
    void upnpMapsWhenPcpAndNatPmpAreSilent();
    void upnpRetriesConflictsAndPermanentLeases();
    void upnpReportsRefusal();

private:
    // A mapper aimed at the fake gateway, whose UPnP search goes to a
    // socket nobody answers unless a test says otherwise; never at the
    // real network.
    std::unique_ptr<PortMapper> makeMapper(quint16 searchPort = 0);

    std::unique_ptr<QUdpSocket> m_gateway;
    std::unique_ptr<QUdpSocket> m_silent;
};

void TestPortMapper::init()
{
    m_gateway = std::make_unique<QUdpSocket>();
    QVERIFY(m_gateway->bind(QHostAddress::LocalHost, 0));
    m_silent = std::make_unique<QUdpSocket>();
    QVERIFY(m_silent->bind(QHostAddress::LocalHost, 0));
}

std::unique_ptr<PortMapper> TestPortMapper::makeMapper(quint16 searchPort)
{
    auto mapper = std::make_unique<PortMapper>();
    mapper->setGatewayOverride(QHostAddress::LocalHost, m_gateway->localPort());
    mapper->setUpnpSearchTarget(QHostAddress::LocalHost, searchPort ? searchPort : m_silent->localPort());
    mapper->setUpnpSearchDelays({100, 100});
    return mapper;
}

void TestPortMapper::pcpMapsAndReleases()
{
    auto mapper = makeMapper();
    mapper->start(6881);

    const QNetworkDatagram request = nextDatagram(*m_gateway);
    QVERIFY(request.isValid());
    const QByteArray req = request.data();
    QCOMPARE(req.size(), 60);
    QCOMPARE(quint8(req[0]), quint8(2));
    QCOMPARE(quint8(req[1]), quint8(1));
    QCOMPARE(quint8(req[36]), quint8(17));
    QCOMPARE(qFromBigEndian<quint16>(req.constData() + 40), quint16(6881));
    QCOMPARE(qFromBigEndian<quint32>(req.constData() + 20), QHostAddress(QHostAddress::LocalHost).toIPv4Address());

    m_gateway->writeDatagram(request.makeReply(pcpMapResponse(req, 0, 40000, 0xCB007105)));

    QTRY_COMPARE(mapper->snapshot().state, PortMappingSnapshot::State::Mapped);
    QCOMPARE(mapper->snapshot().protocol, QStringLiteral("PCP"));
    QCOMPARE(mapper->snapshot().externalPort, quint16(40000));
    QCOMPARE(mapper->snapshot().externalAddress, QHostAddress(QStringLiteral("203.0.113.5")));
    QCOMPARE(mapper->snapshot().lifetimeSeconds, quint32(7200));

    mapper->stop();
    const QNetworkDatagram release = nextDatagram(*m_gateway);
    QVERIFY(release.isValid());
    QCOMPARE(release.data().size(), 60);
    QCOMPARE(qFromBigEndian<quint32>(release.data().constData() + 4), quint32(0));
    QCOMPARE(release.data().mid(24, 12), req.mid(24, 12));
    QCOMPARE(mapper->snapshot().state, PortMappingSnapshot::State::Disabled);
    QVERIFY(!m_silent->hasPendingDatagrams());  // UPnP was never needed
}

void TestPortMapper::fallsBackToNatPmp()
{
    auto mapper = makeMapper();
    mapper->start(6881);

    // A NAT-PMP-only gateway answers PCP with "unsupported version".
    const QNetworkDatagram pcp = nextDatagram(*m_gateway);
    QVERIFY(pcp.isValid());
    QByteArray unsupported(8, '\0');
    unsupported[1] = char(0x81);
    unsupported[3] = 1;
    m_gateway->writeDatagram(pcp.makeReply(unsupported));

    QNetworkDatagram map = nextDatagram(*m_gateway);
    while (map.isValid() && map.data().size() == 60) // drop any PCP retransmit
        map = nextDatagram(*m_gateway);
    QVERIFY(map.isValid());
    QCOMPARE(map.data().size(), 12);
    QCOMPARE(quint8(map.data()[1]), quint8(1));
    QCOMPARE(qFromBigEndian<quint16>(map.data().constData() + 4), quint16(6881));

    QByteArray mapped(16, '\0');
    mapped[1] = char(129);
    qToBigEndian<quint16>(6881, mapped.data() + 8);
    qToBigEndian<quint16>(40001, mapped.data() + 10);
    qToBigEndian<quint32>(3600, mapped.data() + 12);
    m_gateway->writeDatagram(map.makeReply(mapped));

    QTRY_COMPARE(mapper->snapshot().state, PortMappingSnapshot::State::Mapped);
    QCOMPARE(mapper->snapshot().protocol, QStringLiteral("NAT-PMP"));
    QCOMPARE(mapper->snapshot().externalPort, quint16(40001));

    const QNetworkDatagram addressRequest = nextDatagram(*m_gateway);
    QVERIFY(addressRequest.isValid());
    QCOMPARE(addressRequest.data(), QByteArray(2, '\0'));
    QByteArray address(12, '\0');
    address[1] = char(128);
    qToBigEndian<quint32>(0xCB007106, address.data() + 8);
    m_gateway->writeDatagram(addressRequest.makeReply(address));

    QTRY_COMPARE(mapper->snapshot().externalAddress, QHostAddress(QStringLiteral("203.0.113.6")));
}

void TestPortMapper::reportsRefusal()
{
    auto mapper = makeMapper();
    mapper->setRetryDelays({50, 50});
    mapper->start(6881);

    const QNetworkDatagram request = nextDatagram(*m_gateway);
    QVERIFY(request.isValid());
    m_gateway->writeDatagram(request.makeReply(pcpMapResponse(request.data(), 2, 0, 0)));

    // The refusal is kept while NAT-PMP and UPnP are tried in turn.
    QTRY_COMPARE_WITH_TIMEOUT(mapper->snapshot().state, PortMappingSnapshot::State::Failed, 5000);
    const QString message = mapper->snapshot().message;
    QVERIFY2(message.contains(QStringLiteral("not authorized")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("No UPnP gateway answered")), qPrintable(message));
}

void TestPortMapper::failsWhenGatewaySilent()
{
    auto mapper = makeMapper();
    mapper->setRetryDelays({50, 50});
    mapper->start(6881);

    QTRY_VERIFY_WITH_TIMEOUT(mapper->snapshot().protocol == QStringLiteral("UPnP IGD"), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(mapper->snapshot().state, PortMappingSnapshot::State::Failed, 3000);
    const QString message = mapper->snapshot().message;
    QVERIFY2(message.contains(QStringLiteral("did not answer PCP or NAT-PMP")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("No UPnP gateway answered")), qPrintable(message));
    // The search went where it was sent, twice, for both device versions.
    int searches = 0;
    while (m_silent->hasPendingDatagrams()) {
        const QByteArray d = m_silent->receiveDatagram().data();
        QVERIFY(d.startsWith("M-SEARCH * HTTP/1.1\r\n"));
        QVERIFY(d.contains("MAN: \"ssdp:discover\""));
        QVERIFY(d.contains("InternetGatewayDevice:"));
        ++searches;
    }
    QCOMPARE(searches, 4);
}

void TestPortMapper::parsesSearchResponses()
{
    const auto r = upnp::parseSearchResponse("HTTP/1.1 200 OK\r\nst: urn:x\r\nLocation: http://192.168.1.1:5000/rootDesc.xml\r\n\r\n");
    QVERIFY(r);
    QCOMPARE(r->location, QUrl(QStringLiteral("http://192.168.1.1:5000/rootDesc.xml")));
    QCOMPARE(r->searchTarget, QByteArray("urn:x"));
    // Bare line feeds are tolerated.
    QVERIFY(upnp::parseSearchResponse("HTTP/1.1 200 OK\nLOCATION: http://10.0.0.1/d.xml\n\n"));

    QVERIFY(!upnp::parseSearchResponse("HTTP/1.1 404 Not Found\r\nLOCATION: http://10.0.0.1/\r\n\r\n"));
    QVERIFY(!upnp::parseSearchResponse("NOTIFY * HTTP/1.1\r\nLOCATION: http://10.0.0.1/\r\n\r\n"));
    QVERIFY(!upnp::parseSearchResponse("HTTP/1.1 200 OK\r\nST: urn:x\r\n\r\n"));
    QVERIFY(!upnp::parseSearchResponse("HTTP/1.1 200 OK\r\nLOCATION: https://10.0.0.1/\r\n\r\n"));
    QVERIFY(!upnp::parseSearchResponse(QByteArray()));
}

void TestPortMapper::parsesDescriptions()
{
    const QUrl location(QStringLiteral("http://192.168.1.1:5000/igd/desc.xml"));
    const auto services = upnp::parseDescription(Description, location);
    QCOMPARE(int(services.size()), 2);
    QCOMPARE(services[0].type, QByteArray("urn:schemas-upnp-org:service:WANIPConnection:1"));
    QCOMPARE(services[0].controlUrl, QUrl(QStringLiteral("http://192.168.1.1:5000/igd/ctl/IPConn")));
    QCOMPARE(services[1].type, QByteArray("urn:schemas-upnp-org:service:WANPPPConnection:1"));
    QCOMPARE(services[1].controlUrl, QUrl(QStringLiteral("http://192.168.1.1:5000/ppp")));

    // URLBase wins over the description's own address; version 2 comes first.
    const QByteArray withBase = R"(<root><URLBase>http://192.168.1.254:80/</URLBase><device><serviceList>
        <service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>v1</controlURL></service>
        <service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType><controlURL>/v2</controlURL></service>
        </serviceList></device></root>)";
    const auto based = upnp::parseDescription(withBase, location);
    QCOMPARE(int(based.size()), 2);
    QCOMPARE(based[0].controlUrl, QUrl(QStringLiteral("http://192.168.1.254:80/v2")));
    QCOMPARE(based[1].controlUrl, QUrl(QStringLiteral("http://192.168.1.254:80/v1")));

    QVERIFY(upnp::parseDescription("<root><device/></root>", location).empty());
    QVERIFY(upnp::parseDescription("not xml at all", location).empty());
}

void TestPortMapper::parsesSoap()
{
    const QByteArray envelope = upnp::soapEnvelope("urn:schemas-upnp-org:service:WANIPConnection:1", "AddPortMapping",
                                                   {{"NewRemoteHost", QString()},
                                                    {"NewPortMappingDescription", QStringLiteral("a <b> & c")}});
    QVERIFY(envelope.contains("<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"));
    QVERIFY(envelope.contains("<NewRemoteHost></NewRemoteHost>"));
    QVERIFY(envelope.contains("<NewPortMappingDescription>a &lt;b&gt; &amp; c</NewPortMappingDescription>"));
    QVERIFY(envelope.indexOf("NewRemoteHost") < envelope.indexOf("NewPortMappingDescription"));

    const upnp::SoapResult ok = upnp::parseSoapResponse(
        200, soapOk("GetExternalIPAddress", "<NewExternalIPAddress> 198.51.100.4 </NewExternalIPAddress>"));
    QVERIFY(ok.ok);
    QCOMPARE(ok.value("NewExternalIPAddress"), QStringLiteral("198.51.100.4"));
    QCOMPARE(ok.value("Missing"), QString());

    const upnp::SoapResult fault = upnp::parseSoapResponse(500, soapFault(718, "ConflictInMappingEntry"));
    QVERIFY(!fault.ok);
    QCOMPARE(fault.errorCode, 718);
    QCOMPARE(fault.errorDescription, QStringLiteral("ConflictInMappingEntry"));
    QCOMPARE(fault.value("faultstring"), QStringLiteral("UPnPError"));

    // A fault is a fault even with status 200, and 500 without one fails too.
    QVERIFY(!upnp::parseSoapResponse(200, soapFault(606, "Action not authorized")).ok);
    QVERIFY(!upnp::parseSoapResponse(500, QByteArray()).ok);
}

void TestPortMapper::upnpMapsWhenPcpAndNatPmpAreSilent()
{
    FakeIgd igd;
    QVERIFY(igd.start());
    auto mapper = makeMapper(igd.ssdp.localPort());
    mapper->setRetryDelays({50, 50});
    mapper->start(6881);

    QTRY_COMPARE_WITH_TIMEOUT(mapper->snapshot().state, PortMappingSnapshot::State::Mapped, 5000);
    PortMappingSnapshot s = mapper->snapshot();
    QCOMPARE(s.protocol, QStringLiteral("UPnP IGD"));
    QCOMPARE(s.externalPort, quint16(6881));
    QCOMPARE(s.lifetimeSeconds, PortMapper::RequestedLifetime);
    QVERIFY2(s.message.contains(QStringLiteral("mapped via UPnP IGD")), qPrintable(s.message));
    QTRY_COMPARE(mapper->snapshot().externalAddress, QHostAddress(QStringLiteral("203.0.113.7")));

    // The IP connection was used, at its resolved address, with every
    // argument the gateway needs.
    const QList<FakeIgd::Request> adds = igd.calls("AddPortMapping");
    QCOMPARE(adds.size(), 1);
    QCOMPARE(adds[0].method, QByteArray("POST"));
    QCOMPARE(adds[0].path, QByteArray("/igd/ctl/IPConn"));
    QCOMPARE(adds[0].soapAction, QByteArray("\"urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping\""));
    const QByteArray body = adds[0].body;
    QCOMPARE(FakeIgd::argument(body, "NewExternalPort"), QByteArray("6881"));
    QCOMPARE(FakeIgd::argument(body, "NewInternalPort"), QByteArray("6881"));
    QCOMPARE(FakeIgd::argument(body, "NewProtocol"), QByteArray("UDP"));
    QCOMPARE(FakeIgd::argument(body, "NewInternalClient"), QByteArray("127.0.0.1"));
    QCOMPARE(FakeIgd::argument(body, "NewEnabled"), QByteArray("1"));
    QCOMPARE(FakeIgd::argument(body, "NewLeaseDuration"), QByteArray("3600"));
    QCOMPARE(igd.calls("GetExternalIPAddress").size(), 1);

    // Stopping removes the mapping. The release blocks this thread, so the
    // fake gateway only reads it afterwards.
    mapper->stop();
    QCOMPARE(mapper->snapshot().state, PortMappingSnapshot::State::Disabled);
    QTRY_COMPARE(igd.calls("DeletePortMapping").size(), 1);
    const QByteArray release = igd.calls("DeletePortMapping")[0].body;
    QCOMPARE(FakeIgd::argument(release, "NewExternalPort"), QByteArray("6881"));
    QCOMPARE(FakeIgd::argument(release, "NewProtocol"), QByteArray("UDP"));
}

void TestPortMapper::upnpRetriesConflictsAndPermanentLeases()
{
    FakeIgd igd;
    QVERIFY(igd.start());
    int adds = 0;
    igd.handler = [&adds](const FakeIgd::Request &r) -> std::pair<int, QByteArray> {
        if (!r.soapAction.contains("#AddPortMapping"))
            return {500, soapFault(401, "Invalid Action")};  // no external address on offer
        switch (++adds) {
        case 1: return {500, soapFault(718, "ConflictInMappingEntry")};
        case 2: return {500, soapFault(725, "OnlyPermanentLeasesSupported")};
        default: return {200, soapOk("AddPortMapping")};
        }
    };
    auto mapper = makeMapper(igd.ssdp.localPort());
    mapper->setRetryDelays({50, 50});
    mapper->start(6881);

    QTRY_COMPARE_WITH_TIMEOUT(mapper->snapshot().state, PortMappingSnapshot::State::Mapped, 5000);
    const PortMappingSnapshot s = mapper->snapshot();
    QCOMPARE(adds, 3);
    QVERIFY(s.externalPort >= 49152);
    QCOMPARE(s.lifetimeSeconds, quint32(0));
    QVERIFY(s.externalAddress.isNull());
    const QList<FakeIgd::Request> calls = igd.calls("AddPortMapping");
    QCOMPARE(FakeIgd::argument(calls[0].body, "NewExternalPort"), QByteArray("6881"));
    QCOMPARE(FakeIgd::argument(calls[1].body, "NewExternalPort"), QByteArray::number(s.externalPort));
    QCOMPARE(FakeIgd::argument(calls[1].body, "NewLeaseDuration"), QByteArray("3600"));
    QCOMPARE(FakeIgd::argument(calls[2].body, "NewLeaseDuration"), QByteArray("0"));
    QCOMPARE(FakeIgd::argument(calls[2].body, "NewInternalPort"), QByteArray("6881"));
}

void TestPortMapper::upnpReportsRefusal()
{
    FakeIgd igd;
    QVERIFY(igd.start());
    igd.handler = [](const FakeIgd::Request &) -> std::pair<int, QByteArray> {
        return {500, soapFault(606, "Action not authorized")};
    };
    auto mapper = makeMapper(igd.ssdp.localPort());
    mapper->setRetryDelays({50, 50});
    mapper->start(6881);

    QTRY_COMPARE_WITH_TIMEOUT(mapper->snapshot().state, PortMappingSnapshot::State::Failed, 5000);
    const QString message = mapper->snapshot().message;
    QVERIFY2(message.contains(QStringLiteral("606 Action not authorized")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("did not answer PCP or NAT-PMP")), qPrintable(message));
    // Both connection services were tried before giving up.
    const QList<FakeIgd::Request> adds = igd.calls("AddPortMapping");
    QCOMPARE(adds.size(), 2);
    QCOMPARE(adds[0].path, QByteArray("/igd/ctl/IPConn"));
    QCOMPARE(adds[1].path, QByteArray("/ppp"));
    QVERIFY(igd.calls("DeletePortMapping").isEmpty());
}

int runTestPortMapper(int argc, char **argv)
{
    TestPortMapper test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestPortMapper.moc"
