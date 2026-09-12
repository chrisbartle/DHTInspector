#include "dhtcore/Bencode.h"
#include "dhtcore/Bep44.h"
#include "dhtcore/Support.h"

#include <QTest>

using namespace dht;

class TestBep44 : public QObject
{
    Q_OBJECT

private slots:
    void immutableTargetVector();
    void mutableTargetVectors();
    void signingBufferVectors();
    void rawSpansSurviveDecoding();
    void storesImmutableItems();
    void storesMutableItemsBySequence();
    void casProtectsAgainstLostUpdates();
    void rejectsOversizedValuesAndSalt();
    void itemsExpire();
    void rfc8032Vectors_data();
    void rfc8032Vectors();
    void signAndVerifyRoundTrip();
    void rejectsTamperedSignatures();
};

void TestBep44::immutableTargetVector()
{
    // BEP 44: the bencoded value 12:Hello World! hashes to this target.
    QCOMPARE(bep44::immutableTarget("12:Hello World!").toHex(),
             QStringLiteral("e5f96f6f38320f0f33959cb4d3d656452117aadb"));
}

void TestBep44::mutableTargetVectors()
{
    const QByteArray key = QByteArray::fromHex("77ff84905a91936367c01360803104f92432fcd904a43511876df5cdf3e7e548");
    QCOMPARE(key.size(), bep44::PublicKeyBytes);

    // BEP 44 test vectors: without a salt, and with the salt "foobar".
    QCOMPARE(bep44::mutableTarget(key, QByteArray()).toHex(),
             QStringLiteral("4a533d47ec9c7d95b1ad75f576cffc641853b750"));
    QCOMPARE(bep44::mutableTarget(key, "foobar").toHex(),
             QStringLiteral("411eba73b6f087ca51a3795d9c8c938d365e32c1"));
}

void TestBep44::signingBufferVectors()
{
    // The sequence covered by a signature, from BEP 44.
    QCOMPARE(bep44::signingBuffer(QByteArray(), 1, "12:Hello World!"),
             QByteArray("3:seqi1e1:v12:Hello World!"));
    QCOMPARE(bep44::signingBuffer("foobar", 1, "12:Hello World!"),
             QByteArray("4:salt6:foobar3:seqi1e1:v12:Hello World!"));
}

void TestBep44::rawSpansSurviveDecoding()
{
    // A target is the hash of the value's bytes as sent, so the decoder has
    // to hand back the original span rather than a re-encoding.
    const QByteArray datagram = "d1:ad1:vli1ei2eee1:q3:pute";
    const BDecodeResult decoded = bdecode(datagram);
    QVERIFY(decoded.ok());
    const BValue *args = decoded.value.dictAt("a");
    QVERIFY(args);
    const BValue *value = args->find("v");
    QVERIFY(value);
    QCOMPARE(value->rawSpan(datagram), QByteArray("li1ei2ee"));
    QCOMPARE(bep44::immutableTarget(value->rawSpan(datagram)),
             bep44::immutableTarget("li1ei2ee"));
}

void TestBep44::storesImmutableItems()
{
    ItemStorage storage;
    const QByteArray value = "12:Hello World!";
    const NodeId target = bep44::immutableTarget(value);

    QCOMPARE(storage.putImmutable(target, value, 0), ItemStorage::PutResult::Stored);
    QCOMPARE(storage.immutableCount(), 1);
    const ImmutableItem *item = storage.immutableItem(target);
    QVERIFY(item);
    QCOMPARE(item->value, value);

    // Storing it again just refreshes the expiry.
    QCOMPARE(storage.putImmutable(target, value, 1000), ItemStorage::PutResult::Refreshed);
    QCOMPARE(storage.immutableCount(), 1);
    QCOMPARE(storage.immutableItem(target)->storedAt, qint64(1000));

    QVERIFY(!storage.immutableItem(NodeId::random()));
    QVERIFY(!storage.mutableItem(target));
}

