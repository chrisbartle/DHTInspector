#include "CatalogModels.h"

CatalogListModel::CatalogListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CatalogListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QString CatalogListModel::ago(qint64 ms)
{
    if (ms < 0)
        return tr("never");
    const qint64 s = ms / 1000;
    if (s < 60)
        return tr("%1 s ago").arg(s);
    if (s < 3600)
        return tr("%1 min ago").arg(s / 60);
    return tr("%1 h %2 min ago").arg(s / 3600).arg((s % 3600) / 60);
}

namespace {

// What the feature checks found: terse for the table, spelled out for
// its tooltip. Only features a node has are listed in the terse form.
QString featureSummary(const dht::NodeListRow &row, bool detailed)
{
    using F = dht::CatalogEntry;
    struct Check
    {
        F::Flag tested;
        F::Flag has;
        const char *shortName;
        const char *longName;
    };
    static constexpr Check checks[] = {
        {F::Tested51, F::Has51, "51", "BEP 51 sample_infohashes"},
        {F::Tested44, F::Has44, "44", "BEP 44 get"},
        {F::Tested32, F::Has32, "32", "BEP 32 lists the other family"},
        {F::TestedIp, F::SendsIp, "ip", "BEP 42 ip field"},
    };
    QStringList parts;
    for (const Check &c : checks) {
        const QString answer = dht::featureAnswer(row.flags, c.tested, c.has);
        if (detailed)
            parts << QStringLiteral("%1: %2").arg(QLatin1String(c.longName),
                                                  answer.isEmpty() ? QStringLiteral("not checked") : answer);
        else if (answer == QLatin1String("yes"))
            parts << QLatin1String(c.shortName);
    }
    const QString unknown = dht::unknownQueryAnswer(row.flags);
    if (detailed) {
        parts << QStringLiteral("Unknown query: %1").arg(unknown.isEmpty() ? QStringLiteral("not checked") : unknown);
        if (row.selfListSharePercent >= 0) {
            parts << QStringLiteral("Lists its own subnet: %1%").arg(row.selfListSharePercent);
            parts << QStringLiteral("Lists unreachable addresses: %1")
                         .arg((row.flags & F::ListsBogons) ? QStringLiteral("yes") : QStringLiteral("no"));
        }
        if (row.flags & F::Has51)
            parts << QStringLiteral("Stored infohashes (BEP 51 num): %1").arg(row.bep51Samples);
        return parts.join(QLatin1Char('\n'));
    }
    if (unknown == QLatin1String("204"))
        parts << QStringLiteral("204");
    return parts.join(QLatin1Char(' '));
}

} // namespace

QVariant CatalogListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const dht::NodeListRow &row = m_rows[index.row()];

    switch (role) {
    case AddressRole: return row.endpoint.toString();
    case FamilyRole: return dht::familyName(row.endpoint.family());
    case StateRole: return dht::stateName(row.state);
    case AnsweredRole: return row.answered;
    case ClientRole: return row.client;
    case VersionRole: return row.version;
    case ClientKindRole: return row.clientKind;
    case RawVersionRole: return QString::fromLatin1(row.rawVersion.toHex(' '));
    case RttRole: return row.rttMs;
    case Bep42Role: return row.answered ? dht::bep42::statusName(row.bep42) : QString();
    case NodeIdRole: return row.id.toHex();
    case NodeIdShortRole: return row.id.toHex().left(12) + QChar(0x2026);
    case NodesAtAddressRole: return row.nodesAtAddress;
    case FailuresRole: return row.failures;
    case SightingsRole: return row.sightings;
    case FirstSeenRole: return ago(row.firstSeenAgoMs);
    case LastAnsweredRole: return ago(row.lastAnsweredAgoMs);
    case LastQueriedRole: return ago(row.lastQueriedAgoMs);
    case FeaturesRole: return featureSummary(row, false);
    case FeatureDetailRole: return featureSummary(row, true);
    case SuspicionRole: return dht::signalNames(row.suspicion);
    case ProblemRole:
        return row.problem == dht::AddressProblem::None ? QString() : dht::addressProblemName(row.problem);
    }
    return {};
}

QHash<int, QByteArray> CatalogListModel::roleNames() const
{
    return {
        {AddressRole, "address"},
        {FamilyRole, "family"},
        {StateRole, "state"},
        {AnsweredRole, "answered"},
        {ClientRole, "client"},
        {VersionRole, "version"},
        {ClientKindRole, "clientKind"},
        {RawVersionRole, "rawVersion"},
        {RttRole, "rtt"},
        {Bep42Role, "bep42"},
        {NodeIdRole, "nodeId"},
        {NodeIdShortRole, "nodeIdShort"},
        {NodesAtAddressRole, "nodesAtAddress"},
        {FailuresRole, "failures"},
        {SightingsRole, "sightings"},
        {FirstSeenRole, "firstSeen"},
        {LastAnsweredRole, "lastAnswered"},
        {LastQueriedRole, "lastQueried"},
        {FeaturesRole, "features"},
        {FeatureDetailRole, "featureDetail"},
        {SuspicionRole, "suspicion"},
        {ProblemRole, "problem"},
    };
}

void CatalogListModel::setRows(std::vector<dht::NodeListRow> rows)
{
    const bool countChanges = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (countChanges)
        emit countChanged();
}

void CatalogListModel::clear()
{
    setRows({});
}
