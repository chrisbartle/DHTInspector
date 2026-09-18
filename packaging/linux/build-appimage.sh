#!/usr/bin/env bash
#
# Builds a Linux AppImage of DHT Inspector.
#
# Requires linuxdeploy and linuxdeploy-plugin-qt on PATH, and a Qt 6.5+
# development install that CMake can find (set CMAKE_PREFIX_PATH if it is
# not in a standard location).
#
# The AppImage lands in dist/. Its name carries the version, which comes
# from project() in the top-level CMakeLists.txt unless VERSION is already
# set in the environment.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/build-appimage"
APPDIR="$BUILD/AppDir"
DIST="$ROOT/dist"

rm -rf "$APPDIR"
mkdir -p "$DIST"

cmake -S "$ROOT" -B "$BUILD" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

cmake --build "$BUILD"

DESTDIR="$APPDIR" cmake --install "$BUILD"

# linuxdeploy puts $VERSION in the file name. Take it from the build we just
# configured, so the name cannot drift from what the binary reports.
if [[ -z "${VERSION:-}" ]]; then
    VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt")"
    export VERSION
fi
echo "Packaging version ${VERSION:-unknown}"

# The Qt plugin needs to know where our QML lives so it can resolve imports
# and bundle the right Qt QML modules.
export QML_SOURCES_PATHS="$ROOT/qml"

cd "$DIST"
linuxdeploy \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage

echo "AppImage written to:"
ls -1 "$DIST"/*.AppImage
