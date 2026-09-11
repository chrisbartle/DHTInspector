#pragma once

#include "NodeListModel.h"
#include "StatusTypes.h"

#include "dhtcore/Snapshot.h"

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QThread;

namespace dht {
class DhtEngine;
}

// The QML-facing side of the engine. Owns the engine thread: starting
// creates a fresh DhtEngine there, stopping destroys it and every piece of
// state it held.
class DhtController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)

    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(bool ipv6Enabled READ ipv6Enabled WRITE setIpv6Enabled NOTIFY ipv6EnabledChanged)
    Q_PROPERTY(bool portForwarding READ portForwarding WRITE setPortForwarding NOTIFY portForwardingChanged)
    Q_PROPERTY(bool bep42Enabled READ bep42Enabled WRITE setBep42Enabled NOTIFY bep42EnabledChanged)
    Q_PROPERTY(QString nodeIdV4 READ nodeIdV4 WRITE setNodeIdV4 NOTIFY nodeIdV4Changed)
    Q_PROPERTY(QString nodeIdV6 READ nodeIdV6 WRITE setNodeIdV6 NOTIFY nodeIdV6Changed)

    Q_PROPERTY(FamilyStatus ipv4 READ ipv4 NOTIFY snapshotChanged)
    Q_PROPERTY(FamilyStatus ipv6 READ ipv6 NOTIFY snapshotChanged)
    Q_PROPERTY(PortMappingStatus portMapping READ portMapping NOTIFY snapshotChanged)
    Q_PROPERTY(EngineStatistics stats READ stats NOTIFY snapshotChanged)
    Q_PROPERTY(NodeListModel *nodes READ nodes CONSTANT)

    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool noticeIsError READ noticeIsError NOTIFY noticeChanged)

public:
    explicit DhtController(QObject *parent = nullptr);
    ~DhtController() override;

    bool running() const { return m_running; }
    void setRunning(bool running);
    QString statusText() const;
    QString lastError() const { return m_lastError; }

    int port() const { return m_port; }
    void setPort(int port);
    bool ipv6Enabled() const { return m_ipv6Enabled; }
    void setIpv6Enabled(bool enabled);
    bool portForwarding() const { return m_portForwarding; }
    void setPortForwarding(bool enabled);
    bool bep42Enabled() const { return m_bep42Enabled; }
    void setBep42Enabled(bool enabled);

    // Node IDs as typed, normally 40 hex digits. Editable while stopped;
    // while running they follow the engine, which may replace them (BEP 42).
    QString nodeIdV4() const { return m_nodeIdV4; }
    void setNodeIdV4(const QString &id);
    QString nodeIdV6() const { return m_nodeIdV6; }
    void setNodeIdV6(const QString &id);

    FamilyStatus ipv4() const { return m_ipv4; }
    FamilyStatus ipv6() const { return m_ipv6; }
    PortMappingStatus portMapping() const { return m_portMapping; }
    EngineStatistics stats() const { return m_stats; }
    NodeListModel *nodes() const { return m_nodes; }

    QString notice() const { return m_notice; }
    bool noticeIsError() const { return m_noticeIsError; }

    // Empty string when `text` is acceptable (or empty), otherwise why not.
    Q_INVOKABLE QString validateEndpoint(const QString &text) const;
    Q_INVOKABLE bool injectNode(const QString &text);
    Q_INVOKABLE void autoBootstrap();
    Q_INVOKABLE QStringList bootstrapRouters() const;

    // Empty string when `text` is a valid node ID, otherwise why not.
    Q_INVOKABLE QString validateNodeId(const QString &text) const;
    Q_INVOKABLE void randomizeNodeId(bool ipv6);

signals:
    void runningChanged();
    void statusChanged();
    void portChanged();
    void ipv6EnabledChanged();
    void portForwardingChanged();
    void bep42EnabledChanged();
    void nodeIdV4Changed();
    void nodeIdV6Changed();
    void snapshotChanged();
    void noticeChanged();

private:
    void startEngine();
    void stopEngine();
    void destroyEngine();
    void applySnapshot(const dht::EngineSnapshot &snapshot);
    void resetStatus();
    void setNotice(const QString &text, bool isError);

    QThread *m_thread = nullptr;
    dht::DhtEngine *m_engine = nullptr;
    quint64 m_generation = 0;

    bool m_running = false;
    QString m_lastError;
    int m_port = 6881;
    bool m_ipv6Enabled = false;
    bool m_portForwarding = false;
    bool m_bep42Enabled = true;
    QString m_nodeIdV4;
    QString m_nodeIdV6;

    FamilyStatus m_ipv4;
    FamilyStatus m_ipv6;
    PortMappingStatus m_portMapping;
    EngineStatistics m_stats;
    NodeListModel *m_nodes;

    QString m_notice;
    bool m_noticeIsError = false;
};
