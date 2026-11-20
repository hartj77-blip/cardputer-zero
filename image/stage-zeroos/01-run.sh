#!/bin/bash -e
# ZeroOS stage: ZeroShell + system tuning for the Cardputer Zero.
# Each tweak cites the problem it fixes (see docs/HARDWARE.md, "Known issues").

install -m 0755 files/zeroshell              "${ROOTFS_DIR}/usr/bin/zeroshell"
echo "ZeroOS @ZEROOS_VERSION@ (built $(date -u +%Y-%m-%d))" > "${ROOTFS_DIR}/etc/zeroos-release"
install -m 0644 files/zeroshell.service      "${ROOTFS_DIR}/etc/systemd/system/zeroshell.service"
install -d -m 0755                           "${ROOTFS_DIR}/etc/polkit-1/rules.d"
install -m 0644 files/90-zeroos.rules        "${ROOTFS_DIR}/etc/polkit-1/rules.d/90-zeroos.rules"
install -m 0644 files/90-zeroos.udev         "${ROOTFS_DIR}/etc/udev/rules.d/90-zeroos.rules"
install -d "${ROOTFS_DIR}/etc/rpi/swap.conf.d" "${ROOTFS_DIR}/etc/systemd/zram-generator.conf.d" \
           "${ROOTFS_DIR}/etc/systemd/journald.conf.d"
install -m 0644 files/swap.conf              "${ROOTFS_DIR}/etc/rpi/swap.conf.d/80-zeroos.conf"
install -m 0644 files/zram-zeroos.conf       "${ROOTFS_DIR}/etc/systemd/zram-generator.conf.d/30-zeroos.conf"
install -m 0644 files/90-zeroos-memory.conf  "${ROOTFS_DIR}/etc/sysctl.d/90-zeroos-memory.conf"
install -m 0644 files/journald.conf          "${ROOTFS_DIR}/etc/systemd/journald.conf.d/90-zeroos.conf"
install -m 0644 files/earlyoom               "${ROOTFS_DIR}/etc/default/earlyoom"
install -m 0644 files/zeroos-cloud-init-once.service "${ROOTFS_DIR}/etc/systemd/system/"
install -m 0755 files/zeroos-measure         "${ROOTFS_DIR}/usr/bin/zeroos-measure"
install -D -m 0755 files/zeroos-power-perms  "${ROOTFS_DIR}/usr/lib/zeroos/power-perms"
# Dock mode: labwc desktop on the HDMI monitor (docs/DOCK.md).
install -d -m 0755                           "${ROOTFS_DIR}/etc/zeroos/dock"
install -m 0644 files/dock/rc.xml files/dock/menu.xml files/dock/autostart files/dock/environment \
                files/dock/waybar.json files/dock/waybar.css "${ROOTFS_DIR}/etc/zeroos/dock/"
install -m 0755 files/dock/dock-session      "${ROOTFS_DIR}/usr/lib/zeroos/dock-session"
install -m 0755 files/dock/zeroos-dock files/dock/zeroos-browser files/dock/zeroos-video "${ROOTFS_DIR}/usr/bin/"
install -m 0644 files/dock/zeroos-dock.service "${ROOTFS_DIR}/etc/systemd/system/"
install -d -o 1000 -g 1000 "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/Videos" 2>/dev/null || true
# Remote mode: Terminal app (tmux on tty3), Tailscale helpers (docs/REMOTE.md).
install -m 0644 files/remote/zeroos-terminal.service files/remote/zeroos-taildrop.service \
                                             "${ROOTFS_DIR}/etc/systemd/system/"
install -m 0755 files/remote/terminal-session "${ROOTFS_DIR}/usr/lib/zeroos/terminal-session"
install -m 0644 files/remote/tmux.conf       "${ROOTFS_DIR}/etc/zeroos/tmux.conf"
install -D -m 0644 files/remote/tailscaled-zeroos.conf \
                   "${ROOTFS_DIR}/etc/systemd/system/tailscaled.service.d/zeroos.conf"
