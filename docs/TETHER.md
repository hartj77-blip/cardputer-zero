# Tethering: online through your phone

Away from Wi-Fi, the Cardputer Zero can use your phone's mobile data three
ways. **Tether** shows which one is in use and sets up the Bluetooth one.

| | Speed | Battery | Setup |
| --- | --- | --- | --- |
| **Wi-Fi hotspot** | Fastest | Heaviest on the phone | Join it like any Wi-Fi network |
| **USB** | Fast | The Cardputer charges the phone a little | Plug in, switch on |
| **Bluetooth** | Slow (about 1-2 Mbit/s): fine for SSH, chat, mail, sync | Lightest on both | Pair once |

Phone data is marked as metered on the Bluetooth connection, and
NetworkManager usually recognises phone hotspots and USB as metered by itself.

## Wi-Fi hotspot

Turn on the hotspot on the phone, then either:

- open **Wi-Fi** and pick it, or
- open **Camera** and point it at the hotspot's QR code (Android: hotspot
  settings, QR icon), then press **Enter**. See [CAMERA.md](CAMERA.md).

## USB

Plug the phone into the Cardputer's USB-A port (or a USB-C port in host
mode), then:

- **Android:** Settings > Network > Hotspot & tethering > **USB tethering**.
- **iPhone:** turn on **Personal Hotspot**, and tap **Trust** when the phone
  asks about this computer.

That's all: the Cardputer goes online by itself in a few seconds, and Tether
shows "Online through *phone* (USB)".

## Bluetooth

First time:

1. On the phone, turn on Bluetooth tethering (Android: Hotspot & tethering >
   **Bluetooth tethering**; iPhone: **Personal Hotspot**), and leave the
   phone's Bluetooth settings open so it can be found.
2. In **Tether**, choose **Add a phone over Bluetooth**. It searches for
   about ten seconds; pick your phone.
3. Both screens show a six-digit number. If they match, tap **Pair** on the
   phone. (If they don't, tap Cancel: something else is trying to pair.)
4. The Cardputer goes online through the phone.

After that the phone is listed in Tether:

| | |
| --- | --- |
| **Enter** on the phone | Go online through it, or offline |
| **Del** twice | Forget the phone (unpair) |

It doesn't connect by itself: Bluetooth turns off after ten minutes with
nothing connected (Settings), so going online is one key when you want it.

## In the top bar

A phone icon shows while the Cardputer is online through USB or Bluetooth.

## How it works

| Piece | What |
| --- | --- |
| USB | The kernel makes a network interface for a phone that shares its connection (`rndis_host` or `cdc_ncm` for Android, `ipheth` for iPhone, with `usbmuxd` for the iPhone's Trust question). NetworkManager brings it up. ZeroShell only reports it. |
| Bluetooth | BlueZ pairs; NetworkManager connects to the phone's network service (a `bluetooth` connection of type `panu`, named `zeroos-tether-<address>`). |
| `/usr/bin/zeroos-tether` | A small Python helper for ZeroShell: `status`, `search`, `pair`, `connect`, `disconnect`, `forget`. Pairing drives `bluetoothctl`, passes the six-digit number to the screen and accepts on the Cardputer's side; you confirm on the phone. |
| ZeroShell | Runs the helper in the background and reads its output, so the screen doesn't freeze during a search or pairing. |

From a terminal:

```sh
zeroos-tether status
zeroos-tether search
zeroos-tether pair AA:BB:CC:DD:EE:FF
```

## To confirm on hardware

- Pairing with an Android phone and an iPhone: the wording of
  `bluetoothctl`'s questions, and that the number appears on both.
- That NetworkManager's Bluetooth support is in the image's
  `network-manager` package (if `zeroos-tether connect` says it couldn't set
  up the connection, it isn't).
- That `pi` may use BlueZ (the image adds `pi` to the `bluetooth` group).
- USB: that the phone's interface comes up without help, on the USB-A port
  and on a USB-C port in host mode.
- Speed and battery cost of each way **(measure)**.
