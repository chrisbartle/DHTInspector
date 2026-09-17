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
| DHT engine (`dhtcore`) | Working: BEP 5, BEP 32, BEP 42, BEP 43, BEP 44, BEP 51, peer storage |
| Port forwarding | Working against test gateways: PCP, then NAT-PMP, then UPnP IGD |
| Setup tab | Working |
| Search tab | Working: peer and item lookups, announce, BEP 44 publishing |
| Data Store tab | Working: announced peers with addresses and expiry |
| Global Health tab | Working: network scan, address counts, quick and precise size, statistics, feature checks, queries to us, suspicious groups, churn, lookup performance, charts over time, filterable node list with export |
| Probe Node tab | Working: one node, any query, replies decoded in full |

### What the engine does

- Runs one node per enabled address family (BEP 32), each with its own UDP
  socket, routing table and node ID. IPv6 is off by default.
- Never joins on its own. Nodes are added by hand, or by contacting the
  well-known bootstrap routers. Routers are used to join but never enter the
  routing table.
- Answers `ping`, `find_node`, `get_peers` and `announce_peer`, honours BEP 32
  `want`, and stores announced peers for 30 minutes. Answers BEP 51
  `sample_infohashes` with up to 20 random stored infohashes and a six-hour
  interval.
- Establishes its own external address from what a clear majority of other
  nodes report. With BEP 42 on (the default) it then switches to a compliant ID
  for that address and includes `ip` in every response; local-network addresses
  are exempt. With BEP 42 off, each node keeps its random ID and sends no `ip`.
- Only adds a node to the routing table after it answers one of our queries,
  keeps at most one node per public IP, and rate-limits queries per source.
- Never floods a node: every query we send, whatever asked for it, is held
  to two a second per IP address with a burst of two (at most 4 in any
  second, 22 in any ten; libtorrent bans addresses averaging over 5 a second
  across 10 seconds). Extra queries wait their turn, and their timeout only
  starts once sent. More than 64 waiting for one address are refused, and a
  refused query is not held against the node.
- Optional overall send limit (Setup tab, adjustable while running, off by
  default): a byte budget shared by both address families, counting UDP
  payload. Over it, our own queries wait their turn, taken host by host so
  none is starved, and incoming queries are dropped unanswered before they
  change anything, as libtorrent does. Current send and receive rates are on
  the status bar.
- Network scan (Global Health tab, Monitoring switch, off by default): asks
  every node it hears of for its neighbours, gives a silent node a second
  try, then keeps rechecking what it knows, longest-unchecked first. It runs
  as fast as the limits above and this machine allow, backing off when the
  engine falls behind or the OS refuses datagrams (a refused send is never
  recorded as a silent node), and never lets more than two of its queries
  wait behind one IP address. Nodes are kept in a compact catalogue, 80
  bytes each, capped at 2 million by default (adjustable while running);
  when full, a sampled choice of the longest-silent nodes makes way.
  Switching monitoring off pauses the scan; stopping the engine discards it.
- Everything on Global Health is counted by IP address, never by node ID:
  IDs can be changed at will and one address can run many nodes. Headline
  counts are addresses heard about, addresses that have answered
  (connected), and addresses answering now, alongside per-node detail.
- Network statistics over the answering addresses, for IPv4, IPv6 or both:
  clients by name and by version (decoded as on the Probe tab), BEP 42
  compliance, round-trip median/90th/99th percentile with a histogram, and
  port distribution. An address running several nodes counts once, split
  evenly between them. Recomputed every few seconds, less often if a pass
  gets expensive.
- Quick size estimate: random-ID lookups run alongside the scan, and the
  spread of the eight closest nodes estimates the number of nodes; divided
  by the nodes per answering address seen so far, it gives a rough count
  of addresses. It is a rough figure that rests on assumptions about how
  node IDs are spread and how complete lookups are; on the live network it
  has come out well above the precise count, which takes precedence.
- Precise count (a button, with or without monitoring): takes random
  slices of the ID space, a few thousand nodes each, and keeps asking the
  nodes inside with different targets until two rounds turn up nothing
  new. Addresses heard about and addresses that answered are scaled up by
  the slice's share of the ID space, each weighted by its chance of landing
  in a slice given how many nodes it runs (Horvitz-Thompson), so busy hosts
  are not multiplied. Eight slices per family give a mean and a 95%
  interval.
