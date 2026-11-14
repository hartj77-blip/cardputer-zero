# Remote mode and Tailscale

Remote mode makes the Cardputer Zero a screen and keyboard for your bigger
computers: SSH into a home PC, server or cloud machine from anywhere, or open
a full remote desktop on a monitor in Dock mode. Tailscale connects them
without port forwarding, from any Wi-Fi or phone hotspot.

## First time

1. Open **Remote**. Tailscale starts and shows a QR code.
2. Scan it with your phone, sign in to Tailscale (or create a free account),
   and approve the device.
3. A few seconds later Remote shows **Connected** and lists your devices.

Install Tailscale on the computers you want to reach
([tailscale.com/download](https://tailscale.com/download)). For SSH, the
computer needs an SSH server (built into Linux and macOS; on Windows, turn on
"OpenSSH Server" in Optional features). For remote desktop, Windows Pro's
Remote Desktop or any RDP server (xrdp on Linux).

## Using it

In **Remote**:

| Key | Does |
| --- | --- |
| Enter on a device | SSH to it in the Terminal (or mosh, if turned on) |
| D on a device | Remote desktop to it, on the monitor (needs Dock mode) |
| Connect to... | SSH to any host or address, e.g. `john@example.com` |
| SSH as | The user name for SSH (default `pi`) |
| Use mosh | mosh instead of ssh: survives Wi-Fi changes and sleep; the other end needs `mosh` installed |
| Turn off Tailscale / Sign out | |

**Terminal** (also its own app) is a real Linux console on the built-in
screen: 53 x 14 characters, running tmux. Alt+` goes back to ZeroShell and
leaves everything running (SSH included); open Terminal again to pick up
where you were. Alt+Left/Right switch tmux windows, Alt+N opens a new one,
and each SSH connection opens in its own window.

**Taildrop**: files sent to the Cardputer from your other devices ("Send
with Tailscale" / share sheet) land in `~/Downloads`. To send a file, press
**S** on it in Files and pick a device.

The status bar shows **TS** while Tailscale is connected.

## When Tailscale runs

Settings → **Tailscale**:

- **When needed** (default): starts when you open Remote; stops after 15
  minutes with no Remote app, terminal or Dock session. Saves memory (about
  30-40 MB) and battery **(measure)** when you aren't using it.
- **Always on**: starts at boot; the Cardputer is reachable from your other
  devices all the time (SSH in to it, send files).
- **Off**: only when you turn it on in Remote.

## How it works

| Piece | What |
| --- | --- |
| `tailscale` package | From Tailscale's Debian repository. `tailscaled.service` is disabled at boot; ZeroShell starts it (polkit allows `pi` exactly this unit). Log uploads are off (`--no-logs-no-support`). |
| `tailscaled.service.d/zeroos.conf` | Makes `pi` the Tailscale operator, so ZeroShell runs `tailscale up`, `status`, `file cp` without root. |
| Sign-in | `tailscale up` in the background; ZeroShell reads the sign-in link from `tailscale status --json` and draws it as a QR code. |
| `zeroos-taildrop.service` | `tailscale file get --loop` into `~/Downloads`, started with tailscaled. |
| `zeroos-terminal.service` | A login session on tty3 running `/usr/lib/zeroos/terminal-session` (tmux, config in `/etc/zeroos/tmux.conf`). It switches the screen to tty3 and back to tty1 when tmux detaches or exits. ZeroShell stops drawing and ignores keys meanwhile, but still dims and wakes the screen. |
| Console | Terminus 6x12 font. ZeroShell keeps tty1 in graphics mode with its keyboard off, so the console never draws over the UI. |
| Remote desktop | `zeroos-dock open rdp HOST`: FreeRDP (Wayland) in a terminal window on the monitor, which asks for the user and password. |

## Still to do

- Saved hosts outside Tailscale, with their own user names.
- Remote desktop on the built-in screen (it is 320 x 170, so likely a
  zoomed viewport), and VNC for Macs.
- Tailscale SSH (signing in without keys or passwords).
