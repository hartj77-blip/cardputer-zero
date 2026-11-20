#!/usr/bin/env bash
# Lay out every file ZeroOS owns under <dest>. This is what an update can
# change; package.sh packs it into the zeroos .deb. What only the image
# build does (Debian packages, boot settings, the initramfs, the user's
# folders) stays in 01-run.sh.
# Each tweak cites the problem it fixes (see docs/HARDWARE.md, "Known issues").
#
# Usage: payload.sh <files-dir> <dest> <version> <zeroshell-binary>
set -euo pipefail
umask 022
F=$1 D=$2 VERSION=$3 BIN=$4

install -d "$D/usr/bin" "$D/usr/lib/zeroos" "$D/etc/zeroos/dock" "$D/etc/systemd/system" \
           "$D/etc/polkit-1/rules.d" "$D/etc/udev/rules.d" "$D/etc/sysctl.d" "$D/etc/modules-load.d" \
           "$D/etc/rpi/swap.conf.d" "$D/etc/systemd/zram-generator.conf.d" "$D/etc/systemd/journald.conf.d"

install -m 0755 "$BIN"                         "$D/usr/bin/zeroshell"
echo "ZeroOS $VERSION (built $(date -u +%Y-%m-%d))" > "$D/etc/zeroos-release"
install -m 0644 "$F/zeroshell.service"         "$D/etc/systemd/system/zeroshell.service"
install -m 0644 "$F/90-zeroos.rules"           "$D/etc/polkit-1/rules.d/90-zeroos.rules"
install -m 0644 "$F/90-zeroos.udev"            "$D/etc/udev/rules.d/90-zeroos.rules"
install -m 0644 "$F/swap.conf"                 "$D/etc/rpi/swap.conf.d/80-zeroos.conf"
install -m 0644 "$F/zram-zeroos.conf"          "$D/etc/systemd/zram-generator.conf.d/30-zeroos.conf"
install -m 0644 "$F/90-zeroos-memory.conf"     "$D/etc/sysctl.d/90-zeroos-memory.conf"
install -m 0644 "$F/journald.conf"             "$D/etc/systemd/journald.conf.d/90-zeroos.conf"
install -m 0644 "$F/zeroos-cloud-init-once.service" "$D/etc/systemd/system/"
install -m 0755 "$F/zeroos-measure"            "$D/usr/bin/zeroos-measure"
install -m 0755 "$F/zeroos-power-perms"        "$D/usr/lib/zeroos/power-perms"
# Dock mode: labwc desktop on the HDMI monitor (docs/DOCK.md).
install -m 0644 "$F/dock/rc.xml" "$F/dock/menu.xml" "$F/dock/autostart" "$F/dock/environment" \
                "$F/dock/waybar.json" "$F/dock/waybar.css" "$D/etc/zeroos/dock/"
install -m 0755 "$F/dock/dock-session"         "$D/usr/lib/zeroos/dock-session"
install -m 0755 "$F/dock/zeroos-dock" "$F/dock/zeroos-browser" "$F/dock/zeroos-video" "$D/usr/bin/"
install -m 0644 "$F/dock/zeroos-dock.service"  "$D/etc/systemd/system/"
# Remote mode: Terminal app (tmux on tty3), Tailscale helpers (docs/REMOTE.md).
install -m 0644 "$F/remote/zeroos-terminal.service" "$F/remote/zeroos-taildrop.service" \
                                               "$D/etc/systemd/system/"
install -m 0755 "$F/remote/terminal-session"   "$D/usr/lib/zeroos/terminal-session"
install -m 0644 "$F/remote/tmux.conf"          "$D/etc/zeroos/tmux.conf"
install -D -m 0644 "$F/remote/tailscaled-zeroos.conf" \
                   "$D/etc/systemd/system/tailscaled.service.d/zeroos.conf"
# Sync: Syncthing for Notes, Documents and Recordings (docs/SYNC.md).
install -m 0755 "$F/sync/zeroos-sync"          "$D/usr/bin/zeroos-sync"
install -D -m 0644 "$F/sync/syncthing-zeroos.conf" \
                   "$D/etc/systemd/system/syncthing@.service.d/zeroos.conf"
# Car: USB stick mode and Bluetooth audio (docs/CAR.md).
install -m 0755 "$F/car/zeroos-usb-stick"      "$D/usr/lib/zeroos/usb-stick"
install -m 0644 "$F/car/zeroos-usb-stick.service" "$D/etc/systemd/system/"
# uinput: BlueZ turns the car's play/skip buttons into key presses with it.
printf 'uinput\nlibcomposite\n' >              "$D/etc/modules-load.d/zeroos-car.conf"
# Expansion: LoRa on SPI0 CE1 (GPS: see the overrides below).
install -D -m 0644 "$F/expansion/meshtasticd-cap-lora-1262.yaml" \
                   "$D/etc/meshtasticd/config.d/zeroos-cap-lora-1262.yaml"
install -D -m 0644 "$F/expansion/meshtasticd-zeroos.conf" \
                   "$D/etc/systemd/system/meshtasticd.service.d/zeroos.conf"
# Tethering through a phone, over USB or Bluetooth (docs/TETHER.md).
install -m 0755 "$F/tether/zeroos-tether"      "$D/usr/bin/zeroos-tether"
# Updates over the network or from a PC (docs/UPDATE.md).
install -m 0755 "$F/update/zeroos-update"      "$D/usr/bin/zeroos-update"
install -m 0644 "$F/update/zeroos-update.service" "$F/update/zeroos-update-check.service" \
                                               "$D/etc/systemd/system/"
install -m 0644 "$F/update/update.conf"        "$D/etc/zeroos/update.conf"
# First-boot volume: WirePlumber's default is close to silent on this speaker (M5 bug #262).
install -D -m 0644 "$F/50-zeroos-default-volume.conf" \
                   "$D/etc/wireplumber/wireplumber.conf.d/50-zeroos-default-volume.conf"
# /dev/i2c-* for the Grove port and i2c-tools (M5 loads this in a script we drop).
echo i2c-dev >                                 "$D/etc/modules-load.d/zeroos.conf"
# ZeroShell restores brightness itself. systemd-backlight would save the level at
# shutdown (0 if the screen was off) and put it back at boot; M5 masks it too.
ln -sf /dev/null "$D/etc/systemd/system/systemd-backlight@backlight:backlight.service"
install -D -m 0644 "$F/nm-wifi-powersave.conf" "$D/etc/NetworkManager/conf.d/90-zeroos-powersave.conf"
# Console-setup blanks the LCD on boot (race); unblank afterwards (owner report).
install -d "$D/etc/systemd/system/console-setup.service.d"
cat > "$D/etc/systemd/system/console-setup.service.d/zeroos-unblank.conf" <<'UNIT'
[Service]
ExecStartPost=/bin/sh -c 'echo 1 > /sys/class/graphics/fb0/blank; echo 0 > /sys/class/graphics/fb0/blank'
UNIT

# Overrides: settings files that belong to another Debian package, so ours
# can't ship them in place. The package's postinst copies this tree over /.
#   earlyoom: what to kill first when memory runs out (docs/PERFORMANCE.md)
#   gpsd: GPS on the UART (G14/G15, EXT and Grove)
O="$D/usr/share/zeroos/overrides"
install -D -m 0644 "$F/earlyoom"               "$O/etc/default/earlyoom"
install -D -m 0644 "$F/expansion/gpsd.default" "$O/etc/default/gpsd"
