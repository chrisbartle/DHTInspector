#include "dhtcore/Crc32c.h"

#include <array>

namespace dht {

namespace {

constexpr std::array<quint32, 256> makeTable()
{
    std::array<quint32, 256> table{};
    for (quint32 i = 0; i < 256; ++i) {
        quint32 c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (c >> 1) ^ 0x82F63B78u : (c >> 1);
        table[i] = c;
    }
    return table;
}

constexpr auto kTable = makeTable();

} // namespace

quint32 crc32c(QByteArrayView data)
{
    quint32 crc = 0xFFFFFFFFu;
    for (char ch : data)
        crc = kTable[(crc ^ quint8(ch)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

} // namespace dht
