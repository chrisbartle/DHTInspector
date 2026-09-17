#pragma once

#include <QString>

#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

namespace dht {

// --- lookup performance -------------------------------------------------------

// How the scan's random-target lookups went, over the latest ones.
struct LookupPerformance
{
    int samples = 0;
    double medianMs = -1;
    double p90Ms = -1;
    double medianQueries = -1;
    double medianHops = -1;
    double responseRate = -1;  // answers per query, over all of them
    double fullShare = -1;     // lookups that found a full set of closest nodes
};

class LookupTracker
{
public:
    static constexpr int MaxSamples = 400;

    void add(qint64 durationMs, int queried, int responded, int hops, bool full);
    void clear() { m_samples.clear(); }
    LookupPerformance summary() const;

private:
    struct Sample
    {
        qint64 durationMs;
        int queried;
        int responded;
        int hops;
        bool full;
    };
    std::deque<Sample> m_samples;
};

// --- history --------------------------------------------------------------------

// What the history records, one number each per sample. NaN means not
// known at the time.
enum class Metric {
    HeardIps,
    ConnectedIps,
    AnsweringIps,
    SizeEstimate,       // quick estimate, in addresses
    QueriesPerSecond,   // scan queries
    AnswersPerSecond,
    FeatureChecksPerSecond,
    InboundPerSecond,   // queries sent to us
    RttMedianMs,
    Bep42Share,         // of answering addresses
    Bep51Share,         // of those checked
    Bep44Share,
    SendsIpShare,
    Flagged,            // addresses with two or more signals
    ManyNodes,
    DenseSubnets,
    SharedIds,
    DepartedPerHour,    // addresses that stopped answering, over the last hour
    ReturnedPerHour,
    LookupMedianMs,
    LookupMedianQueries,
    LookupResponseRate,
    Count
};
constexpr int MetricCount = int(Metric::Count);

// Machine name of a metric, for export and the page.
const char *metricKey(Metric metric);

struct HistorySample
{
    qint64 atMs = 0;       // engine clock (nowMs()) at the end of the span
    qint64 spanMs = 0;     // time this sample stands for; grows as history is thinned
    bool gapBefore = false; // the scan was paused (or not yet running) just before
    std::array<double, MetricCount> values;
    // Answering-address shares of the most common clients at the time.
    std::vector<std::pair<QString, double>> clientShares;

    HistorySample() { values.fill(std::numeric_limits<double>::quiet_NaN()); }
    double value(Metric m) const { return values[size_t(int(m))]; }
    void set(Metric m, double v) { values[size_t(int(m))] = v; }
};

// A bounded record of the session. New samples are kept as they are; once
// full, the older part is thinned by merging neighbours, so a long session
// keeps its whole span at a coarser grain. Nothing is ever saved.
class History
{
public:
    static constexpr int MaxSamples = 720;
    static constexpr int KeepFullDetail = 360;  // newest samples never merged

    void add(HistorySample sample);
    void clear() { m_samples.clear(); }
    const std::vector<HistorySample> &samples() const { return m_samples; }

    // Two samples as one, weighted by span. Unknown values do not count.
    static HistorySample merge(const HistorySample &older, const HistorySample &newer);

private:
    void thin();

    std::vector<HistorySample> m_samples;
};

} // namespace dht
