#!/bin/bash -e
# ZeroOS stage: ZeroShell + system tuning for the Cardputer Zero.
# Each tweak cites the problem it fixes (see docs/HARDWARE.md, "Known issues").

install -m 0755 files/zeroshell              "${ROOTFS_DIR}/usr/bin/zeroshell"
install -m 0644 files/zeroshell.service      "${ROOTFS_DIR}/etc/systemd/system/zeroshell.service"
install -d -m 0755                           "${ROOTFS_DIR}/etc/polkit-1/rules.d"
install -m 0644 files/90-zeroos.rules        "${ROOTFS_DIR}/etc/polkit-1/rules.d/90-zeroos.rules"
install -m 0644 files/90-zeroos.udev         "${ROOTFS_DIR}/etc/udev/rules.d/90-zeroos.rules"
install -m 0644 files/zramswap               "${ROOTFS_DIR}/etc/default/zramswap"
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
systemctl enable zeroshell.service NetworkManager.service bluetooth.service zramswap.service
systemctl set-default multi-user.target

groupadd -f spi   # /dev/spidev* stays root-only without it (owner report)
for g in video input audio netdev i2c gpio spi; do
    getent group "$g" >/dev/null && usermod -aG "$g" pi
done
update-initramfs -u -k all || true
CHROOT
