#include "DhtController.h"

#include "dhtcore/Bep44.h"
#include "dhtcore/ClientVersion.h"
#include "dhtcore/DhtEngine.h"
#include "dhtcore/Support.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHostInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QPointer>
#include <QSaveFile>
#include <QRegularExpression>
#include <QTime>
// Settings persistence is disabled; see DhtController::DhtController().
// #include <QSettings>
#include <QThread>

#include <cmath>
#include <limits>
#include <tuple>

namespace {

// const QString PortKey = QStringLiteral("engine/port");
// const QString Ipv6Key = QStringLiteral("engine/ipv6");
// const QString PortForwardingKey = QStringLiteral("engine/portForwarding");
// const QString Bep42Key = QStringLiteral("engine/bep42");

FamilyStatus toFamilyStatus(const dht::FamilySnapshot &f)
{
    FamilyStatus out;
    out.enabled = f.enabled;
    out.bound = f.bound;
    out.port = f.port;
    out.nodeId = f.bound ? f.id.toHex() : QString();
    out.externalAddress = f.externalAddress.isNull() ? QString() : f.externalAddress.toString();
    out.bep42 = dht::bep42::statusName(f.bep42);
    out.error = f.error;
    out.nodeCount = f.nodeCount;
    out.bucketCount = f.bucketCount;
    return out;
}

PortMappingStatus toPortMappingStatus(const dht::PortMappingSnapshot &m)
{
    PortMappingStatus out;
    switch (m.state) {
    case dht::PortMappingSnapshot::State::Disabled: out.state = QStringLiteral("disabled"); break;
    case dht::PortMappingSnapshot::State::Discovering: out.state = QStringLiteral("discovering"); break;
    case dht::PortMappingSnapshot::State::Mapped: out.state = QStringLiteral("mapped"); break;
    case dht::PortMappingSnapshot::State::Failed: out.state = QStringLiteral("failed"); break;
    }
    out.protocol = m.protocol;
    out.gateway = m.gateway.isNull() ? QString() : m.gateway.toString();
    out.externalAddress = m.externalAddress.isNull() ? QString() : m.externalAddress.toString();
    out.externalPort = m.externalPort;
    out.message = m.message;
    return out;
}

EngineStatistics toStatistics(const dht::EngineStats &s)
{
    EngineStatistics out;
    out.packetsIn = s.packetsIn;
    out.packetsOut = s.packetsOut;
    out.bytesIn = s.bytesIn;
    out.bytesOut = s.bytesOut;
    out.queriesIn = s.queriesIn;
    out.queriesOut = s.queriesOut;
    out.responsesIn = s.responsesIn;
    out.errorsIn = s.errorsIn;
    out.timeouts = s.timeouts;
    out.malformedIn = s.malformedIn;
    out.rateLimited = s.rateLimited;
    out.readOnlyDropped = s.readOnlyDropped;
    out.queriesDelayed = s.queriesDelayed;
    out.queriesRefused = s.queriesRefused;
    out.queriesWaiting = s.queriesWaiting;
    out.repliesShed = s.repliesShed;
    out.sendFailures = s.sendFailures;
    out.storedInfohashes = s.storedInfohashes;
    out.storedPeers = s.storedPeers;
    out.activeLookups = s.activeLookups;
    return out;
}

CrawlStatus toCrawlStatus(const dht::CrawlSnapshot &c)
{
    CrawlStatus out;
    switch (c.phase) {
    case dht::CrawlSnapshot::Phase::Off: out.phase = QStringLiteral("off"); break;
    case dht::CrawlSnapshot::Phase::WaitingForNodes: out.phase = QStringLiteral("waiting"); break;
    case dht::CrawlSnapshot::Phase::Discovering: out.phase = QStringLiteral("discovering"); break;
    case dht::CrawlSnapshot::Phase::Rechecking: out.phase = QStringLiteral("rechecking"); break;
    case dht::CrawlSnapshot::Phase::UpToDate: out.phase = QStringLiteral("up to date"); break;
    }
    out.known = c.known;
    out.notAsked = c.notAsked;
    out.responsive = c.responsive;
    out.silent = c.silent;
    out.gone = c.gone;
    out.unroutable = c.unroutable;
    out.cap = c.cap;
    out.evicted = c.evicted;
    out.memoryBytes = double(c.memoryBytes);
    out.bytesPerEntry = double(c.bytesPerEntry);
    out.queries = c.queries;
    out.answers = c.answers;
    out.errors = c.errors;
    out.timeouts = c.timeouts;
    out.notSent = c.notSent;
    out.outstanding = c.outstanding;
    out.waiting = c.waiting;
    out.batch = c.batch;
    out.featureQueries = c.featureQueries;
    out.featureWaiting = c.featureWaiting;
    out.randomLookups = c.randomLookups;
    out.lookupsWithInventedPeers = c.lookupsWithInventedPeers;
    out.monitoredSeconds = double(c.monitoredMs) / 1000.0;
    return out;
}

QVariantMap toCensusMap(const dht::CensusSnapshot &c)
{
    const auto familyName = [](dht::Family f) {
        return f == dht::Family::IPv4 ? QStringLiteral("IPv4") : QStringLiteral("IPv6");
    };
    QString state;
    switch (c.state) {
    case dht::CensusSnapshot::State::Idle: state = QStringLiteral("idle"); break;
    case dht::CensusSnapshot::State::Running: state = QStringLiteral("running"); break;
    case dht::CensusSnapshot::State::Done: state = QStringLiteral("done"); break;
    case dht::CensusSnapshot::State::Cancelled: state = QStringLiteral("cancelled"); break;
    }

    QVariantList totals;
    for (const auto &[family, t] : {std::pair{dht::Family::IPv4, c.ipv4}, std::pair{dht::Family::IPv6, c.ipv6}}) {
        if (t.slices == 0)
            continue;
        int bits = 0;
        for (const dht::CensusSlice &slice : c.slices) {
            if (slice.family == family)
                bits = slice.bits;
        }
        totals.append(QVariantMap{
            {QStringLiteral("family"), familyName(family)},
            {QStringLiteral("slices"), t.slices},
            {QStringLiteral("bits"), bits},
            {QStringLiteral("heard"), t.heard},
            {QStringLiteral("heardLow"), t.heardLow},
            {QStringLiteral("heardHigh"), t.heardHigh},
            {QStringLiteral("connected"), t.connected},
            {QStringLiteral("connectedLow"), t.connectedLow},
            {QStringLiteral("connectedHigh"), t.connectedHigh},
        });
    }

    QVariantList slices;
    for (const dht::CensusSlice &s : c.slices) {
        slices.append(QVariantMap{
            {QStringLiteral("family"), familyName(s.family)},
            {QStringLiteral("bits"), s.bits},
            {QStringLiteral("ipsHeard"), s.ipsHeard},
            {QStringLiteral("ipsAnswered"), s.ipsAnswered},
            {QStringLiteral("nodesHeard"), s.nodesHeard},
            {QStringLiteral("nodesAnswered"), s.nodesAnswered},
            {QStringLiteral("heardEstimate"), s.heardEstimate},
            {QStringLiteral("connectedEstimate"), s.connectedEstimate},
            {QStringLiteral("rounds"), s.rounds},
            {QStringLiteral("queries"), double(s.queries)},
            {QStringLiteral("seconds"), double(s.durationMs) / 1000.0},
        });
    }

    return QVariantMap{
        {QStringLiteral("state"), state},
        {QStringLiteral("slicesDone"), c.slicesDone},
        {QStringLiteral("slicesTotal"), c.slicesTotal},
        {QStringLiteral("family"), familyName(c.family)},
        {QStringLiteral("bits"), c.bits},
        {QStringLiteral("round"), c.round},
        {QStringLiteral("nodesFound"), c.nodesFound},
        {QStringLiteral("nodesAnswered"), c.nodesAnswered},
        {QStringLiteral("outstanding"), c.outstanding},
        {QStringLiteral("queries"), double(c.queries)},
        {QStringLiteral("seconds"), double(c.elapsedMs) / 1000.0},
        {QStringLiteral("totals"), totals},
        {QStringLiteral("slices"), slices},
    };
}

} // namespace

DhtController::DhtController(QObject *parent)
    : QObject(parent)
    , m_nodes(new NodeListModel(this))
    , m_storedInfohashes(new StoredInfohashModel(this))
    , m_storedPeers(new StoredPeerModel(this))
    , m_storedItems(new StoredItemModel(this))
    , m_peerResults(new PeerResultModel(this))
    , m_probeHistory(new ProbeHistoryModel(this))
    , m_nodeList(new CatalogListModel(this))
{
    qRegisterMetaType<dht::EngineSnapshot>();
    qRegisterMetaType<dht::StorageSnapshot>();
    qRegisterMetaType<dht::PeerSearchResult>();
    qRegisterMetaType<dht::ItemSearchResult>();
    qRegisterMetaType<dht::PublishResult>();
    qRegisterMetaType<dht::ProbeResult>();
    qRegisterMetaType<dht::NodeQuery>();
    qRegisterMetaType<dht::NodeListPage>();
    qRegisterMetaType<std::shared_ptr<dht::NodeExport>>();
    resetNodeList();
    setExportStatus(false, QString(), false);

    // DHT Inspector is a standalone utility with no preconditions: every
    // launch starts from the defaults in DhtController.h and nothing is saved
    // between runs. The QSettings persistence below and in the setters is
    // commented out rather than deleted, in case it is wanted back.
    //
    // QSettings settings;
    // m_port = std::clamp(settings.value(PortKey, 6881).toInt(), 1, 65535);
    // m_ipv6Enabled = settings.value(Ipv6Key, false).toBool();
    // m_portForwarding = settings.value(PortForwardingKey, false).toBool();
    // m_bep42Enabled = settings.value(Bep42Key, true).toBool();

    // Deliberately not persisted: every launch starts from fresh random IDs.
    m_nodeIdV4 = dht::NodeId::random().toHex();
    m_nodeIdV6 = dht::NodeId::random().toHex();
}