# Sync: Syncthing for Notes, Documents and Recordings (docs/SYNC.md).
install -m 0755 files/sync/zeroos-sync       "${ROOTFS_DIR}/usr/bin/zeroos-sync"
install -D -m 0644 files/sync/syncthing-zeroos.conf \
                   "${ROOTFS_DIR}/etc/systemd/system/syncthing@.service.d/zeroos.conf"
for d in Notes Documents Recordings; do
    install -d -o 1000 -g 1000 "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/$d" 2>/dev/null || true
done
# Expansion: GPS on the UART (G14/G15, EXT and Grove), LoRa on SPI0 CE1.
install -m 0644 files/expansion/gpsd.default "${ROOTFS_DIR}/etc/default/gpsd"
install -D -m 0644 files/expansion/meshtasticd-cap-lora-1262.yaml \
                   "${ROOTFS_DIR}/etc/meshtasticd/config.d/zeroos-cap-lora-1262.yaml"
install -D -m 0644 files/expansion/meshtasticd-zeroos.conf \
                   "${ROOTFS_DIR}/etc/systemd/system/meshtasticd.service.d/zeroos.conf"
# Tethering through a phone, over USB or Bluetooth (docs/TETHER.md).
install -m 0755 files/tether/zeroos-tether   "${ROOTFS_DIR}/usr/bin/zeroos-tether"
install -d -o 1000 -g 1000 "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/Pictures" 2>/dev/null || true
# Console font for the Terminal app: Terminus 6x12 gives 53 x 14 on 320 x 170.
sed -i -e 's/^FONTFACE=.*/FONTFACE="Terminus"/' -e 's/^FONTSIZE=.*/FONTSIZE="6x12"/' \
    "${ROOTFS_DIR}/etc/default/console-setup" 2>/dev/null || true
install -D -m 0644 files/nm-wifi-powersave.conf "${ROOTFS_DIR}/etc/NetworkManager/conf.d/90-zeroos-powersave.conf"
cat files/initramfs-modules >>               "${ROOTFS_DIR}/etc/initramfs-tools/modules"
install -m 0755 files/initramfs-hook         "${ROOTFS_DIR}/etc/initramfs-tools/hooks/zeroos-panel"

CONFIG_TXT="${ROOTFS_DIR}/boot/firmware/config.txt"
CMDLINE_TXT="${ROOTFS_DIR}/boot/firmware/cmdline.txt"

# Smaller GPU split: no desktop, so give RAM back to Linux (owner report: gpu_mem=32 works).
grep -q '^gpu_mem=' "$CONFIG_TXT" && sed -i 's/^gpu_mem=.*/gpu_mem=32/' "$CONFIG_TXT" || echo 'gpu_mem=32' >> "$CONFIG_TXT"

# Wi-Fi regulatory domain (M5 ships AE); quiet boot without Plymouth.
sed -i -e 's/cfg80211.ieee80211_regdom=[A-Z]*//' -e 's/ splash//' -e 's/ plymouth.ignore-serial-consoles//' "$CMDLINE_TXT"
sed -i '1 s/$/ cfg80211.ieee80211_regdom=@WIFI_COUNTRY@/' "$CMDLINE_TXT"
# The UART belongs to GPS modules, not a serial console.
sed -i -e 's/console=serial0,[0-9]* *//' -e 's/console=ttyS0,[0-9]* *//' -e 's/console=ttyAMA0,[0-9]* *//' "$CMDLINE_TXT"
grep -q '^enable_uart=' "$CONFIG_TXT" && sed -i 's/^enable_uart=.*/enable_uart=1/' "$CONFIG_TXT" || echo 'enable_uart=1' >> "$CONFIG_TXT"

# Keyboard symbol layer system-wide, not only under M5's launcher (owner report).
KMAP=/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map
if [ -f "${ROOTFS_DIR}${KMAP}" ]; then
    sed -i '/^KMAP=/d' "${ROOTFS_DIR}/etc/default/keyboard"
    echo "KMAP=\"${KMAP}\"" >> "${ROOTFS_DIR}/etc/default/keyboard"
fi

