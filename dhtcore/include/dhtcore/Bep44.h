#pragma once

#include "dhtcore/NodeId.h"

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

// BEP 44: storing arbitrary data in the DHT.
namespace dht::bep44 {

constexpr int MaxValueBytes = 1000;
constexpr int MaxSaltBytes = 64;
constexpr int PublicKeyBytes = 32;
constexpr int SignatureBytes = 64;

// Items are dropped if nobody refreshes them. BEP 44 suggests two hours.
constexpr qint64 ItemTtlMs = 2 * 60 * 60 * 1000;

// Errors specific to BEP 44, alongside the BEP 5 ones.
enum ErrorCode {
    MessageTooBig = 205,
    InvalidSignature = 206,
    SaltTooLong = 207,
    CasMismatch = 301,
    SequenceTooLow = 302,
};

// SHA-1 of the value's bencoded bytes, exactly as they were sent.
NodeId immutableTarget(QByteArrayView bencodedValue);

// SHA-1 of the public key followed by the salt.
NodeId mutableTarget(QByteArrayView publicKey, QByteArrayView salt);

// The byte sequence a mutable item's signature covers: the salt (when
// present), seq and v as they would appear in a dictionary, without the
// enclosing "d" and "e".
QByteArray signingBuffer(QByteArrayView salt, qint64 sequence, QByteArrayView bencodedValue);

} // namespace dht::bep44

namespace dht::ed25519 {

// Whether signature verification is available. Mutable BEP 44 items cannot
// be accepted without it, because storing them unverified would let anyone
// overwrite anyone else's data.
bool available();

// Returns nullopt when no implementation is linked in.
std::optional<bool> verify(QByteArrayView signature, QByteArrayView message, QByteArrayView publicKey);

struct KeyPair
{
    QByteArray publicKey;  // 32 bytes
    QByteArray secretKey;  // 64 bytes, as RFC 8032 expands a 32-byte seed
};

// RFC 8032 key pair from a 32-byte seed, or from fresh random bytes.
KeyPair keyPairFromSeed(QByteArrayView seed);
KeyPair randomKeyPair();

// Empty when no implementation is linked in.
QByteArray sign(QByteArrayView message, QByteArrayView secretKey);

} // namespace dht::ed25519
