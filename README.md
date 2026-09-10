# DHT Diagnostics

A cross-platform diagnostic tool for the mainline BitTorrent DHT (BEP 5), built
with C++20, Qt 6 and QML.

Two jobs, one tool:

- **Probe Node** — point at a single `address:port` and characterise it:
  reachability, protocol conformance, node ID validity, and which extensions it
  actually speaks.
- **Global Health** — run a node of our own and measure the population:
  size estimation, lookup performance, churn, client mix, ID-space coverage.

## Status

Early. This is UI scaffolding only — three tabs with their intended structure
laid out, and no engine behind any of it. Nothing touches the network yet.

## Layout

```
src/                 application entry point
qml/                 QML module "DhtDiag"
  Theme.qml          singleton: colours, metrics, fonts
  Main.qml           window, tab bar, status bar
  Panel.qml          titled card container
  LabeledField.qml   read-only "label: value" row
  EmptyState.qml     placeholder for a view with no data yet
  *Page.qml          one per tab
packaging/linux/     .desktop file, icon, AppImage build script
```

The protocol engine will land as a separate `dhtcore` static library that links
Qt Core only — no Gui, no Qml — so it stays headless-testable.

## Building on Windows (static Qt, MSVC)

From a shell with the MSVC environment loaded (`vcvars64.bat`):

```bash
cmake -S . -B build/win-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/static/6.10.1_MSVC_64
cmake --build build/win-static
```

Produces a self-contained `build/win-static/DhtDiag.exe` (~30 MB) with no Qt
DLLs to ship alongside it.

## Building on Linux

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

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
- The Controls style is pinned to **Basic** in `main.cpp`. It is the only style
  guaranteed to be linked into a static build, and everything visual comes from
  `Theme.qml` rather than from the platform style — so Windows and Linux render
  identically.
- Configuring against the static Qt emits a few `QtFeature.cmake` warnings
  about `Qt6::ScxmlGlobalPrivate`. They come from that Qt install's own package
  metadata, not from this project, and are harmless.
