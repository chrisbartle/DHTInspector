#include "dhtcore/ClientVersion.h"

#include "dhtcore/Krpc.h"

#include <QHash>

namespace dht {

namespace {

struct KnownClient
{
    const char *name;
    const char *note;
};

// Names are UTF-8; the micro sign is spelled as bytes so the source
// encoding does not matter. Azureus-style client codes as used in peer IDs, which most clients reuse
// in the DHT "v" field. Case matters: "LT" and "lt" are different projects.
const QHash<QString, KnownClient> &knownClients()
{
    static const QHash<QString, KnownClient> table = {
        {QStringLiteral("LT"), {"libtorrent (Rasterbar)", "the engine behind qBittorrent, Deluge and many others"}},
        {QStringLiteral("lt"), {"libTorrent (Rakshasa)", "the engine behind rtorrent"}},
        {QStringLiteral("UT"), {"\xc2\xb5" "Torrent", "version layout unpublished"}},
        {QStringLiteral("UM"), {"\xc2\xb5" "Torrent for Mac", "version layout unpublished"}},
        {QStringLiteral("BT"), {"BitTorrent (mainline)", "version layout unpublished"}},
        {QStringLiteral("TR"), {"Transmission", ""}},
        {QStringLiteral("AZ"), {"Vuze (Azureus)", ""}},
        {QStringLiteral("BI"), {"BiglyBT", ""}},
        {QStringLiteral("BC"), {"BitComet", ""}},
        {QStringLiteral("KT"), {"KTorrent", ""}},
        {QStringLiteral("qB"), {"qBittorrent", ""}},
        {QStringLiteral("DE"), {"Deluge", ""}},
        {QStringLiteral("FW"), {"FrostWire", ""}},
        {QStringLiteral("XL"), {"Xunlei (Thunder)", ""}},
        {QStringLiteral("SD"), {"Xunlei (Thunder)", ""}},
        {QStringLiteral("QD"), {"QQDownload", ""}},
        {QStringLiteral("TT"), {"TuoTu", ""}},
        {QStringLiteral("MO"), {"MonoTorrent", ""}},
        {QStringLiteral("ML"), {"MLDonkey", ""}},
        {QStringLiteral("WW"), {"WebTorrent", ""}},
        {QStringLiteral("WD"), {"WebTorrent Desktop", ""}},
        {QStringLiteral("FD"), {"Free Download Manager", ""}},
        {QStringLiteral("HL"), {"Halite", ""}},
        {QStringLiteral("BF"), {"Bitflu", ""}},
        {QStringLiteral("LW"), {"LimeWire", ""}},
        {QStringLiteral("SZ"), {"Shareaza", ""}},
        {QStringLiteral("DI"), {"DHT Inspector", "this tool"}},
    };
    return table;
}

bool printable(char c)
{
    return c >= 0x21 && c <= 0x7e;
}

// Version layouts below are taken from each project's source; clients whose
// layout is not published are left undecoded rather than guessed at.
QString decodeVersion(const QString &code, quint8 a, quint8 b)
{
    if (code == QLatin1String("LT")) {
        // Up to 1.1: {major, minor}. From 1.2/2.0: {major, minor << 4 | tiny}.
        // The two cannot collide: the old layout never had a minor >= 16, and
        // the new one starts at 1.2, so a major of 1 with b < 0x10 is 1.0/1.1.
        const bool packed = a >= 2 || (a == 1 && b >= 0x10);
        if (packed)
            return QStringLiteral("%1.%2.%3").arg(a).arg(b >> 4).arg(b & 0x0f);
        return QStringLiteral("%1.%2").arg(a).arg(b);
    }
    if (code == QLatin1String("lt")) {
        // Major is always 0. Up to 0.13 the patch sat in the high nibble
        // ("lt\x0D\x80" is 0.13.8); from 0.14 it is the whole byte
        // ("lt\x10\x17" is 0.16.23).
        const int patch = a <= 13 ? b >> 4 : b;
        return QStringLiteral("0.%1.%2").arg(a).arg(patch);
    }
    if (code == QLatin1String("DI"))
        return QStringLiteral("%1.%2").arg(a).arg(b);
    return {};
}

} // namespace

QString clientName(const QString &code)
{
    const auto it = knownClients().constFind(code);
    return it == knownClients().cend() ? QString() : QString::fromUtf8(it->name);
}

QString clientKindName(ClientInfo::Kind kind)
{
    switch (kind) {
    case ClientInfo::Kind::Absent: return QStringLiteral("absent");
    case ClientInfo::Kind::Known: return QStringLiteral("known");
    case ClientInfo::Kind::UnknownCode: return QStringLiteral("unknown");
    case ClientInfo::Kind::Nonstandard: return QStringLiteral("nonstandard");
    }
    return {};
}

ClientInfo decodeClientVersion(const QByteArray &v)
{
    ClientInfo info;
    info.raw = v;
    if (v.isEmpty())
        return info;

    if (v.size() >= 2 && printable(v[0]) && printable(v[1]))
        info.code = QString::fromLatin1(v.left(2));

    if (v.size() != 4 || info.code.isEmpty()) {
        info.kind = ClientInfo::Kind::Nonstandard;
        info.name = clientName(info.code);
        return info;
    }

    const auto it = knownClients().constFind(info.code);
    if (it == knownClients().cend()) {
        info.kind = ClientInfo::Kind::UnknownCode;
        return info;
    }

    info.kind = ClientInfo::Kind::Known;
    info.name = QString::fromUtf8(it->name);
    info.note = QString::fromUtf8(it->note);
    info.version = decodeVersion(info.code, quint8(v[2]), quint8(v[3]));
    return info;
}

QString clientLabel(const ClientInfo &info)
{
    return info.kind == ClientInfo::Kind::Known ? info.name : info.display();
}

QString versionLabel(const ClientInfo &info)
{
    if (info.kind != ClientInfo::Kind::Known)
        return {};
    if (!info.version.isEmpty())
        return info.version;
    return QStringLiteral("bytes %1").arg(QString::fromLatin1(info.raw.mid(2).toHex(' ')));
}

QString ClientInfo::display() const
{
    switch (kind) {
    case Kind::Absent:
        return QStringLiteral("no version sent");
    case Kind::Known:
        return version.isEmpty() ? name : name + QLatin1Char(' ') + version;
    case Kind::UnknownCode:
        return QStringLiteral("unknown client %1").arg(code);
    case Kind::Nonstandard:
        if (!name.isEmpty())
            return QStringLiteral("%1, nonstandard %2-byte field").arg(name).arg(raw.size());
        return QStringLiteral("nonstandard %1-byte field").arg(raw.size());
    }
    return {};
}

QString ClientInfo::rawText() const
{
    if (raw.isEmpty())
        return {};
    return QStringLiteral("\"%1\"  (%2)").arg(krpc::escapeBytes(raw), QString::fromLatin1(raw.toHex(' ')));
}

} // namespace dht
