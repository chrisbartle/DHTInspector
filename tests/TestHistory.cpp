#include "dhtcore/History.h"

#include <QTest>

#include <cmath>
#include <set>
#include <string>

using namespace dht;

namespace {

HistorySample sample(qint64 atMs, qint64 spanMs, double answering, bool gap = false)
{
    HistorySample s;
    s.atMs = atMs;
    s.spanMs = spanMs;
    s.gapBefore = gap;
    s.set(Metric::AnsweringIps, answering);
    return s;
}

} // namespace

class TestHistory : public QObject
{
    Q_OBJECT

private slots:
    void summarisesLookups();
    void keepsTheLatestLookups();
    void namesEveryMetric();
    void mergesWeightedBySpan();
    void keepsRecentSamplesAndThinsOld();
    void neverMergesAcrossGaps();
};

void TestHistory::summarisesLookups()
{
    LookupTracker tracker;
    QCOMPARE(tracker.summary().samples, 0);
    QCOMPARE(tracker.summary().medianMs, -1.0);

    // Ten lookups: 100..1000 ms, 10..19 queries, answers 90%, hops 0..4
    // except one that never found anyone, and every other one full.
    for (int i = 1; i <= 10; ++i)
        tracker.add(i * 100, 9 + i, i == 10 ? 0 : 9 + i - (i % 2), i == 10 ? -1 : i % 5, i % 2 == 0);
    const LookupPerformance p = tracker.summary();
    QCOMPARE(p.samples, 10);
    QCOMPARE(p.medianMs, 500.0);
    QCOMPARE(p.p90Ms, 900.0);
    QCOMPARE(p.medianQueries, 14.0);
    QCOMPARE(p.medianHops, 2.0);  // of 1,2,3,4,0,1,2,3,4 (the failed one has none)
    double queried = 0;
    double answered = 0;
    for (int i = 1; i <= 10; ++i) {
        queried += 9 + i;
        answered += i == 10 ? 0 : 9 + i - (i % 2);
    }
    QCOMPARE(p.responseRate, answered / queried);
    QCOMPARE(p.fullShare, 0.5);
}

void TestHistory::keepsTheLatestLookups()
{
    LookupTracker tracker;
    for (int i = 0; i < LookupTracker::MaxSamples; ++i)
        tracker.add(10, 1, 1, 1, true);
    for (int i = 0; i < LookupTracker::MaxSamples; ++i)
        tracker.add(5000, 1, 0, 1, false);
    const LookupPerformance p = tracker.summary();
    QCOMPARE(p.samples, LookupTracker::MaxSamples);
    QCOMPARE(p.medianMs, 5000.0);
    QCOMPARE(p.responseRate, 0.0);
    QCOMPARE(p.fullShare, 0.0);
    tracker.clear();
    QCOMPARE(tracker.summary().samples, 0);
}

void TestHistory::namesEveryMetric()
{
    std::set<std::string> keys;
    for (int m = 0; m < MetricCount; ++m) {
        const std::string key = metricKey(Metric(m));
        QVERIFY2(!key.empty(), qPrintable(QString::number(m)));
        keys.insert(key);
    }
    QCOMPARE(int(keys.size()), MetricCount);

    HistorySample s;
    for (int m = 0; m < MetricCount; ++m)
        QVERIFY(std::isnan(s.value(Metric(m))));
}

