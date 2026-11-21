#!/usr/bin/env bash
# Build the ZeroOS update package: the same files the image installs, as one
# .deb, plus the manifest the on-device updater reads (docs/UPDATE.md).
#
# Usage: image/package.sh [path/to/zeroshell-arm64] [out-dir]
# Env:   ZEROOS_VERSION (default: image/version.sh)
# Needs: dpkg-deb (any Debian or Ubuntu; no root).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
BIN=${1:-$ROOT/build/device/zeroshell}
OUT=${2:-$ROOT/build/update}
VERSION=${ZEROOS_VERSION:-$("$HERE/version.sh")}
exec "$HERE/stage-zeroos/package.sh" "$BIN" "$OUT" "$VERSION"