DhtController::~DhtController()
{
    destroyEngine();
    if (m_exportThread.joinable())
        m_exportThread.join();
}

QString DhtController::statusText() const
{
    if (m_running) {
        if (m_ipv4.port > 0)
            return tr("Running on port %1").arg(m_ipv4.port);
        return tr("Running");
    }
    return m_lastError.isEmpty() ? tr("Stopped") : tr("Stopped — %1").arg(m_lastError);
}

void DhtController::setRunning(bool running)
{
    if (running == m_running)
        return;
    if (running)
        startEngine();
    else
        stopEngine();
}

void DhtController::setPort(int port)
{
    port = std::clamp(port, 1, 65535);
    if (m_running || port == m_port)
        return;
    m_port = port;
    // QSettings().setValue(PortKey, m_port);
    emit portChanged();
}

void DhtController::setIpv6Enabled(bool enabled)
{
    if (m_running || enabled == m_ipv6Enabled)
        return;
    m_ipv6Enabled = enabled;
    // QSettings().setValue(Ipv6Key, m_ipv6Enabled);
    emit ipv6EnabledChanged();
}

void DhtController::setBep42Enabled(bool enabled)
{
    if (m_running || enabled == m_bep42Enabled)
        return;
    m_bep42Enabled = enabled;
    // QSettings().setValue(Bep42Key, m_bep42Enabled);
    emit bep42EnabledChanged();
}

void DhtController::setNodeIdV4(const QString &id)
{
    if (m_running || id == m_nodeIdV4)
        return;
    m_nodeIdV4 = id;
    emit nodeIdV4Changed();
}

void DhtController::setNodeIdV6(const QString &id)
{
    if (m_running || id == m_nodeIdV6)
        return;
    m_nodeIdV6 = id;
    emit nodeIdV6Changed();
}

void DhtController::randomizeNodeId(bool ipv6)
{
    if (ipv6)
        setNodeIdV6(dht::NodeId::random().toHex());
    else
        setNodeIdV4(dht::NodeId::random().toHex());
}

QString DhtController::validateNodeId(const QString &text) const
{
    const QString id = text.trimmed();
    static const QRegularExpression hex(QStringLiteral("^[0-9A-Fa-f]*$"));
    if (!hex.match(id).hasMatch())
        return tr("only hexadecimal digits 0-9 and a-f are allowed");
    if (id.size() != dht::NodeId::Size * 2)
        return tr("needs 40 hexadecimal digits, has %1").arg(id.size());
    return {};
}

void DhtController::setSendLimit(int bytesPerSecond)
{
    bytesPerSecond = std::max(0, bytesPerSecond);
    if (bytesPerSecond == m_sendLimit)
        return;
    m_sendLimit = bytesPerSecond;
    emit sendLimitChanged();

    if (m_engine) {
        QMetaObject::invokeMethod(
            m_engine, [engine = m_engine, bytesPerSecond] { engine->setSendLimit(bytesPerSecond); },
            Qt::QueuedConnection);
    }
}

void DhtController::setMonitoring(bool on)
{
    // Only while running; the switch snaps back otherwise.
    if (on == m_monitoring || (on && !m_engine))
        return;
    m_monitoring = on;
    emit monitoringChanged();
    if (m_engine) {
        QMetaObject::invokeMethod(m_engine, [engine = m_engine, on] { engine->setMonitoring(on); },
                                  Qt::QueuedConnection);
    }
}

void DhtController::setStatsFamily(const QString &family)
{
    if (family == m_statsFamily
        || (family != QLatin1String("all") && family != QLatin1String("ipv4") && family != QLatin1String("ipv6")))
        return;
    m_statsFamily = family;
    emit statsFamilyChanged();
    buildNetworkStats();
}

namespace {

constexpr int ShownClients = 12;
constexpr int ShownVersions = 15;
constexpr int ShownPorts = 10;
constexpr int ShownHistoryClients = 5;

double shareOf(double part, double whole)
{
    return whole > 0 ? part / whole : 0.0;
}

// The most common entries, then everything else folded into one line.
QVariantList talliesFor(const std::vector<dht::ClientTally> &tallies, int shown, int responsive)
{
    QVariantList out;
    double rest = 0;
    int restKinds = 0;
    for (size_t i = 0; i < tallies.size(); ++i) {
        const dht::ClientTally &t = tallies[i];
        if (int(i) < shown) {
            out.append(QVariantMap{
                {QStringLiteral("name"), t.name},
                {QStringLiteral("version"), t.version},
                {QStringLiteral("kind"), t.kind},
                {QStringLiteral("count"), t.count},
                {QStringLiteral("share"), shareOf(t.count, responsive)},
            });
        } else {
            rest += t.count;
            ++restKinds;
        }
    }
    if (rest > 0) {
        out.append(QVariantMap{
            {QStringLiteral("name"), DhtController::tr("%n other(s)", "", restKinds)},
            {QStringLiteral("version"), QString()},
            {QStringLiteral("kind"), QStringLiteral("other")},
            {QStringLiteral("count"), rest},
            {QStringLiteral("share"), shareOf(rest, responsive)},
        });
    }
    return out;
}

// Round-trip histogram in display bins: 50 ms wide up to a second, then
// coarser.
QVariantList rttBins(const std::vector<double> &histogram, double samples)
{
    struct Bin
    {
        int from;
        int to;  // exclusive
    };
    std::vector<Bin> bins;
    for (int from = 0; from < 1000; from += 50)
        bins.push_back({from, from + 50});
    bins.push_back({1000, 1500});
    bins.push_back({1500, 2000});
    bins.push_back({2000, dht::NetworkStats::MaxRttMs + 1});

    QVariantList out;
    for (const Bin &bin : bins) {
        double count = 0;
        for (int ms = bin.from; ms < bin.to && ms < int(histogram.size()); ++ms)
            count += histogram[ms];
        out.append(QVariantMap{
            {QStringLiteral("from"), bin.from},
            {QStringLiteral("to"), bin.to},
            {QStringLiteral("count"), count},
            {QStringLiteral("share"), shareOf(count, samples)},
        });
    }
    return out;
}

} // namespace

