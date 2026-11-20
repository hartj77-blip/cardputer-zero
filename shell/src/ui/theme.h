// Look and feel: Ubuntu's desktop (GNOME with the Yaru dark theme), cut down
// to 320 x 170. Black top bar, aubergine wallpaper, orange accent, the Ubuntu
// typeface.
#pragma once

#include "lvgl.h"

// Ubuntu typeface with LVGL's symbols; see tools/fonts.sh.
LV_FONT_DECLARE(zs_font_12)   // 12 and 16 also carry the ZS_ICON_* app icons
LV_FONT_DECLARE(zs_font_14)
LV_FONT_DECLARE(zs_font_16)
LV_FONT_DECLARE(zs_font_20)
LV_FONT_DECLARE(zs_font_28)

// App icons (Font Awesome), in zs_font_12 and zs_font_16.
#define ZS_ICON_TERMINAL   "\xEF\x84\xA0"   // U+F120
#define ZS_ICON_COMPASS    "\xEF\x85\x8E"   // U+F14E
#define ZS_ICON_CLOCK      "\xEF\x80\x97"   // U+F017
#define ZS_ICON_DESKTOP    "\xEF\x84\x88"   // U+F108
#define ZS_ICON_CHAT       "\xEF\x82\x86"   // U+F086
#define ZS_ICON_CHIP       "\xEF\x8B\x9B"   // U+F2DB
#define ZS_ICON_NOTE       "\xEF\x89\x89"   // U+F249
#define ZS_ICON_GRID       "\xEF\x80\x8A"   // U+F00A
#define ZS_ICON_SATELLITE  "\xEF\x9F\x80"   // U+F7C0
#define ZS_ICON_NETWORK    "\xEF\x9B\xBF"   // U+F6FF
#define ZS_ICON_SYNC       "\xEF\x8B\xB1"   // U+F2F1
#define ZS_ICON_GLOBE      "\xEF\x82\xAC"   // U+F0AC
#define ZS_ICON_SEARCH     "\xEF\x80\x82"   // U+F002
#define ZS_ICON_PIN        "\xEF\x8F\x85"   // U+F3C5
#define ZS_ICON_TOWER      "\xEF\x94\x99"   // U+F519
#define ZS_ICON_SLIDERS    "\xEF\x87\x9E"   // U+F1DE
#define ZS_ICON_GAUGE      "\xEF\x8F\xBD"   // U+F3FD
#define ZS_ICON_LAPTOP     "\xEF\x84\x89"   // U+F109
#define ZS_ICON_CAMERA     "\xEF\x80\xB0"   // U+F030
#define ZS_ICON_PHONE      "\xEF\x8F\x8D"   // U+F3CD

namespace zs::theme {

constexpr int kScreenW = 320;
constexpr int kScreenH = 170;
constexpr int kStatusH = 20;
constexpr int kContentH = kScreenH - kStatusH;

// Yaru dark.
constexpr uint32_t kBg = 0x1e1e1e;        // window background
constexpr uint32_t kSurface = 0x303030;   // cards, list rows
constexpr uint32_t kSurface2 = 0x424242;  // raised: buttons, troughs
constexpr uint32_t kText = 0xf7f7f7;
constexpr uint32_t kMuted = 0xaea79f;     // Ubuntu warm grey
constexpr uint32_t kAccent = 0xe95420;    // Ubuntu orange
constexpr uint32_t kOnAccent = 0xffffff;  // text on the accent
constexpr uint32_t kGood = 0x4bc16b;
constexpr uint32_t kWarn = 0xf99b11;
constexpr uint32_t kBad = 0xf0524f;
constexpr uint32_t kInfo = 0x19b6ee;
constexpr uint32_t kTopBar = 0x000000;
constexpr uint32_t kAubergine = 0x2c001e;       // wallpaper, terminal
constexpr uint32_t kAubergineLight = 0x77216f;
constexpr uint32_t kTerminalBg = 0x300a24;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

// Common styling helpers.
void style_panel(lv_obj_t* o);            // rounded surface card
void style_plain(lv_obj_t* o);            // transparent, no border/padding
lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font = &zs_font_12,
                uint32_t color = kText);
void style_focus_list(lv_obj_t* list);    // list buttons with accent focus

}  // namespace zs::theme
