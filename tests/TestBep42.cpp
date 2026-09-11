#include "dhtcore/Bep42.h"
#include "dhtcore/Crc32c.h"

#include <QTest>

using namespace dht;

class TestBep42 : public QObject
{
    Q_OBJECT

private slots:
    void crc32cKnownValue();
    void specVectors_data();
    void specVectors();
    void generatedIdsAreCompliant_data();
    void generatedIdsAreCompliant();
    void tamperedIdIsNotCompliant();
    void exemptions_data();
    void exemptions();
    void unknownWithoutAddress();
};

void TestBep42::crc32cKnownValue()
{
    QCOMPARE(crc32c("123456789"), quint32(0xE3069283));
    QCOMPARE(crc32c(QByteArrayView()), quint32(0));
}

void TestBep42::specVectors_data()
{
    QTest::addColumn<QString>("ip");
    QTest::addColumn<int>("rand");
    QTest::addColumn<QString>("id");

    // Test vectors from BEP 42.
    QTest::newRow("124.31.75.21") << "124.31.75.21" << 1 << "5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee401";
    QTest::newRow("21.75.31.124") << "21.75.31.124" << 86 << "5a3ce9c14e7a08645677bbd1cfe7d8f956d53256";
    QTest::newRow("65.23.51.170") << "65.23.51.170" << 22 << "a5d43220bc8f112a3d426c84764f8c2a1150e616";
    QTest::newRow("84.124.73.14") << "84.124.73.14" << 65 << "1b0321dd1bb1fe518101ceef99462b947a01ff41";
    QTest::newRow("43.213.53.83") << "43.213.53.83" << 90 << "e56f6cbf5b7c4be0237986d5243b87aa6d51305a";
}

void TestBep42::specVectors()
{
    QFETCH(QString, ip);
    QFETCH(int, rand);
    QFETCH(QString, id);

    const QHostAddress address(ip);
    const auto expected = NodeId::fromHex(id);
    QVERIFY(expected);

    // The published IDs must verify.
    QVERIFY(bep42::isCompliant(*expected, address));
    QCOMPARE(bep42::check(*expected, address), bep42::Status::Compliant);

    // Our generator must reproduce the constrained bits.
    const NodeId generated = bep42::generate(address, quint8(rand));
    QCOMPARE(generated[0], (*expected)[0]);
    QCOMPARE(generated[1], (*expected)[1]);
    QCOMPARE(quint8(generated[2] & 0xf8), quint8((*expected)[2] & 0xf8));
    QCOMPARE(generated[19], quint8(rand));
}

void TestBep42::generatedIdsAreCompliant_data()
{
    QTest::addColumn<QString>("ip");
    QTest::newRow("v4") << "203.0.113.77";
    QTest::newRow("v6") << "2001:db8:85a3::8a2e:370:7334";
}

void TestBep42::generatedIdsAreCompliant()
{
    QFETCH(QString, ip);
    const QHostAddress address(ip);
    for (int i = 0; i < 50; ++i)
        QVERIFY(bep42::isCompliant(bep42::generate(address), address));
}

void TestBep42::tamperedIdIsNotCompliant()
{
    const QHostAddress address(QStringLiteral("124.31.75.21"));
    NodeId id = *NodeId::fromHex(u"5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee401");
    id[0] ^= 0x01;
    QCOMPARE(bep42::check(id, address), bep42::Status::NonCompliant);

    // A compliant ID for one address is not compliant for another.
    const NodeId other = bep42::generate(QHostAddress(QStringLiteral("198.51.100.1")));
    QCOMPARE(bep42::check(other, QHostAddress(QStringLiteral("203.0.113.9"))), bep42::Status::NonCompliant);
}

void TestBep42::exemptions_data()
{
    QTest::addColumn<QString>("ip");
    QTest::addColumn<bool>("exempt");
    QTest::newRow("10/8") << "10.1.2.3" << true;
    QTest::newRow("172.16/12") << "172.20.5.4" << true;
    QTest::newRow("172.32 is public") << "172.32.0.1" << false;
    QTest::newRow("192.168/16") << "192.168.1.1" << true;
    QTest::newRow("169.254/16") << "169.254.10.10" << true;
    QTest::newRow("loopback") << "127.0.0.1" << true;
    QTest::newRow("public v4") << "8.8.8.8" << false;
    QTest::newRow("v6 loopback") << "::1" << true;
    QTest::newRow("v6 ula") << "fd12:3456::1" << true;
    QTest::newRow("v6 link local") << "fe80::1" << true;
    QTest::newRow("v6 public") << "2001:4860:4860::8888" << false;
    QTest::newRow("v4-mapped private") << "::ffff:192.168.0.5" << true;
}

void TestBep42::exemptions()
{
    QFETCH(QString, ip);
    QFETCH(bool, exempt);
    const QHostAddress address(ip);
    QCOMPARE(bep42::isExempt(address), exempt);
    if (exempt)
        QCOMPARE(bep42::check(NodeId::random(), address), bep42::Status::Exempt);
}

void TestBep42::unknownWithoutAddress()
{
    QCOMPARE(bep42::check(NodeId::random(), QHostAddress()), bep42::Status::Unknown);
}

int runTestBep42(int argc, char **argv)
{
    TestBep42 test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestBep42.moc"
