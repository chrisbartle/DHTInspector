#include "dhtcore/ClientVersion.h"
#include "dhtcore/DhtEngine.h"

#include <QTest>

using namespace dht;

class TestClientVersion : public QObject
{
    Q_OBJECT

private slots:
    void decodes_data();
    void decodes();
    void describesItsOwnVersion();
    void rawTextShowsBytes();
};

Q_DECLARE_METATYPE(dht::ClientInfo::Kind)

void TestClientVersion::decodes_data()
{
    QTest::addColumn<QByteArray>("v");
    QTest::addColumn<ClientInfo::Kind>("kind");
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("version");
    QTest::addColumn<QString>("display");

    using K = ClientInfo::Kind;
    const auto b = [](const char *s, int n) { return QByteArray(s, n); };

    QTest::newRow("absent") << QByteArray() << K::Absent << QString() << QString() << "no version sent";

    // libtorrent (Rasterbar): {major, minor} up to 1.1, {major, minor<<4|tiny} after.
    QTest::newRow("LT 2.0.11") << b("LT\x02\x0b", 4) << K::Known << "LT" << "2.0.11" << "libtorrent (Rasterbar) 2.0.11";
    QTest::newRow("LT 1.2.15") << b("LT\x01\x2f", 4) << K::Known << "LT" << "1.2.15" << "libtorrent (Rasterbar) 1.2.15";
    QTest::newRow("LT 1.1") << b("LT\x01\x01", 4) << K::Known << "LT" << "1.1" << "libtorrent (Rasterbar) 1.1";
    QTest::newRow("LT 0.16") << b("LT\x00\x10", 4) << K::Known << "LT" << "0.16" << "libtorrent (Rasterbar) 0.16";

    // libTorrent (Rakshasa), from its configure.ac: 0.13.8, 0.14.0, 0.15.1, 0.16.23.
    QTest::newRow("lt 0.13.8") << b("lt\x0d\x80", 4) << K::Known << "lt" << "0.13.8" << "libTorrent (Rakshasa) 0.13.8";
    QTest::newRow("lt 0.14.0") << b("lt\x0e\x00", 4) << K::Known << "lt" << "0.14.0" << "libTorrent (Rakshasa) 0.14.0";
    QTest::newRow("lt 0.15.1") << b("lt\x0f\x01", 4) << K::Known << "lt" << "0.15.1" << "libTorrent (Rakshasa) 0.15.1";
    QTest::newRow("lt 0.16.23") << b("lt\x10\x17", 4) << K::Known << "lt" << "0.16.23" << "libTorrent (Rakshasa) 0.16.23";
    QTest::newRow("lt 0.16.17") << b("lt\x10\x11", 4) << K::Known << "lt" << "0.16.17" << "libTorrent (Rakshasa) 0.16.17";

    // Known client whose layout is unpublished: named, not versioned.
    QTest::newRow("UT") << b("UT\xab\xcd", 4) << K::Known << "UT" << QString() << QString(QChar(0x00b5)) + QStringLiteral("Torrent");

    QTest::newRow("unknown code") << b("ZZ\x01\x02", 4) << K::UnknownCode << "ZZ" << QString() << "unknown client ZZ";
    QTest::newRow("too short") << b("LT\x01", 3) << K::Nonstandard << "LT" << QString()
                               << "libtorrent (Rasterbar), nonstandard 3-byte field";
    QTest::newRow("too long") << b("XYZ\x01\x02", 5) << K::Nonstandard << "XY" << QString() << "nonstandard 5-byte field";
    QTest::newRow("binary code") << b("\x01\x02\x03\x04", 4) << K::Nonstandard << QString() << QString()
                                 << "nonstandard 4-byte field";
    QTest::newRow("case matters") << b("Lt\x01\x02", 4) << K::UnknownCode << "Lt" << QString() << "unknown client Lt";
}

void TestClientVersion::decodes()
{
    QFETCH(QByteArray, v);
    QFETCH(ClientInfo::Kind, kind);
    QFETCH(QString, code);
    QFETCH(QString, version);
    QFETCH(QString, display);

    const ClientInfo info = decodeClientVersion(v);
    QCOMPARE(info.kind, kind);
    QCOMPARE(info.raw, v);
    QCOMPARE(info.code, code);
    QCOMPARE(info.version, version);
    QCOMPARE(info.display(), display);
}

void TestClientVersion::describesItsOwnVersion()
{
    const ClientInfo info = decodeClientVersion(clientVersion());
    QCOMPARE(info.kind, ClientInfo::Kind::Known);
    QCOMPARE(info.name, QStringLiteral("DHT Inspector"));
    QCOMPARE(info.version, QStringLiteral("0.1"));
}

void TestClientVersion::rawTextShowsBytes()
{
    const ClientInfo info = decodeClientVersion(QByteArray("lt\x10\x11", 4));
    QCOMPARE(info.rawText(), QStringLiteral("\"lt\\x10\\x11\"  (6c 74 10 11)"));
    QCOMPARE(decodeClientVersion({}).rawText(), QString());
    QVERIFY(!info.note.isEmpty());
}

int runTestClientVersion(int argc, char **argv)
{
    TestClientVersion test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestClientVersion.moc"
