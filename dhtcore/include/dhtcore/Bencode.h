#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include <map>
#include <optional>
#include <vector>

namespace dht {

class BValue
{
public:
    enum class Type { Invalid, Integer, String, List, Dict, PreEncoded };

    using List = std::vector<BValue>;
    using Dict = std::map<QByteArray, BValue>;

    BValue() = default;
    BValue(qint64 value) : m_type(Type::Integer), m_integer(value) {}
    BValue(int value) : m_type(Type::Integer), m_integer(value) {}
    BValue(const QByteArray &value) : m_type(Type::String), m_string(value) {}
    BValue(const char *value) : m_type(Type::String), m_string(value) {}
    BValue(List value) : m_type(Type::List), m_list(std::move(value)) {}
    BValue(Dict value) : m_type(Type::Dict), m_dict(std::move(value)) {}

    // A value that is already bencoded and must be emitted byte for byte,
    // so a BEP 44 item can be echoed exactly as it was stored.
    static BValue preEncoded(const QByteArray &bytes);

    Type type() const { return m_type; }
    bool isValid() const { return m_type != Type::Invalid; }
    bool isPreEncoded() const { return m_type == Type::PreEncoded; }
    bool isInteger() const { return m_type == Type::Integer; }
    bool isString() const { return m_type == Type::String; }
    bool isList() const { return m_type == Type::List; }
    bool isDict() const { return m_type == Type::Dict; }

    qint64 toInteger(qint64 fallback = 0) const { return isInteger() ? m_integer : fallback; }
    QByteArray toPreEncodedBytes() const { return isPreEncoded() ? m_string : QByteArray(); }
    QByteArray toString() const { return isString() ? m_string : QByteArray(); }
    const List &toList() const;
    const Dict &toDict() const;

    // Dictionary lookups. All return empty/nullptr when this is not a dict,
    // the key is absent, or the value has the wrong type.
    const BValue *find(const QByteArray &key) const;
    std::optional<QByteArray> stringAt(const QByteArray &key) const;
    std::optional<qint64> integerAt(const QByteArray &key) const;
    const BValue *listAt(const QByteArray &key) const;
    const BValue *dictAt(const QByteArray &key) const;

    // Where this value sat in the decoded input. BEP 44 targets are hashes of
    // the bytes as sent, which a re-encoding could change. Empty for values
    // built in code.
    QByteArray rawSpan(QByteArrayView source) const;
    void setRawSpan(qsizetype start, qsizetype length);  // used by the decoder

private:
    Type m_type = Type::Invalid;
    qint64 m_integer = 0;
    QByteArray m_string;
    List m_list;
    Dict m_dict;
    qsizetype m_rawStart = -1;
    qsizetype m_rawLength = 0;
};

struct BDecodeError
{
    qsizetype offset = 0;
    QString message;
};

struct BDecodeResult
{
    BValue value;
    std::optional<BDecodeError> error;
    // Non-fatal deviations from canonical bencode: leading zeros, unsorted
    // or duplicate dictionary keys, trailing bytes. Reported, not rejected,
    // because real clients produce them and a diagnostic tool should say so.
    QStringList warnings;

    bool ok() const { return !error.has_value(); }
};

BDecodeResult bdecode(QByteArrayView data, int maxDepth = 32);

// Always produces canonical bencode (dictionary keys sorted by raw bytes).
QByteArray bencode(const BValue &value);

} // namespace dht
