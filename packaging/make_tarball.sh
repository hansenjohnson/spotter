#!/usr/bin/env bash
# Assembles a tarball in the layout OpenCPN's Options > Plugins > Import
# button expects: metadata.xml at the top level, alongside a partial
# OpenCPN.app/Contents/PlugIns/ tree containing the built dylib.
#
# Usage (run from the spotter_pi project root, after building):
#   ./packaging/make_tarball.sh
#
# Produces: packaging/spotter_pi-2.0.0-darwin-arm64.tar.gz

set -euo pipefail
cd "$(dirname "$0")/.."

DYLIB="build/libspotter_pi.dylib"
if [ ! -f "$DYLIB" ]; then
  echo "error: $DYLIB not found -- build the plugin first (see README.md)" >&2
  exit 1
fi

STAGE="packaging/stage"
NAME="spotter_pi-2.0.0-darwin-arm64"
rm -rf "$STAGE"
mkdir -p "$STAGE/$NAME/OpenCPN.app/Contents/PlugIns"

cp "$DYLIB" "$STAGE/$NAME/OpenCPN.app/Contents/PlugIns/"
cp packaging/metadata.xml "$STAGE/$NAME/metadata.xml"

( cd "$STAGE" && tar czf "../${NAME}.tar.gz" "$NAME" )
rm -rf "$STAGE"

echo "Wrote packaging/${NAME}.tar.gz"
echo "In OpenCPN: Options > Plugins > Import, and select that file."
