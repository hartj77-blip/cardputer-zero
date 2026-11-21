#!/usr/bin/env bash
# Pack the ZeroOS payload (payload.sh) into zeroos_<version>_<arch>.deb and
# write zeroos-update.manifest beside it. Called by image/package.sh and by
# the image build (01-run.sh), which installs the same package.
#
# Usage: package.sh <zeroshell-binary> <out-dir> <version>
# Env:   ZEROOS_ARCH (default arm64)
set -euo pipefail
umask 022
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$1 OUT=$2 VERSION=$3
ARCH=${ZEROOS_ARCH:-arm64}

case "$VERSION" in
    [0-9]*) ;;
    *) echo "error: version '$VERSION' must start with a digit (see image/version.sh)" >&2; exit 1 ;;
esac
[ -f "$BIN" ] || { echo "error: zeroshell binary not found at $BIN" >&2; exit 1; }

STAGE=$(mktemp -d)
chmod 0755 "$STAGE"   # becomes "/" in the package; mktemp makes it 0700
trap 'rm -rf "$STAGE"' EXIT
"$HERE/payload.sh" "$HERE/files" "$STAGE" "$VERSION" "$BIN"

mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: zeroos
Version: $VERSION
Architecture: $ARCH
Maintainer: ZeroOS <noreply@github.com>
Section: misc
Priority: optional
Installed-Size: $(du -sk --exclude=DEBIAN "$STAGE" | cut -f1)
Homepage: https://github.com/hartj77-blip/cardputer-zero
Description: ZeroOS for the Cardputer Zero
 ZeroShell (the home screen and apps), its services and system settings.
 Installed by the image and by zeroos-update.
CONTROL
# Files that another package also ships (/etc/default/gpsd, earlyoom) can't be
# in ours; they travel under /usr/share/zeroos/overrides and are copied here.
cat > "$STAGE/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
cd /usr/share/zeroos/overrides
find . -type f | while read -r f; do install -D -m 0644 "$f" "/$f"; done
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || true
    udevadm control --reload || true
fi
POSTINST
chmod 0755 "$STAGE/DEBIAN/postinst"
# The one file people edit: an update leaves their copy alone.
echo /etc/zeroos/update.conf > "$STAGE/DEBIAN/conffiles"

mkdir -p "$OUT"
DEB="zeroos_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT/$DEB" >/dev/null
{
    echo "version=$VERSION"
    echo "file=$DEB"
    echo "sha256=$(sha256sum "$OUT/$DEB" | cut -d' ' -f1)"
    echo "size=$(stat -c %s "$OUT/$DEB")"
} > "$OUT/zeroos-update.manifest"
echo "$OUT/$DEB"
