#include "dhtcore/Bencode.h"

#include <limits>

namespace dht {

BValue BValue::preEncoded(const QByteArray &bytes)
{
    BValue value;
    value.m_type = Type::PreEncoded;
    value.m_string = bytes;
    return value;
}

QByteArray BValue::rawSpan(QByteArrayView source) const
{
    if (m_rawStart < 0 || m_rawLength <= 0 || m_rawStart + m_rawLength > source.size())
        return {};
    return source.sliced(m_rawStart, m_rawLength).toByteArray();
}

void BValue::setRawSpan(qsizetype start, qsizetype length)
{
    m_rawStart = start;
    m_rawLength = length;
}

const BValue::List &BValue::toList() const
{
    static const List empty;
    return isList() ? m_list : empty;
}

const BValue::Dict &BValue::toDict() const
{
    static const Dict empty;
    return isDict() ? m_dict : empty;
}

const BValue *BValue::find(const QByteArray &key) const
{
    if (!isDict())
        return nullptr;
    const auto it = m_dict.find(key);
    return it == m_dict.end() ? nullptr : &it->second;
}

std::optional<QByteArray> BValue::stringAt(const QByteArray &key) const
{
    const BValue *v = find(key);
    if (!v || !v->isString())
        return std::nullopt;
    return v->m_string;
}

std::optional<qint64> BValue::integerAt(const QByteArray &key) const
{
    const BValue *v = find(key);
    if (!v || !v->isInteger())
        return std::nullopt;
    return v->m_integer;
}

const BValue *BValue::listAt(const QByteArray &key) const
{
    const BValue *v = find(key);
    return v && v->isList() ? v : nullptr;
}

const BValue *BValue::dictAt(const QByteArray &key) const
{
    const BValue *v = find(key);
    return v && v->isDict() ? v : nullptr;
}

namespace {

class Decoder
{
public:
    Decoder(QByteArrayView data, int maxDepth, BDecodeResult &result)
        : m_data(data), m_maxDepth(maxDepth), m_result(result)
    {
    }

    void run()
    {
        BValue value;
        if (!parseValue(value, 0))
            return;
        m_result.value = std::move(value);
        if (m_pos < m_data.size())
            warn(QStringLiteral("%1 trailing byte(s) after the top-level value").arg(m_data.size() - m_pos));
    }

private:
    bool fail(const QString &message)
    {
        if (!m_result.error)
            m_result.error = BDecodeError{m_pos, message};
        return false;
    }

    void warn(const QString &message)
    {
        m_result.warnings << QStringLiteral("offset %1: %2").arg(m_pos).arg(message);
    }

    bool atEnd() const { return m_pos >= m_data.size(); }
    char peek() const { return m_data[m_pos]; }
    static bool isDigit(char c) { return c >= '0' && c <= '9'; }

    bool parseValue(BValue &out, int depth)
    {
        const qsizetype start = m_pos;
        if (!parseValueInner(out, depth))
            return false;
        out.setRawSpan(start, m_pos - start);
        return true;
    }

    bool parseValueInner(BValue &out, int depth)
    {
        if (atEnd())
            return fail(QStringLiteral("unexpected end of data"));
        const char c = peek();
        if (c == 'i')
            return parseInteger(out);
        if (isDigit(c)) {
            QByteArray s;
            if (!parseString(s))
                return false;
            out = BValue(s);
            return true;
        }
        if (c == 'l' || c == 'd') {
            if (depth >= m_maxDepth)
                return fail(QStringLiteral("nesting deeper than %1 levels").arg(m_maxDepth));
            return c == 'l' ? parseList(out, depth) : parseDict(out, depth);
        }
        return fail(QStringLiteral("unexpected byte 0x%1").arg(quint8(c), 2, 16, QLatin1Char('0')));
    }

    bool parseInteger(BValue &out)
    {
        const qsizetype start = m_pos;
        ++m_pos; // 'i'
        bool negative = false;
        if (!atEnd() && peek() == '-') {
            negative = true;
            ++m_pos;
        }
        const qsizetype digitsStart = m_pos;
        quint64 magnitude = 0;
        while (!atEnd() && isDigit(peek())) {
            const quint64 digit = quint64(peek() - '0');
            if (magnitude > (std::numeric_limits<quint64>::max() - digit) / 10)
                return fail(QStringLiteral("integer out of range"));
            magnitude = magnitude * 10 + digit;
            ++m_pos;
        }
        const qsizetype digitCount = m_pos - digitsStart;
        if (atEnd())
            return fail(QStringLiteral("unterminated integer starting at offset %1").arg(start));
        if (peek() != 'e')
            return fail(QStringLiteral("invalid character in integer"));
        if (digitCount == 0)
            return fail(QStringLiteral("integer has no digits"));

        const quint64 limit = negative ? quint64(std::numeric_limits<qint64>::max()) + 1
                                       : quint64(std::numeric_limits<qint64>::max());
        if (magnitude > limit)
            return fail(QStringLiteral("integer out of range"));

        if (digitCount > 1 && m_data[digitsStart] == '0')
            warn(QStringLiteral("integer has leading zeros"));
        if (negative && magnitude == 0)
            warn(QStringLiteral("negative zero"));

        ++m_pos; // 'e'
        out = BValue(negative ? qint64(0 - magnitude) : qint64(magnitude));
        return true;
    }

