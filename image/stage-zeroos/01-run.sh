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
for g in video input audio netdev i2c gpio spi; do
    getent group "$g" >/dev/null && usermod -aG "$g" pi
done
update-initramfs -u -k all || true
CHROOT
