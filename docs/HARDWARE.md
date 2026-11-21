# Hardware map

Sources: [M5 CardputerZero docs](https://docs.m5stack.com/en/CardputerZero), M5's
[pi-gen fork](https://github.com/CardputerZero/pi-gen) and
[board drivers](https://github.com/m5stack/m5stack-linux-dtoverlays), and early owner reports
([Markus Birth](https://blog.mbirth.uk/2026/11/11/taming-the-cardputerzero.html),
[M5CZ-Kali-Graft](https://github.com/n0xa/M5CZ-Kali-Graft)). Items marked * are absent on the Lite.

| Part | Chip | Linux interface | ZeroOS uses it for |
| --- | --- | --- | --- |
| SoC | RP3A0 (BCM2837), 4x A53 @ 1 GHz, 512 MB | | Everything |
| Display | ST7789V3 320x170, SPI | `/dev/fb_lcd`, usually fb1 (DRM mipi-dbi + firmware blob) | ZeroShell |
| Backlight | PWM | `/sys/class/backlight/*` | Settings > Brightness |
| Keyboard | TCA8418 (I2C 0x34) | evdev input device | All input |
| Key LEDs | Shift, Fn, Sym, 3 status | `/sys/class/leds/*` | `Hal::set_led` (modifier state, alerts: Phase 1) |
| Fuel gauge | BQ27220 | `/sys/class/power_supply/bq27220-0/` | Status bar, Battery app |
| RTC | RX8130CE | `/dev/rtc0` | Offline time, Clock app |
| IMU* | BMI270 + BMM150 | iio (`in_accel_*`, `in_anglvel_*`, `in_magn_*`) | Compass, level |
| IR | TX + RX on GPIO | rc-core: `ir-ctl`, `ir-keytable` | IR Remote |
| Audio | ES8389 codec, AW8737A amp, 1 W speaker, mic, TRRS | ALSA/PipeWire; sox `play`/`rec` | Media app (music, voice notes), volume, mute |
| Wi-Fi / BT | BCM43439 2.4 GHz | NetworkManager, BlueZ | Wi-Fi app, Settings, Bluetooth audio in the Car app |
| Ethernet | SR9900A (USB) | `eth0` | Status, System app |
| USB | GL852G hub, USB-A, 2x USB-C, Host/Slave switch (Slave: right USB-C is a device port) | `dwc2`, configfs gadgets | USB stick mode in the Car app, see CAR.md |
| Camera* | IMX219 8 MP, CSI | libcamera (RPi fork) | Camera app (M5's CameraApp, started by ZeroShell; QR codes read by ZeroShell through `rpicam-vid`), see CAMERA.md |
| Video | HDMI 1080p30, H.264 enc/dec | vc4 (usually card0/fb0), V4L2 M2M | Dock mode (labwc on the HDMI card only; mpv hardware H.264), see DOCK.md |
| Storage | microSD | | Root filesystem |
| Expansion | Grove HY2.0 (I2C/UART), 14-pin EXT bus | i2c-dev, spidev, UART | GPS app (gpsd), Mesh app (meshtasticd, Cap LoRa-1262); see EXPANSION.md |

## Known issues from early owners, and what the image does

| Problem | Fix in `image/stage-zeroos/01-run.sh` |
| --- | --- |
| Symbol layer only correct while M5's launcher runs | Load `tca8418_keypad_m5stack_keymap.map` system-wide via `/etc/default/keyboard` |
| Keys leak to the Linux console behind the UI | Mask `getty@tty1`; ZeroShell conflicts with it |
| LCD blank without Plymouth (driver not in initramfs) | Panel modules + `cardputerzero,st7789v_lcd.bin` in the initramfs |
| LCD blanks when console-setup runs | Drop-in that unblanks the framebuffers afterwards |
| LCD stays on M5's logo or blank: `fb_load --keep-open` holds the LCD until M5's launcher starts, which ZeroOS doesn't ship | Drop-in runs `fb_load` without `--keep-open` |
| 512 MB RAM is tight | No desktop, `gpu_mem=32`, zram (zstd, 1.5x RAM) with SD writeback via rpi-swap, earlyoom, trimmed services; see PERFORMANCE.md |
| armhf enabled, apt twice as slow | Remove the armhf architecture |
| nexmon breaks onboard Wi-Fi | Purge `brcmfmac-nexmon-dkms` |
| Wi-Fi region hard-coded to AE | `cfg80211.ieee80211_regdom` from `WIFI_COUNTRY` (default US) |
| `/dev/spidev*` root-only | Create the `spi` group |
| Volume and mute need PipeWire, which only runs in a login session | Linger for `pi`; ZeroShell starts after `user@1000`; `pipewire-alsa` so sox plays through it |
| Speaker close to silent on first boot (WirePlumber default 0.064) | Default sink volume 0.216 (60%), as M5 ships |
| Wrong Wi-Fi password stays saved and is retried | ZeroShell deletes the profile when a connect with a typed password fails |
| `/dev/i2c-*` missing (M5 loads `i2c-dev` in a script we drop) | `/etc/modules-load.d/zeroos.conf` |
| systemd-backlight restores the shutdown brightness at boot | Masked, as on M5's image; ZeroShell restores its own setting |
| Kernel upgrade breaks the display and keyboard (modules are built for one kernel) | Inherited from M5's pi-gen: kernel packages pinned and held. Don't `apt-mark unhold` them |
| No suspend or hibernate on the Pi kernel | Not fixable in Phase 0; plan for shutdown, show time left |
| No kernel key repeat (DT lacks autorepeat) | ZeroShell repeats held keys itself; console repeat in Phase 1 |

## To confirm on hardware (Phase 1)

- Exact names under `/sys/class/leds`, `/sys/class/backlight` and the iio devices.
- IMU axis orientation and magnetometer calibration. Motion wake assumes `in_accel_z` reads +1 g lying face up; if face down and pickup act the wrong way round, set `motion.z_sign=-1` in `~/.config/zeroos/settings.conf`.
- Whether IR transmit works with the stock driver (one owner saw no output on a non-stock image).
- Boot time and idle power with ZeroOS versus stock (stock: about 2.5 W idle).
