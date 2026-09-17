#include "dhtcore/History.h"

#include <QHash>

#include <algorithm>

namespace dht {

namespace {

double quantile(std::vector<double> values, double q)
{
    if (values.empty())
        return -1;
    std::sort(values.begin(), values.end());
    const size_t index = std::min(values.size() - 1, size_t(std::ceil(q * double(values.size())) - 1));
    return values[q <= 0 ? 0 : index];
}

} // namespace

void LookupTracker::add(qint64 durationMs, int queried, int responded, int hops, bool full)
{
    m_samples.push_back({durationMs, queried, responded, hops, full});
    while (int(m_samples.size()) > MaxSamples)
        m_samples.pop_front();
}

LookupPerformance LookupTracker::summary() const
{
    LookupPerformance p;
    p.samples = int(m_samples.size());
    if (m_samples.empty())
        return p;
    std::vector<double> durations, queries, hops;
    double queried = 0;
    double responded = 0;
    int full = 0;
    for (const Sample &s : m_samples) {
        durations.push_back(double(s.durationMs));
        queries.push_back(s.queried);
        if (s.hops >= 0)
            hops.push_back(s.hops);
        queried += s.queried;
        responded += s.responded;
        full += s.full ? 1 : 0;
    }
    p.medianMs = quantile(durations, 0.5);
    p.p90Ms = quantile(durations, 0.9);
    p.medianQueries = quantile(queries, 0.5);
    p.medianHops = quantile(hops, 0.5);
    p.responseRate = queried > 0 ? responded / queried : -1;
    p.fullShare = double(full) / double(m_samples.size());
    return p;
}

const char *metricKey(Metric metric)
{
    switch (metric) {
    case Metric::HeardIps: return "heardIps";
    case Metric::ConnectedIps: return "connectedIps";
    case Metric::AnsweringIps: return "answeringIps";
    case Metric::SizeEstimate: return "sizeEstimate";
    case Metric::QueriesPerSecond: return "queriesPerSecond";
    case Metric::AnswersPerSecond: return "answersPerSecond";
    case Metric::FeatureChecksPerSecond: return "featureChecksPerSecond";
    case Metric::InboundPerSecond: return "inboundPerSecond";
    case Metric::RttMedianMs: return "rttMedianMs";
    case Metric::Bep42Share: return "bep42Share";
    case Metric::Bep51Share: return "bep51Share";
    case Metric::Bep44Share: return "bep44Share";
    case Metric::SendsIpShare: return "sendsIpShare";
    case Metric::Flagged: return "flagged";
    case Metric::ManyNodes: return "manyNodes";
    case Metric::DenseSubnets: return "denseSubnets";
    case Metric::SharedIds: return "sharedIds";
    case Metric::DepartedPerHour: return "departedPerHour";
    case Metric::ReturnedPerHour: return "returnedPerHour";
    case Metric::LookupMedianMs: return "lookupMedianMs";
    case Metric::LookupMedianQueries: return "lookupMedianQueries";
    case Metric::LookupResponseRate: return "lookupResponseRate";
    case Metric::Count: break;
    }
    return "";
}

HistorySample History::merge(const HistorySample &older, const HistorySample &newer)
{
    HistorySample out;
    out.atMs = newer.atMs;
    out.spanMs = older.spanMs + newer.spanMs;
    out.gapBefore = older.gapBefore;
    const double wa = double(std::max<qint64>(1, older.spanMs));
    const double wb = double(std::max<qint64>(1, newer.spanMs));
    for (size_t i = 0; i < out.values.size(); ++i) {
        const double a = older.values[i];
        const double b = newer.values[i];
        if (std::isnan(a))
            out.values[i] = b;
        else if (std::isnan(b))
            out.values[i] = a;
        else
            out.values[i] = (a * wa + b * wb) / (wa + wb);
    }
    // Clients: a client missing from one side had a share too small to list there.
    QHash<QString, double> shares;
    QList<QString> order;
    for (const auto &[name, share] : older.clientShares) {
        shares[name] += share * wa;
        order << name;
    }
    for (const auto &[name, share] : newer.clientShares) {
        if (!shares.contains(name))
            order << name;
        shares[name] += share * wb;
    }
    for (const QString &name : std::as_const(order))
        out.clientShares.emplace_back(name, shares.value(name) / (wa + wb));
    return out;
}

void History::add(HistorySample sample)
{
    m_samples.push_back(std::move(sample));
    if (int(m_samples.size()) > MaxSamples)
        thin();
}

void History::thin()
{
    // Merge neighbours in the older part, never across a gap.
    const size_t older = m_samples.size() - size_t(KeepFullDetail);
    std::vector<HistorySample> out;
    out.reserve(m_samples.size());
    size_t i = 0;
    while (i < older) {
        if (i + 1 < older && !m_samples[i + 1].gapBefore) {
            out.push_back(merge(m_samples[i], m_samples[i + 1]));
            i += 2;
        } else {
            out.push_back(m_samples[i]);
            ++i;
        }
    }
    for (; i < m_samples.size(); ++i)
        out.push_back(std::move(m_samples[i]));
    // Every span in the older part ended at a gap: drop the oldest instead.
    if (int(out.size()) > MaxSamples)
        out.erase(out.begin(), out.begin() + (out.size() - size_t(MaxSamples)));
    m_samples = std::move(out);
}

} // namespace dht