namespace {

QVariantMap addressCounts(const dht::NetworkStats &n)
{
    return QVariantMap{
        {QStringLiteral("heardIps"), n.heardIps},
        {QStringLiteral("connectedIps"), n.connectedIps},
        {QStringLiteral("answeringIps"), n.answeringIps},
        {QStringLiteral("unroutableIps"), n.unroutableIps},
        {QStringLiteral("multiNodeIps"), n.multiNodeIps},
        {QStringLiteral("maxNodesPerIp"), n.maxNodesPerIp},
        {QStringLiteral("answeringNodes"), n.answeringNodes},
    };
}

// Everything about one family's statistics, listing at most `shownClients`
// and `shownVersions` before folding the rest.
QVariantMap statsMap(const dht::NetworkStats &s, int shownClients, int shownVersions, int shownPorts)
{
    QVariantMap out;
    out.insert(QStringLiteral("counts"), addressCounts(s));
    out.insert(QStringLiteral("answeringIps"), s.answeringIps);
    out.insert(QStringLiteral("clients"), talliesFor(s.clients, shownClients, s.answeringIps));
    out.insert(QStringLiteral("versions"), talliesFor(s.versions, shownVersions, s.answeringIps));
    out.insert(QStringLiteral("distinctClients"), int(s.clients.size()));
    out.insert(QStringLiteral("distinctVersions"), int(s.versions.size()));
    double noVersion = 0;
    for (const dht::ClientTally &t : s.clients) {
        if (t.kind == QLatin1String("absent"))
            noVersion += t.count;
    }
    out.insert(QStringLiteral("noVersionShare"), shareOf(noVersion, s.answeringIps));

    using dht::bep42::Status;
    const auto bep = [&](Status status) { return s.bep42[int(status)]; };
    out.insert(QStringLiteral("bep42"), QVariantMap{
        {QStringLiteral("compliant"), bep(Status::Compliant)},
        {QStringLiteral("noncompliant"), bep(Status::NonCompliant)},
        {QStringLiteral("exempt"), bep(Status::Exempt)},
        {QStringLiteral("unknown"), bep(Status::Unknown)},
    });

    QVariantList bins = rttBins(s.rttHistogram, s.rttWeight);
    double peak = 0;
    for (const QVariant &bin : std::as_const(bins))
        peak = std::max(peak, bin.toMap().value(QStringLiteral("share")).toDouble());
    out.insert(QStringLiteral("rtt"), QVariantMap{
        {QStringLiteral("samples"), s.rttWeight},
        {QStringLiteral("mean"), s.rttMeanMs},
        {QStringLiteral("median"), s.rttMedianMs},
        {QStringLiteral("p90"), s.rttP90Ms},
        {QStringLiteral("p99"), s.rttP99Ms},
        {QStringLiteral("bins"), bins},
        {QStringLiteral("peakShare"), peak},
    });

    QVariantList ports;
    for (size_t i = 0; i < s.topPorts.size() && int(i) < shownPorts; ++i) {
        ports.append(QVariantMap{
            {QStringLiteral("port"), s.topPorts[i].first},
            {QStringLiteral("count"), s.topPorts[i].second},
            {QStringLiteral("share"), shareOf(s.topPorts[i].second, s.answeringIps)},
        });
    }
    out.insert(QStringLiteral("ports"), QVariantMap{
        {QStringLiteral("distinct"), s.distinctPorts},
        {QStringLiteral("defaultCount"), s.defaultPortCount},
        {QStringLiteral("defaultShare"), shareOf(s.defaultPortCount, s.answeringIps)},
        {QStringLiteral("top"), ports},
    });

    // Shares are of the addresses checked, -1 while none are.
    const auto feature = [](const dht::FeatureTally &t) {
        return QVariantMap{
            {QStringLiteral("tested"), t.tested},
            {QStringLiteral("yes"), t.yes},
            {QStringLiteral("share"), t.tested > 0 ? t.yes / t.tested : -1.0},
        };
    };
    const double unknownTested = s.answers204.tested;
    out.insert(QStringLiteral("features"), QVariantMap{
        {QStringLiteral("bep51"), feature(s.bep51)},
        {QStringLiteral("bep44"), feature(s.bep44)},
        {QStringLiteral("bep32"), feature(s.bep32)},
        {QStringLiteral("sendsIp"), feature(s.sendsIp)},
        {QStringLiteral("listsBogons"), feature(s.listsBogons)},
        {QStringLiteral("inventsPeers"), feature(s.inventsPeers)},
        {QStringLiteral("bep51SamplesMedian"), s.bep51SamplesMedian},
        {QStringLiteral("unknown"), QVariantMap{
            {QStringLiteral("tested"), unknownTested},
            {QStringLiteral("error204"), s.answers204.yes},
            {QStringLiteral("otherError"), s.unknownOtherError},
            {QStringLiteral("reply"), std::max(0.0, s.unknownOther - s.unknownOtherError)},
            {QStringLiteral("silent"), std::max(0.0, unknownTested - s.answers204.yes - s.unknownOther)},
            {QStringLiteral("share204"), unknownTested > 0 ? s.answers204.yes / unknownTested : -1.0},
        }},
    });

    QVariantList problems;
    for (int i = 1; i < dht::AddressProblemCount; ++i) {
        problems.append(QVariantMap{
            {QStringLiteral("name"), dht::addressProblemName(dht::AddressProblem(i))},
            {QStringLiteral("count"), s.unroutableByProblem[size_t(i)]},
        });
    }
    out.insert(QStringLiteral("badAddresses"), QVariantMap{
        {QStringLiteral("unreachable"), s.unroutableIps},
        {QStringLiteral("unclassified"), s.unroutableByProblem[0]},
        {QStringLiteral("byProblem"), problems},
    });

    // Churn. Times in minutes; shares -1 while there is nothing to go on.
    const dht::ChurnStats &c = s.churn;
    QVariantList survival;
    for (size_t i = 0; i < c.survivalBase.size(); ++i) {
        survival.append(QVariantMap{
            {QStringLiteral("hours"), double(dht::ChurnStats::SurvivalMs[i]) / 3600000.0},
            {QStringLiteral("base"), c.survivalBase[i]},
            {QStringLiteral("kept"), c.survivalKept[i]},
            {QStringLiteral("share"), c.survivalBase[i] > 0 ? double(c.survivalKept[i]) / c.survivalBase[i] : -1.0},
        });
    }
    static const QStringList binLabels = {
        DhtController::tr("under 10 min"), DhtController::tr("10–30 min"), DhtController::tr("30–60 min"),
        DhtController::tr("1–3 h"),        DhtController::tr("3–6 h"),     DhtController::tr("6–12 h"),
        DhtController::tr("12–24 h"),      DhtController::tr("over 24 h")};
    const auto timeBins = [](const std::array<int, 8> &counts) {
        int total = 0;
        for (int n : counts)
            total += n;
        QVariantList out;
        for (size_t i = 0; i < counts.size(); ++i) {
            out.append(QVariantMap{
                {QStringLiteral("label"), binLabels.value(int(i))},
                {QStringLiteral("count"), counts[i]},
                {QStringLiteral("share"), shareOf(counts[i], total)},
            });
        }
        return out;
    };
    const auto minutes = [](qint64 ms) { return ms < 0 ? -1.0 : double(ms) / 60000.0; };
    out.insert(QStringLiteral("churn"), QVariantMap{
        {QStringLiteral("departedLastHour"), c.departedLastHour},
        {QStringLiteral("returnedLastHour"), c.returnedLastHour},
        {QStringLiteral("arrivedLastHour"), c.arrivedLastHour},
        {QStringLiteral("departedShare"), shareOf(c.departedLastHour, s.answeringIps + c.departedLastHour)},
        {QStringLiteral("survival"), survival},
        {QStringLiteral("uptime"), timeBins(c.uptimeBins)},
        {QStringLiteral("sessions"), timeBins(c.sessionBins)},
        {QStringLiteral("medianUptimeMinutes"), minutes(c.medianUptimeMs)},
        {QStringLiteral("medianSessionMinutes"), minutes(c.medianSessionMs)},
    });
    return out;
}

QVariantMap lookupMap(const QString &family, const dht::LookupPerformance &p)
{
    return QVariantMap{
        {QStringLiteral("family"), family},
        {QStringLiteral("samples"), p.samples},
        {QStringLiteral("medianMs"), p.medianMs},
        {QStringLiteral("p90Ms"), p.p90Ms},
        {QStringLiteral("medianQueries"), p.medianQueries},
        {QStringLiteral("medianHops"), p.medianHops},
        {QStringLiteral("responseRate"), p.responseRate},
        {QStringLiteral("fullShare"), p.fullShare},
    };
}

// The session history as columns for the charts: wall-clock times, and one
// list per metric with null where a value was not known. Client shares
// follow the clients most common now, the rest folded into "other".
QVariantMap historyMap(const std::vector<dht::HistorySample> &samples, int shownClients)
{
    const qint64 wallOffset = QDateTime::currentMSecsSinceEpoch() - dht::nowMs();
    QVariantList times, spans, gaps;
    std::array<QVariantList, dht::MetricCount> series;
    for (const dht::HistorySample &s : samples) {
        times.append(double(s.atMs + wallOffset));
        spans.append(double(s.spanMs));
        gaps.append(s.gapBefore);
        for (int m = 0; m < dht::MetricCount; ++m) {
            const double v = s.values[size_t(m)];
            series[size_t(m)].append(std::isnan(v) ? QVariant() : QVariant(v));
        }
    }
    QVariantMap bySeries;
    for (int m = 0; m < dht::MetricCount; ++m)
        bySeries.insert(QString::fromLatin1(dht::metricKey(dht::Metric(m))), series[size_t(m)]);

    QStringList names;
    if (!samples.empty()) {
        for (const auto &[name, share] : samples.back().clientShares) {
            if (names.size() < shownClients)
                names << name;
        }
    }
    QVariantMap clientSeries;
    QVariantList other;
    for (const QString &name : std::as_const(names))
        clientSeries.insert(name, QVariantList());
    for (const dht::HistorySample &s : samples) {
        double listed = 0;
        QHash<QString, double> shares;
        for (const auto &[name, share] : s.clientShares) {
            shares.insert(name, share);
            if (names.contains(name))
                listed += share;
        }
        for (const QString &name : std::as_const(names)) {
            QVariantList list = clientSeries.value(name).toList();
            list.append(shares.contains(name) ? QVariant(shares.value(name)) : QVariant());
            clientSeries.insert(name, list);
        }
        other.append(s.clientShares.empty() ? QVariant() : QVariant(std::max(0.0, 1.0 - listed)));
    }

    return QVariantMap{
        {QStringLiteral("count"), int(samples.size())},
        {QStringLiteral("times"), times},
        {QStringLiteral("spans"), spans},
        {QStringLiteral("gaps"), gaps},
        {QStringLiteral("series"), bySeries},
        {QStringLiteral("clientNames"), names},
        {QStringLiteral("clients"), clientSeries},
        {QStringLiteral("otherClients"), other},
    };
}

QString idPrefixText(const dht::NodeId &id, int bits)
{
    const int digits = (bits + 3) / 4;
    return id.toHex().left(digits) + QLatin1Char('/') + QString::number(bits);
}

QString subnetText(const QHostAddress &base, int bits)
{
    return base.toString() + QLatin1Char('/') + QString::number(bits);
}

QVariantList signalList(quint8 bits)
{
    QVariantList out;
    for (const auto &[bit, name] : {std::pair{dht::ManyNodes, "many"}, std::pair{dht::DenseSubnet, "subnet"},
                                    std::pair{dht::SharedId, "sharedId"}, std::pair{dht::DenseIds, "denseIds"},
                                    std::pair{dht::PointsToSelf, "self"},
                                    std::pair{dht::InventsPeers, "invents"}}) {
        if (bits & bit)
            out.append(QString::fromLatin1(name));
    }
    return out;
}

QVariantMap suspiciousMap(const dht::SybilReport &r)
{
    const auto address = [](const dht::SuspectAddress &a) {
        return QVariantMap{
            {QStringLiteral("address"), a.address.toString()},
            {QStringLiteral("nodes"), a.answeringNodes},
            {QStringLiteral("compliant"), a.compliantNodes},
            {QStringLiteral("prefixes"), a.distinctPrefixes},
            {QStringLiteral("signals"), signalList(a.suspicion)},
            {QStringLiteral("signalText"), dht::signalNames(a.suspicion)},
        };
    };
    QVariantList many, flagged, subnets, shared, windows, self;
    for (const auto &a : r.manyNodes)
        many.append(address(a));
    for (const auto &a : r.flagged)
        flagged.append(address(a));
    for (const auto &n : r.denseSubnets) {
        subnets.append(QVariantMap{
            {QStringLiteral("subnet"), subnetText(n.base, n.bits)},
            {QStringLiteral("addresses"), n.addresses},
            {QStringLiteral("nodes"), n.answeringNodes},
        });
    }
    for (const auto &g : r.sharedIds) {
        QStringList sample;
        for (const QHostAddress &a : g.sample)
            sample << a.toString();
        shared.append(QVariantMap{
            {QStringLiteral("id"), g.id.toHex()},
            {QStringLiteral("addresses"), g.addresses},
            {QStringLiteral("sample"), sample.join(QStringLiteral(", "))},
        });
    }
    for (const auto &w : r.denseWindows) {
        windows.append(QVariantMap{
            {QStringLiteral("prefix"), idPrefixText(w.prefix, w.bits)},
            {QStringLiteral("bits"), w.bits},
            {QStringLiteral("nodes"), w.nodes},
            {QStringLiteral("expected"), w.expected},
            {QStringLiteral("chance"), w.chance},
            {QStringLiteral("addresses"), w.addresses},
        });
    }
    for (const auto &p : r.selfPointers) {
        self.append(QVariantMap{
            {QStringLiteral("address"), dht::Endpoint(p.address, p.port).toString()},
            {QStringLiteral("share"), p.sharePercent / 100.0},
        });
    }
    QVariantList inventors;
    for (const auto &p : r.peerInventors)
        inventors.append(QVariantMap{{QStringLiteral("address"), dht::Endpoint(p.address, p.port).toString()}});
    return QVariantMap{
        {QStringLiteral("answeringNodes"), r.answeringNodes},
        {QStringLiteral("manyNodesAt"), dht::SybilReport::ManyNodesAt},
        {QStringLiteral("denseSubnetAt"), dht::SybilReport::DenseSubnetAt},
        {QStringLiteral("pointsToSelfAt"), dht::SybilReport::PointsToSelfAt},
        {QStringLiteral("denseWindowChance"), dht::SybilReport::DenseWindowChance},
        {QStringLiteral("shownAtMost"), dht::SybilReport::MaxListed},
        {QStringLiteral("flaggedCount"), r.flaggedCount},
        {QStringLiteral("manyNodesCount"), r.manyNodesCount},
        {QStringLiteral("denseSubnetCount"), r.denseSubnetCount},
        {QStringLiteral("sharedIdCount"), r.sharedIdCount},
        {QStringLiteral("denseWindowCount"), r.denseWindowCount},
        {QStringLiteral("selfPointerCount"), r.selfPointerCount},
        {QStringLiteral("peerInventorCount"), r.peerInventorCount},
        {QStringLiteral("flagged"), flagged},
        {QStringLiteral("manyNodes"), many},
        {QStringLiteral("denseSubnets"), subnets},
        {QStringLiteral("sharedIds"), shared},
        {QStringLiteral("denseWindows"), windows},
        {QStringLiteral("selfPointers"), self},
        {QStringLiteral("peerInventors"), inventors},
    };
}

QVariantMap inboundMap(const dht::InboundSummary &s, int shownClients)
{
    QVariantList methods;
    for (const auto &[name, count] : s.methods) {
        methods.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("count"), count},
            {QStringLiteral("share"), shareOf(double(count), double(s.queries))},
        });
    }
    return QVariantMap{
        {QStringLiteral("queries"), s.queries},
        {QStringLiteral("addresses"), s.addresses},
        {QStringLiteral("ipv4Addresses"), s.ipv4Addresses},
        {QStringLiteral("ipv6Addresses"), s.ipv6Addresses},
        {QStringLiteral("readOnlyAddresses"), s.readOnlyAddresses},
        {QStringLiteral("readOnlyShare"), shareOf(s.readOnlyAddresses, s.addresses)},
        {QStringLiteral("untrackedQueries"), s.untrackedQueries},
        {QStringLiteral("trackedAtMost"), dht::InboundTally::MaxAddresses},
        {QStringLiteral("clients"), talliesFor(s.clients, shownClients, s.addresses)},
        {QStringLiteral("distinctClients"), int(s.clients.size())},
        {QStringLiteral("methods"), methods},
    };
}

} // namespace

