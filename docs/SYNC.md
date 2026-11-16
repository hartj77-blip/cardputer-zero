# Sync

Sync keeps **Notes**, **Documents** and **Recordings** the same on the
Cardputer Zero and your other computers and phone. Write a note on the train,
and it's on your PC when you get home; drop a PDF in Documents on the PC, and
it's on the Cardputer. It uses [Syncthing](https://syncthing.net): free,
open source, no account or cloud. Your devices copy files directly to each
other, encrypted, over Wi-Fi at home or over the internet (Tailscale works
too, but isn't needed).

## First time

1. Install Syncthing on your PC ([syncthing.net/downloads](https://syncthing.net/downloads/)),
   or on an Android phone (Syncthing-Fork) or iPhone (Möbius Sync).
2. On the Cardputer, open **Sync** and choose **Show my device ID**.
3. In Syncthing on the PC, choose **Add Remote Device** and enter that ID (on
   the same Wi-Fi, the PC usually lists the Cardputer under "nearby devices",
   so you can just click it). Phones can scan the QR code.
4. The Cardputer shows "*PC* asks". Press **Enter** to accept. It shares
   Notes, Documents and Recordings with the PC.
5. On the PC, accept the three folders when Syncthing offers them and pick
   where they go.

Or go the other way: **Add device by ID** on the Cardputer and type the PC's
ID (Syncthing on the PC: Actions > Show ID), then accept the Cardputer on the PC.

## Using it

In **Sync**:

| | |
| --- | --- |
| Top left | Up to date, Syncing *n*%, Waiting for devices, or Problem |
| *device* asks / *device*: *folder* | Enter accepts, Del ignores. A folder someone shares with you goes into `~/Sync/<name>`. |
| A folder | Enter looks for changes now (it also notices changes by itself) |
| A device | Del twice stops syncing with it |
| Sync now | Runs until everything is up to date, then stops |
| Show my device ID | QR code and ID, to add this Cardputer on another device |
| Runs: ... | When Sync runs (below) |

The status bar shows the sync arrows while Syncthing is running.

Notes now saves to `~/Notes/notes.txt` (older images used `~/notes.txt`; it
moves over the first time). Voice notes are already in `~/Recordings`. Put
anything else you want everywhere in `~/Documents`.

A file deleted or overwritten by another device is kept for 14 days in the
folder's `.stversions`, in case you need it back.

## When it runs

Settings → **Sync** (also in the Sync app):

- **While charging** (default): runs when the Cardputer is on USB power and
  has a network, so it catches up every time you plug it in without costing
  battery.
- **Always on**: runs all the time; changes arrive within seconds, but
  Syncthing keeps the Wi-Fi and CPU a little busier **(measure)**.
- **When asked**: only with **Sync now** or while the Sync app is open.

In every mode it also runs while the Sync app is open, and **Sync now**
works anywhere: it runs until all your online devices are up to date, then
stops (or gives up after 3 minutes if none of them is online, or 10 minutes
in all). It stops a minute after it's no longer wanted.

## How it works

| Piece | What |
| --- | --- |
| `syncthing` package | From Debian. `syncthing@pi.service` is never started at boot; ZeroShell starts and stops it (polkit allows `pi` exactly this unit). |
| `syncthing@.service.d/zeroos.conf` | Small for 512 MB: Go heap limit 48 MB, two cores, low CPU and disk priority, no default folder, no self-upgrade. earlyoom closes it before anything you're using. After start it runs `zeroos-sync setup`. |
| `zeroos-sync` | Python helper that talks to Syncthing's local API (key from Syncthing's own config). `setup` turns off usage and crash reports, syncs one folder at a time, and creates the three folders (`zeroos-notes`, `zeroos-documents`, `zeroos-recordings`) with file watching and 14-day trash-can versioning. Also `status`, `add-device`, `remove-device`, `ignore-device`, `accept-folder`, `ignore-folder`, `rescan`. Works over ssh too. |
| ZeroShell | Decides when Syncthing runs (`sync.mode`), runs "Sync now", and draws the Sync app from `zeroos-sync status` (every few seconds, only while the app is open). |

The Syncthing web page stays on the Cardputer only (`127.0.0.1:8384`); in
Dock mode you can open it in the browser for anything the Sync app doesn't
cover (ignore patterns, receive-only folders, more folders).

## Limits and things to measure

- **Memory**: Syncthing should take about 30-50 MB while running **(measure)**.
  It isn't running most of the time on battery.
- **Battery**: "Always on" keeps connections to your devices open; measure
  its cost before making it the default.
- **Big folders**: the first sync of a big Documents folder takes a while on
  the Cardputer's CPU and SD card. Large media is better left out of it.
- **Conflicts**: editing the same note on two devices before they sync keeps
  both copies (`notes.sync-conflict-....txt`); Files shows both.
