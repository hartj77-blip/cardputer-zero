#!/usr/bin/env bash
# Render every built-in screen with the headless build and save PNGs.
#   tools/screenshots.sh <path/to/headless/zeroshell> <out-dir>
# Uses a throwaway HOME with demo files so Files/Notes have content.
set -euo pipefail
BIN=$(realpath "$1")
OUT=$(realpath -m "$2")
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
DEMO=$(mktemp -d)
trap 'rm -rf "$DEMO"' EXIT
mkdir -p "$DEMO"/{Documents,Music,Pictures,Projects}
printf 'Shopping: solder, heat shrink, 18650s\n\nIdeas:\n- LoRa chat cap\n- IR macro for the TV\n' > "$DEMO/notes.txt"
head -c 48213 /dev/zero > "$DEMO/Pictures/garden.jpg"
echo "# my project" > "$DEMO/Projects/README.md"

STEPS="shot:01-home.ppm"
STEPS+=",right,right,right,right,right,right,right,right,right,right,right,right,shot:02-home-navigate.ppm"
STEPS+=",enter,wait:300,shot:03-compass.ppm"
STEPS+=",open:battery,shot:04-battery.ppm"
STEPS+=",open:wifi,shot:05-wifi.ppm"
STEPS+=",down,enter,wait:200,type:hunter2,shot:06-wifi-password.ppm"
STEPS+=",open:ir,right,shot:07-ir-remote.ppm"
STEPS+=",open:files,shot:08-files.ppm"
STEPS+=",open:notes,shot:09-notes.ppm"
STEPS+=",open:clock,key: ,wait:2300,shot:10-clock.ppm"
STEPS+=",open:sysmon,wait:1200,shot:11-system.ppm"
STEPS+=",right,wait:1200,shot:16-system-memory.ppm"
STEPS+=",open:media,down,down,enter,wait:2600,shot:13-media-music.ppm"
STEPS+=",right,key:r,wait:3200,shot:14-media-recording.ppm"
STEPS+=",key:r,wait:300,shot:15-media-voice-notes.ppm"
STEPS+=",open:settings,down,down,shot:12-settings.ppm"
STEPS+=",open:settings,down,down,down,down,down,down,down,down,wait:300,shot:17-settings-power.ppm"
STEPS+=",home,wait:31000,shot:18-screen-dimmed.ppm"
STEPS+=",right,open:dock,wait:600,shot:19-dock-no-monitor.ppm"
STEPS+=",hdmi:1,wait:1500,shot:20-dock-monitor.ppm"
STEPS+=",enter,wait:1500,shot:21-docked.ppm,hdmi:0,wait:4500"
STEPS+=",open:remote,wait:1500,shot:22-remote-signin.ppm"
STEPS+=",wait:9000,right,shot:23-remote-devices.ppm"
STEPS+=",open:terminal,wait:300,shot:24-terminal.ppm"
STEPS+=",enter,wait:300,shot:34-terminal-preview.ppm,esc"
STEPS+=",home,wait:300,left,down,shot:35-home-dock.ppm,esc"
STEPS+=",open:sync,wait:9000,shot:25-sync.ppm"
STEPS+=",enter,wait:2500,shot:26-sync-accepted.ppm"
STEPS+=",down,down,down,down,down,down,enter,wait:300,shot:27-sync-id.ppm"
STEPS+=",open:gps,wait:2000,shot:28-gps-searching.ppm"
STEPS+=",wait:6000,shot:29-gps-fix.ppm"
STEPS+=",open:mesh,wait:300,shot:30-mesh-off.ppm"
STEPS+=",enter,wait:3500,shot:31-mesh-chat.ppm"
STEPS+=",type:On my way,enter,wait:4500,shot:32-mesh-reply.ppm"
STEPS+=",tab,wait:300,shot:33-mesh-info.ppm"
STEPS+=",open:media,right,right,wait:300,shot:36-jellyfin-sign-in.ppm"
STEPS+=",enter,wait:300,type:nas.local,shot:37-jellyfin-server.ppm"
STEPS+=",enter,wait:300,type:john,enter,wait:300,type:secret,enter,wait:2500,shot:38-jellyfin-menu.ppm"
STEPS+=",down,down,enter,wait:2500,shot:39-jellyfin-albums.ppm"
STEPS+=",enter,wait:2500,down,enter,wait:2600,shot:40-jellyfin-playing.ppm"
STEPS+=",home,wait:246000,up,open:media,right,right,wait:300,shot:41-jellyfin-screen-was-off.ppm,key:s"
STEPS+=",open:camera,wait:300,shot:42-camera-preview.ppm,esc,wait:300,shot:43-camera.ppm"
STEPS+=",open:car,wait:300,shot:44-car.ppm"
STEPS+=",enter,wait:2500,shot:45-car-copying.ppm,wait:6000,shot:46-car-usb-in-use.ppm"
STEPS+=",down,enter,wait:1500,shot:47-car-bluetooth.ppm,enter,wait:1500,enter"

(cd "$OUT" && HOME="$DEMO" TZ=UTC "$BIN" --script "$STEPS")
python3 "$HERE/ppm2png.py" "$OUT"