void DhtController::buildNetworkStats()
{
    QVariantMap out;
    if (m_statsSet) {
        const dht::NetworkStats &s = m_statsFamily == QLatin1String("ipv4") ? m_statsSet->ipv4
                                     : m_statsFamily == QLatin1String("ipv6") ? m_statsSet->ipv6
                                                                                : m_statsSet->all;
        out = statsMap(s, ShownClients, ShownVersions, ShownPorts);
        out.insert(QStringLiteral("computeMs"), m_statsSet->computeMs);
        out.insert(QStringLiteral("totals"), addressCounts(m_statsSet->all));  // whatever the selected family
    }
    out.insert(QStringLiteral("available"), bool(m_statsSet));
    m_networkStats = out;
    m_suspicious = m_statsSet ? suspiciousMap(m_statsSet->suspicious) : QVariantMap{};
    m_suspicious.insert(QStringLiteral("available"), bool(m_statsSet));
    m_inbound = m_statsSet ? inboundMap(m_statsSet->inbound, ShownClients) : QVariantMap{};
    m_inbound.insert(QStringLiteral("available"), bool(m_statsSet));
    emit networkStatsChanged();
}

// --- node list ----------------------------------------------------------------

namespace {

dht::NodeQuery::Sort sortFromName(const QString &name)
{
    using Sort = dht::NodeQuery::Sort;
    if (name == QLatin1String("rtt"))
        return Sort::RoundTrip;
    if (name == QLatin1String("lastAnswered"))
        return Sort::LastAnswered;
    if (name == QLatin1String("firstSeen"))
        return Sort::FirstSeen;
    if (name == QLatin1String("client"))
        return Sort::Client;
    if (name == QLatin1String("nodesAtAddress"))
        return Sort::NodesAtAddress;
    return Sort::Address;
}

// Feature filters by name: flags that must be set, and that must be clear.
std::pair<quint32, quint32> featureFlags(const QString &name)
{
    using F = dht::CatalogEntry;
    static const QHash<QString, std::pair<quint32, quint32>> filters = {
        {QStringLiteral("bep51-yes"), {F::Tested51 | F::Has51, 0}},
        {QStringLiteral("bep51-no"), {F::Tested51, F::Has51}},
        {QStringLiteral("bep44-yes"), {F::Tested44 | F::Has44, 0}},
        {QStringLiteral("bep44-no"), {F::Tested44, F::Has44}},
        {QStringLiteral("bep32-yes"), {F::Tested32 | F::Has32, 0}},
        {QStringLiteral("bep32-no"), {F::Tested32, F::Has32}},
        {QStringLiteral("ip-yes"), {F::TestedIp | F::SendsIp, 0}},
        {QStringLiteral("ip-no"), {F::TestedIp, F::SendsIp}},
        {QStringLiteral("unknown-204"), {F::TestedUnknown | F::Answers204, 0}},
        {QStringLiteral("unknown-error"), {F::TestedUnknown | F::AnswersOther | F::AnswersError, 0}},
        {QStringLiteral("unknown-reply"), {F::TestedUnknown | F::AnswersOther, F::AnswersError}},
        {QStringLiteral("unknown-none"), {F::TestedUnknown, F::Answers204 | F::AnswersOther}},
        {QStringLiteral("bogons"), {F::ListsBogons, 0}},
        // Set by a lookup too, so it does not imply the check has run; the
        // "checked" form is the set the Optional Features share counts.
        {QStringLiteral("invents-peers"), {F::InventsPeers, 0}},
        {QStringLiteral("invents-peers-checked"), {F::TestedPeers | F::InventsPeers, 0}},
        {QStringLiteral("peers-honest"), {F::TestedPeers, F::InventsPeers}},
    };
    return filters.value(name, {0, 0});
}

quint8 suspicionFromName(const QString &name)
{
    if (name == QLatin1String("many"))
        return dht::ManyNodes;
    if (name == QLatin1String("subnet"))
        return dht::DenseSubnet;
    if (name == QLatin1String("sharedId"))
        return dht::SharedId;
    if (name == QLatin1String("denseIds"))
        return dht::DenseIds;
    if (name == QLatin1String("self"))
        return dht::PointsToSelf;
    if (name == QLatin1String("invents"))
        return dht::InventsPeers;
    if (name == QLatin1String("several"))
        return dht::SeveralSignals;
    if (name == QLatin1String("any"))
        return dht::ManyNodes | dht::DenseSubnet | dht::SharedId | dht::DenseIds | dht::PointsToSelf
               | dht::InventsPeers;
    return 0;
}

} // namespace

void DhtController::resetNodeList()
{
    m_nodeList->clear();
    m_nodeQueryInFlight = false;
    m_nodeQueryAgain = false;
    m_nodeListInfo = QVariantMap{
        {QStringLiteral("matchedNodes"), 0},
        {QStringLiteral("matchedAddresses"), 0},
        {QStringLiteral("offset"), 0},
        {QStringLiteral("limit"), m_nodeQuery.limit},
        {QStringLiteral("queryMs"), 0},
        {QStringLiteral("loaded"), false},
        {QStringLiteral("error"), QString()},
    };
    emit nodeListChanged();
}

QString DhtController::validateIdPrefixFilter(const QString &text) const
{
    dht::NodeId prefix;
    int bits = -1;
    QString error;
    return dht::parseIdPrefixFilter(text, &prefix, &bits, &error) ? QString() : error;
}

QString DhtController::validateAddressFilter(const QString &text) const
{
    QHostAddress address;
    int bits = -1;
    QString error;
    return dht::parseAddressFilter(text, &address, &bits, &error) ? QString() : error;
}

