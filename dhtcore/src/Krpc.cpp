#include "dhtcore/Krpc.h"

#include <algorithm>
#include <utility>

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
                       BValue::Dict arguments, const QByteArray &version, bool readOnly)
{
    BValue::Dict root;
    root.emplace("t", transactionId);
    root.emplace("y", "q");
    root.emplace("q", method);
    root.emplace("a", std::move(arguments));
    if (readOnly)
        root.emplace("ro", BValue(1));  // BEP 43
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

namespace {

bool isPrintable(const QByteArray &bytes)
{
    return std::all_of(bytes.begin(), bytes.end(), [](char c) { return c >= 0x20 && c < 0x7f; });
}

QString indentOf(int depth)
{
    return QString(depth * 2, QLatin1Char(' '));
}

// Keys whose contents are compact addresses or hashes rather than text.
QString describeValue(const BValue &value, const QByteArray &key, int depth);

QString describeCompactNodes(const QByteArray &bytes, Family family, int depth)
{
    const DecodedNodes decoded = decodeNodes(bytes, family);
    QString out = QStringLiteral("%1 node(s)%2\n")
                      .arg(decoded.nodes.size())
                      .arg(decoded.malformed ? QStringLiteral(", trailing bytes ignored") : QString());
    for (const CompactNode &node : decoded.nodes)
        out += indentOf(depth + 1) + node.id.toHex() + QStringLiteral("  ") + node.endpoint.toString() + QLatin1Char('\n');
    return out;
}

QString describeValue(const BValue &value, const QByteArray &key, int depth)
{
    switch (value.type()) {
    case BValue::Type::Integer:
        return QString::number(value.toInteger()) + QLatin1Char('\n');
    case BValue::Type::String: {
        const QByteArray bytes = value.toString();
        if (key == "nodes")
            return describeCompactNodes(bytes, Family::IPv4, depth);
        if (key == "nodes6")
            return describeCompactNodes(bytes, Family::IPv6, depth);
        if (key == "ip") {
            if (const auto endpoint = Endpoint::fromCompact(bytes))
                return endpoint->toString() + QLatin1Char('\n');
        }
        if (key == "id" || key == "target" || key == "info_hash" || key == "token" || key == "k"
            || key == "sig" || key == "nonce") {
            return QString::fromLatin1(bytes.toHex()) + QStringLiteral(" (%1 bytes)\n").arg(bytes.size());
        }
        if (isPrintable(bytes))
            return QLatin1Char('"') + QString::fromLatin1(bytes) + QStringLiteral("\"\n");
        return QString::fromLatin1(bytes.toHex()) + QStringLiteral(" (%1 bytes)\n").arg(bytes.size());
    }
    case BValue::Type::List: {
        const BValue::List &items = value.toList();
        if (key == "values") {
            QString out = QStringLiteral("%1 peer(s)\n").arg(items.size());
            for (const BValue &item : items) {
                const QByteArray bytes = item.toString();
                const auto endpoint = Endpoint::fromCompact(bytes);
                out += indentOf(depth + 1)
                       + (endpoint ? endpoint->toString() : escapeBytes(bytes)) + QLatin1Char('\n');
            }
            return out;
        }
        QString out = QStringLiteral("%1 item(s)\n").arg(items.size());
        for (const BValue &item : items)
            out += indentOf(depth + 1) + describeValue(item, {}, depth + 1);
        return out;
    }
    case BValue::Type::Dict: {
        QString out = QStringLiteral("\n");
        for (const auto &[childKey, childValue] : value.toDict()) {
            out += indentOf(depth + 1) + QString::fromLatin1(childKey) + QStringLiteral(": ")
                   + describeValue(childValue, childKey, depth + 1);
        }
        return out;
    }
    case BValue::Type::PreEncoded:
        return escapeBytes(value.toPreEncodedBytes()) + QLatin1Char('\n');
    case BValue::Type::Invalid:
        break;
    }
    return QStringLiteral("(empty)\n");
}

} // namespace

QString escapeBytes(QByteArrayView bytes)
{
    QString out;
    out.reserve(int(bytes.size()));
    for (char c : bytes) {
        if (c >= 0x20 && c < 0x7f)
            out += QLatin1Char(c);
        else
            out += QStringLiteral("\\x%1").arg(quint8(c), 2, 16, QLatin1Char('0'));
    }
    return out;
}

QString describe(const Message &message)
{
    QString out;
    out += QStringLiteral("transaction id: %1 (%2)\n")
               .arg(escapeBytes(message.transactionId), QString::fromLatin1(message.transactionId.toHex()));
    switch (message.type) {
    case MessageType::Query:
        out += QStringLiteral("type: query\nmethod: %1\n").arg(QString::fromLatin1(message.method));
        break;
    case MessageType::Response:
        out += QStringLiteral("type: response\n");
        break;
    case MessageType::Error:
        out += QStringLiteral("type: error\ncode: %1\nmessage: %2\n")
                   .arg(message.errorCode)
                   .arg(escapeBytes(message.errorMessage));
        break;
    }
    if (!message.version.isEmpty()) {
        out += QStringLiteral("version: %1 (%2)\n")
                   .arg(escapeBytes(message.version), QString::fromLatin1(message.version.toHex()));
    }
    if (message.reportedAddress)
        out += QStringLiteral("ip (how it sees us): %1\n").arg(message.reportedAddress->toString());
    if (message.readOnly)
        out += QStringLiteral("read-only: yes\n");

    if (message.body.isDict()) {
        out += message.type == MessageType::Query ? QStringLiteral("a:") : QStringLiteral("r:");
        out += describeValue(message.body, {}, 0);
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
