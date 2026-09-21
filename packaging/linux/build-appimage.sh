#!/usr/bin/env bash
#
# Builds a Linux AppImage of DHT Inspector.
#
# Requires linuxdeploy and linuxdeploy-plugin-qt on PATH, and a Qt 6.5+
# development install that CMake can find (set CMAKE_PREFIX_PATH if it is
# not in a standard location).
#
# The AppImage lands in dist/ as DHTInspector-x86_64.AppImage. The name
# carries no version, so a link to the latest release's download never
# changes; the version goes into the AppImage's own desktop entry instead,
# from project() in the top-level CMakeLists.txt unless VERSION is set in
# the environment.
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

# Take the version from the build we just configured, so what the AppImage
# records cannot drift from what the binary reports.
if [[ -z "${VERSION:-}" ]]; then
    VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt")"
fi
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
export LDAI_OUTPUT="DHTInspector-x86_64.AppImage"
echo "Packaging version ${VERSION:-unknown}"

# The Qt plugin needs to know where our QML lives so it can resolve imports
# and bundle the right Qt QML modules.
export QML_SOURCES_PATHS="$ROOT/qml"

cd "$DIST"
linuxdeploy \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage

# An older plugin that ignored LDAI_OUTPUT would name the file itself, and
# the release would then quietly publish the wrong thing.
if [[ ! -f "$DIST/$LDAI_OUTPUT" ]]; then
    echo "expected $DIST/$LDAI_OUTPUT, which linuxdeploy did not produce" >&2
    exit 1
fi
echo "AppImage written to $DIST/$LDAI_OUTPUT"