void TestBep44::storesMutableItemsBySequence()
{
    ItemStorage storage;
    MutableItem item;
    item.publicKey = QByteArray(bep44::PublicKeyBytes, 'k');
    item.salt = "foobar";
    item.signature = QByteArray(bep44::SignatureBytes, 's');
    item.value = "12:Hello World!";
    item.sequence = 2;
    item.target = bep44::mutableTarget(item.publicKey, item.salt);

    QCOMPARE(storage.putMutable(item, std::nullopt, 0), ItemStorage::PutResult::Stored);
    QCOMPARE(storage.mutableCount(), 1);
    QCOMPARE(storage.mutableItem(item.target)->sequence, qint64(2));

    // An older sequence number is refused, the same one refreshes.
    MutableItem older = item;
    older.sequence = 1;
    QCOMPARE(storage.putMutable(older, std::nullopt, 10), ItemStorage::PutResult::SequenceTooLow);
    QCOMPARE(storage.mutableItem(item.target)->sequence, qint64(2));

    MutableItem same = item;
    QCOMPARE(storage.putMutable(same, std::nullopt, 20), ItemStorage::PutResult::Refreshed);

    MutableItem newer = item;
    newer.sequence = 3;
    newer.value = "3:new";
    QCOMPARE(storage.putMutable(newer, std::nullopt, 30), ItemStorage::PutResult::Stored);
    QCOMPARE(storage.mutableItem(item.target)->sequence, qint64(3));
    QCOMPARE(storage.mutableItem(item.target)->value, QByteArray("3:new"));
}

void TestBep44::casProtectsAgainstLostUpdates()
{
    ItemStorage storage;
    MutableItem item;
    item.publicKey = QByteArray(bep44::PublicKeyBytes, 'k');
    item.signature = QByteArray(bep44::SignatureBytes, 's');
    item.value = "1:a";
    item.sequence = 5;
    item.target = bep44::mutableTarget(item.publicKey, QByteArray());
    QCOMPARE(storage.putMutable(item, std::nullopt, 0), ItemStorage::PutResult::Stored);

    MutableItem update = item;
    update.sequence = 6;
    update.value = "1:b";
    QCOMPARE(storage.putMutable(update, 4, 10), ItemStorage::PutResult::CasMismatch);
    QCOMPARE(storage.mutableItem(item.target)->value, QByteArray("1:a"));
    QCOMPARE(storage.putMutable(update, 5, 10), ItemStorage::PutResult::Stored);
    QCOMPARE(storage.mutableItem(item.target)->value, QByteArray("1:b"));
}

void TestBep44::rejectsOversizedValuesAndSalt()
{
    ItemStorage storage;
    const QByteArray big(bep44::MaxValueBytes + 1, 'x');
    QCOMPARE(storage.putImmutable(bep44::immutableTarget(big), big, 0), ItemStorage::PutResult::TooBig);
    QCOMPARE(storage.immutableCount(), 0);

    MutableItem item;
    item.publicKey = QByteArray(bep44::PublicKeyBytes, 'k');
    item.signature = QByteArray(bep44::SignatureBytes, 's');
    item.salt = QByteArray(bep44::MaxSaltBytes + 1, 'z');
    item.value = "1:a";
    item.target = bep44::mutableTarget(item.publicKey, item.salt);
    QCOMPARE(storage.putMutable(item, std::nullopt, 0), ItemStorage::PutResult::SaltTooLong);
    QCOMPARE(storage.mutableCount(), 0);
}

