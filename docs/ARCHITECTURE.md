# Architecture

ZeroOS is five layers. Each one only talks to the layer below it, so apps never touch raw hardware.

| Layer | What it is | Where |
| --- | --- | --- |
| 5. Apps | Built-in apps (C++/LVGL). Later: Python SDK and store apps | `shell/src/apps/` |
| 4. ZeroShell | Status bar, launcher, app switching, toasts, key routing | `shell/src/ui/` |
| 3. Hardware layer | One interface (`zs::Hal`) for battery, IMU, radios, audio, IR, LEDs, power | `shell/src/hal/` |
| 2. Linux services | NetworkManager, BlueZ, PipeWire, libcamera, systemd | Debian packages |
| 1. Base image | Raspberry Pi OS trixie arm64, pinned 6.18.39 kernel, M5 board drivers | `image/` |

## ZeroShell

- **LVGL 9.2** (pinned), 16-bit color, Montserrat fonts built in. One binary, three backends chosen at
  build time: `sim` (SDL2 window), `device` (`/dev/fb0` + evdev keyboard), `headless` (renders to
  files, drives CI screenshots and smoke tests).
- **Key routing.** The shell wraps the keypad driver, so every key press goes first to the current
  app's `on_key()`, then to shell keys (Esc = back, Up/Down = move focus), and only then to the
  focused LVGL widget. This is how an app can use letters as shortcuts while text fields still type.
- **Apps** implement `zs::App` (`build`, `tick`, `on_key`, `on_back`) and are listed in
  `apps/registry.cpp`. Switching apps is deferred until the current key handler returns, so an app
  can safely open another app from its own handler.

## Hardware layer

`zs::Hal` (`hal/hal.h`) is the only way the UI reaches hardware.

- `MockHal`: plausible simulated readings for the simulator and CI.
- `LinuxHal`: real readings through standard kernel interfaces (sysfs power_supply, iio,
  backlight, leds, rc-core) and system tools (nmcli, wpctl, rfkill, ir-ctl). Every read degrades to
  "unknown" instead of crashing, so a missing driver shows `--`, not a dead UI.

Phase 1 moves `LinuxHal` behind a small system service (`zerod`) with a D-Bus API, so Python apps
and scripts get the same hardware access and the UI process no longer needs extra permissions.
The `Hal` interface stays the same.

## Image

`image/build.sh` does not fork M5's builder. It pins M5's open pi-gen fork at a known commit and:

1. keeps M5's kernel pin and board-driver build, but pins the driver repo to a commit (M5 builds `main`);
2. removes M5's launcher and demo apps;
3. stops after stage 2 (no desktop, 512 MB RAM);
4. adds `stage2/06-zeroos` (`image/stage-zeroos/`): ZeroShell as a systemd service, polkit and udev
   rules so it runs as `pi`, zram swap, and the fixes from early owner reports (see HARDWARE.md).
