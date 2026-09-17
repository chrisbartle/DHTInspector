#include "dhtcore/NodeList.h"

#include <QElapsedTimer>
#include <QHashFunctions>
#include <QSaveFile>

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace dht {

namespace {

using AddressKey = std::array<quint8, 16>;

struct AddressKeyHash
{
    size_t operator()(const AddressKey &key) const noexcept { return qHashBits(key.data(), key.size(), 0); }
};

using AddressCounts = std::unordered_map<AddressKey, int, AddressKeyHash>;

AddressKey keyOf(const QHostAddress &address)
{
    AddressKey key{};
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v4 = address.toIPv4Address();
        key[10] = 0xff;
        key[11] = 0xff;
        key[12] = quint8(v4 >> 24);
        key[13] = quint8(v4 >> 16);
        key[14] = quint8(v4 >> 8);
        key[15] = quint8(v4);
    } else {
        const Q_IPV6ADDR v6 = address.toIPv6Address();
        std::memcpy(key.data(), v6.c, 16);
    }
    return key;
}

// An address filter, prepared once per query.
struct SubnetMatcher
{
    bool any = true;
    AddressKey key{};
    int bits = 0;  // over the 128-bit key

    SubnetMatcher(const QHostAddress &subnet, int prefixBits)
    {
        if (subnet.isNull() || prefixBits < 0)
            return;
        const QHostAddress address = normalizeAddress(subnet);
        any = false;
        key = keyOf(address);
        const bool v4 = address.protocol() == QAbstractSocket::IPv4Protocol;
        bits = std::clamp(v4 ? prefixBits + 96 : prefixBits, 0, 128);
    }

    bool matches(const AddressKey &address) const
    {
        if (any)
            return true;
        const int bytes = bits / 8;
        if (std::memcmp(address.data(), key.data(), size_t(bytes)) != 0)
            return false;
        const int rest = bits % 8;
        if (rest == 0)
            return true;
        const quint8 mask = quint8(0xff << (8 - rest));
        return (address[bytes] & mask) == (key[bytes] & mask);
    }
};

qint64 ageMs(quint32 stamp, quint32 now, int tickMs)
{
    return stamp == 0 ? -1 : (qint64(now) - qint64(stamp)) * tickMs;
}

AddressCounts countAddresses(const NodeCatalog &catalog)
{
    AddressCounts counts;
    counts.reserve(size_t(catalog.size()));
    catalog.forEach([&](NodeCatalog::Slot, const CatalogEntry &e) { ++counts[e.address]; });
    return counts;
}

bool idHasPrefix(const NodeId &id, const NodeId &prefix, int bits)
{
    return bits <= 0 || NodeId::commonPrefixLength(id, prefix) >= std::min(bits, NodeId::Bits);
}

std::vector<NodeCatalog::Slot> filterNodes(const NodeCatalog &catalog, const NodeQuery &q, ClientLabelCache &labels,
                                           const AddressCounts *counts, const AddressSignals *suspicion)
{
    const SubnetMatcher subnet(q.subnet, q.subnetBits);
    const bool rttFilter = q.minRttMs >= 0 || q.maxRttMs >= 0;
    const bool clientFilter = !q.client.isEmpty() || !q.version.isEmpty();

    std::vector<NodeCatalog::Slot> matches;
    catalog.forEach([&](NodeCatalog::Slot slot, const CatalogEntry &e) {
        if (q.family && e.family() != *q.family)
            return;
        if (!(q.states & (1u << int(e.state))))
            return;
        if (q.port != 0 && e.port != q.port)
            return;
        if (!subnet.matches(e.address))
            return;
        if (q.idPrefixBits > 0 && !idHasPrefix(e.id, q.idPrefix, q.idPrefixBits))
            return;
        if ((e.flags & q.flagsSet) != q.flagsSet || (e.flags & q.flagsClear) != 0)
            return;
        if (q.suspicion != 0 && !matchesSignals(e, q.suspicion, suspicion))
            return;
        if (q.bep42 && (e.lastAnswered == 0 || bep42::Status(e.bep42) != *q.bep42))
            return;
        if (rttFilter) {
            if (e.rttMs == CatalogEntry::NoRtt)
                return;
            if (q.minRttMs >= 0 && e.rttMs < q.minRttMs)
                return;
            if (q.maxRttMs >= 0 && e.rttMs > q.maxRttMs)
                return;
        }
        if (clientFilter) {
            if (e.lastAnswered == 0)
                return;
            const ClientLabelCache::Labels &l = labels.labels(e.versionKey());
            if (!q.client.isEmpty() && l.client != q.client)
                return;
            if (!q.version.isEmpty() && l.version != q.version)
                return;
        }
        if (q.minNodesAtAddress > 1 && counts) {
            const auto it = counts->find(e.address);
            if (it == counts->end() || it->second < q.minNodesAtAddress)
                return;
        }
        matches.push_back(slot);
    });
    return matches;
}

// Orders slots by the query's sort, with nodes lacking the sort value last
// either way, and address then port breaking ties.
struct SlotOrder
{
    const NodeCatalog &catalog;
    const NodeQuery &query;
    ClientLabelCache &labels;
    const AddressCounts *counts;

    bool operator()(NodeCatalog::Slot a, NodeCatalog::Slot b) const
    {
        const CatalogEntry &x = catalog.at(a);
        const CatalogEntry &y = catalog.at(b);
        const int primary = compare(x, y);
        if (primary != 0)
            return primary < 0;
        return byAddress(x, y) < 0;
    }

    static int byAddress(const CatalogEntry &x, const CatalogEntry &y)
    {
        if (const int c = std::memcmp(x.address.data(), y.address.data(), 16); c != 0)
            return c;
        return int(x.port) - int(y.port);
    }

    // Negative when x goes first. Missing values always sort last.
    int compare(const CatalogEntry &x, const CatalogEntry &y) const
    {
        const auto ordered = [this](qint64 vx, qint64 vy, bool missingX, bool missingY) {
            if (missingX != missingY)
                return missingX ? 1 : -1;
            if (missingX || vx == vy)
                return 0;
            const int c = vx < vy ? -1 : 1;
            return query.descending ? -c : c;
        };
        switch (query.sort) {
        case NodeQuery::Sort::Address: {
            const int c = byAddress(x, y);
            return query.descending ? -c : c;
        }
        case NodeQuery::Sort::RoundTrip:
            return ordered(x.rttMs, y.rttMs, x.rttMs == CatalogEntry::NoRtt, y.rttMs == CatalogEntry::NoRtt);
        case NodeQuery::Sort::LastAnswered:
            return ordered(x.lastAnswered, y.lastAnswered, x.lastAnswered == 0, y.lastAnswered == 0);
        case NodeQuery::Sort::FirstSeen:
            return ordered(x.firstSeen, y.firstSeen, x.firstSeen == 0, y.firstSeen == 0);
        case NodeQuery::Sort::NodesAtAddress: {
            const auto count = [this](const CatalogEntry &e) {
                if (!counts)
                    return 1;
                const auto it = counts->find(e.address);
                return it == counts->end() ? 1 : it->second;
            };
            return ordered(count(x), count(y), false, false);
        }
        case NodeQuery::Sort::Client: {
            const bool missingX = x.lastAnswered == 0;
            const bool missingY = y.lastAnswered == 0;
            if (missingX != missingY)
                return missingX ? 1 : -1;
            if (missingX)
                return 0;
            // Copies: looking up the second label may move the first.
            const QString cx = labels.labels(x.versionKey()).client + QChar(1) + labels.labels(x.versionKey()).version;
            const QString cy = labels.labels(y.versionKey()).client + QChar(1) + labels.labels(y.versionKey()).version;
            const int c = QString::compare(cx, cy, Qt::CaseInsensitive);
            return query.descending ? -c : c;
        }
        }
        return 0;
    }
};

NodeListRow rowFor(const CatalogEntry &e, quint32 nowStamp, int tickMs, int nodesAtAddress, quint8 suspicion,
                   ClientLabelCache &labels)
{
    NodeListRow row;
    row.endpoint = e.endpoint();
    row.id = e.id;
    row.state = e.state;
    row.answered = e.lastAnswered != 0;
    if (row.answered) {
        const ClientLabelCache::Labels &l = labels.labels(e.versionKey());
        row.client = l.client;
        row.version = l.version;
        row.clientKind = l.kind;
        row.rawVersion = e.versionBytes();
        row.bep42 = bep42::Status(e.bep42);
    }
    row.rttMs = e.rttMs == CatalogEntry::NoRtt ? -1 : e.rttMs;
    row.nodesAtAddress = nodesAtAddress;
    // Unreachable entries are never asked; their failure count holds the reason.
    if (e.state == CatalogEntry::State::Unroutable)
        row.problem = AddressProblem(e.failures);
    else
        row.failures = e.failures;
    row.flags = e.flags;
    row.selfListSharePercent = e.selfListShare == CatalogEntry::NoShare ? -1 : e.selfListShare;
    row.bep51Samples = e.sampleCount();
    row.suspicion = suspicion;
    row.sightings = e.sightings;
    row.firstSeenAgoMs = ageMs(e.firstSeen, nowStamp, tickMs);
    row.lastAnsweredAgoMs = ageMs(e.lastAnswered, nowStamp, tickMs);
    row.lastQueriedAgoMs = ageMs(e.lastQueried, nowStamp, tickMs);
    return row;
}

QByteArray csvField(const QString &text)
{
    QByteArray bytes = text.toUtf8();
    if (bytes.contains(',') || bytes.contains('"') || bytes.contains('\n')) {
        bytes.replace("\"", "\"\"");
        bytes = '"' + bytes + '"';
    }
    return bytes;
}

} // namespace

