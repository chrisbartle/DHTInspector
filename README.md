# DHT Inspector

A cross-platform diagnostic tool for the mainline BitTorrent DHT (BEP 5), built
with C++20, Qt 6 and QML.

Two jobs, one tool:

- **Probe Node**: point at a single `address:port` and characterise it:
  reachability, protocol conformance, node ID validity, and which extensions it
  actually speaks.
- **Global Health**: run a node of our own and measure the population:
  size estimation, lookup performance, churn, client mix, ID-space coverage.

## Status

| Area | State |
|---|---|
| DHT engine (`dhtcore`) | Working: BEP 5, BEP 32, BEP 42, BEP 43 handling, peer storage |
| Port forwarding | Working against test gateways: PCP with NAT-PMP fallback |
| Setup tab | Working |
| Search tab | Working: peer and item lookups, announce, BEP 44 publishing |
| Data Store tab | Working: announced peers with addresses and expiry |
| Global Health tab | Layout only |
| Probe Node tab | Layout only |

### What the engine does

- Runs one node per enabled address family (BEP 32), each with its own UDP
  socket, routing table and node ID. IPv6 is off by default.
- Never joins on its own. Nodes are added by hand, or by contacting the
  well-known bootstrap routers. Routers are used to join but never enter the
  routing table.
- Answers `ping`, `find_node`, `get_peers` and `announce_peer`, honours BEP 32
  `want`, and stores announced peers for 30 minutes.
- Establishes its own external address from what a clear majority of other
  nodes report. With BEP 42 on (the default) it then switches to a compliant ID
  for that address and includes `ip` in every response; local-network addresses
  are exempt. With BEP 42 off, each node keeps its random ID and sends no `ip`.
- Only adds a node to the routing table after it answers one of our queries,
  keeps at most one node per public IP, and rate-limits queries per source.
- Node IDs can be given explicitly; otherwise each node picks a random one.
- Stopping the engine destroys it: routing tables, stored peers and tokens are
  all discarded. The Setup tab keeps the node IDs that were in use, so a
  restart reuses them unless edited or randomised.
- Identifies itself with client version `DI` + two version bytes.

- BEP 44: stores immutable and mutable items, with Ed25519 signature checks,
  sequence numbers and compare-and-swap. Items expire after two hours.

Not yet implemented: BEP 33 (scrape), BEP 51 (infohash sampling), UPnP port
mapping.

## Layout

```
third_party/         vendored Monocypher (Ed25519 for BEP 44), see its README
dhtcore/             protocol engine, static library, Qt Core + Network only
  Bencode            observing decoder: reports non-canonical input as warnings
  Krpc               message codec, compact node/peer encoding
  NodeId, Bep42      160-bit IDs, CRC32C, BEP 42 generation and checks
  RoutingTable       k-buckets with splitting, replacement cache, node states
  RpcManager         transactions and timeouts
  Lookup             iterative find_node / get_peers
  DhtNode            one node on one address family
  DhtEngine          nodes + storage + port mapping; the public entry point
  PortMapper         PCP (RFC 6887) with NAT-PMP (RFC 6886) fallback
  Gateway            default gateway discovery (Windows, Linux)
src/                 application: QML-facing controller and node list model
qml/                 QML module "DHTInspector"
tests/               Qt Test suites, including a loopback multi-engine swarm
packaging/linux/     .desktop file, icon, AppImage build script
```

The engine runs on its own thread. `DhtController` owns that thread and talks
to the engine through queued calls; the UI only ever sees copied snapshots.

## Building on Windows (static Qt, MSVC)

From a shell with the MSVC environment loaded (`vcvars64.bat`):

```bash
cmake -S . -B build/win-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/static/6.10.1_MSVC_64
cmake --build build/win-static
```

Produces a self-contained `build/win-static/DHTInspector.exe` with no Qt DLLs to
ship alongside it.

## Building on Linux

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Gateway discovery on Linux reads `/proc/net/route`. That code path has not yet
been compiled or run on Linux.

## Tests

Built by default; turn off with `-DDHTINSPECTOR_BUILD_TESTS=OFF`.

```bash
cmake --build build/win-static --target dhtcore_tests
```

Run `dhtcore_tests` from the build directory. Each suite also writes
`<Suite>.log` there, because on the static Windows build QTest's console
output is lost when stdout is redirected. The engine suite binds loopback
sockets only and never touches the public network.

## AppImage

Requires `linuxdeploy` and `linuxdeploy-plugin-qt` on `PATH`:

```bash
./packaging/linux/build-appimage.sh
```

The script configures with `CMAKE_INSTALL_PREFIX=/usr`, installs into an
`AppDir`, and points the Qt plugin at `qml/` via `QML_SOURCES_PATHS` so it can
resolve imports and bundle the right Qt QML modules.

## Notes

- Qt 6.5 is the declared minimum; 6.10.1 is what this is developed against.
- The Controls style is pinned to **Basic** in `main.cpp`, and everything
  visual comes from `Theme.qml`, so Windows and Linux render identically.
  (A static build links every Controls style; Basic is a choice for
  consistency, not a static-linking constraint.)
- Nothing persists between runs. Every launch starts from default settings,
  fresh random node IDs and the engine off. QML runs from the units compiled
  into the executable, and no `.qmlc` cache files are read or written. The
  `QSettings` persistence code is still in `DhtController.cpp`, commented out.
- Configuring against the static Qt emits a few `QtFeature.cmake` warnings
  about `Qt6::ScxmlGlobalPrivate`. They come from that Qt install's own package
  metadata, not from this project, and are harmless.
