#include "apps/common.h"

#include <cstdarg>
#include <cstdlib>

namespace zs {

using namespace theme;

lv_obj_t* kv_row(lv_obj_t* parent, const char* key, const char* value) {
    lv_obj_t* row = lv_obj_create(parent);
    style_plain(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* k = label(row, key, &lv_font_montserrat_12, kMuted);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* v = label(row, value, &lv_font_montserrat_12, kText);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    return v;
}

lv_obj_t* thin_bar(lv_obj_t* parent, int w, int h, uint32_t color) {
    lv_obj_t* b = lv_bar_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, c(kSurface2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, c(color), LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, h / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
    return b;
}

lv_obj_t* make_list(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* list = lv_list_create(parent);
    lv_obj_set_pos(list, x, y);
    lv_obj_set_size(list, w, h);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    return list;
}

lv_obj_t* list_button(lv_obj_t* list, const char* symbol, const char* text, lv_event_cb_t cb, void* user) {
    lv_obj_t* b = lv_list_add_button(list, symbol, text);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    shell_add_focus(b);
    return b;
}

void finish_list(lv_obj_t* list) { style_focus_list(list); }

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? h : "/tmp";
}

}  // namespace zs
