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

Leaving the camera returns to a page with **Open camera**, where Esc goes
home.

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
- No video recording or QR scanning from ZeroOS yet.
