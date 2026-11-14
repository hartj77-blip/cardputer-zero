# Performance: memory, CPU and battery

The Cardputer Zero has 512 MB of RAM and four 1 GHz Cortex-A53 cores. This page
covers what ZeroOS changes to make that go further, what we have measured so
far, and how to measure the rest on a real unit.

Anything marked **(measure)** is an estimate until someone runs
`zeroos-measure` on the hardware. Please replace estimates with real numbers
as they come in.

## What ZeroOS changes

| Change | Where | Why |
| --- | --- | --- |
| No desktop. Image stops after pi-gen stage2 | `image/build.sh` | X/Wayland plus a desktop costs 150-250 MB. ZeroShell draws straight to the framebuffer. |
| `gpu_mem=32` | `stage-zeroos/01-run.sh` | No 3D desktop to feed; gives the rest back to Linux. |
| zram swap, zstd, 1.5x RAM | `files/swap.conf`, `files/zram-zeroos.conf` | Idle pages are compressed in RAM (typically 2.5-3x) instead of written to the SD card. Uses Raspberry Pi OS's own `rpi-swap`, so there is only one zram device. |
| 512 MB SD swap file as zram writeback | `files/swap.conf` (`Mechanism=zram+file`) | Pages that stay idle get moved from zram to the card now and then, which frees RAM with very little card wear. It is also the last-resort safety net. |
| `vm.swappiness=100`, `vfs_cache_pressure=80`, `min_free_kbytes=8192` | `files/90-zeroos-memory.conf` | Swap to zram early (it is RAM speed), keep file and directory caches, keep a reserve for the display and Wi-Fi drivers. |
| earlyoom (4% RAM and 8% swap left) | `files/earlyoom` | Kills the biggest app (browser, player, Python) before the whole system stalls. Never picks ZeroShell, ssh, networking or audio. |
| Logs in RAM, capped at 16 MB | `files/journald.conf` | No constant log writes to the card. |
| Services off: apt-daily, man-db, e2scrub, wait-online, eeprom-update, ModemManager, rpi-connect | `stage-zeroos/01-run.sh` | Each costs RAM, CPU at boot, or random background work. Update with `sudo apt upgrade` when you choose. |
| Removed: armhf, nexmon, plymouth, desktop packages | `stage-zeroos/01-run.sh` | Smaller image, fewer things starting. |
| ZeroShell instead of a desktop | `shell/` | One small C++ process (LVGL) with a fixed 512 KB UI memory pool. |

## Battery

Stock idle draw is reported at about 2.5 W, which drains the 1750 mAh cell
(about 6.5 Wh) in under 3 hours. The Pi has no suspend, so ZeroOS saves power
by doing less while awake and almost nothing while the screen is off.

| What | Default | Where |
| --- | --- | --- |
| Dim the backlight to 10% | after 30 s idle | Settings → Dim screen after |
| Backlight and panel off, CPU to lowest speed, UI stops drawing | after 1 min idle, on battery only | Settings → Screen off after |
| First key after screen-off only wakes the screen | always | `ui/power.cpp` |
| Power profile: Saver (on-demand, capped near 70% speed), Balanced (on-demand to full speed), Performance (full speed) | Balanced | Settings, or P in the Battery app |
| Low battery: Saver turns on by itself | 15% | `ui/power.cpp` |
| Clean shutdown | 3% | `ui/power.cpp` |
| Bluetooth off when nothing is connected | after 10 min | Settings → Bluetooth auto-off |
| Wi-Fi off while the screen is off | off by default (after 10 min when on) | Settings → Wi-Fi off with screen |
| Wi-Fi chip power save between beacons | on | `files/nm-wifi-powersave.conf` |
| Main loop sleeps until the next timer or key, instead of waking every 5 ms | always | `platform.cpp` |
| Screen off: keyboard and redraw timers stop; the loop wakes about every 2 s or on a key | always | `platform.cpp` |
| Logs in RAM, background jobs off | always | step 1 |

**Time left** in the top bar and the Battery app comes from our own average of
the gauge's voltage x current with a 3-minute time constant, divided into the
gauge's remaining charge. The gauge's own estimate is used for the first 30 s
and while charging. Turn it off in Settings → Time left in top bar.

