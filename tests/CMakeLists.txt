find_package(Qt6 REQUIRED COMPONENTS Test)

# One executable for every suite: with a static Qt each link is expensive.
qt_add_executable(dhtcore_tests
    main.cpp
    TestBencode.cpp
    TestBep42.cpp
    TestEngine.cpp
    TestKrpc.cpp
    TestPortMapper.cpp
    TestRoutingTable.cpp
    TestSupport.cpp
)

target_link_libraries(dhtcore_tests PRIVATE
    dhtcore
    Qt6::Test
)

add_test(NAME dhtcore_tests COMMAND dhtcore_tests)
