#!/bin/bash -e
# ZeroOS stage: ZeroShell + system tuning for the Cardputer Zero.
# Each tweak cites the problem it fixes (see docs/HARDWARE.md, "Known issues").
# Files that an update can change are in payload.sh; this script does the rest.

# ZeroOS's own files go on as one package, zeroos_<version>_arm64.deb: the
# same package zeroos-update installs later (docs/UPDATE.md). payload.sh
# lists what is in it. A copy stays on the image so the first update has
# something to roll back to.
./package.sh files/zeroshell "${ROOTFS_DIR}/tmp/zeroos-pkg" "@ZEROOS_VERSION@"
install -D -m 0644 "${ROOTFS_DIR}"/tmp/zeroos-pkg/zeroos_*.deb "${ROOTFS_DIR}/var/lib/zeroos/update/current.deb"
install -d -o 1000 -g 1000 "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/Videos" 2>/dev/null || true
for d in Notes Documents Recordings; do
    install -d -o 1000 -g 1000 "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/$d" 2>/dev/null || true
done
# Console font for the Terminal app: Terminus 6x12 gives 53 x 14 on 320 x 170.
sed -i -e 's/^FONTFACE=.*/FONTFACE="Terminus"/' -e 's/^FONTSIZE=.*/FONTSIZE="6x12"/' \
    "${ROOTFS_DIR}/etc/default/console-setup" 2>/dev/null || true
# Audio: ZeroShell is a system service, so nothing logs pi in. Lingering starts
# pi's user session (PipeWire, WirePlumber) at boot; without it volume and mute
# have no sound server to talk to. M5's image gets this from its launcher.
install -d -m 0755 "${ROOTFS_DIR}/var/lib/systemd/linger"
touch "${ROOTFS_DIR}/var/lib/systemd/linger/${FIRST_USER_NAME}"
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

# Camera: M5Stack's CameraApp (MIT), the one their own image ships, pinned by
# version and checksum. ZeroShell's Camera app starts it (docs/CAMERA.md).
CAMERA_DEB_URL=https://github.com/CardputerZero/CameraApp/releases/download/v1.0.4/Camera_1.0.4_m5stack1_arm64.deb
CAMERA_DEB_SHA256=9fab11fb9a01a481e301bbe4213ef59bcd53386047ef9208683b1d57cab6b280
curl -fsSL --retry 3 -o "${ROOTFS_DIR}/tmp/camera.deb" "$CAMERA_DEB_URL"
echo "${CAMERA_DEB_SHA256}  ${ROOTFS_DIR}/tmp/camera.deb" | sha256sum -c -

on_chroot << 'CHROOT'
set -e
# The zeroos package: ZeroShell, its services and settings (built above).
dpkg -i /tmp/zeroos-pkg/zeroos_*.deb
rm -rf /tmp/zeroos-pkg
echo "zeroos: package installed: $(dpkg-query -W -f '${Version}' zeroos)"
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
# The camera app and the libraries it names (libcamera, libfmt, cJSON...).
# If it can't be installed the image is still built: ZeroShell's Camera app
# then says the camera app is missing.
if apt-get install -y --no-install-recommends /tmp/camera.deb; then
    echo "zeroos: camera app installed: $(dpkg-query -W -f '${Version}' camera)"
else
    echo "zeroos: WARNING: camera app NOT installed"
fi
rm -f /tmp/camera.deb
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
groupadd -f bluetooth   # normally made by bluez; zeroshell.service names it
for g in video input audio netdev i2c gpio spi dialout adm bluetooth; do   # adm: read service logs; bluetooth: pair devices
    getent group "$g" >/dev/null && usermod -aG "$g" pi
done
update-initramfs -u -k all || true
CHROOT