const ClientLabelCache::Labels &ClientLabelCache::labels(quint64 versionKey)
{
    auto it = m_labels.find(versionKey);
    if (it == m_labels.end()) {
        Labels l;
        l.info = decodeClientVersion(CatalogEntry::versionBytesForKey(versionKey));
        l.client = clientLabel(l.info);
        l.version = versionLabel(l.info);
        l.kind = clientKindName(l.info.kind);
        it = m_labels.insert(versionKey, l);
    }
    return *it;
}

QString featureAnswer(quint32 flags, CatalogEntry::Flag tested, CatalogEntry::Flag has)
{
    if (!(flags & tested))
        return QString();
    return (flags & has) ? QStringLiteral("yes") : QStringLiteral("no");
}

QString unknownQueryAnswer(quint32 flags)
{
    if (!(flags & CatalogEntry::TestedUnknown))
        return QString();
    if (flags & CatalogEntry::Answers204)
        return QStringLiteral("204");
    if (flags & CatalogEntry::AnswersOther)
        return (flags & CatalogEntry::AnswersError) ? QStringLiteral("other error") : QStringLiteral("reply");
    return QStringLiteral("no answer");
}

QString signalNames(quint8 suspicion)
{
    QStringList names;
    if (suspicion & ManyNodes)
        names << QStringLiteral("many nodes");
    if (suspicion & DenseSubnet)
        names << QStringLiteral("dense subnet");
    if (suspicion & SharedId)
        names << QStringLiteral("shared ID");
    if (suspicion & DenseIds)
        names << QStringLiteral("dense IDs");
    if (suspicion & PointsToSelf)
        names << QStringLiteral("points to self");
    return names.join(QStringLiteral(", "));
}

