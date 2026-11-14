// Small helpers shared by the built-in apps.
#pragma once

#include <string>

#include "hal/hal.h"
#include "lvgl.h"
#include "ui/app.h"
#include "ui/theme.h"

namespace zs {

// A label + value row inside a panel; returns the value label.
lv_obj_t* kv_row(lv_obj_t* parent, const char* key, const char* value);

// Horizontal progress bar styled for the dark theme.
lv_obj_t* thin_bar(lv_obj_t* parent, int w, int h, uint32_t color);

// Focusable list in the app area, styled; caller adds buttons then calls finish_list().
lv_obj_t* make_list(lv_obj_t* parent, int x, int y, int w, int h);
lv_obj_t* list_button(lv_obj_t* list, const char* symbol, const char* text, lv_event_cb_t cb, void* user);
void finish_list(lv_obj_t* list);

std::string fmt(const char* f, ...);

std::string home_dir();

}  // namespace zs
