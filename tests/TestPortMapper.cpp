#include "dhtcore/PortMapper.h"

#include <QNetworkDatagram>
#include <QTest>
#include <QUdpSocket>
#include <QtEndian>

#include <cstring>
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

private:
    std::unique_ptr<QUdpSocket> m_gateway;
};

void TestPortMapper::init()
{
    m_gateway = std::make_unique<QUdpSocket>();
    QVERIFY(m_gateway->bind(QHostAddress::LocalHost, 0));
}

void TestPortMapper::pcpMapsAndReleases()
{
    PortMapper mapper;
    mapper.setGatewayOverride(QHostAddress::LocalHost, m_gateway->localPort());
    mapper.start(6881);

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

    QTRY_COMPARE(mapper.snapshot().state, PortMappingSnapshot::State::Mapped);
    QCOMPARE(mapper.snapshot().protocol, QStringLiteral("PCP"));
    QCOMPARE(mapper.snapshot().externalPort, quint16(40000));
    QCOMPARE(mapper.snapshot().externalAddress, QHostAddress(QStringLiteral("203.0.113.5")));
    QCOMPARE(mapper.snapshot().lifetimeSeconds, quint32(7200));

    mapper.stop();
    const QNetworkDatagram release = nextDatagram(*m_gateway);
    QVERIFY(release.isValid());
    QCOMPARE(release.data().size(), 60);
    QCOMPARE(qFromBigEndian<quint32>(release.data().constData() + 4), quint32(0));
    QCOMPARE(release.data().mid(24, 12), req.mid(24, 12));
    QCOMPARE(mapper.snapshot().state, PortMappingSnapshot::State::Disabled);
}

void TestPortMapper::fallsBackToNatPmp()
{
    PortMapper mapper;
    mapper.setGatewayOverride(QHostAddress::LocalHost, m_gateway->localPort());
    mapper.start(6881);

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

    QTRY_COMPARE(mapper.snapshot().state, PortMappingSnapshot::State::Mapped);
    QCOMPARE(mapper.snapshot().protocol, QStringLiteral("NAT-PMP"));
    QCOMPARE(mapper.snapshot().externalPort, quint16(40001));

    const QNetworkDatagram addressRequest = nextDatagram(*m_gateway);
    QVERIFY(addressRequest.isValid());
    QCOMPARE(addressRequest.data(), QByteArray(2, '\0'));
    QByteArray address(12, '\0');
    address[1] = char(128);
    qToBigEndian<quint32>(0xCB007106, address.data() + 8);
    m_gateway->writeDatagram(addressRequest.makeReply(address));

    QTRY_COMPARE(mapper.snapshot().externalAddress, QHostAddress(QStringLiteral("203.0.113.6")));
}

void TestPortMapper::reportsRefusal()
{
    PortMapper mapper;
    mapper.setGatewayOverride(QHostAddress::LocalHost, m_gateway->localPort());
    mapper.start(6881);

    const QNetworkDatagram request = nextDatagram(*m_gateway);
    QVERIFY(request.isValid());
    m_gateway->writeDatagram(request.makeReply(pcpMapResponse(request.data(), 2, 0, 0)));

    QTRY_COMPARE(mapper.snapshot().state, PortMappingSnapshot::State::Failed);
    QVERIFY2(mapper.snapshot().message.contains(QStringLiteral("not authorized")), qPrintable(mapper.snapshot().message));
}

void TestPortMapper::failsWhenGatewaySilent()
{
    PortMapper mapper;
    mapper.setGatewayOverride(QHostAddress::LocalHost, m_gateway->localPort());
    mapper.setRetryDelays({50, 50});
    mapper.start(6881);

    QTRY_COMPARE_WITH_TIMEOUT(mapper.snapshot().state, PortMappingSnapshot::State::Failed, 3000);
    QVERIFY(mapper.snapshot().message.contains(QStringLiteral("did not answer")));
}

int runTestPortMapper(int argc, char **argv)
{
    TestPortMapper test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestPortMapper.moc"