bool parseIdPrefixFilter(const QString &text, NodeId *prefix, int *bits, QString *error)
{
    QString t = text.trimmed();
    *prefix = NodeId();
    if (t.isEmpty()) {
        *bits = -1;
        return true;
    }
    int count = -1;
    if (const qsizetype slash = t.indexOf(QLatin1Char('/')); slash >= 0) {
        bool ok = false;
        count = t.mid(slash + 1).trimmed().toInt(&ok);
        if (!ok || count < 1 || count > NodeId::Bits) {
            if (error)
                *error = QStringLiteral("the bit count must be 1 to 160");
            return false;
        }
        t = t.left(slash).trimmed();
    }
    const int digits = int(t.size());
    if (digits == 0 || digits > NodeId::Size * 2) {
        if (error)
            *error = QStringLiteral("enter up to 40 hex digits, e.g. a1b2 or a1b2/13");
        return false;
    }
    for (QChar c : std::as_const(t)) {
        const bool hex = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
        if (!hex) {
            if (error)
                *error = QStringLiteral("not hex, e.g. a1b2 or a1b2/13");
            return false;
        }
    }
    const QByteArray padded = QByteArray::fromHex((t + QString(NodeId::Size * 2 - digits, QLatin1Char('0'))).toLatin1());
    for (int i = 0; i < NodeId::Size; ++i)
        (*prefix)[i] = quint8(padded[i]);
    if (count < 0)
        count = digits * 4;
    if (count > digits * 4) {
        if (error)
            *error = QStringLiteral("%1 hex digits give at most %2 bits").arg(digits).arg(digits * 4);
        return false;
    }
    *bits = count;
    return true;
}

