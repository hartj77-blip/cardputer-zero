# Roadmap

**Goal:** a general-purpose portable computer that does as much as possible on 512 MB of RAM and a
1 GHz quad-core A53.

**Design rule:** it can do almost anything, but only one heavy thing at a time.

Items marked *(measure)* are estimates to confirm on the real unit before we rely on them.

## Three modes

| Mode | When | What it is |
| --- | --- | --- |
| **Pocket** | On the go, on battery | Terminal-first on the built-in screen, tuned for battery. Boots to the terminal with the launcher one key away. A setting picks the boot target: terminal or launcher. The graphical UI stays; it is small. |
| **Dock** | HDMI monitor + USB keyboard | A small desktop. The best place for video and web browsing. |
| **Remote** | Anywhere with a network | The device is a screen and keyboard for a home PC or cloud machine, over SSH or remote desktop. Heavy work runs elsewhere. |

## Tailscale

- Remote mode runs over Tailscale, so the home PC is reachable from any network without port forwarding.
- **Taildrop** for sending files to and from the device.
- A toggle in Settings, a terminal shortcut, and an indicator in the status bar.
- **On when needed** by default: it starts with Wi-Fi in Remote mode. An **always on** option is in Settings.
- Sign-in by link or QR code shown on screen and approved from a phone.
- Memory and battery cost of `tailscaled` *(measure)*.

## Memory and CPU

- **zram** compressed swap on by default (already in the image), with idle pages written back to an SD swap file. Real gain *(measure)*; see [PERFORMANCE.md](PERFORMANCE.md).
- A small **swap file on the SD card** as a last-resort safety net, not for everyday use.
- **Lean base:** no desktop environment; strip services we don't use.
- **One app in front:** background apps are frozen or closed.
- **Minimal GPU memory** (`gpu_mem=32`), raised only when the camera or HDMI is in use.
- **CPU governor:** fast while typing, slow when idle. An optional **Boost** toggle for a mild overclock.
- **Load on demand:** graphics-heavy apps (video player, web browser) start when opened and fully unload on close.
- **System monitor:** real memory and CPU readouts, including ZeroShell's own usage *(measure)*.

## Battery

Battery is likely the daily limit (stock idle is about 2.5 W on a 1750 mAh cell, and there is no
suspend). So: dim the screen quickly, turn radios off when unused, keep the CPU slow when idle, and
show time-left estimates in the status bar.

## What runs where

| Tier | Examples |
| --- | --- |
| **Runs well on the device** | Terminal, coding, SSH, notes, files, text email and chat, music, voice memos, camera, QR scanning, IR remote, e-books and documents, light text or reader-style browsing |
| **Runs with care** | Video at modest resolution with hardware decoding *(measure)*; a full modern browser, available but not the default |
| **Borrowed (Remote mode)** | Heavy websites, big builds, AI tools |

## Hardware extras

- Keyboard shortcuts everywhere, readable fonts, split views in the terminal.
- Note and file sync over Wi-Fi *(done in software; see [SYNC.md](SYNC.md))*.
- Phone as a hotspot, over Wi-Fi, USB or Bluetooth *(done in software; see [TETHER.md](TETHER.md))*.
- Camera with QR code scanning *(done in software; see [CAMERA.md](CAMERA.md))*.
- IR universal remote (started: IR Remote app).
- Motion sensor: wake on pickup, sleep when placed face down *(done in software; see [PERFORMANCE.md](PERFORMANCE.md#battery))*.
- Expansion ports for GPS or LoRa modules, with GPS track recording *(done in software; see [EXPANSION.md](EXPANSION.md))*.

## Order of work

1. **Lean base and memory tuning, with real measurements.** *(done in software; measuring waits for the unit)* Includes device bring-up: display, keyboard, battery, Wi-Fi, audio, RTC, IMU and IR working on the unit.
2. **Battery life.** Governor, dimming, radio management, time-left estimates. *(done in software; see [PERFORMANCE.md](PERFORMANCE.md#battery))*
3. **Dock mode.** HDMI desktop, video, web browsing. *(done in software; see [DOCK.md](DOCK.md))*
4. **Extras.** Tailscale and Remote mode *(done in software; see [REMOTE.md](REMOTE.md))*, sync, motion wake and sleep, GPS and LoRa mesh *(all done in software; see [SYNC.md](SYNC.md), [EXPANSION.md](EXPANSION.md))*.

## Done so far (Phase 0)

ZeroShell with 10 apps (Files, Notes, Media, Wi-Fi, IR Remote, Compass, Battery, Clock, System,
Settings) running in a desktop and browser simulator; a hardware layer with simulated and Linux
backends; the image builder on M5's pinned pi-gen; CI with screenshots, an arm64 build and the browser build.
