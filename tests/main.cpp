#include <QByteArray>
#include <QCoreApplication>

#include <vector>

int runTestBencode(int argc, char **argv);
int runTestBep42(int argc, char **argv);
int runTestBep44(int argc, char **argv);
int runTestCensus(int argc, char **argv);
int runTestClientVersion(int argc, char **argv);
int runTestEngine(int argc, char **argv);
int runTestKrpc(int argc, char **argv);
int runTestNetworkStats(int argc, char **argv);
int runTestNodeCatalog(int argc, char **argv);
int runTestNodeList(int argc, char **argv);
int runTestPortMapper(int argc, char **argv);
int runTestRoutingTable(int argc, char **argv);
int runTestRpcManager(int argc, char **argv);
int runTestSupport(int argc, char **argv);

namespace {

// With this static Windows build, QTest writes nothing when stdout is
// redirected, and a shared -o file is overwritten by each suite. Every suite
// therefore also logs to <Suite>.log in the working directory.
int runSuite(int (*run)(int, char **), const char *name, int argc, char **argv)
{
    std::vector<QByteArray> storage;
    for (int i = 0; i < argc; ++i)
        storage.emplace_back(argv[i]);
    storage.emplace_back("-o");
    storage.emplace_back(QByteArray(name) + ".log,txt");
    storage.emplace_back("-o");
    storage.emplace_back("-,txt");

    std::vector<char *> args;
    for (QByteArray &arg : storage)
        args.push_back(arg.data());
    return run(int(args.size()), args.data());
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    int failures = 0;
    failures += runSuite(runTestBencode, "TestBencode", argc, argv);
    failures += runSuite(runTestBep42, "TestBep42", argc, argv);
    failures += runSuite(runTestBep44, "TestBep44", argc, argv);
    failures += runSuite(runTestCensus, "TestCensus", argc, argv);
    failures += runSuite(runTestClientVersion, "TestClientVersion", argc, argv);
    failures += runSuite(runTestKrpc, "TestKrpc", argc, argv);
    failures += runSuite(runTestNetworkStats, "TestNetworkStats", argc, argv);
    failures += runSuite(runTestNodeCatalog, "TestNodeCatalog", argc, argv);
    failures += runSuite(runTestNodeList, "TestNodeList", argc, argv);
    failures += runSuite(runTestSupport, "TestSupport", argc, argv);
    failures += runSuite(runTestRoutingTable, "TestRoutingTable", argc, argv);
    failures += runSuite(runTestRpcManager, "TestRpcManager", argc, argv);
    failures += runSuite(runTestPortMapper, "TestPortMapper", argc, argv);
    failures += runSuite(runTestEngine, "TestEngine", argc, argv);
    return failures;
}
