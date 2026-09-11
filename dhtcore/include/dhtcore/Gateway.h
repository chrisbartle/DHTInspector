#pragma once

#include <QHostAddress>

namespace dht {

// The IPv4 default gateway, or a null address when there is none or the
// platform is not supported (currently Windows and Linux only).
QHostAddress defaultGatewayIpv4();

} // namespace dht
