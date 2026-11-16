# GPS and LoRa modules

The Cardputer Zero has two expansion ports: the 14-pin **EXT** header on the
back and the 4-pin **Grove** port. ZeroOS supports:

- **M5Stack Cap LoRa-1262** on EXT: an SX1262 LoRa radio (868-923 MHz) and
  an AT6668 GPS. It makes the Cardputer a [Meshtastic](https://meshtastic.org)
  node: text messages over radio, kilometres apart, with no phone signal or
  internet.
- **Any NMEA GPS** on the Grove port (in its default UART mode), or a USB GPS
  dongle.

Power the Cardputer off before plugging in a module; the ports aren't hot-swap.

## GPS

Open **GPS**. It turns on the expansion port's 5 V, waits for satellites
(about 30 s outdoors the first time; longer indoors) and shows:

- latitude and longitude, altitude, speed, heading, accuracy
- satellites used of those seen, and UTC time

**Enter** saves the spot to `~/Documents/waypoints.txt` (Sync copies it to your
PC). Leaving the app turns the 5 V off again unless the mesh is running.

## Mesh (Meshtastic)

Open **Mesh** and choose **Turn on mesh**. The radio starts in a few seconds,
and Mesh switches to its chat page:

- **On the Cardputer:** messages on the primary channel scroll above a text
  box; type and press **Enter** to send to everyone. Messages sent only to you
  are orange. **Tab** switches between chat and the info page (QR code, on/off).
  A message that arrives while you're in another app shows at the top of the
  screen with a beep.
- **From your phone:** scan the QR code; it opens the Meshtastic web client
  on the Cardputer (accept the certificate warning once). Or in the
  Meshtastic app, add a node by IP address (the address under the QR code).
  The phone and Cardputer need the same Wi-Fi, a phone hotspot, or Tailscale.
- **On a monitor:** in Dock mode, **Open on monitor** opens the web client in
  the browser.

The first time, set your **region** (for example US or EU_868) in the web
client's radio settings. The radio doesn't transmit until a region is set,
and it must match where you are and what other nodes use.

Still to do: direct messages to one node, other channels, and keeping
messages after a restart (they're kept in memory, the last 200).

**Turn off mesh** stops the radio and the expansion port's 5 V. The mesh
uses its own battery: leave it off when you don't need it **(measure)**.

## How it works

| Piece | What |
| --- | --- |
| EXT pins | SPI0 on G9/G10/G11 with chip select CE1 (G7, `/dev/spidev0.1`; the LCD uses CE0), GPIO 22/23/26 for the radio's BUSY, IRQ and RESET, UART on G14/G15, I2C1 on G2/G3, 5 V out (off by default). From M5's pin map and [trail-mate](https://github.com/vicliu624/trail-mate)'s CardputerZero port. |
| Grove | UART on G14/G15 by default (G4 low), or I2C1 (G4 high). Same UART as EXT. |
| 5 V | An LED-class supply under `/sys/class/leds/` (`ext_5v_out` on M5's image). ZeroShell switches every `*5v*` one; `power-perms` lets it. |
| `config.txt`, `cmdline.txt` | `enable_uart=1`; no serial console on the UART, so it's free for GPS. |
| `gpsd` | Reads `/dev/serial0` at 115200 (and USB GPS dongles). Socket-activated: it starts when the GPS app or meshtasticd connects and stops reading the receiver when they stop watching. Config: `/etc/default/gpsd`. |
| `meshtasticd` | Meshtastic's Linux daemon, from Meshtastic's Debian repository (beta channel). Off at boot; the Mesh app starts it (polkit allows `pi` exactly this unit). Radio config: `/etc/meshtasticd/config.d/zeroos-cap-lora-1262.yaml` (SX1262 on `spidev0.1`, RESET 26, BUSY 22, IRQ 23, DIO2 as RF switch, DIO3 TCXO). It takes its position from gpsd, serves the web client on port 9443 and the app API on TCP 4403. |
| Chat in ZeroShell | A small client for that API (`hal/meshtastic.cpp`, hand-written protobuf, no library): asks for the node list, reads text messages, sends to the primary channel. |

## To confirm on hardware

- The radio pins and `/dev/spidev0.1`. If the Mesh app says **No radio**,
  check `journalctl -u meshtasticd` and the pins in the config file.
- The RF switch: M5's page drives it from an IO expander on the Cardputer
  ADV; trail-mate uses DIO2 on the Zero, as we do.
- Whether `ext_5v_out` (and a Grove 5 V switch) appear under
  `/sys/class/leds/`.
- That the UART is `/dev/serial0` (the mini UART, since Bluetooth uses the
  main one) and the GPS answers at 115200.
- Memory and battery cost of meshtasticd **(measure)**.