void DhtController::setNodeQuery(const QVariantMap &q)
{
    dht::NodeQuery query;
    const QString family = q.value(QStringLiteral("family")).toString();
    if (family == QLatin1String("ipv4"))
        query.family = dht::Family::IPv4;
    else if (family == QLatin1String("ipv6"))
        query.family = dht::Family::IPv6;

    if (q.contains(QStringLiteral("states"))) {
        query.states = 0;
        const QStringList names = q.value(QStringLiteral("states")).toStringList();
        for (auto state : {dht::CatalogEntry::State::New, dht::CatalogEntry::State::Responsive,
                           dht::CatalogEntry::State::Silent, dht::CatalogEntry::State::Gone,
                           dht::CatalogEntry::State::Unroutable}) {
            if (names.contains(dht::stateName(state)))
                query.states |= 1u << int(state);
        }
    }

    query.client = q.value(QStringLiteral("client")).toString();
    query.version = q.value(QStringLiteral("version")).toString().trimmed();
    const QString bep = q.value(QStringLiteral("bep42")).toString();
    for (auto status : {dht::bep42::Status::Unknown, dht::bep42::Status::Compliant,
                        dht::bep42::Status::NonCompliant, dht::bep42::Status::Exempt}) {
        if (bep == dht::bep42::statusName(status))
            query.bep42 = status;
    }
    query.minRttMs = q.value(QStringLiteral("minRtt"), -1).toInt();
    query.maxRttMs = q.value(QStringLiteral("maxRtt"), -1).toInt();
    query.port = quint16(std::clamp(q.value(QStringLiteral("port"), 0).toInt(), 0, 65535));
    query.minNodesAtAddress = std::max(0, q.value(QStringLiteral("minNodes"), 0).toInt());
    std::tie(query.flagsSet, query.flagsClear) = featureFlags(q.value(QStringLiteral("feature")).toString());
    query.suspicion = suspicionFromName(q.value(QStringLiteral("suspicion")).toString());
    query.sort = sortFromName(q.value(QStringLiteral("sort")).toString());
    query.descending = q.value(QStringLiteral("descending")).toBool();
    query.offset = std::max(0, q.value(QStringLiteral("offset"), 0).toInt());
    query.limit = std::clamp(q.value(QStringLiteral("limit"), 100).toInt(), 1, 1000);

    QString error;
    if (!dht::parseAddressFilter(q.value(QStringLiteral("address")).toString(), &query.subnet, &query.subnetBits,
                                 &error)) {
        m_nodeListInfo.insert(QStringLiteral("error"), error);
        emit nodeListChanged();
        return;
    }
    if (!dht::parseIdPrefixFilter(q.value(QStringLiteral("idPrefix")).toString(), &query.idPrefix,
                                  &query.idPrefixBits, &error)) {
        m_nodeListInfo.insert(QStringLiteral("error"), error);
        emit nodeListChanged();
        return;
    }
    m_nodeListInfo.insert(QStringLiteral("error"), QString());
    m_nodeQuery = query;
    refreshNodeList();
}

void DhtController::showNodePage(int offset)
{
    m_nodeQuery.offset = std::max(0, offset);
    refreshNodeList();
}

void DhtController::refreshNodeList()
{
    if (!m_engine)
        return;
    if (m_nodeQueryInFlight) {
        m_nodeQueryAgain = true;
        return;
    }
    sendNodeQuery();
}

void DhtController::sendNodeQuery()
{
    m_nodeQueryInFlight = true;
    m_nodeQueryAgain = false;
    const quint64 id = ++m_nodeRequestId;
    QMetaObject::invokeMethod(
        m_engine, [engine = m_engine, query = m_nodeQuery, id] { engine->queryNodes(query, id); },
        Qt::QueuedConnection);
}

void DhtController::applyNodePage(const dht::NodeListPage &page)
{
    m_nodeQueryInFlight = false;
    if (page.requestId == m_nodeRequestId) {
        m_nodeQuery.offset = page.offset;
        m_nodeList->setRows(page.rows);
        m_nodeListInfo.insert(QStringLiteral("matchedNodes"), page.matchedNodes);
        m_nodeListInfo.insert(QStringLiteral("matchedAddresses"), page.matchedAddresses);
        m_nodeListInfo.insert(QStringLiteral("offset"), page.offset);
        m_nodeListInfo.insert(QStringLiteral("limit"), m_nodeQuery.limit);
        m_nodeListInfo.insert(QStringLiteral("queryMs"), page.queryMs);
        m_nodeListInfo.insert(QStringLiteral("loaded"), true);
        emit nodeListChanged();
    }
    if (m_nodeQueryAgain && m_engine)
        sendNodeQuery();
}

QStringList DhtController::clientNames() const
{
    QStringList names;
    if (m_statsSet) {
        for (const dht::ClientTally &t : m_statsSet->all.clients)
            names << t.name;
    }
    return names;
}

QStringList DhtController::versionsFor(const QString &client) const
{
    QStringList versions;
    if (m_statsSet) {
        for (const dht::ClientTally &t : m_statsSet->all.versions) {
            if (t.name == client && !t.version.isEmpty())
                versions << t.version;
        }
    }
    return versions;
}

// --- export -------------------------------------------------------------------

void DhtController::setExportStatus(bool busy, const QString &message, bool isError)
{
    m_exportStatus = QVariantMap{
        {QStringLiteral("busy"), busy},
        {QStringLiteral("message"), message},
        {QStringLiteral("error"), isError},
    };
    emit exportStatusChanged();
}

void DhtController::exportNodes(const QUrl &file)
{
    if (!m_engine || m_exportStatus.value(QStringLiteral("busy")).toBool())
        return;
    m_exportPath = file.isLocalFile() ? file.toLocalFile() : file.toString();
    setExportStatus(true, tr("Collecting nodes…"), false);
    const quint64 id = ++m_exportRequestId;
    QMetaObject::invokeMethod(
        m_engine, [engine = m_engine, query = m_nodeQuery, id] { engine->exportNodes(query, id); },
        Qt::QueuedConnection);
}

void DhtController::writeExport(std::shared_ptr<dht::NodeExport> nodes)
{
    if (m_exportThread.joinable())
        m_exportThread.join();
    const qsizetype rows = qsizetype(nodes->entries.size());
    setExportStatus(true, tr("Writing %1 nodes…").arg(QLocale().toString(rows)), false);

    // Formatting millions of rows takes seconds, so it happens off both
    // the UI and the engine threads.
    QPointer<DhtController> self(this);
    const QString path = m_exportPath;
    const quint64 id = m_exportRequestId;
    m_exportThread = std::thread([self, nodes = std::move(nodes), path, rows, id] {
        QString error;
        const bool ok = dht::writeNodesCsv(*nodes, path, &error);
        QMetaObject::invokeMethod(self, [self, ok, error, path, rows, id] {
            if (!self || id != self->m_exportRequestId)
                return;
            if (ok)
                self->setExportStatus(false, tr("Wrote %1 nodes to %2").arg(QLocale().toString(rows), path), false);
            else
                self->setExportStatus(false, tr("Could not write %1: %2").arg(path, error), true);
        });
    });
}

void DhtController::exportSummary(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();

    QVariantMap summary{
        {QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("application"), QStringLiteral("DHT Inspector %1").arg(QCoreApplication::applicationVersion())},
        {QStringLiteral("countedBy"), QStringLiteral("IP address")},
        {QStringLiteral("sizeEstimates"), m_sizeEstimates},
        {QStringLiteral("lookupPerformance"), m_lookupPerformance},
        {QStringLiteral("history"), m_historySource ? historyMap(*m_historySource, 10) : QVariantMap{}},
        {QStringLiteral("census"), m_census},
    };
    if (m_statsSet) {
        const int all = std::numeric_limits<int>::max();
        summary.insert(QStringLiteral("statistics"), QVariantMap{
            {QStringLiteral("all"), statsMap(m_statsSet->all, all, all, all)},
            {QStringLiteral("ipv4"), statsMap(m_statsSet->ipv4, all, all, all)},
            {QStringLiteral("ipv6"), statsMap(m_statsSet->ipv6, all, all, all)},
        });
        summary.insert(QStringLiteral("suspicious"), suspiciousMap(m_statsSet->suspicious));
        summary.insert(QStringLiteral("queriesToUs"), inboundMap(m_statsSet->inbound, all));
    }
    const CrawlStatus &c = m_crawl;
    summary.insert(QStringLiteral("scan"), QVariantMap{
        {QStringLiteral("phase"), c.phase},
        {QStringLiteral("nodesKnown"), c.known},
        {QStringLiteral("nodesResponsive"), c.responsive},
        {QStringLiteral("nodesSilent"), c.silent},
        {QStringLiteral("nodesGone"), c.gone},
        {QStringLiteral("nodesAwaiting"), c.notAsked},
        {QStringLiteral("nodesUnreachable"), c.unroutable},
        {QStringLiteral("queries"), c.queries},
        {QStringLiteral("answers"), c.answers},
        {QStringLiteral("errors"), c.errors},
        {QStringLiteral("timeouts"), c.timeouts},
        {QStringLiteral("featureChecks"), c.featureQueries},
        {QStringLiteral("randomLookups"), c.randomLookups},
        {QStringLiteral("lookupsWithInventedPeers"), c.lookupsWithInventedPeers},
        {QStringLiteral("scanningSeconds"), c.monitoredSeconds},
        {QStringLiteral("nodeListCap"), c.cap},
    });

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setExportStatus(false, tr("Could not write %1: %2").arg(path, out.errorString()), true);
        return;
    }
    out.write(QJsonDocument::fromVariant(summary).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        setExportStatus(false, tr("Could not write %1: %2").arg(path, out.errorString()), true);
        return;
    }
    setExportStatus(false, tr("Wrote the summary to %1").arg(path), false);
}

void DhtController::startCensus()
{
    if (m_engine)
        QMetaObject::invokeMethod(m_engine, &dht::DhtEngine::startCensus, Qt::QueuedConnection);
}

void DhtController::cancelCensus()
{
    if (m_engine)
        QMetaObject::invokeMethod(m_engine, &dht::DhtEngine::cancelCensus, Qt::QueuedConnection);
}

void DhtController::setCatalogCap(int cap)
{
    cap = std::clamp(cap, 0, dht::NodeCatalog::MaxCap);
    if (cap == m_catalogCap)
        return;
    m_catalogCap = cap;
    emit catalogCapChanged();
    if (m_engine) {
        QMetaObject::invokeMethod(m_engine, [engine = m_engine, cap] { engine->setCatalogCap(cap); },
                                  Qt::QueuedConnection);
    }
}

void DhtController::setReadOnlyMode(bool enabled)
{
    if (enabled == m_readOnlyMode)
        return;
    m_readOnlyMode = enabled;
    emit readOnlyModeChanged();

    if (m_engine) {
        QMetaObject::invokeMethod(m_engine, [engine = m_engine, enabled] { engine->setReadOnly(enabled); },
                                  Qt::QueuedConnection);
    }
}