# Console-setup blanks the LCD on boot (race); unblank afterwards (owner report).
install -d "${ROOTFS_DIR}/etc/systemd/system/console-setup.service.d"
cat > "${ROOTFS_DIR}/etc/systemd/system/console-setup.service.d/zeroos-unblank.conf" <<'UNIT'
[Service]
ExecStartPost=/bin/sh -c 'echo 1 > /sys/class/graphics/fb0/blank; echo 0 > /sys/class/graphics/fb0/blank'
UNIT

on_chroot << 'CHROOT'
set -e
# Tailscale from its own repository (not in Debian). Installed but off at
# boot: ZeroShell starts it when needed (Settings > Tailscale).
curl -fsSL https://pkgs.tailscale.com/stable/debian/trixie.noarmor.gpg \
    -o /usr/share/keyrings/tailscale-archive-keyring.gpg
curl -fsSL https://pkgs.tailscale.com/stable/debian/trixie.tailscale-keyring.list \
    -o /etc/apt/sources.list.d/tailscale.list
apt-get update
apt-get install -y tailscale
sed -i 's/^FLAGS=.*/FLAGS="--no-logs-no-support"/' /etc/default/tailscaled
systemctl disable tailscaled.service
systemctl enable zeroos-taildrop.service
# Meshtastic for the Cap LoRa-1262 (docs/EXPANSION.md), also off at boot.
curl -fsSL https://download.opensuse.org/repositories/network:/Meshtastic:/beta/Debian_13/Release.key \
    -o /usr/share/keyrings/meshtastic.asc
echo 'deb [signed-by=/usr/share/keyrings/meshtastic.asc] http://download.opensuse.org/repositories/network:/Meshtastic:/beta/Debian_13/ /' \
    > /etc/apt/sources.list.d/meshtastic.list
apt-get update
apt-get install -y meshtasticd
systemctl disable meshtasticd.service
# GPS: gpsd starts when a client connects (gpsd.socket) and only reads the
# receiver while someone watches.
systemctl enable gpsd.socket
systemctl disable gpsd.service 2>/dev/null || true
# arm64 only: M5 enables armhf too, which doubles apt indexes (owner report).
dpkg --remove-architecture armhf 2>/dev/null || true
# nexmon shadows the onboard Wi-Fi driver and has no BCM43439 support.
apt-get purge -y brcmfmac-nexmon-dkms 2>/dev/null || true
apt-get purge -y plymouth 2>/dev/null || true

# ZeroShell owns tty1's screen; stop keys leaking into a login prompt behind it.
systemctl mask getty@tty1.service
systemctl enable zeroshell.service NetworkManager.service bluetooth.service earlyoom.service
systemctl enable zeroos-cloud-init-once.service
systemctl set-default multi-user.target

# Lean base: switch off what a pocket computer doesn't need running.
# Each is only touched if installed, so a changed base image doesn't break the build.
disable_if_present() {
    for u in "$@"; do
        if [ -e "/usr/lib/systemd/system/$u" ] || [ -e "/lib/systemd/system/$u" ]; then
            systemctl disable "$u" 2>/dev/null || true
            echo "zeroos: disabled $u"
        fi
    done
}
#   apt-daily*, man-db: daily jobs that spike CPU, RAM and SD writes on battery
#   e2scrub_all: weekly filesystem scrub (needs LVM; does nothing useful here)
#   NetworkManager-wait-online: holds up boot until Wi-Fi connects
#   rpi-eeprom-update: the CM0 has no bootloader EEPROM
#   rpi-connect / rpi-connect-wayvnc: Raspberry Pi's remote access (Tailscale replaces it)
#   ModemManager: no modem
disable_if_present apt-daily.timer apt-daily-upgrade.timer man-db.timer e2scrub_all.timer \
    e2scrub_reap.service NetworkManager-wait-online.service rpi-eeprom-update.service \
    ModemManager.service
systemctl --global disable rpi-connect.service rpi-connect-wayvnc.service 2>/dev/null || true

groupadd -f spi   # /dev/spidev* stays root-only without it (owner report)
for g in video input audio netdev i2c gpio spi dialout adm bluetooth; do   # adm: read service logs
    getent group "$g" >/dev/null && usermod -aG "$g" pi
done
update-initramfs -u -k all || true
CHROOT