ZeroShell runs as `pi`; `zeroshell.service` runs `/usr/lib/zeroos/power-perms`
as root first, which lets the `video` group change the CPU governor and top
speed and blank the panel.

Still to measure on the unit: real draw in each profile with the screen on,
dimmed and off; whether the speaker amplifier draws power while silent; how
much Wi-Fi power save and Bluetooth off actually save.

## Measured so far

These were measured in the cloud build machine, not on the device.

| What | Result | How |
| --- | --- | --- |
| ZeroShell peak memory, all 10 apps opened in turn | **5.2 MB** resident (x86-64, headless build) | `/proc/<pid>/status` VmHWM during a scripted run of every app |
| ZeroShell binary, arm64 | 1.1 MB (965 KB code), plus 0.5 MB of zeroed memory for the UI pool | `aarch64-linux-gnu-size` |
| One full screen frame | 109 KB (320 x 170, 16-bit) | arithmetic |

On the device, expect ZeroShell at about **5-9 MB** resident **(measure)**:
same code, plus the shared C++ runtime and the evdev/fbdev buffers. The
System app shows the real number under Memory → This UI.

## Estimates for a unit (measure)

| | Stock M5 image | ZeroOS (estimate) |
| --- | --- | --- |
| RAM visible to Linux | ~440 MB | ~470 MB (smaller GPU split) |
| In use at idle (total - available) | **(measure)** | 90-120 MB |
| Available to apps at idle | **(measure)** | 340-380 MB |
| Usable memory with zram full | n/a | about 600-750 MB worth of apps |
| Boot to UI | **(measure)** | under 20 s |
| Services running | **(measure)** | ~15 |
| Idle CPU (screen on, Wi-Fi up) | **(measure)** | under 3% |
| Idle power, screen on | **(measure)** | 0.7-1.0 W, about 6-9 h on the 1750 mAh cell |
| Idle power, screen off | **(measure)** | 0.4-0.6 W, 11-16 h |

The zram figure works like this: 768 MB of swap space at a 2.5-3x
compression ratio takes about 260-300 MB of real RAM when completely full.
Idle pages also spill to the SD file, so in practice the device behaves like a
machine with roughly 0.9-1.1 GB of memory for anything that isn't all busy at
once. Heavy apps that keep everything busy (a browser on a big page) still
only have the real 512 MB.

## Measuring on the unit

ZeroOS ships `zeroos-measure`. It also runs on M5's stock image (copy
`image/stage-zeroos/files/zeroos-measure` across), so we can compare them.

```sh
zeroos-measure            # full report: boot, memory, zram, processes, CPU, 60 s idle power
zeroos-measure -s 300     # sample idle power for 5 minutes (better runtime estimate)
zeroos-measure --quick    # skip the CPU and power samples
```

It prints Markdown and saves it to `~/zeroos-measure-<host>-<date>.md`.

For a fair comparison:

1. Flash the image, boot, connect Wi-Fi, then reboot.
2. Wait 3 minutes after the UI appears, so first-boot jobs have finished.
3. Unplug USB power, screen on, Wi-Fi connected, nothing playing.
4. Run `zeroos-measure -s 300` over ssh and leave the device alone.
5. Do the same on the stock image, then paste both reports into an issue or
   below.

The System app (Memory page) shows the live view of the same numbers: RAM
used and available, file cache, zram (data stored, RAM it takes, ratio), SD
swap, ZeroShell's own memory, and the five biggest processes. In the
simulator these are marked "simulated".

## Things to try once we have numbers

- `RamMultiplier` 1.5 vs 2, and lz4 vs zstd (speed vs ratio).
- `vm.page-cluster=0` (read one page at a time from zram; often faster).
- Turn off Bluetooth and the audio stack until first used (saves ~15-20 MB).
- CPU governor `schedutil` vs `ondemand`, and a lower max clock on battery.
- Splitting ZeroShell's hardware work into the `zerod` service (Phase 1), so
  apps outside the shell can share it.