void DhtController::setPortForwarding(bool enabled)
{
    if (enabled == m_portForwarding)
        return;
    m_portForwarding = enabled;
    // QSettings().setValue(PortForwardingKey, m_portForwarding);
    emit portForwardingChanged();

    if (m_engine) {
        QMetaObject::invokeMethod(m_engine, [engine = m_engine, enabled] { engine->setPortForwarding(enabled); },
                                  Qt::QueuedConnection);
    }
}

void DhtController::startEngine()
{
    if (m_engine)
        return;

    // Refuse to start on an ID that is still half-typed.
    const auto idV4 = dht::NodeId::fromHex(m_nodeIdV4.trimmed());
    const auto idV6 = dht::NodeId::fromHex(m_nodeIdV6.trimmed());
    QString idError;
    if (!idV4)
        idError = tr("IPv4 node ID: %1").arg(validateNodeId(m_nodeIdV4));
    else if (m_ipv6Enabled && !idV6)
        idError = tr("IPv6 node ID: %1").arg(validateNodeId(m_nodeIdV6));
    if (!idError.isEmpty()) {
        m_lastError = idError;
        setNotice(idError, true);
        emit statusChanged();
        return;
    }
    // Normalise case and whitespace so the fields show exactly what is used.
    if (idV4->toHex() != m_nodeIdV4) {
        m_nodeIdV4 = idV4->toHex();
        emit nodeIdV4Changed();
    }
    if (idV6 && idV6->toHex() != m_nodeIdV6) {
        m_nodeIdV6 = idV6->toHex();
        emit nodeIdV6Changed();
    }

    const quint64 generation = ++m_generation;
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("dht-engine"));
    m_engine = new dht::DhtEngine;
    m_engine->moveToThread(m_thread);

    // Snapshots already queued when the engine is torn down must not
    // repopulate the UI, hence the generation check.
    connect(m_engine, &dht::DhtEngine::snapshotReady, this, [this, generation](const dht::EngineSnapshot &snapshot) {
        if (generation == m_generation && m_running)
            applySnapshot(snapshot);
    });
    connect(m_engine, &dht::DhtEngine::notice, this, [this, generation](const QString &text, bool isError) {
        if (generation == m_generation)
            setNotice(text, isError);
    });
    connect(m_engine, &dht::DhtEngine::peerSearchFinished, this,
            [this, generation](const dht::PeerSearchResult &result) {
                if (generation == m_generation)
                    applyPeerSearch(result);
            });
    connect(m_engine, &dht::DhtEngine::itemSearchFinished, this,
            [this, generation](const dht::ItemSearchResult &result) {
                if (generation == m_generation)
                    applyItemSearch(result);
            });
    connect(m_engine, &dht::DhtEngine::publishFinished, this,
            [this, generation](const dht::PublishResult &result) {
                if (generation == m_generation)
                    applyPublish(result);
            });
    connect(m_engine, &dht::DhtEngine::probeFinished, this,
            [this, generation](const dht::ProbeResult &result) {
                if (generation == m_generation)
                    applyProbe(result);
            });
    connect(m_engine, &dht::DhtEngine::nodePageReady, this, [this, generation](const dht::NodeListPage &page) {
        if (generation == m_generation && m_running)
            applyNodePage(page);
    });
    connect(m_engine, &dht::DhtEngine::nodeExportReady, this,
            [this, generation](quint64 requestId, std::shared_ptr<dht::NodeExport> nodes) {
                if (generation == m_generation && requestId == m_exportRequestId)
                    writeExport(std::move(nodes));
            });
    connect(m_engine, &dht::DhtEngine::storageSnapshotReady, this,
            [this, generation](const dht::StorageSnapshot &snapshot) {
                if (generation == m_generation && m_running)
                    applyStorageSnapshot(snapshot);
            });
    m_thread->start();

    dht::EngineConfig config;
    config.port = quint16(m_port);
    config.enableIpv6 = m_ipv6Enabled;
    config.portForwarding = m_portForwarding;
    config.bep42 = m_bep42Enabled;
    config.readOnly = m_readOnlyMode;
    config.sendLimit = m_sendLimit;
    config.catalogCap = m_catalogCap;
    config.nodeIdV4 = *idV4;
    config.nodeIdV6 = idV6;

    bool ok = false;
    QString error;
    QMetaObject::invokeMethod(
        m_engine, [engine = m_engine, config, &ok, &error] { ok = engine->start(config, &error); },
        Qt::BlockingQueuedConnection);

    if (!ok) {
        destroyEngine();
        m_lastError = error;
        setNotice(error, true);
        emit statusChanged();
        return;
    }

    m_lastError.clear();
    m_running = true;
    setNotice(QString(), false);
    emit runningChanged();
    emit statusChanged();
}

void DhtController::stopEngine()
{
    destroyEngine();
    m_running = false;
    if (m_monitoring) {
        m_monitoring = false;
        emit monitoringChanged();
    }
    resetStatus();
    setNotice(QString(), false);
    emit runningChanged();
    emit statusChanged();
}

void DhtController::destroyEngine()
{
    if (!m_engine)
        return;

    ++m_generation;
    QMetaObject::invokeMethod(m_engine, &dht::DhtEngine::shutdown, Qt::BlockingQueuedConnection);
    m_engine->disconnect(this);

    // Delete on the engine thread, then let the thread finish.
    connect(m_engine, &QObject::destroyed, m_thread, &QThread::quit, Qt::DirectConnection);
    m_engine->deleteLater();
    m_thread->wait();

    delete m_thread;
    m_thread = nullptr;
    m_engine = nullptr;
}

void DhtController::resetStatus()
{
    m_ipv4 = FamilyStatus{};
    m_ipv6 = FamilyStatus{};
    m_portMapping = PortMappingStatus{};
    m_stats = EngineStatistics{};
    m_crawl = CrawlStatus{};
    m_sizeEstimates.clear();
    m_lookupPerformance.clear();
    m_historySource.reset();
    m_history = historyMap({}, ShownHistoryClients);
    emit historyChanged();
    m_census = toCensusMap(dht::CensusSnapshot{});
    resetNodeList();
    m_statsSet.reset();
    buildNetworkStats();
    m_traffic.clear();
    m_nodes->clear();
    clearDataStore();
    clearSearch();
    emit snapshotChanged();
}

void DhtController::applySnapshot(const dht::EngineSnapshot &snapshot)
{
    const int previousPort = m_ipv4.port;
    m_ipv4 = toFamilyStatus(snapshot.ipv4);
    m_ipv6 = toFamilyStatus(snapshot.ipv6);

    // BEP 42 may have replaced an ID; the fields always show the one in use,
    // and keep it after the engine stops.
    if (snapshot.ipv4.bound && m_ipv4.nodeId != m_nodeIdV4) {
        m_nodeIdV4 = m_ipv4.nodeId;
        emit nodeIdV4Changed();
    }
    if (snapshot.ipv6.bound && m_ipv6.nodeId != m_nodeIdV6) {
        m_nodeIdV6 = m_ipv6.nodeId;
        emit nodeIdV6Changed();
    }
    m_portMapping = toPortMappingStatus(snapshot.portMapping);
    m_stats = toStatistics(snapshot.stats);

    // Rates over a sliding window: snapshots also arrive early when
    // something changes, so single intervals would be noisy.
    constexpr qint64 WindowMs = 3000;
    if (!m_trafficClock.isValid())
        m_trafficClock.start();
    const qint64 now = m_trafficClock.elapsed();
    const dht::CrawlSnapshot &c = snapshot.crawl;
    m_traffic.push_back({now, snapshot.stats.bytesIn, snapshot.stats.bytesOut, c.queries, c.answers + c.errors});
    while (m_traffic.size() > 2 && now - m_traffic[1].atMs >= WindowMs)
        m_traffic.pop_front();
    const TrafficSample &oldest = m_traffic.front();
    if (now - oldest.atMs >= 250) {
        const double seconds = double(now - oldest.atMs) / 1000.0;
        m_stats.bytesInPerSecond = double(snapshot.stats.bytesIn - oldest.bytesIn) / seconds;
        m_stats.bytesOutPerSecond = double(snapshot.stats.bytesOut - oldest.bytesOut) / seconds;
    }

    m_crawl = toCrawlStatus(c);

    if (c.stats != m_statsSet) {
        m_statsSet = c.stats;
        buildNetworkStats();
    }
    // Size estimates, with how much of each network the scan has reached.
    m_sizeEstimates.clear();
    // The quick estimate counts nodes; divided by the nodes per answering
    // address seen so far, it becomes a rough count of addresses.
    const auto addEstimate = [&](const QString &family, const dht::SizeEstimate &e,
                                 const dht::NetworkStats *stats, bool enabled) {
        if (!enabled)
            return;
        const double perIp = stats ? stats->nodesPerIp() : 1.0;
        const int answering = stats ? stats->answeringIps : 0;
        const double ips = e.median / perIp;
        m_sizeEstimates.append(QVariantMap{
            {QStringLiteral("family"), family},
            {QStringLiteral("samples"), e.samples},
            {QStringLiteral("nodes"), e.median},
            {QStringLiteral("nodesPerIp"), perIp},
            {QStringLiteral("median"), ips},
            {QStringLiteral("low"), e.low / perIp},
            {QStringLiteral("high"), e.high / perIp},
            {QStringLiteral("answeringIps"), answering},
            {QStringLiteral("coverage"), ips > 0 ? double(answering) / ips : 0.0},
        });
    };
    addEstimate(QStringLiteral("IPv4"), c.sizeV4, c.stats ? &c.stats->ipv4 : nullptr, true);
    addEstimate(QStringLiteral("IPv6"), c.sizeV6, c.stats ? &c.stats->ipv6 : nullptr, snapshot.ipv6.enabled);

    m_lookupPerformance.clear();
    m_lookupPerformance.append(lookupMap(QStringLiteral("IPv4"), c.lookupV4));
    if (snapshot.ipv6.enabled)
        m_lookupPerformance.append(lookupMap(QStringLiteral("IPv6"), c.lookupV6));
    if (c.history != m_historySource) {
        m_historySource = c.history;
        m_history = historyMap(m_historySource ? *m_historySource : std::vector<dht::HistorySample>{},
                               ShownHistoryClients);
        emit historyChanged();
    }

    m_census = toCensusMap(snapshot.census);
    if (now - oldest.atMs >= 250) {
        const double seconds = double(now - oldest.atMs) / 1000.0;
        m_crawl.queriesPerSecond = double(c.queries - oldest.crawlQueries) / seconds;
        m_crawl.answersPerSecond = double(c.answers + c.errors - oldest.crawlAnswers) / seconds;
    }

    m_nodes->update(snapshot.nodes);
    emit snapshotChanged();
    if (m_ipv4.port != previousPort)
        emit statusChanged();
}

