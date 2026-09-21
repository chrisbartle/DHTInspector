#pragma once

#include "CatalogModels.h"
#include "DataStoreModels.h"
#include "ProbeModels.h"
#include "SearchModels.h"
#include "NodeListModel.h"
#include "StatusTypes.h"

#include "dhtcore/NodeCatalog.h"
#include "dhtcore/NodeList.h"
#include "dhtcore/Snapshot.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <deque>
#include <thread>
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
    Q_PROPERTY(bool readOnlyMode READ readOnlyMode WRITE setReadOnlyMode NOTIFY readOnlyModeChanged)
    Q_PROPERTY(int sendLimit READ sendLimit WRITE setSendLimit NOTIFY sendLimitChanged)
    Q_PROPERTY(bool monitoring READ monitoring WRITE setMonitoring NOTIFY monitoringChanged)
    Q_PROPERTY(int catalogCap READ catalogCap WRITE setCatalogCap NOTIFY catalogCapChanged)
    Q_PROPERTY(CrawlStatus crawl READ crawl NOTIFY snapshotChanged)
    Q_PROPERTY(QString statsFamily READ statsFamily WRITE setStatsFamily NOTIFY statsFamilyChanged)
    Q_PROPERTY(QVariantMap networkStats READ networkStats NOTIFY networkStatsChanged)
    Q_PROPERTY(QVariantMap suspicious READ suspicious NOTIFY networkStatsChanged)
    Q_PROPERTY(QVariantMap inbound READ inbound NOTIFY networkStatsChanged)
    Q_PROPERTY(QVariantList sizeEstimates READ sizeEstimates NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList lookupPerformance READ lookupPerformance NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantMap history READ history NOTIFY historyChanged)
    Q_PROPERTY(QVariantMap census READ census NOTIFY snapshotChanged)
    Q_PROPERTY(CatalogListModel *nodeList READ nodeList CONSTANT)
    Q_PROPERTY(QVariantMap nodeListInfo READ nodeListInfo NOTIFY nodeListChanged)
    Q_PROPERTY(QStringList clientNames READ clientNames NOTIFY networkStatsChanged)
    Q_PROPERTY(QVariantMap exportStatus READ exportStatus NOTIFY exportStatusChanged)
    Q_PROPERTY(QString nodeIdV4 READ nodeIdV4 WRITE setNodeIdV4 NOTIFY nodeIdV4Changed)
    Q_PROPERTY(QString nodeIdV6 READ nodeIdV6 WRITE setNodeIdV6 NOTIFY nodeIdV6Changed)

    Q_PROPERTY(FamilyStatus ipv4 READ ipv4 NOTIFY snapshotChanged)
    Q_PROPERTY(FamilyStatus ipv6 READ ipv6 NOTIFY snapshotChanged)
    Q_PROPERTY(PortMappingStatus portMapping READ portMapping NOTIFY snapshotChanged)
    Q_PROPERTY(EngineStatistics stats READ stats NOTIFY snapshotChanged)
    Q_PROPERTY(NodeListModel *nodes READ nodes CONSTANT)

    Q_PROPERTY(StoredInfohashModel *storedInfohashes READ storedInfohashes CONSTANT)
    Q_PROPERTY(StoredPeerModel *storedPeers READ storedPeers CONSTANT)
    Q_PROPERTY(StoredItemModel *storedItems READ storedItems CONSTANT)
    Q_PROPERTY(DataStoreSummary dataStore READ dataStore NOTIFY dataStoreChanged)
    Q_PROPERTY(QString selectedInfohash READ selectedInfohash NOTIFY dataStoreChanged)

    Q_PROPERTY(bool searchBusy READ searchBusy NOTIFY searchChanged)
    Q_PROPERTY(QString searchStatus READ searchStatus NOTIFY searchChanged)
    Q_PROPERTY(PeerResultModel *peerResults READ peerResults CONSTANT)
    Q_PROPERTY(PeerSearchStatus peerSearch READ peerSearch NOTIFY searchChanged)
    Q_PROPERTY(ItemSearchStatus itemSearch READ itemSearch NOTIFY searchChanged)
    Q_PROPERTY(bool publishBusy READ publishBusy NOTIFY publishChanged)
    Q_PROPERTY(QString probeAddress READ probeAddress WRITE setProbeAddress NOTIFY probeAddressChanged)
    Q_PROPERTY(ProbeHistoryModel *probeHistory READ probeHistory CONSTANT)
    Q_PROPERTY(ProbeStatus probe READ probe NOTIFY probeChanged)
    Q_PROPERTY(int selectedProbe READ selectedProbe NOTIFY probeChanged)
    Q_PROPERTY(bool probeBusy READ probeBusy NOTIFY probeChanged)
    Q_PROPERTY(PublishStatus publishStatus READ publishStatus NOTIFY publishChanged)

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
    // BEP 43. Unlike the other engine settings this one can be flipped while
    // the engine is running, because it changes nothing but behaviour.
    bool readOnlyMode() const { return m_readOnlyMode; }
    void setReadOnlyMode(bool enabled);
    // Bytes per second for everything we send; 0 is unlimited. Applies at
    // once, whether or not the engine is running.
    int sendLimit() const { return m_sendLimit; }
    void setSendLimit(int bytesPerSecond);

    // Global Health. Monitoring needs a running engine; switching it off
    // pauses the scan, stopping the engine discards what it found.
    bool monitoring() const { return m_monitoring; }
    void setMonitoring(bool on);
    int catalogCap() const { return m_catalogCap; }
    void setCatalogCap(int cap);
    CrawlStatus crawl() const { return m_crawl; }
    // Which family the statistics describe: "all", "ipv4" or "ipv6".
    QString statsFamily() const { return m_statsFamily; }
    void setStatsFamily(const QString &family);
    // Ready to display: see buildNetworkStats() for the shape.
    QVariantMap networkStats() const { return m_networkStats; }
    QVariantList sizeEstimates() const { return m_sizeEstimates; }
    QVariantList lookupPerformance() const { return m_lookupPerformance; }
    QVariantMap history() const { return m_history; }
    // The precise count: progress, per-slice results and totals.
    QVariantMap census() const { return m_census; }
    Q_INVOKABLE void startCensus();
    Q_INVOKABLE void cancelCensus();

    // The node list. A query is a map of filters: family ("any", "ipv4",
    // "ipv6"), states (list of state names), client, version, bep42
    // ("any" or a status name), minRtt and maxRtt (ms, -1 for none),
    // address (address or subnet), port (0 for any), minNodes, sort
    // ("address", "rtt", "lastAnswered", "firstSeen", "client",
    // "nodesAtAddress"), descending, offset and limit.
    CatalogListModel *nodeList() const { return m_nodeList; }
    QVariantMap nodeListInfo() const { return m_nodeListInfo; }
    Q_INVOKABLE void setNodeQuery(const QVariantMap &query);
    Q_INVOKABLE void refreshNodeList();
    Q_INVOKABLE void showNodePage(int offset);
    // Empty when acceptable, otherwise why not.
    Q_INVOKABLE QString validateAddressFilter(const QString &text) const;
    Q_INVOKABLE QString validateIdPrefixFilter(const QString &text) const;
    QVariantMap suspicious() const { return m_suspicious; }
    QVariantMap inbound() const { return m_inbound; }
    // Client names seen among answering addresses, most common first, and
    // the versions seen for one of them.
    QStringList clientNames() const;
    Q_INVOKABLE QStringList versionsFor(const QString &client) const;

    // Writing results out, only when asked: every node matching the current
    // filter as CSV, or the statistics and counts as JSON.
    QVariantMap exportStatus() const { return m_exportStatus; }
    Q_INVOKABLE void exportNodes(const QUrl &file);
    Q_INVOKABLE void exportSummary(const QUrl &file);

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
    StoredInfohashModel *storedInfohashes() const { return m_storedInfohashes; }
    StoredPeerModel *storedPeers() const { return m_storedPeers; }
    StoredItemModel *storedItems() const { return m_storedItems; }
    DataStoreSummary dataStore() const { return m_dataStore; }
    QString selectedInfohash() const { return m_selectedInfohash; }

    bool searchBusy() const { return m_searchBusy; }
    QString searchStatus() const { return m_searchStatus; }
    PeerResultModel *peerResults() const { return m_peerResults; }
    PeerSearchStatus peerSearch() const { return m_peerSearch; }
    ItemSearchStatus itemSearch() const { return m_itemSearch; }
    bool publishBusy() const { return m_publishBusy; }
    // The address the Probe tab is aimed at. Held here rather than in the
    // page so that a node address anywhere in the UI can load it.
    QString probeAddress() const { return m_probeAddress; }
    void setProbeAddress(const QString &address);
    ProbeHistoryModel *probeHistory() const { return m_probeHistory; }
    ProbeStatus probe() const;
    int selectedProbe() const { return m_selectedProbe; }
    bool probeBusy() const { return m_probeBusy; }
    PublishStatus publishStatus() const { return m_publishStatus; }

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

    // The Data Store page asks for this while it is visible.
    Q_INVOKABLE void refreshDataStore();
    Q_INVOKABLE void selectInfohash(const QString &infohash);

    // Search tab. Empty string from validateHash() means the text is usable.
    Q_INVOKABLE QString validateHash(const QString &text) const;
    // A fresh 160-bit hash, so a search target is genuinely random
    // rather than a typed pattern others may have announced to.
    Q_INVOKABLE QString randomHash() const;
    Q_INVOKABLE void searchPeers(const QString &hash);
    Q_INVOKABLE void searchItem(const QString &hash, const QString &salt);
    Q_INVOKABLE void announcePeer(const QString &hash, int port, bool impliedPort);
    Q_INVOKABLE void publishImmutable(const QString &text);
    Q_INVOKABLE void publishMutable(const QString &publicKeyHex, const QString &secretKeyHex,
                                    const QString &salt, int sequence, const QString &text);
    Q_INVOKABLE QVariantMap generateKeyPair() const;
    Q_INVOKABLE QString immutableTargetFor(const QString &text) const;
    Q_INVOKABLE QString mutableTargetFor(const QString &publicKeyHex, const QString &salt) const;
    // For the copy buttons beside hashes: Qt Quick has no clipboard of its
    // own.
    Q_INVOKABLE void copyToClipboard(const QString &text) const;

    // Probe tab: one query to one node, whatever the method.
    Q_INVOKABLE void probeNode(const QString &address, const QString &method, const QString &hashHex);
    Q_INVOKABLE void probeAnnounce(const QString &address, const QString &infohashHex, int port, bool impliedPort);
    // Aims the Probe tab at an address and asks the UI to show that tab.
    Q_INVOKABLE void openProbe(const QString &address);
    Q_INVOKABLE void selectProbe(int row);
    Q_INVOKABLE void clearProbes();