    bool parseString(QByteArray &out)
    {
        const qsizetype digitsStart = m_pos;
        qint64 length = 0;
        while (!atEnd() && isDigit(peek())) {
            if (m_pos - digitsStart >= 10)
                return fail(QStringLiteral("string length prefix too long"));
            length = length * 10 + (peek() - '0');
            ++m_pos;
        }
        if (atEnd())
            return fail(QStringLiteral("unterminated string length"));
        if (peek() != ':')
            return fail(QStringLiteral("expected ':' after string length"));
        if (m_pos - digitsStart > 1 && m_data[digitsStart] == '0')
            warn(QStringLiteral("string length has leading zeros"));
        ++m_pos; // ':'
        const qsizetype remaining = m_data.size() - m_pos;
        if (length > remaining)
            return fail(QStringLiteral("string length %1 exceeds the %2 byte(s) remaining").arg(length).arg(remaining));
        out = m_data.sliced(m_pos, qsizetype(length)).toByteArray();
        m_pos += qsizetype(length);
        return true;
    }

    bool parseList(BValue &out, int depth)
    {
        const qsizetype start = m_pos;
        ++m_pos; // 'l'
        BValue::List items;
        while (true) {
            if (atEnd())
                return fail(QStringLiteral("unterminated list starting at offset %1").arg(start));
            if (peek() == 'e') {
                ++m_pos;
                break;
            }
            BValue item;
            if (!parseValue(item, depth + 1))
                return false;
            items.push_back(std::move(item));
        }
        out = BValue(std::move(items));
        return true;
    }

    bool parseDict(BValue &out, int depth)
    {
        const qsizetype start = m_pos;
        ++m_pos; // 'd'
        BValue::Dict dict;
        std::optional<QByteArray> previousKey;
        while (true) {
            if (atEnd())
                return fail(QStringLiteral("unterminated dictionary starting at offset %1").arg(start));
            if (peek() == 'e') {
                ++m_pos;
                break;
            }
            if (!isDigit(peek()))
                return fail(QStringLiteral("dictionary key must be a string"));
            QByteArray key;
            if (!parseString(key))
                return false;

            const bool duplicate = dict.find(key) != dict.end();
            if (duplicate)
                warn(QStringLiteral("duplicate dictionary key \"%1\"").arg(QString::fromLatin1(key.toPercentEncoding())));
            else if (previousKey && key < *previousKey)
                warn(QStringLiteral("dictionary keys not sorted"));
            previousKey = key;

            BValue value;
            if (!parseValue(value, depth + 1))
                return false;
            if (!duplicate)
                dict.emplace(std::move(key), std::move(value));
        }
        out = BValue(std::move(dict));
        return true;
    }

    QByteArrayView m_data;
    qsizetype m_pos = 0;
    int m_maxDepth;
    BDecodeResult &m_result;
};

void encodeInto(const BValue &value, QByteArray &out)
{
    switch (value.type()) {
    case BValue::Type::Invalid:
        break;
    case BValue::Type::Integer:
        out += 'i';
        out += QByteArray::number(value.toInteger());
        out += 'e';
        break;
    case BValue::Type::PreEncoded:
        out += value.toPreEncodedBytes();
        break;
    case BValue::Type::String: {
        const QByteArray s = value.toString();
        out += QByteArray::number(s.size());
        out += ':';
        out += s;
        break;
    }
    case BValue::Type::List:
        out += 'l';
        for (const BValue &item : value.toList())
            encodeInto(item, out);
        out += 'e';
        break;
    case BValue::Type::Dict:
        out += 'd';
        for (const auto &[key, item] : value.toDict()) {
            out += QByteArray::number(key.size());
            out += ':';
            out += key;
            encodeInto(item, out);
        }
        out += 'e';
        break;
    }
}

} // namespace

BDecodeResult bdecode(QByteArrayView data, int maxDepth)
{
    BDecodeResult result;
    Decoder(data, maxDepth, result).run();
    return result;
}

QByteArray bencode(const BValue &value)
{
    QByteArray out;
    encodeInto(value, out);
    return out;
}

} // namespace dht