bool addressInSubnet(const std::array<quint8, 16> &address, const QHostAddress &subnet, int bits)
{
    return SubnetMatcher(subnet, bits).matches(address);
}

bool parseAddressFilter(const QString &text, QHostAddress *address, int *bits, QString *error)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        *address = QHostAddress();
        *bits = -1;
        return true;
    }
    if (t.contains(QLatin1Char('/'))) {
        const auto subnet = QHostAddress::parseSubnet(t);
        if (subnet.first.isNull()) {
            if (error)
                *error = QStringLiteral("not an address or subnet, e.g. 203.0.113.0/24");
            return false;
        }
        *address = normalizeAddress(subnet.first);
        *bits = subnet.second;
        return true;
    }
    QHostAddress parsed;
    if (!parsed.setAddress(t)) {
        if (error)
            *error = QStringLiteral("not an address or subnet, e.g. 203.0.113.7 or 2001:db8::/32");
        return false;
    }
    *address = normalizeAddress(parsed);
    *bits = address->protocol() == QAbstractSocket::IPv4Protocol ? 32 : 128;
    return true;
}

QString stateName(CatalogEntry::State state)
{
    switch (state) {
    case CatalogEntry::State::New: return QStringLiteral("awaiting");
    case CatalogEntry::State::Responsive: return QStringLiteral("responsive");
    case CatalogEntry::State::Silent: return QStringLiteral("silent");
    case CatalogEntry::State::Gone: return QStringLiteral("gone");
    case CatalogEntry::State::Unroutable: return QStringLiteral("unreachable");
    }
    return {};
}

NodeListPage queryNodes(const NodeCatalog &catalog, const NodeQuery &query, qint64 nowMs, ClientLabelCache &labels,
                        const AddressSignals *suspicion)
{
    QElapsedTimer timer;
    timer.start();

    const bool needCounts = query.minNodesAtAddress > 1 || query.sort == NodeQuery::Sort::NodesAtAddress;
    AddressCounts counts;
    if (needCounts)
        counts = countAddresses(catalog);

    std::vector<NodeCatalog::Slot> matches = filterNodes(catalog, query, labels,
                                                        needCounts ? &counts : nullptr, suspicion);

    NodeListPage page;
    page.matchedNodes = int(matches.size());
    {
        std::vector<AddressKey> keys;
        keys.reserve(matches.size());
        for (NodeCatalog::Slot slot : matches)
            keys.push_back(catalog.at(slot).address);
        std::sort(keys.begin(), keys.end());
        page.matchedAddresses = int(std::unique(keys.begin(), keys.end()) - keys.begin());
    }

    const int limit = std::max(1, query.limit);
    int offset = std::max(0, query.offset);
    if (offset >= page.matchedNodes)
        offset = page.matchedNodes > 0 ? (page.matchedNodes - 1) / limit * limit : 0;
    const int end = std::min(page.matchedNodes, offset + limit);
    page.offset = offset;

    const SlotOrder order{catalog, query, labels, needCounts ? &counts : nullptr};
    std::partial_sort(matches.begin(), matches.begin() + end, matches.end(), order);

    // How many nodes share each shown address, when not already counted.
    if (!needCounts && end > offset) {
        for (int i = offset; i < end; ++i)
            counts.emplace(catalog.at(matches[i]).address, 0);
        catalog.forEach([&](NodeCatalog::Slot, const CatalogEntry &e) {
            if (const auto it = counts.find(e.address); it != counts.end())
                ++it->second;
        });
    }

    const quint32 now = catalog.stamp(nowMs);
    for (int i = offset; i < end; ++i) {
        const CatalogEntry &e = catalog.at(matches[i]);
        const auto it = counts.find(e.address);
        page.rows.push_back(rowFor(e, now, NodeCatalog::TickMs, it == counts.end() ? 1 : it->second,
                                   suspicion ? suspicion->at(e.address) : 0, labels));
    }
    page.queryMs = int(timer.elapsed());
    return page;
}

