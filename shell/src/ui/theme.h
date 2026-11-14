#pragma once

#include "lvgl.h"

namespace zs::theme {

constexpr int kScreenW = 320;
constexpr int kScreenH = 170;
constexpr int kStatusH = 20;
constexpr int kContentH = kScreenH - kStatusH;

constexpr uint32_t kBg = 0x0b0f14;
constexpr uint32_t kSurface = 0x161c24;
constexpr uint32_t kSurface2 = 0x1f2731;
constexpr uint32_t kText = 0xe6edf3;
constexpr uint32_t kMuted = 0x8b97a5;
constexpr uint32_t kAccent = 0xff8a1f;   // M5-style orange
constexpr uint32_t kGood = 0x3fb950;
constexpr uint32_t kWarn = 0xd29922;
constexpr uint32_t kBad = 0xf85149;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

// Common styling helpers.
void style_panel(lv_obj_t* o);            // rounded surface card
void style_plain(lv_obj_t* o);            // transparent, no border/padding
lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font = &lv_font_montserrat_12,
                uint32_t color = kText);
void style_focus_list(lv_obj_t* list);    // list buttons with accent focus

}  // namespace zs::theme
