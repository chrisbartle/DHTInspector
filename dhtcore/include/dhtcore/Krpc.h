#pragma once

#include "dhtcore/Bencode.h"
#include "dhtcore/Endpoint.h"
#include "dhtcore/NodeId.h"

#include <optional>
#include <vector>

namespace dht::krpc {

enum ErrorCode {
    GenericError = 201,
    ServerError = 202,
    ProtocolError = 203,
    MethodUnknown = 204,
};

enum class MessageType { Query, Response, Error };

struct Message
{
    MessageType type = MessageType::Query;
    QByteArray transactionId;
    QByteArray method;                        // queries only
    BValue body;                              // "a" for queries, "r" for responses
    qint64 errorCode = 0;                     // errors only
    QByteArray errorMessage;
    QByteArray version;                       // "v", if present
    std::optional<Endpoint> reportedAddress;  // "ip" (BEP 42): how the sender sees us
    bool readOnly = false;                    // "ro" (BEP 43)

    // The "id" argument/return value, if present and well formed.
    std::optional<NodeId> senderId() const;
};

struct ParseResult
{
    std::optional<Message> message;
    QString error;
    QByteArray transactionId;  // recovered whenever "t" was readable
    QStringList warnings;
};

ParseResult parse(QByteArrayView datagram);

QByteArray encodeQuery(const QByteArray &transactionId, const QByteArray &method,
                       BValue::Dict arguments, const QByteArray &version);

QByteArray encodeResponse(const QByteArray &transactionId, BValue::Dict values,
                          const QByteArray &version, const Endpoint &requester);

QByteArray encodeError(const QByteArray &transactionId, int code, const QByteArray &message,
                       const QByteArray &version);

struct CompactNode
{
    NodeId id;
    Endpoint endpoint;
};

// BEP 5 "nodes" (26 bytes each) or BEP 32 "nodes6" (38 bytes each).
// Nodes of the other family are skipped.
QByteArray encodeNodes(const std::vector<CompactNode> &nodes, Family family);

struct DecodedNodes
{
    std::vector<CompactNode> nodes;
    bool malformed = false;  // length was not a multiple of the entry size
};

DecodedNodes decodeNodes(QByteArrayView data, Family family);

// Decodes a get_peers "values" list, keeping entries of the given family.
std::vector<Endpoint> decodePeers(const BValue *values, Family family);

} // namespace dht::krpc
