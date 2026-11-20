#!/usr/bin/env bash
# Regenerate shell/src/fonts/zs_font_*.c: the Ubuntu typeface plus the Font
# Awesome symbols LVGL uses (LV_SYMBOL_*) and the launcher's app icons.
#   tools/fonts.sh <dir with Ubuntu-Regular.ttf and Ubuntu-Medium.ttf> <lvgl checkout>
# Needs Node (npx lv_font_conv). The typeface is under the Ubuntu Font Licence
# (src/fonts/UBUNTU-FONT-LICENCE.txt).
set -euo pipefail
TTF=$(realpath "$1")
FA=$(realpath "$2")/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff
OUT=$(cd "$(dirname "$0")/../src/fonts" && pwd)
TEXT=0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026
# LVGL's own symbol set (lv_symbol_def.h).
SYM=61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650
# App icons, in the 12 and 16 px fonts (ZS_ICON_* in ui/theme.h).
ICONS=0xf120,0xf14e,0xf017,0xf108,0xf086,0xf2db,0xf249,0xf00a,0xf7c0,0xf6ff,0xf2f1,0xf0ac,0xf002,0xf3c5,0xf519,0xf1de,0xf3fd,0xf109,0xf030,0xf3cd
gen() {  # size weight [extra symbols]
    npx --yes lv_font_conv --no-compress --no-prefilter --bpp 4 --size "$1" \
        --font "$TTF/Ubuntu-$2.ttf" -r "$TEXT" --font "$FA" -r "$SYM${3:+,$3}" \
        --format lvgl --lv-include lvgl.h --force-fast-kern-format -o "$OUT/zs_font_$1.c"
}
gen 12 Medium "$ICONS"
gen 14 Medium
gen 16 Medium "$ICONS"
gen 20 Medium
gen 28 Regular
