#!/usr/bin/env bash
# Tests for the update path, with no device: builds a real package from the
# payload, then drives zeroos-update against a folder standing in for the
# update server. dpkg -i and systemctl are stand-ins (stubs on PATH), so
# nothing on this machine is installed or restarted.
#   image/tests/update-test.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
ARCH=$(dpkg --print-architecture)
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
expect() {   # expect <name> <key> <value>: a line of the status file
    if grep -qx "$2=$3" "$T/run/update.status"; then ok "$1"; else bad "$1 (wanted $2=$3)"; sed 's/^/     /' "$T/run/update.status"; fi
}

# --- the package ---------------------------------------------------------
printf '#!/bin/sh\necho fake zeroshell\n' > "$T/zeroshell"
chmod +x "$T/zeroshell"
build() { ZEROOS_ARCH=$ARCH "$ROOT/image/stage-zeroos/package.sh" "$T/zeroshell" "$T/server-$1" "$1" >/dev/null; }
build 1.0.0; build 1.1.0
DEB=$T/server-1.1.0/zeroos_1.1.0_$ARCH.deb
[ "$(dpkg-deb -f "$DEB" Package)" = zeroos ] && ok "package is called zeroos" || bad "package name"
dpkg-deb -x "$DEB" "$T/x"
for f in usr/bin/zeroshell usr/bin/zeroos-update etc/systemd/system/zeroshell.service \
         etc/systemd/system/zeroos-update.service etc/zeroos/update.conf etc/zeroos-release \
         usr/share/zeroos/overrides/etc/default/gpsd; do
    [ -e "$T/x/$f" ] && ok "package has /$f" || bad "package is missing /$f"
done
# Files other Debian packages own must not be in ours (dpkg would refuse).
for f in etc/default/gpsd etc/default/earlyoom; do
    [ ! -e "$T/x/$f" ] && ok "package leaves /$f to its owner" || bad "package ships /$f"
done
grep -q '^ZeroOS 1.1.0 ' "$T/x/etc/zeroos-release" && ok "release file names the version" || bad "release file"
# Every file the old image stage installed by hand is in the payload.
missing=$(cd "$ROOT/image/stage-zeroos/files" && find . -type f ! -name 'initramfs-*' | while read -r f; do
    grep -q "$(basename "$f")" "$ROOT/image/stage-zeroos/payload.sh" || echo "$f"; done)
[ -z "$missing" ] && ok "every file in files/ is in the payload" || bad "not in payload.sh: $missing"

# --- the updater ---------------------------------------------------------
mkdir -p "$T/bin" "$T/run" "$T/state"
# dpkg: "install" = note the version; everything else is the real dpkg.
cat > "$T/bin/dpkg" <<STUB
#!/bin/sh
case " \$* " in *" -i "*)
    for deb; do :; done
    v=\$(dpkg-deb -f "\$deb" Version)
    echo "ZeroOS \$v (test)" > "$T/release"
    echo "\$v" >> "$T/installed.log"
    exit 0 ;;
esac
exec /usr/bin/dpkg "\$@"
STUB
# systemctl: ZeroShell stays up unless the version in $T/broken is installed.
cat > "$T/bin/systemctl" <<STUB
#!/bin/sh
case "\$*" in
    *is-enabled*|*restart*) exit 0 ;;
    *MainPID*) if grep -qsx "\$(awk '{print \$2}' "$T/release")" "$T/broken"; then date +%N; else echo 4242; fi ;;
    *is-active*) ! grep -qsx "\$(awk '{print \$2}' "$T/release")" "$T/broken" ;;
esac
STUB
cat > "$T/bin/id" <<'STUB'
#!/bin/sh
[ "$1" = -u ] && { echo 0; exit 0; }
exec /usr/bin/id "$@"
STUB
chmod +x "$T/bin"/*
update() {
    PATH="$T/bin:$PATH" ZEROOS_UPDATE_CONF=$T/update.conf ZEROOS_UPDATE_STATE=$T/state ZEROOS_UPDATE_RUN=$T/run \
        ZEROOS_RELEASE=$T/release ZEROOS_UPDATE_SETTLE=0 \
        "$ROOT/image/stage-zeroos/files/update/zeroos-update" "$@" 2>>"$T/log"
}
serve() { echo "UPDATE_URL=file://$T/server-$1" > "$T/update.conf"; }
echo "ZeroOS 1.0.0 (test)" > "$T/release"
cp "$T/server-1.0.0/zeroos_1.0.0_$ARCH.deb" "$T/state/current.deb"

serve 1.0.0; update check
expect "same version: up to date" state up-to-date

serve 1.1.0; update check
expect "newer version: available" state available
expect "newer version: named" latest 1.1.0

echo "UPDATE_URL=file://$T/nowhere" > "$T/update.conf"; update check || true
expect "no server: offline" state offline

serve 1.1.0
cp "$T/server-1.1.0/zeroos-update.manifest" "$T/manifest.good"
sed -i 's/^sha256=.*/sha256=0000000000000000000000000000000000000000000000000000000000000000/' "$T/server-1.1.0/zeroos-update.manifest"
update install || true
expect "wrong checksum: refused" state failed
grep -q '^ZeroOS 1.0.0' "$T/release" && ok "wrong checksum: nothing installed" || bad "wrong checksum installed anyway"
printf 'version=1.1.0; rm -rf /\nfile=../../etc/passwd\nsha256=x\nsize=1\n' > "$T/server-1.1.0/zeroos-update.manifest"
update check || true
expect "odd manifest: refused" state failed
cp "$T/manifest.good" "$T/server-1.1.0/zeroos-update.manifest"

update install
expect "install: done" state done
expect "install: now on 1.1.0" current 1.1.0
cmp -s "$T/state/previous.deb" "$T/server-1.0.0/zeroos_1.0.0_$ARCH.deb" && ok "install: old package kept" || bad "old package not kept"

update rollback
expect "rollback: done" state done
grep -q '^ZeroOS 1.0.0' "$T/release" && ok "rollback: back on 1.0.0" || bad "rollback did not go back"

# A version whose ZeroShell keeps crashing is taken off again.
build 1.2.0; serve 1.2.0; echo 1.2.0 > "$T/broken"
update install || true
expect "crashing version: rolled back" state rolled-back
grep -q '^ZeroOS 1.0.0' "$T/release" && ok "crashing version: old one is back" || bad "crashing version left installed"
rm "$T/broken"

# A package pushed from a PC, and one that is not ours.
update install-file "$T/server-1.2.0/zeroos_1.2.0_$ARCH.deb"
expect "install-file: done" current 1.2.0
mkdir -p "$T/other/DEBIAN"
printf 'Package: other\nVersion: 9\nArchitecture: all\nMaintainer: x\nDescription: x\n' > "$T/other/DEBIAN/control"
dpkg-deb --build "$T/other" "$T/other.deb" >/dev/null
update install-file "$T/other.deb" || true
expect "foreign package: refused" state failed

# --- versions ------------------------------------------------------------
dpkg --compare-versions 0.1.0 gt "0.0.0+30.gcb85d31" && ok "a release is newer than an untagged build" || bad "version order"
dpkg --compare-versions "0.1.0+3.gabc1234" gt 0.1.0 && ok "a build after a release is newer than it" || bad "version order"
v=$("$ROOT/image/version.sh")
[[ $v =~ ^[0-9][0-9A-Za-z.+~-]*$ ]] && ok "version.sh gives a dpkg version ($v)" || bad "version.sh: $v"

[ "$fails" = 0 ] && echo "all passed" || { echo "$fails failed"; echo "--- log"; cat "$T/log"; exit 1; }
