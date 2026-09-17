#include "dhtcore/Census.h"

#include <QTest>

#include <cmath>

using namespace dht;

class TestCensus : public QObject
{
    Q_OBJECT

private slots:
    void inclusionProbability();
    void summarisesSlices();
};

void TestCensus::inclusionProbability()
{
    QCOMPARE(sliceInclusionProbability(0, 12), 0.0);
    QCOMPARE(sliceInclusionProbability(1, 0), 1.0);  // the whole ID space
    QCOMPARE(sliceInclusionProbability(7, 0), 1.0);
    QVERIFY(qAbs(sliceInclusionProbability(1, 12) - 1.0 / 4096) < 1e-15);
    QVERIFY(qAbs(sliceInclusionProbability(1, 1) - 0.5) < 1e-15);
    QVERIFY(qAbs(sliceInclusionProbability(2, 1) - 0.75) < 1e-15);
    // An address with as many nodes as there are slices lands in a given
    // slice about 63% of the time, so it is scaled up by about 1.6, not 4096.
    QVERIFY(qAbs(sliceInclusionProbability(4096, 12) - (1 - std::exp(-1.0))) < 1e-4);
    // More nodes, more likely; never above one.
    double previous = 0;
    for (int ids = 1; ids < 100000; ids *= 3) {
        const double p = sliceInclusionProbability(ids, 12);
        QVERIFY(p > previous && p <= 1.0);
        previous = p;
    }
}

void TestCensus::summarisesSlices()
{
    double mean = -1;
    double low = -1;
    double high = -1;

    summariseSlices({}, &mean, &low, &high);
    QCOMPARE(mean, 0.0);

    summariseSlices({10.0}, &mean, &low, &high);
    QCOMPARE(mean, 10.0);
    QCOMPARE(low, 10.0);
    QCOMPARE(high, 10.0);

    // Mean 10, sample standard deviation 2, t(2) = 4.303.
    summariseSlices({8.0, 10.0, 12.0}, &mean, &low, &high);
    const double half = 4.303 * 2 / std::sqrt(3.0);
    QCOMPARE(mean, 10.0);
    QVERIFY(qAbs(low - (10 - half)) < 1e-9);
    QVERIFY(qAbs(high - (10 + half)) < 1e-9);

    // The interval never goes below zero.
    summariseSlices({0.0, 1.0}, &mean, &low, &high);
    QCOMPARE(low, 0.0);
    QVERIFY(high > 1.0);
}

int runTestCensus(int argc, char **argv)
{
    TestCensus test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestCensus.moc"
