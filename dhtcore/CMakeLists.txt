qt_add_library(dhtcore STATIC
    include/dhtcore/Bencode.h       src/Bencode.cpp
    include/dhtcore/Bep42.h         src/Bep42.cpp
    include/dhtcore/Bep44.h         src/Bep44.cpp
    include/dhtcore/ClientVersion.h src/ClientVersion.cpp
    include/dhtcore/Crawler.h       src/Crawler.cpp
    include/dhtcore/Crc32c.h        src/Crc32c.cpp
    include/dhtcore/DhtEngine.h     src/DhtEngine.cpp
    include/dhtcore/DhtNode.h       src/DhtNode.cpp
    include/dhtcore/Endpoint.h      src/Endpoint.cpp
    include/dhtcore/Gateway.h       src/Gateway.cpp
    include/dhtcore/Krpc.h          src/Krpc.cpp
    include/dhtcore/Lookup.h        src/Lookup.cpp
    include/dhtcore/NodeCatalog.h   src/NodeCatalog.cpp
    include/dhtcore/NodeId.h        src/NodeId.cpp
    include/dhtcore/PortMapper.h    src/PortMapper.cpp
    include/dhtcore/RoutingTable.h  src/RoutingTable.cpp
    include/dhtcore/RpcManager.h    src/RpcManager.cpp
    include/dhtcore/Snapshot.h
    include/dhtcore/Support.h       src/Support.cpp
)

target_include_directories(dhtcore PUBLIC include)

# The protocol engine never touches Gui or Qml; keep it that way.
target_link_libraries(dhtcore PUBLIC
    Qt6::Core
    Qt6::Network
)

# Ed25519 for BEP 44 mutable items.
target_link_libraries(dhtcore PRIVATE monocypher)

if(WIN32)
    target_link_libraries(dhtcore PRIVATE iphlpapi ws2_32)
endif()
