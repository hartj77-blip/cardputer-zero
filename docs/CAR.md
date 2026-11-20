# In the car

The **Car** app gives two ways to play music through a car stereo. Neither is
CarPlay or Android Auto: those only accept a phone certified by Apple or
Google, and the Cardputer cannot stand in for one.

| | USB stick mode | Bluetooth |
| --- | --- | --- |
| Works with | Any stereo that plays music from a USB stick | Stereos, speakers and headphones with Bluetooth music |
| What plays | Files in `~/Music` | Anything the Media app plays, Jellyfin included |
| Who is the player | The car | The Cardputer |
| Car buttons | All of them (it is the car's own player) | Play, pause, next, previous |
| Cable | One USB-C cable, data and power | None for sound; any USB charger for power |

## USB stick mode

1. Set the **Host / Slave switch** on the left side of the Cardputer to
   **Slave**. In Slave the USB-A port, the other USB-C port and Ethernet are
   off; that is how the board is wired.
2. Open **Car** and press Enter on **USB stick mode**. The first time (and
   whenever `~/Music` has changed) it copies the music into a disk image:
   **Copying music 43%**. Then it says **on**.
3. Plug the **right-hand USB-C port** into the car's USB socket. When the car
   has taken it up the app says **in use**. Pick USB as the source on the
   stereo and use the car's buttons.

Enter on the row again turns it off. Set the switch back to **Host** to use
USB devices and Ethernet again.

What the car sees is a read-only FAT32 stick called `MUSIC` with the same
folders as `~/Music`, files in name order. It is a copy
(`/var/lib/zeroos/car/music.img`), so it needs as much free space again as
the music takes, and the car cannot change your files. Only music files are
copied: mp3, m4a, aac, wma, wav, flac, ogg, opus. An older stereo may only
play mp3 (and perhaps wma or aac) and skip the rest.

## Bluetooth

In **Car**, under **Bluetooth**:

- **Scan for devices**: put the car, speaker or headphones in pairing mode,
  then scan (8 seconds). Press Enter on it in the list to pair and connect.
- **Let the car find me**: for cars that search for a phone from their own
  screen. The Cardputer is visible as `cardputer` for 2 minutes; pick it on
  the car's screen.
- Enter on a paired device connects or disconnects it. **X** forgets it.

Once connected, sound from the Media app goes to that device, and its play,
pause, next and previous buttons drive the play queue, also with the screen
off. Pressing play with nothing queued starts `~/Music` from the top.

A paired car reconnects by itself when it starts, as long as the Cardputer's
Bluetooth is on. **Settings > Bluetooth auto-off** turns Bluetooth off after
a while with nothing connected, so set it to **Never** if the Cardputer lives
in the car.

## Power

A car's USB socket often gives only 0.5 A (2.5 W), about what the Cardputer
uses when idle, so it may hold its charge rather than gain any. M5 says to
charge through "USB-C2" and recommends 5 V at 2 A; a 12 V socket charger
gives that. Whether the right-hand (data) port also charges is one of the
things still to check on a device.

## How it works

| Piece | What it does |
| --- | --- |
| `zeroos-usb-stick.service` | Runs `/usr/lib/zeroos/usb-stick` as root: builds the image with `sfdisk`, `mkfs.vfat` and mtools, then sets up a USB mass-storage gadget (configfs) on the `dwc2` controller. Progress goes to `/run/zeroos/usb-stick.state`. |
| Bluetooth | `bluetoothctl` pairs, trusts and connects; PipeWire's Bluetooth module (`libspa-0.2-bluetooth`) carries the sound; `pipewire-alsa` routes sox's playback of local files through PipeWire as well. |
| Car buttons | BlueZ presents a connected device's buttons as a Linux input device (via `uinput`). ZeroShell reads any input device that has media keys but no letters, so a USB multimedia remote works too. |

## Not checked on a device yet

None of this has run on a Cardputer or in a car. In particular:

- that the right-hand USB-C port enumerates as a device with the stock
  `dtoverlay=dwc2` once the switch is on Slave;
- whether a given car accepts the stick (old stereos can be picky about
  size, folder depth and file types);
- whether the data port charges the battery;
- pairing with a real car, and that sound moves to it when it connects;
- that the car's buttons arrive as media keys.

## Limits

- The car's screen does not show the track name over Bluetooth.
- No iPod emulation, so a stereo that only knows "iPod" on its USB socket
  will not see the Cardputer.
- Pairing and scanning freeze the screen while they run (up to 40 seconds for
  a first pairing).
