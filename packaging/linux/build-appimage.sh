#!/usr/bin/env bash
#
# Builds a Linux AppImage of DHT Diagnostics.
#
# Requires linuxdeploy and linuxdeploy-plugin-qt on PATH, and a Qt 6.5+
# development install that CMake can find (set CMAKE_PREFIX_PATH if it is
# not in a standard location).
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/build-appimage"
APPDIR="$BUILD/AppDir"

rm -rf "$APPDIR"

cmake -S "$ROOT" -B "$BUILD" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

cmake --build "$BUILD"

DESTDIR="$APPDIR" cmake --install "$BUILD"

# The Qt plugin needs to know where our QML lives so it can resolve imports
# and bundle the right Qt QML modules.
export QML_SOURCES_PATHS="$ROOT/qml"

linuxdeploy \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage

echo "AppImage written to $(ls -1 "$PWD"/DHT_Diagnostics*.AppImage 2>/dev/null || echo "$PWD")"
