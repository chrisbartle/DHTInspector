#include "dhtcore/Krpc.h"

namespace dht::krpc {

std::optional<NodeId> Message::senderId() const
{
    const auto id = body.stringAt("id");
    if (!id)
        return std::nullopt;
    return NodeId::fromBytes(*id);
}

ParseResult parse(QByteArrayView datagram)
{
    ParseResult out;

    BDecodeResult decoded = bdecode(datagram);
    out.warnings = decoded.warnings;
    if (!decoded.ok()) {
        out.error = QStringLiteral("bencode error at offset %1: %2")
                        .arg(decoded.error->offset)
                        .arg(decoded.error->message);
        return out;
    }

    const BValue &root = decoded.value;
    if (!root.isDict()) {
        out.error = QStringLiteral("top-level value is not a dictionary");
        return out;
    }

    const auto tid = root.stringAt("t");
    if (!tid) {
        out.error = QStringLiteral("missing transaction id 't'");
        return out;
    }
    out.transactionId = *tid;

    const auto y = root.stringAt("y");
    if (!y || y->size() != 1) {
        out.error = QStringLiteral("missing or invalid message type 'y'");
        return out;
    }

    Message message;
    message.transactionId = *tid;
    message.version = root.stringAt("v").value_or(QByteArray());
    if (const auto ip = root.stringAt("ip"))
        message.reportedAddress = Endpoint::fromCompact(*ip);
    if (const auto ro = root.integerAt("ro"))
        message.readOnly = (*ro == 1);

    switch (y->at(0)) {
    case 'q': {
        const auto method = root.stringAt("q");
        const BValue *args = root.dictAt("a");
        if (!method) {
            out.error = QStringLiteral("query without method name 'q'");
            return out;
        }
        if (!args) {
            out.error = QStringLiteral("query without argument dictionary 'a'");
            return out;
        }
        message.type = MessageType::Query;
        message.method = *method;
        message.body = *args;
        break;
    }
    case 'r': {
        const BValue *values = root.dictAt("r");
        if (!values) {
            out.error = QStringLiteral("response without return dictionary 'r'");
            return out;
        }
        message.type = MessageType::Response;
        message.body = *values;
        break;
    }
    case 'e': {
        message.type = MessageType::Error;
        const BValue *e = root.listAt("e");
        const auto &list = e ? e->toList() : BValue::List();
        if (!list.empty() && list[0].isInteger()) {
            message.errorCode = list[0].toInteger();
            if (list.size() > 1 && list[1].isString())
                message.errorMessage = list[1].toString();
        } else {
            out.warnings << QStringLiteral("error message without a [code, message] list");
        }
        break;
    }
    default:
        out.error = QStringLiteral("unknown message type '%1'").arg(QString::fromLatin1(*y));
        return out;
    }

    out.message = std::move(message);
    return out;
}

QByteArray encodeQuery(const QByteArray &transactionId, const QByteArray &method,
                       BValue::Dict arguments, const QByteArray &version)
{
    BValue::Dict root;
    root.emplace("t", transactionId);
    root.emplace("y", "q");
    root.emplace("q", method);
    root.emplace("a", std::move(arguments));
    if (!version.isEmpty())
        root.emplace("v", version);
    return bencode(BValue(std::move(root)));
}

QByteArray encodeResponse(const QByteArray &transactionId, BValue::Dict values,
                          const QByteArray &version, const Endpoint &requester)
{
    BValue::Dict root;
    root.emplace("t", transactionId);
    root.emplace("y", "r");
    root.emplace("r", std::move(values));
    if (requester.isValid())
        root.emplace("ip", requester.toCompact());
    if (!version.isEmpty())
        root.emplace("v", version);
    return bencode(BValue(std::move(root)));
}

QByteArray encodeError(const QByteArray &transactionId, int code, const QByteArray &message,
                       const QByteArray &version)
{
    BValue::Dict root;
    root.emplace("t", transactionId);
    root.emplace("y", "e");
    root.emplace("e", BValue::List{BValue(code), BValue(message)});
    if (!version.isEmpty())
        root.emplace("v", version);
    return bencode(BValue(std::move(root)));
}

QByteArray encodeNodes(const std::vector<CompactNode> &nodes, Family family)
{
    QByteArray out;
    for (const CompactNode &node : nodes) {
        if (node.endpoint.family() != family)
            continue;
        out += node.id.toBytes();
        out += node.endpoint.toCompact();
    }
    return out;
}

DecodedNodes decodeNodes(QByteArrayView data, Family family)
{
    DecodedNodes out;
    const qsizetype entry = family == Family::IPv4 ? 26 : 38;
    out.malformed = (data.size() % entry) != 0;
    for (qsizetype offset = 0; offset + entry <= data.size(); offset += entry) {
        const auto id = NodeId::fromBytes(data.sliced(offset, NodeId::Size));
        const auto endpoint = Endpoint::fromCompact(data.sliced(offset + NodeId::Size, entry - NodeId::Size));
        if (id && endpoint)
            out.nodes.push_back({*id, *endpoint});
    }
    return out;
}

std::vector<Endpoint> decodePeers(const BValue *values, Family family)
{
    std::vector<Endpoint> out;
    if (!values || !values->isList())
        return out;
    const qsizetype expected = family == Family::IPv4 ? 6 : 18;
    for (const BValue &item : values->toList()) {
        if (!item.isString())
            continue;
        const QByteArray bytes = item.toString();
        if (bytes.size() != expected)
            continue;
        if (const auto endpoint = Endpoint::fromCompact(bytes))
            out.push_back(*endpoint);
    }
    return out;
}

} // namespace dht::krpc
