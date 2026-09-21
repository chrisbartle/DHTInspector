# DHT Inspector

A diagnostic tool for the mainline BitTorrent DHT. It runs a DHT node of its
own and lets you look at the network two ways: interrogate a single node in
detail, or survey the whole network and measure its health. Built with C++20
and Qt 6, for Windows and Linux.

## Download

Each [release](https://github.com/chrisbartle/DHTInspector/releases) has two
files:

- **Windows:** `DHTInspector-x64.exe`, a single executable with nothing to
  install.
- **Linux:** `DHTInspector-x86_64.AppImage`. Make it executable and run it.

## Using it

1. On the **Setup** tab, switch on **DHT engine**, then press
   **Auto-bootstrap** to join the network through the well-known bootstrap
   routers. IPv6, BEP 42, port forwarding and a send limit are set here too.
2. Then use whichever tab fits the job:
   - **Search** finds peers for an infohash, announces to it, and fetches or
     publishes BEP 44 items.
   - **Probe Node** sends one query to one `address:port` and shows the reply
     decoded in full. Clicking a node's address anywhere else in the app
     opens it here.
   - **Global Health** scans the network once **Monitoring** is switched on,
     and reports its size, clients, supported features, churn, lookup
     performance and suspicious groups, with charts over time and a
     filterable node list that can be exported.
   - **Data Store** shows what other nodes have announced to this one, and
     the BEP 44 items stored with it.

Hashes and addresses throughout have a copy button beside them.

Nothing is saved between runs: every launch starts with the engine off,
default settings and fresh node IDs, and files are only written when you
export something.

## Building

Needs CMake 3.21 or later, Ninja, and Qt 6.5 or later.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows, build from a shell with the MSVC environment loaded. Pointing
`CMAKE_PREFIX_PATH` at a static Qt gives a single self-contained `.exe`.

The tests are built too, as `dhtcore_tests`; add
`-DDHTINSPECTOR_BUILD_TESTS=OFF` to leave them out.

## More

[NOTES.md](NOTES.md) has the details: exactly what the engine and each
measurement do, the source layout, the AppImage and release process, and
notes on the build.
