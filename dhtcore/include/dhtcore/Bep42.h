#pragma once

#include "dhtcore/NodeId.h"

#include <QHostAddress>

namespace dht::bep42 {

enum class Status {
    Unknown,      // external address not established yet
    Compliant,
    NonCompliant,
    Exempt,       // address is on a local network, BEP 42 does not apply
};

QString statusName(Status status);

bool isExempt(const QHostAddress &address);

// Generates a BEP 42 node ID for `address`. `rand` is the low byte that
// selects the masked variant; the remaining free bits are random.
NodeId generate(const QHostAddress &address, quint8 rand);
NodeId generate(const QHostAddress &address);

bool isCompliant(const NodeId &id, const QHostAddress &address);

Status check(const NodeId &id, const QHostAddress &address);

} // namespace dht::bep42