- Feature checks: every answering node is sent, one at a time and within
  the same per-host limit, `sample_infohashes` (BEP 51), `get` (BEP 44) and a
  query no DHT defines, which BEP 5 says should get error 204. The first two
  ask for both families' nodes, which doubles as a BEP 32 check, and every
  reply shows whether the node sends `ip` (BEP 42). A check that goes
  unanswered twice counts as "no". The scan keeps three quarters of each
  batch; the checks get the rest and anything the scan leaves.
- Bad addresses: listed addresses that cannot be contacted are never
  queried, and are counted by reason: port 0, unspecified, private or local,
  multicast or broadcast, and reserved (documentation, benchmarking, shared
  address space, 240/4, IPv6 outside 2000::/3). Nodes that list any are
  counted too.
- Queries to us: every query this node receives is counted by source
  address (up to 500,000 addresses), with the client each sent, the
  methods used, and how many addresses flagged themselves read-only
  (BEP 43). Read-only nodes never answer and are never listed, so this is
  the only place they appear.
- Suspicious groups, over answering nodes, as leads rather than verdicts:
  addresses running 5 or more nodes; /24 or /48 subnets with 8 or more
  answering addresses; one node ID answering from several addresses; ranges
  of the ID space holding more IDs than chance allows (Poisson, adjusted for
  the number of ranges, at four depths); and nodes whose listed neighbours
  are mostly in their own subnet. Addresses showing two or more signals are
  listed first. Every group has a link that filters the node list to it.
- Churn, by address (up while any node there answers): addresses that
  stopped answering, came back, or newly appeared in the last hour; the
  share still answering after 1, 6 and 24 hours, shown once the scan has
  run that long; and how long answering addresses have been up and how
  long finished spells lasted. Departures are seen when a node is
  rechecked, so they lag by up to ten minutes.
- Lookup performance: time (median and 90th percentile), queries, hops to
  the closest node, share of queries answered and share of lookups that
  found all eight closest nodes, over the latest 400 random-target lookups
  per family.
- Over time: while monitoring, a sample every 30 seconds of address counts,
  the quick estimate, client shares, feature shares, suspicious-group
  counts, churn, query rates, round trip and lookup time, drawn as charts
  with a hover readout and a table view. The newest 360 samples are kept as
  taken and older ones are merged pairwise, so a long session stays within
  720 samples. Pauses show as gaps. The history is included in the JSON
  export and discarded when the engine stops.
- Node list: every node found, one row per address and port, filtered by
  family, status, client, version, BEP 42, round trip, address or subnet,
  node ID prefix, port, nodes per address, feature check results and
  suspicious signals, sorted and paged by the engine so it stays quick with
  millions of entries. Rows show features found and any signals, or why an
  address was not contacted. Each address opens the node on the Probe tab.
- Export, only when asked: every node matching the filters as CSV (written
  on a background thread, with feature results and signals), or the
  statistics, size estimates, precise count, queries to us, suspicious
  groups, lookup performance and the session history as JSON. Nothing is saved otherwise.
- BEP 43 read-only mode, off by default and switchable while running: every
  query we send carries `ro`, and every query we receive is dropped without a
  reply, so the store gains nothing while it is on. Our own lookups still work.
- Port forwarding (Setup tab, off by default): asks the IPv4 gateway to
  forward the UDP port with PCP, then NAT-PMP, then UPnP IGD (SSDP search,
  device description, `AddPortMapping` on the WAN connection service, with
  a new port on a conflict and a permanent mapping where leases are not
  supported). Mappings are renewed while the engine runs and removed when it
  stops. When every method fails, the Setup tab shows why for each.
- Node IDs can be given explicitly; otherwise each node picks a random one.
- Stopping the engine destroys it: routing tables, stored peers and tokens are
  all discarded. The Setup tab keeps the node IDs that were in use, so a
  restart reuses them unless edited or randomised.
- Identifies itself with client version `DI` + two version bytes.

- BEP 44: stores immutable and mutable items, with Ed25519 signature checks,
  sequence numbers and compare-and-swap. Items expire after two hours.

Not yet implemented: BEP 33 (scrape).

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
  PortMapper         PCP (RFC 6887), then NAT-PMP (RFC 6886), then UPnP IGD
  Upnp               SSDP search, device description, SOAP port mapping
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
