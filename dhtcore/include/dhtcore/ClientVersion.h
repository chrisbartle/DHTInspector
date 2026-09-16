#pragma once

#include <QByteArray>
#include <QString>

namespace dht {

// What a node says about its own software, from the KRPC "v" field (BEP 5:
// a two-character client code followed by two version bytes). It is not
// authenticated, so it is a claim rather than a fact.
//
// Kept apart from the UI so that per-node views and network-wide tallies
// decode the field the same way.
struct ClientInfo
{
    enum class Kind {
        Absent,       // no "v" at all; some clients (e.g. Transmission) never send one
        Known,        // a client code we have a name for
        UnknownCode,  // printable two-character code we do not recognise
        Nonstandard,  // not four bytes, or the code is not printable
    };

    Kind kind = Kind::Absent;
    QByteArray raw;   // the field exactly as sent
    QString code;     // the two-character client code, when printable
    QString name;     // e.g. "libtorrent (Rasterbar)"; empty when not known
    QString note;     // what else identifies the client, e.g. which apps use it
    QString version;  // e.g. "2.0.11"; empty when the byte layout is unpublished

    // One line for display, e.g. "libtorrent (Rasterbar) 2.0.11",
    // "unknown client ZZ" or "no version sent".
    QString display() const;

    // The raw field printed for a human: escaped text plus hex bytes.
    QString rawText() const;
};

ClientInfo decodeClientVersion(const QByteArray &v);

// Name for a two-character client code, empty when unknown.
QString clientName(const QString &code);

QString clientKindName(ClientInfo::Kind kind);

} // namespace dht