signals:
    void runningChanged();
    void statusChanged();
    void portChanged();
    void ipv6EnabledChanged();
    void portForwardingChanged();
    void historyChanged();
    void bep42EnabledChanged();
    void readOnlyModeChanged();
    void sendLimitChanged();
    void monitoringChanged();
    void catalogCapChanged();
    void statsFamilyChanged();
    void networkStatsChanged();
    void nodeListChanged();
    void exportStatusChanged();
    void nodeIdV4Changed();
    void nodeIdV6Changed();
    void snapshotChanged();
    void dataStoreChanged();
    void searchChanged();
    void publishChanged();
    void probeChanged();
    void probeAddressChanged();
    void probeRequested();
    void noticeChanged();

private:
    void startEngine();
    void stopEngine();
    void destroyEngine();
    void applySnapshot(const dht::EngineSnapshot &snapshot);
    void applyStorageSnapshot(const dht::StorageSnapshot &snapshot);
    void clearDataStore();
    void clearSearch();
    void applyPeerSearch(const dht::PeerSearchResult &result);
    void applyItemSearch(const dht::ItemSearchResult &result);
    void applyPublish(const dht::PublishResult &result);
    void applyProbe(const dht::ProbeResult &result);
    void probeEndpoint(const dht::Endpoint &endpoint, const QString &method, const QString &hashHex,
                       int announcePort, bool impliedPort, bool isAnnounce);
    void probeFailedLocally(const QString &method, const QString &where, const QString &message);
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
    bool m_readOnlyMode = false;
    int m_sendLimit = 0;
    bool m_monitoring = false;
    int m_catalogCap = dht::NodeCatalog::DefaultCap;
    CrawlStatus m_crawl;
    QString m_statsFamily = QStringLiteral("all");
    std::shared_ptr<const dht::NetworkStatsSet> m_statsSet;
    QVariantMap m_networkStats;
    QVariantList m_sizeEstimates;
    QVariantList m_lookupPerformance;
    QVariantMap m_history;
    std::shared_ptr<const std::vector<dht::HistorySample>> m_historySource;
    QVariantMap m_census;
    void buildNetworkStats();

    CatalogListModel *m_nodeList;
    dht::NodeQuery m_nodeQuery;
    QVariantMap m_nodeListInfo;
    QVariantMap m_suspicious;
    QVariantMap m_inbound;
    quint64 m_nodeRequestId = 0;
    bool m_nodeQueryInFlight = false;
    bool m_nodeQueryAgain = false;
    void sendNodeQuery();
    void applyNodePage(const dht::NodeListPage &page);
    void resetNodeList();

    QVariantMap m_exportStatus;
    quint64 m_exportRequestId = 0;
    QString m_exportPath;
    std::thread m_exportThread;
    void setExportStatus(bool busy, const QString &message, bool isError);
    void writeExport(std::shared_ptr<dht::NodeExport> nodes);

    // Recent byte totals, for the transfer rates.
    struct TrafficSample
    {
        qint64 atMs;
        qint64 bytesIn;
        qint64 bytesOut;
        qint64 crawlQueries;
        qint64 crawlAnswers;
    };
    std::deque<TrafficSample> m_traffic;
    QElapsedTimer m_trafficClock;
    QString m_nodeIdV4;
    QString m_nodeIdV6;

    FamilyStatus m_ipv4;
    FamilyStatus m_ipv6;
    PortMappingStatus m_portMapping;
    EngineStatistics m_stats;
    NodeListModel *m_nodes;
    StoredInfohashModel *m_storedInfohashes;
    StoredPeerModel *m_storedPeers;
    StoredItemModel *m_storedItems;
    DataStoreSummary m_dataStore;
    QString m_selectedInfohash;

    bool m_searchBusy = false;
    QString m_searchStatus;
    PeerResultModel *m_peerResults;
    PeerSearchStatus m_peerSearch;
    ItemSearchStatus m_itemSearch;
    bool m_publishBusy = false;
    PublishStatus m_publishStatus;

    QString m_probeAddress;
    ProbeHistoryModel *m_probeHistory;
    int m_selectedProbe = -1;
    bool m_probeBusy = false;

    QString m_notice;
    bool m_noticeIsError = false;
};
