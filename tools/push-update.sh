#!/usr/bin/env bash
# Send a ZeroOS package from this PC to a Cardputer and install it there.
# Works over anything SSH works over: Ethernet, Wi-Fi, Tailscale.
#
#   tools/push-update.sh [user@]host [package.deb]
#
#   tools/push-update.sh cardputer.local                 # newest build/update/zeroos_*.deb
#   tools/push-update.sh pi@192.168.1.42 ~/Downloads/zeroos_0.2.0_arm64.deb
#
# Make the package with image/package.sh, or download it from a release or
# from the "zeroos-package" artifact of a CI run. See docs/UPDATE.md.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
HOST=${1:?usage: tools/push-update.sh [user@]host [package.deb]}
case "$HOST" in *@*) ;; *) HOST="pi@$HOST" ;; esac
DEB=${2:-$(ls -t "$ROOT"/build/update/zeroos_*.deb 2>/dev/null | head -1)}
[ -f "$DEB" ] || { echo "error: no package. Build one with image/package.sh, or give its path." >&2; exit 1; }

REMOTE=/tmp/$(basename "$DEB")
echo "Sending $(basename "$DEB") to $HOST"
scp -q "$DEB" "$HOST:$REMOTE"
# A device flashed before updates existed has no zeroos-update yet: install
# the package directly that once; it brings zeroos-update with it.
# shellcheck disable=SC2029
ssh "$HOST" "
    if command -v zeroos-update >/dev/null; then
        sudo zeroos-update install-file '$REMOTE'; rc=\$?
        zeroos-update status
    else
        sudo dpkg -i '$REMOTE' && sudo systemctl restart zeroshell.service; rc=\$?
    fi
    rm -f '$REMOTE'
    exit \$rc"
