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
STEPS+=",right,right,right,right,shot:02-home-navigate.ppm"
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

(cd "$OUT" && HOME="$DEMO" TZ=UTC "$BIN" --script "$STEPS")
python3 "$HERE/ppm2png.py" "$OUT"
