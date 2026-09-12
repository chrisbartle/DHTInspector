#include "dhtcore/Bep44.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

extern "C" {
#include "monocypher-ed25519.h"
}

namespace dht::bep44 {

NodeId immutableTarget(QByteArrayView bencodedValue)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(bencodedValue);
    return *NodeId::fromBytes(hash.result());
}

NodeId mutableTarget(QByteArrayView publicKey, QByteArrayView salt)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(publicKey);
    hash.addData(salt);
    return *NodeId::fromBytes(hash.result());
}

QByteArray signingBuffer(QByteArrayView salt, qint64 sequence, QByteArrayView bencodedValue)
{
    QByteArray buffer;
    if (!salt.isEmpty()) {
        buffer += "4:salt";
        buffer += QByteArray::number(salt.size());
        buffer += ':';
        buffer += salt.toByteArray();
    }
    buffer += "3:seqi";
    buffer += QByteArray::number(sequence);
    buffer += "e1:v";
    buffer += bencodedValue.toByteArray();
    return buffer;
}

} // namespace dht::bep44

namespace dht::ed25519 {

// RFC 8032 Ed25519, from the vendored Monocypher. Monocypher's own EdDSA uses
// BLAKE2b; BEP 44 needs the SHA-512 variant, which is what these
// crypto_ed25519_* functions are.

bool available()
{
    return true;
}

std::optional<bool> verify(QByteArrayView signature, QByteArrayView message, QByteArrayView publicKey)
{
    if (signature.size() != bep44::SignatureBytes || publicKey.size() != bep44::PublicKeyBytes)
        return false;
    const int result = crypto_ed25519_check(
        reinterpret_cast<const uint8_t *>(signature.data()),
        reinterpret_cast<const uint8_t *>(publicKey.data()),
        reinterpret_cast<const uint8_t *>(message.data()),
        size_t(message.size()));
    return result == 0;
}

KeyPair keyPairFromSeed(QByteArrayView seed)
{
    if (seed.size() != 32)
        return {};
    QByteArray secret(64, Qt::Uninitialized);
    QByteArray publicKey(bep44::PublicKeyBytes, Qt::Uninitialized);
    QByteArray seedCopy = seed.toByteArray();  // crypto_ed25519_key_pair wipes it
    crypto_ed25519_key_pair(reinterpret_cast<uint8_t *>(secret.data()),
                            reinterpret_cast<uint8_t *>(publicKey.data()),
                            reinterpret_cast<uint8_t *>(seedCopy.data()));
    return KeyPair{publicKey, secret};
}

KeyPair randomKeyPair()
{
    QByteArray seed(32, Qt::Uninitialized);
    QRandomGenerator::system()->generate(seed.begin(), seed.end());
    return keyPairFromSeed(seed);
}

QByteArray sign(QByteArrayView message, QByteArrayView secretKey)
{
    if (secretKey.size() != 64)
        return {};
    QByteArray signature(bep44::SignatureBytes, Qt::Uninitialized);
    crypto_ed25519_sign(reinterpret_cast<uint8_t *>(signature.data()),
                        reinterpret_cast<const uint8_t *>(secretKey.data()),
                        reinterpret_cast<const uint8_t *>(message.data()),
                        size_t(message.size()));
    return signature;
}

} // namespace dht::ed25519
