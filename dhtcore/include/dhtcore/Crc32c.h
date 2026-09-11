#pragma once

#include <QByteArrayView>

namespace dht {

// CRC-32C (Castagnoli), as required by BEP 42.
quint32 crc32c(QByteArrayView data);

} // namespace dht
