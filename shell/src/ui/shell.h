#pragma once

#include <memory>
#include <string>

#include "lvgl.h"
#include "ui/app.h"

namespace zs {

class Shell {
public:
    // Builds the status bar and home screen on the active display, and hooks
    // the keypad so the shell sees every key before LVGL does.
    void start(lv_indev_t* keypad);

    void open_app(const std::string& id);
    void go_home();
    void toast(const std::string& msg);
    void add_focus(lv_obj_t* obj);

    // Called by the indev hook on each key press. True = consumed.
    bool handle_key(uint32_t key);

    static Shell& get();

    // Power manager: redraw the dim/blank overlay and pace the UI timer.
    void screen_changed();

private:
    void apply_pending();
    void build_statusbar();
    void update_statusbar();
    void switch_to(std::unique_ptr<App> app, const std::string& title);
    static void tick_cb(lv_timer_t* t);

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* sb_time_ = nullptr;
    lv_obj_t* sb_title_ = nullptr;
    lv_obj_t* sb_icons_ = nullptr;
    lv_obj_t* toast_ = nullptr;
    lv_obj_t* blank_ = nullptr;     // simulator only: stands in for the backlight
    lv_timer_t* tick_timer_ = nullptr;
    uint32_t toast_until_ = 0;
    lv_indev_t* keypad_ = nullptr;
    std::unique_ptr<App> app_;
    std::string app_id_;
    std::string pending_;
    lv_group_t* group_ = nullptr;
};

std::unique_ptr<App> make_launcher();

}  // namespace zs
