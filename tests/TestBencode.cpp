#include "dhtcore/Bencode.h"

#include <QTest>

#include <limits>
#include <utility>

using namespace dht;

class TestBencode : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void canonicalKeyOrder();
    void decodesKnownValues();
    void extremeIntegers();
    void rejectsMalformed_data();
    void rejectsMalformed();
    void warnsOnNonCanonical_data();
    void warnsOnNonCanonical();
    void duplicateKeysKeepFirst();
    void depthLimit();
};

void TestBencode::roundTrip()
{
    BValue::Dict inner;
    inner.emplace("bin", QByteArray("\x00\xff\x10", 3));
    inner.emplace("neg", BValue(qint64(-42)));

    BValue::Dict root;
    root.emplace("list", BValue::List{BValue(1), BValue("two"), BValue(BValue::List{})});
    root.emplace("dict", BValue(std::move(inner)));
    root.emplace("empty", BValue(QByteArray()));

    const QByteArray encoded = bencode(BValue(std::move(root)));
    const BDecodeResult decoded = bdecode(encoded);
    QVERIFY(decoded.ok());
    QVERIFY(decoded.warnings.isEmpty());
    QCOMPARE(bencode(decoded.value), encoded);
}

void TestBencode::canonicalKeyOrder()
{
    BValue::Dict d;
    d.emplace("b", BValue(1));
    d.emplace("a", BValue(2));
    QCOMPARE(bencode(BValue(std::move(d))), QByteArray("d1:ai2e1:bi1ee"));
}

void TestBencode::decodesKnownValues()
{
    const BDecodeResult r = bdecode("d1:ai-5e1:bl3:foo0:ee");
    QVERIFY(r.ok());
    QCOMPARE(r.value.integerAt("a").value_or(0), qint64(-5));
    const BValue *b = r.value.listAt("b");
    QVERIFY(b);
    QCOMPARE(int(b->toList().size()), 2);
    QCOMPARE(b->toList()[0].toString(), QByteArray("foo"));
    QCOMPARE(b->toList()[1].toString(), QByteArray());
}

void TestBencode::extremeIntegers()
{
    auto min = bdecode("i-9223372036854775808e");
    QVERIFY(min.ok());
    QCOMPARE(min.value.toInteger(), std::numeric_limits<qint64>::min());

    auto max = bdecode("i9223372036854775807e");
    QVERIFY(max.ok());
    QCOMPARE(max.value.toInteger(), std::numeric_limits<qint64>::max());

    QVERIFY(!bdecode("i9223372036854775808e").ok());
    QVERIFY(!bdecode("i99999999999999999999e").ok());
}

void TestBencode::rejectsMalformed_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("bare i") << QByteArray("i");
    QTest::newRow("no digits") << QByteArray("ie");
    QTest::newRow("unterminated int") << QByteArray("i12");
    QTest::newRow("junk in int") << QByteArray("i1x2e");
    QTest::newRow("short string") << QByteArray("5:abc");
    QTest::newRow("no colon") << QByteArray("3abc");
    QTest::newRow("unterminated list") << QByteArray("li1e");
    QTest::newRow("unterminated dict") << QByteArray("d1:a");
    QTest::newRow("integer key") << QByteArray("di1ei2ee");
    QTest::newRow("unknown type") << QByteArray("x");
}

void TestBencode::rejectsMalformed()
{
    QFETCH(QByteArray, input);
    const BDecodeResult r = bdecode(input);
    QVERIFY(!r.ok());
    QVERIFY(!r.error->message.isEmpty());
    QVERIFY(r.error->offset >= 0 && r.error->offset <= input.size());
    QVERIFY(!r.value.isValid());
}

void TestBencode::warnsOnNonCanonical_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<QString>("fragment");
    QTest::newRow("leading zero int") << QByteArray("i03e") << QStringLiteral("leading zeros");
    QTest::newRow("negative zero") << QByteArray("i-0e") << QStringLiteral("negative zero");
    QTest::newRow("leading zero length") << QByteArray("03:abc") << QStringLiteral("leading zeros");
    QTest::newRow("unsorted keys") << QByteArray("d1:bi1e1:ai2ee") << QStringLiteral("not sorted");
    QTest::newRow("trailing bytes") << QByteArray("i1eXYZ") << QStringLiteral("trailing");
}

void TestBencode::warnsOnNonCanonical()
{
    QFETCH(QByteArray, input);
    QFETCH(QString, fragment);
    const BDecodeResult r = bdecode(input);
    QVERIFY(r.ok());
    QCOMPARE(r.warnings.size(), 1);
    QVERIFY2(r.warnings.first().contains(fragment), qPrintable(r.warnings.first()));
}

void TestBencode::duplicateKeysKeepFirst()
{
    const BDecodeResult r = bdecode("d1:ai1e1:ai2ee");
    QVERIFY(r.ok());
    QCOMPARE(r.warnings.size(), 1);
    QVERIFY(r.warnings.first().contains(QStringLiteral("duplicate")));
    QCOMPARE(r.value.integerAt("a").value_or(0), qint64(1));
}

void TestBencode::depthLimit()
{
    const QByteArray shallow = QByteArray(10, 'l') + QByteArray(10, 'e');
    QVERIFY(bdecode(shallow).ok());

    const QByteArray deep = QByteArray(40, 'l') + QByteArray(40, 'e');
    const BDecodeResult r = bdecode(deep, 32);
    QVERIFY(!r.ok());
    QVERIFY(r.error->message.contains(QStringLiteral("nesting")));
}

int runTestBencode(int argc, char **argv)
{
    TestBencode test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestBencode.moc"
