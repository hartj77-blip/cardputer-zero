# Dock mode

Plug the Cardputer Zero into a monitor or TV and it becomes a small desktop
computer: web browser, video player, terminal and file manager on the big
screen, with a USB keyboard and mouse (through a USB hub), or the Cardputer's
own keyboard.

## Using it

1. Plug a monitor into the HDMI port. ZeroShell opens the **Dock** app.
2. Press **Start Dock mode**. The desktop appears on the monitor after a few
   seconds.
3. The built-in screen shows "Docked" and turns itself off after a minute.
   The Cardputer's keys now type on the monitor.
4. To leave, click **Leave dock** in the top bar, choose it from the desktop
   menu (right-click the background), press Ctrl+Alt+Del, or just unplug the
   monitor. The built-in screen comes back.

In the Dock app:

- **On plug-in**: Ask (open the Dock app, the default), Start dock
  (automatically), or Do nothing.
- **Size**: 720p (default), 1080p, or the monitor's best mode. 720p is
  smoother and uses less memory; text is still sharp on most monitors.

## On the desktop

| | Top bar | Keys (USB keyboard) | Keys (Cardputer) |
| --- | --- | --- | --- |
| Terminal | Terminal | Super+Enter | Ctrl+Alt+T |
| Web browser (Chromium) | Web | Super+B | Ctrl+Alt+B |
| Files (Midnight Commander) | Files | Super+E | Ctrl+Alt+F |
| Videos from ~/Videos | Videos | Super+V | Ctrl+Alt+V |
| Search all apps | ZeroOS | Super+Space | Alt+Space |
| Close window | | Super+Q | Alt+F4 |
| Maximize | | Super+F / Super+Up | |
| Leave Dock mode | Leave dock | Ctrl+Alt+Del | Ctrl+Alt+Del |

Windows open maximized; there is only room for one or two apps at a time
anyway. The top bar shows open windows, the clock, CPU, memory, network and
battery.

**Videos** from the Files app on the Cardputer (Enter on a .mp4, .mkv, .webm,
.mov, .avi or .m4v) play on the monitor while docked.

## How it works

| Piece | What |
| --- | --- |
| `zeroos-dock.service` | Runs as `pi` in its own login session on tty2, so it gets the monitor and input devices from logind. Started and stopped by ZeroShell (polkit allows `pi` exactly this unit). |
| `/usr/lib/zeroos/dock-session` | Finds the DRM card that has the HDMI connector (the vc4 GPU) and starts labwc on it only. The built-in LCD is a separate DRM card that ZeroShell keeps drawing to. |
| labwc | Small Wayland compositor (also what Raspberry Pi OS uses). Config in `/etc/zeroos/dock/`. |
| waybar, swaybg, fuzzel, foot | Top bar, background, app search, terminal. |
| `zeroos-browser` | Chromium for 512 MB: low-end device mode, at most 2 page processes, 32 MB cache in RAM, a 192 MB JavaScript heap limit. |
| `zeroos-video` | mpv with hardware H.264 decoding (V4L2 M2M), a 32 MB stream cache; picks from ~/Videos with fuzzel. |
| `zeroos-dock` | `start`, `stop`, `status`, `open terminal\|browser\|files\|video FILE`. Works over ssh too. |
| ZeroShell | Watches the HDMI connector (`/sys/class/drm/*-HDMI-A-*/status`), offers or starts Dock mode, stops it 3 s after the monitor is unplugged, and stops reacting to keys while docked. |

While docked the CPU never drops below the Balanced profile, even with the
built-in screen off. earlyoom closes Chromium first if memory runs out; the
desktop session itself is next (`OOMScoreAdjust=200`), never ZeroShell.

## Limits and things to measure

- **Memory.** The desktop itself (labwc, bar, terminal) should take about
  40-60 MB **(measure)**. Chromium is the big one: expect 150-250 MB with one
  or two tabs, so simple sites and one tab at a time. zram makes room, but
  heavy sites (big web apps, many tabs) are Remote mode territory.
- **Video.** H.264 up to 1080p30 is decoded in hardware; VP9 and H.265 are
  decoded by the CPU and are only smooth at low resolution **(measure)**.
  YouTube in the browser is heavy; a downloaded or streamed H.264 file in
  mpv is much better.
- **Video memory.** M5's image reserves 64 MB of CMA for the GPU
  (`dtoverlay=vc4-kms-v3d,cma-64`). That should cover a 720p desktop plus
  H.264 decoding; 1080p with video may need `cma-96` or `cma-128`
  **(measure)**. CMA is lent back to Linux when the GPU isn't using it.
- **Power.** Docked, the HDMI output and the faster CPU draw more; use USB
  power. On battery the Dock app still works.
- **Video on the built-in screen** (without a monitor) is not done yet.