void TestBep44::itemsExpire()
{
    ItemStorage storage;
    const QByteArray value = "1:a";
    const NodeId target = bep44::immutableTarget(value);
    storage.putImmutable(target, value, 0);

    MutableItem item;
    item.publicKey = QByteArray(bep44::PublicKeyBytes, 'k');
    item.signature = QByteArray(bep44::SignatureBytes, 's');
    item.value = value;
    item.sequence = 1;
    item.target = bep44::mutableTarget(item.publicKey, QByteArray());
    storage.putMutable(item, std::nullopt, bep44::ItemTtlMs / 2);

    storage.expire(bep44::ItemTtlMs);
    QCOMPARE(storage.immutableCount(), 0);
    QCOMPARE(storage.mutableCount(), 1);

    storage.expire(bep44::ItemTtlMs * 2);
    QCOMPARE(storage.mutableCount(), 0);
}

void TestBep44::rfc8032Vectors_data()
{
    QTest::addColumn<QByteArray>("seed");
    QTest::addColumn<QByteArray>("publicKey");
    QTest::addColumn<QByteArray>("message");
    QTest::addColumn<QByteArray>("signature");

    // RFC 8032 section 7.1.
    QTest::newRow("empty message")
        << QByteArray::fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
        << QByteArray::fromHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
        << QByteArray()
        << QByteArray::fromHex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc"
                               "61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    QTest::newRow("one byte")
        << QByteArray::fromHex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb")
        << QByteArray::fromHex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c")
        << QByteArray::fromHex("72")
        << QByteArray::fromHex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e4"
                               "58f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
}

void TestBep44::rfc8032Vectors()
{
    QFETCH(QByteArray, seed);
    QFETCH(QByteArray, publicKey);
    QFETCH(QByteArray, message);
    QFETCH(QByteArray, signature);

    QVERIFY(ed25519::available());

    const ed25519::KeyPair keys = ed25519::keyPairFromSeed(seed);
    QCOMPARE(keys.publicKey.toHex(), publicKey.toHex());
    QCOMPARE(ed25519::sign(message, keys.secretKey).toHex(), signature.toHex());
    QCOMPARE(ed25519::verify(signature, message, publicKey), std::optional<bool>(true));
}

void TestBep44::signAndVerifyRoundTrip()
{
    const ed25519::KeyPair keys = ed25519::randomKeyPair();
    QCOMPARE(keys.publicKey.size(), bep44::PublicKeyBytes);
    QCOMPARE(keys.secretKey.size(), 64);

    const QByteArray message = bep44::signingBuffer("foobar", 7, "12:Hello World!");
    const QByteArray signature = ed25519::sign(message, keys.secretKey);
    QCOMPARE(signature.size(), bep44::SignatureBytes);
    QCOMPARE(ed25519::verify(signature, message, keys.publicKey), std::optional<bool>(true));

    // A different key pair does not verify it.
    QCOMPARE(ed25519::verify(signature, message, ed25519::randomKeyPair().publicKey), std::optional<bool>(false));
}

void TestBep44::rejectsTamperedSignatures()
{
    const ed25519::KeyPair keys = ed25519::randomKeyPair();
    const QByteArray message = bep44::signingBuffer(QByteArray(), 1, "12:Hello World!");
    QByteArray signature = ed25519::sign(message, keys.secretKey);

    QByteArray tampered = signature;
    tampered[0] = char(tampered[0] ^ 0x01);
    QCOMPARE(ed25519::verify(tampered, message, keys.publicKey), std::optional<bool>(false));

    // A changed sequence number invalidates the signature, which is what stops
    // anyone else editing a mutable item.
    const QByteArray otherMessage = bep44::signingBuffer(QByteArray(), 2, "12:Hello World!");
    QCOMPARE(ed25519::verify(signature, otherMessage, keys.publicKey), std::optional<bool>(false));

    // Malformed inputs are refused rather than crashing.
    QCOMPARE(ed25519::verify(QByteArray(10, 'x'), message, keys.publicKey), std::optional<bool>(false));
    QCOMPARE(ed25519::verify(signature, message, QByteArray(10, 'k')), std::optional<bool>(false));
}

int runTestBep44(int argc, char **argv)
{
    TestBep44 test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestBep44.moc"
