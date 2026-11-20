# Jellyfin

The **Media** app plays music from a Jellyfin server as well as from `~/Music`.

## Sign in

1. Open **Media** and press **Right** twice for the **Jellyfin** tab.
2. Press **Enter** on **Sign in...** and answer three questions:
   - **Server**: its name or address. `nas` or `192.168.1.20` means
     `http://nas:8096` (Jellyfin's usual port); type the whole address, such as
     `https://music.example.com`, for anything else.
   - **User name** and **password**: the ones you use in Jellyfin's own apps.

The password is used once and not kept. The server gives back a sign-in token,
which is saved in `~/.config/zeroos/settings.conf` (readable only by you).
**Sign out** forgets it; you can also remove "Cardputer Zero" under Devices in
Jellyfin's dashboard.

## Playing

| Key | Does |
| --- | --- |
| Enter | Open an album, artist or playlist; play a track |
| Esc or ` | Back one level |
| / | Search tracks, albums and artists |
| Space | Pause / resume |
| N / P | Next / previous track |
| S | Stop |
| + / - | Volume |

Playing a track queues the rest of the album or playlist after it. The queue
belongs to ZeroShell, not the Media app, so music carries on to the next track
after you leave the app and while the screen is off. Wi-Fi stays on while
something is playing.

Tracks in mp3, flac, ogg, opus, m4a or wav play as they are; the server
converts anything else to mp3. Lists show the first 200 entries, so use search
in a big library.

## Away from home

With Tailscale on both the Cardputer and the server (see [REMOTE.md](REMOTE.md)),
sign in with the server's Tailscale name, e.g. `homelab`, and it works from any
network. Set **Settings > Tailscale** to **Always on** so it stays connected.

## Limits

- Music only: no video, no album art.
- Names in scripts the built-in font lacks (Chinese, Japanese, Cyrillic...)
  show as boxes.
- Asking the server for a list freezes the screen for a moment, up to 12
  seconds if the server doesn't answer.
- Not yet tried against a real server or on the device. The requests follow
  Jellyfin's published API description (12.4).
