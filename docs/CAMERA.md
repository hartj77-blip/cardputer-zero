# Camera

The **Camera** app on the full Cardputer Zero (the Lite has no camera) is
M5Stack's own [CameraApp](https://github.com/CardputerZero/CameraApp), the one
their stock image ships: live viewfinder, photos at up to 3280 x 2464, zoom
and pan, and a gallery. It is MIT licensed. ZeroOS installs their released
package (version 1.0.4, checked against a pinned checksum when the image is
built) rather than a rewrite, so what runs is the code M5 tested on this
camera.

## Using it

Pick **Camera** on the home screen. The viewfinder opens straight away.

| Key | Does |
| --- | --- |
| Enter | Take a photo |
| H or 1 | Show every key: zoom, pan, gallery, USB camera |
| Hold 4 or Esc for a second | Leave the camera, back to ZeroOS |

Photos are saved in `~/Pictures/DCIM/Camera`; the **Files** app can see them.

Leaving the camera returns to a page with **Open camera** and **Scan a QR
code**, where Esc goes home.

## QR codes

On that page, choose **Scan a QR code** or press **Q**. (Tether's "scan its QR
code" comes straight here.) A live picture appears; a code in view is read at
once, with a beep, and the picture freezes with the code outlined.

| The code holds | **Enter** | **S** |
| --- | --- | --- |
| A Wi-Fi network (a phone's hotspot code, the label on a router) | Joins it. No typing the password on 46 keys. | Saves the text |
| A link | Opens it in the browser on the monitor, when Dock mode is running | Saves the link |
| A Syncthing device ID (Syncthing on a phone shows its own as a QR code) | Adds that device to Sync | Saves the ID |
| Anything else | Saves the text | Saves the text |

**Esc** scans again; Esc once more leaves the reader. Saved text goes to
`~/Notes/scans.txt`, one line each with the date, so Sync copies it to your
other devices.

To share a phone's hotspot this way: on Android, open the hotspot's settings
and tap the QR icon; then point the Cardputer at the phone.

The reader is ZeroShell's own, separate from M5's camera app (which has no QR
reading). Its camera is on only while the reader is open and the screen is
on: it turns off when a code is read, when the screen turns off and when you
leave.

## How it fits in

ZeroShell starts `/usr/bin/camera_app` and steps aside, as it does for the
Terminal: it stops drawing, the camera app draws on the screen itself, and
ZeroShell takes the screen back when the app exits.

- The camera app is told not to take the keyboard exclusively, so ZeroShell
  still notices key presses and keeps the screen from dimming while you use it.
  ZeroShell ignores what the keys mean until the camera closes.
- If the screen does turn off (nothing pressed for the screen-off time), the
  camera app is closed so the sensor doesn't run down the battery.
- Music from the Media app keeps playing.

## How the QR reader works

| Piece | What |
| --- | --- |
| Live picture | `rpicam-vid` writes raw 640 x 480 frames to a pipe, ten a second. ZeroShell keeps the newest, scales it to 200 x 150 and draws it. |
| Reading | Every third frame, the frame's brightness plane goes to [quirc](https://github.com/dlbeer/quirc) (ISC licence, built into ZeroShell; pinned in `shell/CMakeLists.txt`). |
| One at a time | Only one program can hold the camera, so the reader stops before M5's camera app starts. |
| Simulator | A pretend desk with a card carrying a real QR code, so CI runs the reader end to end. Each time the reader opens the card changes: a Wi-Fi code, a link, plain text. |

## If it says...

- **No camera found**: no sensor answered at boot. That is normal on the Lite.
  On the full model, check the camera's ribbon cable. **Open camera** still
  works, for a USB camera: open it and press U.
- **Camera app missing**: the package could not be installed when the image
  was built (the build log has a line starting `zeroos: WARNING`). Install it
  on the device with
  `sudo apt install ./Camera_1.0.4_m5stack1_arm64.deb` from M5's releases page.

## Limits

- Not yet tried on a device with ZeroOS. The camera app itself is M5's
  released build; starting it from ZeroShell and sharing the screen and
  keyboard with it is new.
- The camera app has its own look (it is not restyled to match ZeroOS).
- No video recording from ZeroOS yet.
- The QR reader is not yet tried on a device either. To confirm: which way
  up its picture is (if wrong, add `--vflip` / `--hflip` to the `rpicam-vid`
  line in `shell/src/hal/linux_hal.cpp`); that `rpicam-vid --codec yuv420
  -o -` sends unpadded 640 x 480 frames (a sheared or green picture means it
  doesn't); how close a code can be before the fixed-focus lens blurs it;
  and its memory and battery cost **(measure)**.