void DhtController::setNotice(const QString &text, bool isError)
{
    if (text == m_notice && isError == m_noticeIsError)
        return;
    m_notice = text;
    m_noticeIsError = isError;
    emit noticeChanged();
}

QString DhtController::validateEndpoint(const QString &text) const
{
    if (text.trimmed().isEmpty())
        return {};
    QString error;
    const auto parsed = dht::parseHostPort(text, &error);
    if (!parsed)
        return error;
    if (!parsed->literal.isNull() && dht::familyOf(parsed->literal) == dht::Family::IPv6 && !m_ipv6Enabled)
        return tr("IPv6 is turned off");
    return {};
}

bool DhtController::injectNode(const QString &text)
{
    if (!m_engine)
        return false;
    const QString error = validateEndpoint(text);
    if (!error.isEmpty() || text.trimmed().isEmpty()) {
        setNotice(error.isEmpty() ? tr("Enter an address and port") : error, true);
        return false;
    }
    const auto parsed = dht::parseHostPort(text);
    QMetaObject::invokeMethod(
        m_engine, [engine = m_engine, host = parsed->host, port = parsed->port] { engine->addNode(host, port); },
        Qt::QueuedConnection);
    return true;
}

void DhtController::autoBootstrap()
{
    if (!m_engine)
        return;
    QMetaObject::invokeMethod(m_engine, [engine = m_engine] { engine->bootstrap(); }, Qt::QueuedConnection);
}

void DhtController::refreshDataStore()
{
    if (!m_engine) {
        clearDataStore();
        return;
    }
    QMetaObject::invokeMethod(m_engine, [engine = m_engine] { engine->requestStorageSnapshot(); },
                              Qt::QueuedConnection);
}

void DhtController::selectInfohash(const QString &infohash)
{
    if (infohash == m_selectedInfohash)
        return;
    m_selectedInfohash = infohash;
    m_storedPeers->clear();
    emit dataStoreChanged();
    refreshDataStore();
}

void DhtController::applyStorageSnapshot(const dht::StorageSnapshot &snapshot)
{
    m_dataStore.infohashCount = snapshot.infohashCount;
    m_dataStore.peerCount = snapshot.peerCount;
    m_dataStore.listed = int(snapshot.infohashes.size());
    m_dataStore.maxInfohashes = snapshot.maxInfohashes;
    m_dataStore.maxPeersPerInfohash = snapshot.maxPeersPerInfohash;
    m_dataStore.ttlMinutes = int(snapshot.ttlMs / 60000);
    m_dataStore.truncated = snapshot.truncated;
    m_dataStore.immutableCount = snapshot.immutableCount;
    m_dataStore.mutableCount = snapshot.mutableCount;
    m_dataStore.maxItems = snapshot.maxItems;
    m_dataStore.itemTtlMinutes = int(snapshot.itemTtlMs / 60000);

    std::vector<dht::StoredPeerRow> peers;
    bool selectionFound = false;
    for (const dht::StoredInfohashRow &row : snapshot.infohashes) {
        if (row.infohash.toHex() == m_selectedInfohash) {
            peers = row.peers;
            selectionFound = true;
            break;
        }
    }
    // An infohash whose peers have all expired is gone from the listing.
    if (!selectionFound)
        m_selectedInfohash.clear();

    m_storedInfohashes->update(snapshot.infohashes);
    m_storedPeers->update(std::move(peers));
    m_storedItems->update(snapshot.items);
    emit dataStoreChanged();
}

void DhtController::clearDataStore()
{
    m_storedInfohashes->clear();
    m_storedPeers->clear();
    m_storedItems->clear();
    m_selectedInfohash.clear();
    m_dataStore = DataStoreSummary{};
    emit dataStoreChanged();
}

QString DhtController::validateHash(const QString &text) const
{
    const QString hash = text.trimmed();
    if (hash.isEmpty())
        return {};
    static const QRegularExpression hex(QStringLiteral("^[0-9A-Fa-f]*$"));
    if (!hex.match(hash).hasMatch())
        return tr("only hexadecimal digits 0-9 and a-f are allowed");
    if (hash.size() != dht::NodeId::Size * 2)
        return tr("needs 40 hexadecimal digits, has %1").arg(hash.size());
    return {};
}

QString DhtController::randomHash() const
{
    return dht::NodeId::random().toHex();
}

void DhtController::searchPeers(const QString &hash)
{
    const auto target = dht::NodeId::fromHex(hash.trimmed());
    if (!m_engine || !target)
        return;
    m_peerResults->clear();
    m_peerSearch = PeerSearchStatus{};
    m_peerSearch.infohash = target->toHex();
    m_searchBusy = true;
    m_searchStatus = tr("Searching the DHT for peers on %1…").arg(target->toHex());
    emit searchChanged();
    QMetaObject::invokeMethod(m_engine, [engine = m_engine, id = *target] { engine->searchPeers(id); },
                              Qt::QueuedConnection);
}

void DhtController::searchItem(const QString &hash, const QString &salt)
{
    const auto target = dht::NodeId::fromHex(hash.trimmed());
    if (!m_engine || !target)
        return;
    m_itemSearch = ItemSearchStatus{};
    m_itemSearch.target = target->toHex();
    m_itemSearch.salt = salt;
    m_searchBusy = true;
    m_searchStatus = tr("Fetching the item stored under %1…").arg(target->toHex());
    emit searchChanged();
    QMetaObject::invokeMethod(
        m_engine, [engine = m_engine, id = *target, salt = salt.toUtf8()] { engine->searchItem(id, salt); },
        Qt::QueuedConnection);
}

void DhtController::announcePeer(const QString &hash, int port, bool impliedPort)
{
    const auto target = dht::NodeId::fromHex(hash.trimmed());
    if (!m_engine || !target)
        return;
    m_publishStatus = PublishStatus{};
    m_publishBusy = true;
    emit publishChanged();
    QMetaObject::invokeMethod(m_engine,
                              [engine = m_engine, id = *target, port = quint16(std::clamp(port, 1, 65535)), impliedPort] {
                                  engine->announcePeer(id, port, impliedPort);
                              },
                              Qt::QueuedConnection);
}

void DhtController::publishImmutable(const QString &text)
{
    if (!m_engine)
        return;
    const QByteArray value = dht::bencode(dht::BValue(text.toUtf8()));
    m_publishStatus = PublishStatus{};
    m_publishBusy = true;
    emit publishChanged();
    QMetaObject::invokeMethod(m_engine, [engine = m_engine, value] { engine->publishImmutable(value); },
                              Qt::QueuedConnection);
}

void DhtController::publishMutable(const QString &publicKeyHex, const QString &secretKeyHex, const QString &salt,
                                   int sequence, const QString &text)
{
    if (!m_engine)
        return;
    const QByteArray publicKey = QByteArray::fromHex(publicKeyHex.trimmed().toLatin1());
    const QByteArray secretKey = QByteArray::fromHex(secretKeyHex.trimmed().toLatin1());
    const QByteArray value = dht::bencode(dht::BValue(text.toUtf8()));

    m_publishStatus = PublishStatus{};
    if (publicKey.size() != dht::bep44::PublicKeyBytes || secretKey.size() != 64) {
        m_publishStatus.error = tr("The key pair must be 64 and 128 hexadecimal digits.");
        m_publishStatus.done = true;
        emit publishChanged();
        return;
    }

    m_publishBusy = true;
    emit publishChanged();
    QMetaObject::invokeMethod(m_engine,
                              [engine = m_engine, publicKey, secretKey, salt = salt.toUtf8(),
                               sequence = qint64(sequence), value] {
                                  engine->publishMutable(publicKey, secretKey, salt, sequence, value, std::nullopt);
                              },
                              Qt::QueuedConnection);
}

QVariantMap DhtController::generateKeyPair() const
{
    const dht::ed25519::KeyPair keys = dht::ed25519::randomKeyPair();
    return {{QStringLiteral("publicKey"), QString::fromLatin1(keys.publicKey.toHex())},
            {QStringLiteral("secretKey"), QString::fromLatin1(keys.secretKey.toHex())}};
}

QString DhtController::immutableTargetFor(const QString &text) const
{
    return dht::bep44::immutableTarget(dht::bencode(dht::BValue(text.toUtf8()))).toHex();
}

QString DhtController::mutableTargetFor(const QString &publicKeyHex, const QString &salt) const
{
    const QByteArray publicKey = QByteArray::fromHex(publicKeyHex.trimmed().toLatin1());
    if (publicKey.size() != dht::bep44::PublicKeyBytes)
        return tr("— not a 64-digit public key —");
    return dht::bep44::mutableTarget(publicKey, salt.toUtf8()).toHex();
}

