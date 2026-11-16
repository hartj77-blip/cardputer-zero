#!/usr/bin/env bash
# Build a flashable ZeroOS image for the Cardputer Zero.
#
# We don't fork M5Stack's image builder. We pin their open pi-gen fork (which
# already pins the tested 6.18.39 kernel), tweak it, and add our own stage:
#   - keep M5's kernel pin and board driver build (stage2/05-cardputerzero/01-run.sh)
#   - pin the m5stack-linux-dtoverlays commit instead of building "latest main"
#   - drop M5's launcher and demo apps (stage2/05-cardputerzero/02..04)
#   - stop after stage2 (no desktop: 512 MB RAM)
#   - add stage2/06-zeroos: ZeroShell, services, and fixes from early owner reports
#
# Usage: image/build.sh [path/to/zeroshell-arm64]
# Env:   WIFI_COUNTRY (default US), FIRST_USER_PASS (default raspberry), WORK (build dir),
#        PIGEN_NATIVE=1 to run pi-gen directly with sudo instead of in Docker
# Needs: Docker (or root for native), ~20 GB disk, 1-2 hours. arm64 host is fastest.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
WORK=${WORK:-$ROOT/build/image}

M5_PIGEN_REPO=https://github.com/CardputerZero/pi-gen.git
M5_PIGEN_COMMIT=e3b4e323782a59a426b92356248f5bc09a098be1      # 2026-11-13
DTOVERLAYS_COMMIT=4f7aeee9fc103ebb03d22cac476bb191ecfb321e     # m5stack-linux-dtoverlays main, 2026-11-14

ZEROSHELL_BIN=${1:-$ROOT/build/device/zeroshell}
if [ ! -x "$ZEROSHELL_BIN" ]; then
    echo "error: arm64 zeroshell binary not found at $ZEROSHELL_BIN" >&2
    echo "build it with: cmake -S shell -B build/device -DZS_BACKEND=device && cmake --build build/device" >&2
    exit 1
fi
ZEROSHELL_BIN=$(realpath "$ZEROSHELL_BIN")   # we cd into pi-gen below

mkdir -p "$WORK"
if [ ! -d "$WORK/pi-gen/.git" ]; then
    git clone "$M5_PIGEN_REPO" "$WORK/pi-gen"
fi
cd "$WORK/pi-gen"
git fetch --depth 1 origin "$M5_PIGEN_COMMIT"
git checkout --force "$M5_PIGEN_COMMIT"
git clean -fdx -e work -e deploy

CZ=stage2/05-cardputerzero

# 1. Pin the board-driver commit (M5 builds whatever is on main).
sed -i "s|git clone --depth=1 https://github.com/m5stack/m5stack-linux-dtoverlays.git \\\\|git clone https://github.com/m5stack/m5stack-linux-dtoverlays.git \\\\|" "$CZ/01-run.sh"
sed -i "s|^git -C /tmp/dtoverlays rev-parse HEAD|git -C /tmp/dtoverlays checkout ${DTOVERLAYS_COMMIT}\ngit -C /tmp/dtoverlays rev-parse HEAD|" "$CZ/01-run.sh"
grep -q "checkout ${DTOVERLAYS_COMMIT}" "$CZ/01-run.sh" || { echo "error: could not pin dtoverlays (M5 script changed?)" >&2; exit 1; }

# 2. Drop M5's launcher and demo apps; ZeroShell replaces them.
rm -f "$CZ/02-run.sh" "$CZ/03-run.sh" "$CZ/04-run.sh"

# 3. Lite image: stop after stage2.
touch stage3/SKIP stage4/SKIP stage5/SKIP stage4/SKIP_IMAGES stage5/SKIP_IMAGES
rm -f stage4/EXPORT_IMAGE stage5/EXPORT_IMAGE

# 4. Our stage.
rm -rf stage2/06-zeroos
cp -r "$HERE/stage-zeroos" stage2/06-zeroos
install -m 0755 "$ZEROSHELL_BIN" stage2/06-zeroos/files/zeroshell
ZEROOS_VERSION=${ZEROOS_VERSION:-$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo dev)}
sed -i -e "s|@WIFI_COUNTRY@|${WIFI_COUNTRY:-US}|g" -e "s|@ZEROOS_VERSION@|${ZEROOS_VERSION}|g" stage2/06-zeroos/01-run.sh

IMG_DATE=$(date +%Y-%m-%d)
cat > config <<CONFIG
IMG_NAME=zeroos
IMG_FILENAME=${IMG_DATE}-zeroos-cardputerzero-arm64
ARCHIVE_FILENAME=${IMG_DATE}-zeroos-cardputerzero-arm64
RELEASE=trixie
DEPLOY_COMPRESSION=xz
TARGET_HOSTNAME=cardputer
FIRST_USER_NAME=pi
FIRST_USER_PASS=${FIRST_USER_PASS:-raspberry}
DISABLE_FIRST_BOOT_USER_RENAME=1
ENABLE_SSH=1
LOCALE_DEFAULT=en_US.UTF-8
KEYBOARD_KEYMAP=us
KEYBOARD_LAYOUT="English (US)"
TIMEZONE_DEFAULT=UTC
WPA_COUNTRY=${WIFI_COUNTRY:-US}
STAGE_LIST="stage0 stage1 stage2"
CONFIG

# Native build (needs root, qemu-user-static and binfmt on non-arm64 hosts) or Docker.
if [ "${PIGEN_NATIVE:-0}" = 1 ]; then
    sudo XZ_DEFAULTS="-T0" ./build.sh
else
    ./build-docker.sh
fi
echo "Image written to $WORK/pi-gen/deploy/"
