find_package(Qt6 REQUIRED COMPONENTS Test)

# One executable for every suite: with a static Qt each link is expensive.
qt_add_executable(dhtcore_tests
    main.cpp
    TestBencode.cpp
    TestBep42.cpp
    TestBep44.cpp
    TestCensus.cpp
    TestClientVersion.cpp
    TestEngine.cpp
    TestHistory.cpp
    TestInbound.cpp
    TestKeyedListModel.cpp
    TestKrpc.cpp
    TestLookup.cpp
    TestNetworkStats.cpp
    TestNodeCatalog.cpp
    TestNodeList.cpp
    TestPortMapper.cpp
    TestRoutingTable.cpp
    TestRpcManager.cpp
    TestSupport.cpp
    TestSybil.cpp
)

target_link_libraries(dhtcore_tests PRIVATE
    dhtcore
    Qt6::Test
)

# KeyedListModel is a header-only template from the application, tested
# here without pulling in the rest of it.
target_include_directories(dhtcore_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_test(NAME dhtcore_tests COMMAND dhtcore_tests)