void DhtController::applyPeerSearch(const dht::PeerSearchResult &result)
{
    // Group by peer, keeping every node that claimed it.
    std::vector<PeerResultRow> rows;
    QHash<QString, int> rowOf;
    for (const dht::PeerSighting &sighting : result.sightings) {
        const QString peer = sighting.peer.toString();
        const QString source = sighting.source.toString();
        const auto known = rowOf.constFind(peer);
        if (known == rowOf.constEnd()) {
            rowOf.insert(peer, int(rows.size()));
            rows.push_back(PeerResultRow{peer, QStringList{source}});
        } else if (!rows[*known].sources.contains(source)) {
            rows[*known].sources << source;
        }
    }
    // The peers the most nodes agree on first.
    std::sort(rows.begin(), rows.end(), [](const PeerResultRow &a, const PeerResultRow &b) {
        if (a.sources.size() != b.sources.size())
            return a.sources.size() > b.sources.size();
        return a.peer < b.peer;
    });

    const int peerCount = int(rows.size());
    m_peerResults->update(std::move(rows));
    m_peerSearch.infohash = result.infohash.toHex();
    m_peerSearch.queried = result.queried;
    m_peerSearch.responded = result.responded;
    m_peerSearch.done = true;
    m_searchBusy = false;
    m_searchStatus = tr("%n peer(s) found for %1.", nullptr, peerCount).arg(m_peerSearch.infohash);
    emit searchChanged();
}

void DhtController::applyItemSearch(const dht::ItemSearchResult &result)
{
    m_itemSearch.done = true;
    m_itemSearch.found = result.found;
    m_itemSearch.isMutable = result.isMutable;
    m_itemSearch.target = result.target.toHex();
    m_itemSearch.value = result.found ? decodedPreview(result.value, 200) : QString();
    m_itemSearch.rawValue = result.found ? previewValue(result.value, 200) : QString();
    m_itemSearch.publicKey = QString::fromLatin1(result.publicKey.toHex());
    m_itemSearch.salt = QString::fromUtf8(result.salt);
    m_itemSearch.signature = QString::fromLatin1(result.signature.toHex());
    m_itemSearch.sequence = result.sequence >= 0 ? QString::number(result.sequence) : QString();
    m_itemSearch.queried = result.queried;
    m_itemSearch.responded = result.responded;
    m_searchBusy = false;
    m_searchStatus = result.found ? tr("Item found under %1.").arg(m_itemSearch.target)
                                  : tr("No item stored under %1.").arg(m_itemSearch.target);
    emit searchChanged();
}

void DhtController::applyPublish(const dht::PublishResult &result)
{
    switch (result.kind) {
    case dht::PublishResult::Kind::Announce:
        m_publishStatus.kind = tr("Announce");
        m_publishStatus.kindId = QStringLiteral("announce");
        break;
    case dht::PublishResult::Kind::Immutable:
        m_publishStatus.kind = tr("Immutable item");
        m_publishStatus.kindId = QStringLiteral("immutable");
        break;
    case dht::PublishResult::Kind::Mutable:
        m_publishStatus.kind = tr("Mutable item");
        m_publishStatus.kindId = QStringLiteral("mutable");
        break;
    }
    m_publishStatus.target = result.target.toHex();
    m_publishStatus.accepted = result.accepted;
    m_publishStatus.attempted = result.attempted;
    m_publishStatus.error = result.error;
    m_publishStatus.done = true;
    m_publishBusy = false;
    emit publishChanged();
}

ProbeStatus DhtController::probe() const
{
    ProbeStatus out;
    const ProbeExchange *exchange = m_probeHistory->at(m_selectedProbe);
    if (!exchange)
        return out;

    const dht::ProbeResult &result = exchange->result;
    out.valid = true;
    out.method = QString::fromLatin1(result.method);
    out.endpoint = result.endpoint.toString();
    out.outcome = ProbeHistoryModel::outcomeOf(result);
    out.summary = result.summary;
    out.decoded = result.decoded;
    out.request = dht::krpc::escapeBytes(result.request);
    out.response = dht::krpc::escapeBytes(result.response);
    out.rtt = result.rttMs >= 0 && !result.timedOut ? tr("%1 ms").arg(result.rttMs) : tr("—");
    out.errorMessage = result.errorMessage;
    out.time = exchange->time;

    if (!result.response.isEmpty()) {
        const dht::ClientInfo client = dht::decodeClientVersion(result.version);
        out.client = client.display();
        out.clientKind = dht::clientKindName(client.kind);
        out.clientNote = client.note;
        out.clientRaw = client.rawText();
    }
    return out;
}

void DhtController::applyProbe(const dht::ProbeResult &result)
{
    ProbeExchange exchange;
    exchange.result = result;
    exchange.time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    m_probeHistory->prepend(exchange, 50);
    m_selectedProbe = 0;
    m_probeBusy = false;
    emit probeChanged();
}

void DhtController::probeFailedLocally(const QString &method, const QString &where, const QString &message)
{
    // Keep refusals in the same history as real exchanges, so the record of
    // what was asked stays complete.
    dht::ProbeResult result;
    result.method = method.toLatin1();
    result.errorMessage = message;
    result.summary = where.isEmpty() ? message : QStringLiteral("%1: %2").arg(where, message);
    applyProbe(result);
}

void DhtController::probeEndpoint(const dht::Endpoint &endpoint, const QString &method, const QString &hashHex,
                                  int announcePort, bool impliedPort, bool isAnnounce)
{
    if (!m_engine)
        return;

    const QByteArray methodBytes = method.trimmed().toLatin1();
    const auto hash = dht::NodeId::fromHex(hashHex.trimmed());
    const bool needsTarget = methodBytes == "find_node" || methodBytes == "get";
    const bool needsInfohash = methodBytes == "get_peers" || isAnnounce;

    if ((needsTarget || needsInfohash) && !hash) {
        probeFailedLocally(method, endpoint.toString(), tr("needs a 40 hexadecimal digit hash"));
        return;
    }

    m_probeBusy = true;
    emit probeChanged();

    if (isAnnounce) {
        QMetaObject::invokeMethod(m_engine,
                                  [engine = m_engine, endpoint, id = *hash,
                                   port = quint16(std::clamp(announcePort, 1, 65535)), impliedPort] {
                                      engine->probeAnnounce(endpoint, id, port, impliedPort);
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    dht::BValue::Dict args;
    if (needsTarget)
        args.emplace("target", dht::BValue(hash->toBytes()));
    else if (needsInfohash)
        args.emplace("info_hash", dht::BValue(hash->toBytes()));

    QMetaObject::invokeMethod(
        m_engine,
        [engine = m_engine, endpoint, methodBytes, args = std::move(args)]() mutable {
            engine->probe(endpoint, methodBytes, std::move(args));
        },
        Qt::QueuedConnection);
}

void DhtController::probeNode(const QString &address, const QString &method, const QString &hashHex)
{
    if (!m_engine || method.trimmed().isEmpty())
        return;

    QString error;
    const auto parsed = dht::parseHostPort(address, &error);
    if (!parsed) {
        probeFailedLocally(method, address.trimmed(), error);
        return;
    }
    if (!parsed->literal.isNull()) {
        probeEndpoint(dht::Endpoint(parsed->literal, parsed->port), method, hashHex, 0, false, false);
        return;
    }

    // A hostname: resolve it here, then ask the node itself.
    m_probeBusy = true;
    emit probeChanged();
    QPointer<DhtController> self(this);
    QHostInfo::lookupHost(parsed->host, this,
                          [self, host = parsed->host, port = parsed->port, method, hashHex](const QHostInfo &info) {
                              if (!self)
                                  return;
                              if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                                  self->probeFailedLocally(method, host,
                                                           tr("could not resolve: %1").arg(info.errorString()));
                                  return;
                              }
                              self->probeEndpoint(dht::Endpoint(info.addresses().constFirst(), port), method,
                                                  hashHex, 0, false, false);
                          });
}

void DhtController::probeAnnounce(const QString &address, const QString &infohashHex, int port, bool impliedPort)
{
    if (!m_engine)
        return;
    QString error;
    const auto parsed = dht::parseHostPort(address, &error);
    if (!parsed) {
        probeFailedLocally(QStringLiteral("announce_peer"), address.trimmed(), error);
        return;
    }
    if (!parsed->literal.isNull()) {
        probeEndpoint(dht::Endpoint(parsed->literal, parsed->port), QStringLiteral("announce_peer"), infohashHex,
                      port, impliedPort, true);
        return;
    }

    m_probeBusy = true;
    emit probeChanged();
    QPointer<DhtController> self(this);
    QHostInfo::lookupHost(parsed->host, this,
                          [self, host = parsed->host, hostPort = parsed->port, infohashHex, port,
                           impliedPort](const QHostInfo &info) {
                              if (!self)
                                  return;
                              if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                                  self->probeFailedLocally(QStringLiteral("announce_peer"), host,
                                                           tr("could not resolve: %1").arg(info.errorString()));
                                  return;
                              }
                              self->probeEndpoint(dht::Endpoint(info.addresses().constFirst(), hostPort),
                                                  QStringLiteral("announce_peer"), infohashHex, port, impliedPort,
                                                  true);
                          });
}

void DhtController::setProbeAddress(const QString &address)
{
    if (address == m_probeAddress)
        return;
    m_probeAddress = address;
    emit probeAddressChanged();
}

void DhtController::openProbe(const QString &address)
{
    setProbeAddress(address);
    // Emitted even when the address is unchanged: the point of the request is
    // to bring the Probe tab forward.
    emit probeRequested();
}

void DhtController::selectProbe(int row)
{
    if (row == m_selectedProbe || !m_probeHistory->at(row))
        return;
    m_selectedProbe = row;
    emit probeChanged();
}

void DhtController::clearProbes()
{
    m_probeHistory->clear();
    m_selectedProbe = -1;
    m_probeBusy = false;
    emit probeChanged();
}

void DhtController::clearSearch()
{
    m_peerResults->clear();
    m_peerSearch = PeerSearchStatus{};
    m_itemSearch = ItemSearchStatus{};
    m_publishStatus = PublishStatus{};
    m_searchBusy = false;
    m_publishBusy = false;
    m_searchStatus.clear();
    emit searchChanged();
    emit publishChanged();
    clearProbes();
}

QStringList DhtController::bootstrapRouters() const
{
    QStringList out;
    for (const dht::BootstrapRouter &router : dht::defaultBootstrapRouters())
        out << QStringLiteral("%1:%2").arg(router.host).arg(router.port);
    return out;
}