void TestHistory::mergesWeightedBySpan()
{
    HistorySample a = sample(1000, 1000, 10, true);
    a.set(Metric::RttMedianMs, 100);
    a.clientShares = {{QStringLiteral("libtorrent"), 0.6}, {QStringLiteral("uTorrent"), 0.2}};
    HistorySample b = sample(4000, 3000, 30);
    b.set(Metric::Bep51Share, 0.5);
    b.clientShares = {{QStringLiteral("libtorrent"), 0.4}, {QStringLiteral("Transmission"), 0.1}};

    const HistorySample m = History::merge(a, b);
    QCOMPARE(m.atMs, qint64(4000));
    QCOMPARE(m.spanMs, qint64(4000));
    QVERIFY(m.gapBefore);  // the older one's
    QCOMPARE(m.value(Metric::AnsweringIps), 25.0);        // (10*1 + 30*3) / 4
    QCOMPARE(m.value(Metric::RttMedianMs), 100.0);        // only one side knew
    QCOMPARE(m.value(Metric::Bep51Share), 0.5);
    QVERIFY(std::isnan(m.value(Metric::Flagged)));        // neither knew
    QCOMPARE(int(m.clientShares.size()), 3);
    QCOMPARE(m.clientShares[0].first, QStringLiteral("libtorrent"));
    QCOMPARE(m.clientShares[0].second, 0.45);             // (0.6*1 + 0.4*3) / 4
    QCOMPARE(m.clientShares[1].second, 0.05);             // uTorrent, absent from the newer
    QCOMPARE(m.clientShares[2].first, QStringLiteral("Transmission"));
    QCOMPARE(m.clientShares[2].second, 0.075);
}

void TestHistory::keepsRecentSamplesAndThinsOld()
{
    History history;
    constexpr qint64 Span = 30000;
    constexpr int Total = 5000;
    for (int i = 1; i <= Total; ++i) {
        history.add(sample(i * Span, Span, i, i == 1));
        QVERIFY(int(history.samples().size()) <= History::MaxSamples);
    }
    const auto &samples = history.samples();

    // The newest are untouched, in order, and the whole span is still covered.
    for (int k = 0; k < History::KeepFullDetail; ++k) {
        const HistorySample &s = samples[samples.size() - 1 - size_t(k)];
        QCOMPARE(s.spanMs, Span);
        QCOMPARE(s.value(Metric::AnsweringIps), double(Total - k));
    }
    qint64 covered = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        covered += samples[i].spanMs;
        if (i > 0) {
            QVERIFY(samples[i].atMs > samples[i - 1].atMs);
            QCOMPARE(samples[i].atMs - samples[i].spanMs, samples[i - 1].atMs);  // no holes, no overlaps
        }
    }
    QCOMPARE(covered, Total * Span);
    QVERIFY(samples.front().gapBefore);
    QCOMPARE(samples.back().atMs, Total * Span);
    // Merged samples average what they cover: the value is the midpoint.
    const HistorySample &oldest = samples.front();
    const double first = double(oldest.atMs - oldest.spanMs) / Span + 1;
    const double last = double(oldest.atMs) / Span;
    QCOMPARE(oldest.value(Metric::AnsweringIps), (first + last) / 2);
    QVERIFY(oldest.spanMs > Span);

    history.clear();
    QVERIFY(history.samples().empty());
}

void TestHistory::neverMergesAcrossGaps()
{
    History history;
    // Every sample follows a pause: nothing can be merged, so the oldest go.
    for (int i = 1; i <= History::MaxSamples + 10; ++i)
        history.add(sample(i * 1000, 500, i, true));
    const auto &samples = history.samples();
    QCOMPARE(int(samples.size()), History::MaxSamples);
    QCOMPARE(samples.front().value(Metric::AnsweringIps), 11.0);
    for (const HistorySample &s : samples) {
        QCOMPARE(s.spanMs, qint64(500));
        QVERIFY(s.gapBefore);
    }

    // A gap in the middle of mergeable samples stays a boundary.
    History mixed;
    for (int i = 1; i <= History::MaxSamples + 1; ++i)
        mixed.add(sample(i * 1000, 1000, i, i == 1 || i == 6));
    bool gapKept = false;
    for (const HistorySample &s : mixed.samples()) {
        if (s.atMs - s.spanMs == 5000) {
            QVERIFY(s.gapBefore);
            gapKept = true;
        }
    }
    QVERIFY(gapKept);
}

int runTestHistory(int argc, char **argv)
{
    TestHistory test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestHistory.moc"
