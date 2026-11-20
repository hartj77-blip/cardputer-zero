# Camera

The Cardputer Zero has an 8-megapixel camera on the back (the CardputerZero
Lite doesn't). **Camera** shows what it sees, takes photos and reads QR codes.

| | |
| --- | --- |
| **Enter** | Take a photo. It goes to `~/Pictures` as `photo-<date>-<time>.jpg` (1640 x 1232). In Files, **S** sends it to your phone or PC with Taildrop. |
| A QR code in view | Read at once, with a beep. The picture freezes with the code outlined. |
| **Esc** | Back to the live picture, or home. |

## What a QR code does

| The code holds | **Enter** | **S** |
| --- | --- | --- |
| A Wi-Fi network (a phone's hotspot code, the label on a router) | Joins it. No typing the password on 46 keys. | Saves the text |
| A link | Opens it in the browser on the monitor, when Dock mode is running | Saves the link |
| A Syncthing device ID (Syncthing on a phone shows its own as a QR code) | Adds that device to Sync | Saves the ID |
| Anything else | Saves the text | Saves the text |

Saved text goes to `~/Notes/scans.txt`, one line each with the date, so Sync
copies it to your other devices.

To share a phone's hotspot this way: on Android, open the hotspot's settings
and tap the QR icon; then point the Cardputer at the phone.

## Battery

The camera is on only while the app is open and the screen is on. It turns
off when a code is read, when the screen turns off and when you leave.

## How it works

| Piece | What |
| --- | --- |
| Live picture | `rpicam-vid` writes raw 640 x 480 frames to a pipe, ten a second. ZeroShell keeps the newest, scales it to 200 x 150 and draws it. |
| QR codes | Every third frame, the frame's brightness plane goes to [quirc](https://github.com/dlbeer/quirc) (ISC licence, built into ZeroShell; pinned in `shell/CMakeLists.txt`). |
| Photos | The live picture stops (one program can hold the camera at a time), `rpicam-still` takes the photo after a second for exposure to settle, and the live picture starts again. |
| Simulator | A pretend desk with a card carrying a real QR code, so CI runs the reader end to end. Each time the camera opens the card changes: a Wi-Fi code, a link, plain text. |

## To confirm on hardware

- Which way up the picture is. If it's upside down or mirrored, add
  `--vflip` / `--hflip` to the `rpicam-vid` and `rpicam-still` lines in
  `shell/src/hal/linux_hal.cpp`.
- That `rpicam-vid --codec yuv420 -o -` sends unpadded frames at 640 x 480
  (a sheared or green picture means it doesn't).
- How close a code can be before the fixed-focus lens blurs it, and how long
  reading a frame takes **(measure)**.
- Memory and battery cost of the live picture **(measure)**.