NodeExport collectNodes(const NodeCatalog &catalog, const NodeQuery &query, qint64 nowMs, ClientLabelCache &labels,
                        const AddressSignals *suspicion)
{
    const AddressCounts counts = countAddresses(catalog);
    std::vector<NodeCatalog::Slot> matches = filterNodes(catalog, query, labels, &counts, suspicion);
    std::sort(matches.begin(), matches.end(), SlotOrder{catalog, query, labels, &counts});

    NodeExport out;
    out.query = query;
    out.nowStamp = catalog.stamp(nowMs);
    out.entries.reserve(matches.size());
    out.nodesAtAddress.reserve(matches.size());
    out.suspicion.reserve(matches.size());
    for (NodeCatalog::Slot slot : matches) {
        const CatalogEntry &e = catalog.at(slot);
        out.entries.push_back(e);
        const auto it = counts.find(e.address);
        out.nodesAtAddress.push_back(it == counts.end() ? 1 : it->second);
        out.suspicion.push_back(suspicion ? suspicion->at(e.address) : 0);
    }
    return out;
}

bool writeNodesCsv(const NodeExport &nodes, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QByteArray buffer;
    buffer.reserve(1 << 20);
    buffer += "address,port,family,state,answered,client,version,client_kind,v_hex,rtt_ms,bep42,"
              "node_id,nodes_at_address,failures,sightings,first_seen_s_ago,last_answered_s_ago,"
              "last_queried_s_ago,bep51,bep51_samples,bep44,bep32,sends_ip,unknown_query,lists_bad_addresses,"
              "self_list_share,suspicious,address_problem\n";

    ClientLabelCache labels;
    const auto seconds = [](qint64 ms) { return ms < 0 ? QByteArray() : QByteArray::number(ms / 1000); };
    for (size_t i = 0; i < nodes.entries.size(); ++i) {
        const NodeListRow row = rowFor(nodes.entries[i], nodes.nowStamp, nodes.tickMs, nodes.nodesAtAddress[i],
                                       i < nodes.suspicion.size() ? nodes.suspicion[i] : 0, labels);
        buffer += csvField(row.endpoint.address.toString()) + ',';
        buffer += QByteArray::number(row.endpoint.port) + ',';
        buffer += (row.endpoint.family() == Family::IPv4 ? "ipv4," : "ipv6,");
        buffer += stateName(row.state).toUtf8() + ',';
        buffer += (row.answered ? "yes," : "no,");
        buffer += csvField(row.client) + ',';
        buffer += csvField(row.version) + ',';
        buffer += row.clientKind.toUtf8() + ',';
        buffer += row.rawVersion.toHex() + ',';
        buffer += (row.rttMs < 0 ? QByteArray() : QByteArray::number(row.rttMs)) + ',';
        buffer += (row.answered ? bep42::statusName(row.bep42).toUtf8() : QByteArray()) + ',';
        buffer += row.id.toHex().toLatin1() + ',';
        buffer += QByteArray::number(row.nodesAtAddress) + ',';
        buffer += QByteArray::number(row.failures) + ',';
        buffer += QByteArray::number(row.sightings) + ',';
        buffer += seconds(row.firstSeenAgoMs) + ',';
        buffer += seconds(row.lastAnsweredAgoMs) + ',';
        buffer += seconds(row.lastQueriedAgoMs) + ',';
        const auto text = [](const QString &s) { return s.toLatin1() + ','; };
        const bool listed = row.selfListSharePercent >= 0;
        buffer += text(featureAnswer(row.flags, CatalogEntry::Tested51, CatalogEntry::Has51));
        buffer += ((row.flags & CatalogEntry::Has51) ? QByteArray::number(row.bep51Samples) : QByteArray()) + ',';
        buffer += text(featureAnswer(row.flags, CatalogEntry::Tested44, CatalogEntry::Has44));
        buffer += text(featureAnswer(row.flags, CatalogEntry::Tested32, CatalogEntry::Has32));
        buffer += text(featureAnswer(row.flags, CatalogEntry::TestedIp, CatalogEntry::SendsIp));
        buffer += text(unknownQueryAnswer(row.flags));
        buffer += QByteArray(listed ? ((row.flags & CatalogEntry::ListsBogons) ? "yes" : "no") : "") + ',';
        buffer += (listed ? QByteArray::number(row.selfListSharePercent) : QByteArray()) + ',';
        buffer += csvField(signalNames(row.suspicion)) + ',';
        buffer += (row.problem == AddressProblem::None ? QByteArray() : csvField(addressProblemName(row.problem))) + '\n';
        if (buffer.size() > (1 << 20) - 1024) {
            if (file.write(buffer) != buffer.size()) {
                if (error)
                    *error = file.errorString();
                file.cancelWriting();
                return false;
            }
            buffer.clear();
        }
    }
    if (file.write(buffer) != buffer.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace dht
