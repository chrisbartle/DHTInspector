#include "DhtController.h"

#include "dhtcore/DhtEngine.h"

#include <QRegularExpression>
// Settings persistence is disabled; see DhtController::DhtController().
// #include <QSettings>
#include <QThread>

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
    out.storedInfohashes = s.storedInfohashes;
    out.storedPeers = s.storedPeers;
    out.activeLookups = s.activeLookups;
    return out;
}

} // namespace

DhtController::DhtController(QObject *parent)
    : QObject(parent)
    , m_nodes(new NodeListModel(this))
{
    qRegisterMetaType<dht::EngineSnapshot>();

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
    m_thread->start();

    dht::EngineConfig config;
    config.port = quint16(m_port);
    config.enableIpv6 = m_ipv6Enabled;
    config.portForwarding = m_portForwarding;
    config.bep42 = m_bep42Enabled;
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
    m_nodes->clear();
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

QStringList DhtController::bootstrapRouters() const
{
    QStringList out;
    for (const dht::BootstrapRouter &router : dht::defaultBootstrapRouters())
        out << QStringLiteral("%1:%2").arg(router.host).arg(router.port);
    return out;
}
