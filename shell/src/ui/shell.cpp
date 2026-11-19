#include "ui/shell.h"

#include <ctime>

#include "hal/hal.h"
#include "platform/platform.h"
#include "ui/power.h"
#include "ui/remote.h"
#include "ui/sync.h"
#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {
lv_indev_read_cb_t g_orig_read = nullptr;
uint32_t g_last_key = 0;
lv_indev_state_t g_last_state = LV_INDEV_STATE_RELEASED;
bool g_consumed = false;      // the held key belongs to the shell, not LVGL
bool g_swallowed = false;     // the held key woke the screen; nothing else sees it
uint32_t g_press_ms = 0, g_repeat_ms = 0;
constexpr uint32_t kRepeatDelay = 400, kRepeatEvery = 80;

// Wraps the keypad driver: on each new press, offer the key to the shell first.
// Keys the shell consumes never reach LVGL, so the shell repeats them itself
// while held (the TCA8418 driver has no kernel auto-repeat). Keys passed on
// to widgets get LVGL's own long-press repeat.
void keypad_hook(lv_indev_t* indev, lv_indev_data_t* data) {
    g_orig_read(indev, data);
    bool pressed = data->state == LV_INDEV_STATE_PRESSED;
    bool new_press = pressed && (g_last_state == LV_INDEV_STATE_RELEASED || data->key != g_last_key);
    if (pressed) g_last_key = data->key;
    g_last_state = data->state;
    if (new_press && power().on_key(lv_tick_get())) {
        g_swallowed = true;
    } else if (new_press) {
        g_press_ms = g_repeat_ms = lv_tick_get();
        g_consumed = Shell::get().handle_key(data->key);
    } else if (pressed && g_consumed && lv_tick_elaps(g_press_ms) > kRepeatDelay &&
               lv_tick_elaps(g_repeat_ms) > kRepeatEvery) {
        g_repeat_ms = lv_tick_get();
        Shell::get().handle_key(data->key);
    } else if (!pressed) {
        g_consumed = false;
        g_swallowed = false;
    }
    if (pressed && (g_consumed || g_swallowed)) data->state = LV_INDEV_STATE_RELEASED;  // LVGL never sees it
}

const char* battery_symbol(int pct) {
    if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
    if (pct >= 65) return LV_SYMBOL_BATTERY_3;
    if (pct >= 40) return LV_SYMBOL_BATTERY_2;
    if (pct >= 15) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}
}  // namespace

Shell& Shell::get() {
    static Shell s;
    return s;
}

void Shell::start(lv_indev_t* keypad) {
    keypad_ = keypad;
    if (keypad_) {
        g_orig_read = lv_indev_get_read_cb(keypad_);
        lv_indev_set_read_cb(keypad_, keypad_hook);
    }

    // Dark theme with Ubuntu orange for focus, sliders, cursors.
    lv_display_t* disp = lv_display_get_default();
    lv_theme_t* th = lv_theme_default_init(disp, c(kAccent), c(kAccent), true, &zs_font_12);
    lv_display_set_theme(disp, th);

    screen_ = lv_screen_active();
    lv_obj_set_style_bg_color(screen_, c(kBg), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen_, c(kText), 0);
    lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    build_statusbar();

    content_ = lv_obj_create(screen_);
    style_plain(content_);
    lv_obj_set_size(content_, kScreenW, kContentH);
    lv_obj_set_pos(content_, 0, kStatusH);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    toast_ = lv_label_create(screen_);
    // Notifications drop from under the clock, as on Ubuntu.
    lv_obj_set_style_bg_color(toast_, c(kSurface2), 0);
    lv_obj_set_style_bg_opa(toast_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast_, 11, 0);
    lv_obj_set_style_pad_hor(toast_, 10, 0);
    lv_obj_set_style_pad_ver(toast_, 4, 0);
    lv_obj_set_style_border_color(toast_, c(0x5a5a5a), 0);
    lv_obj_set_style_border_width(toast_, 1, 0);
    lv_obj_set_style_max_width(toast_, kScreenW - 16, 0);
    lv_obj_align(toast_, LV_ALIGN_TOP_MID, 0, kStatusH + 4);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);

    // Simulator stand-in for the backlight: a black layer over everything.
    blank_ = lv_obj_create(lv_layer_top());
    style_plain(blank_);
    lv_obj_set_size(blank_, kScreenW, kScreenH);
    lv_obj_set_style_bg_color(blank_, c(0x000000), 0);
    lv_obj_set_style_bg_opa(blank_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(blank_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(blank_, LV_OBJ_FLAG_HIDDEN);

    power().start();
    go_home();
    apply_pending();
    tick_timer_ = lv_timer_create(tick_cb, 100, this);
    update_statusbar();
}

// Ubuntu's top bar: what's open on the left, date and time in the middle,
// system status on the right.
void Shell::build_statusbar() {
    lv_obj_t* bar = lv_obj_create(screen_);
    style_plain(bar);
    lv_obj_set_size(bar, kScreenW, kStatusH);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, c(kTopBar), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 7, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    sb_title_ = label(bar, "", &zs_font_12, kText);
    lv_obj_align(sb_title_, LV_ALIGN_LEFT_MID, 0, 0);

    // Home has no title: show GNOME's workspace pill instead.
    sb_home_ = lv_obj_create(bar);
    style_plain(sb_home_);
    lv_obj_set_size(sb_home_, 26, 6);
    lv_obj_align(sb_home_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(sb_home_, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t* d = lv_obj_create(sb_home_);
        style_plain(d);
        lv_obj_set_size(d, i == 0 ? 16 : 6, 6);
        lv_obj_set_pos(d, i == 0 ? 0 : 20, 0);
        lv_obj_set_style_radius(d, 3, 0);
        lv_obj_set_style_bg_color(d, c(i == 0 ? kText : 0x6e6e6e), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }

    sb_time_ = label(bar, "--:--", &zs_font_12, kText);
    lv_obj_align(sb_time_, LV_ALIGN_CENTER, 0, 0);

    sb_icons_ = label(bar, "", &zs_font_12, kText);
    lv_obj_align(sb_icons_, LV_ALIGN_RIGHT_MID, 0, 0);
}

void Shell::update_statusbar() {
    char buf[96];
    auto& h = hal();
    BatteryInfo b = h.battery();
    NetStatus n = h.net();
    std::string icons;
    if (h.muted()) icons += LV_SYMBOL_MUTE "  ";
    if (h.record_state().recording) icons += LV_SYMBOL_STOP " REC  ";
    else if (h.playback().playing) icons += LV_SYMBOL_AUDIO "  ";
    if (n.usb_gadget) icons += LV_SYMBOL_USB "  ";
    if (!n.tailscale_ip.empty()) icons += "TS  ";
    if (sync_running_cached()) icons += LV_SYMBOL_LOOP "  ";
    if (n.bt_enabled) icons += LV_SYMBOL_BLUETOOTH "  ";
    if (n.wifi_connected) icons += LV_SYMBOL_WIFI "  ";
    if (b.charging) icons += LV_SYMBOL_CHARGE " ";
    icons += battery_symbol(b.percent < 0 ? 0 : b.percent);
    if (b.percent >= 0) {
        snprintf(buf, sizeof buf, " %d%%", b.percent);
        icons += buf;
    }
    int left = power().minutes_left();
    if (power().show_time_left() && !b.external_power && left >= 0) {
        snprintf(buf, sizeof buf, " %d:%02d", left / 60, left % 60);
        icons += buf;
    }
    lv_label_set_text(sb_icons_, icons.c_str());
    lv_obj_set_style_text_color(sb_icons_, c(b.percent >= 0 && b.percent < 15 && !b.charging ? kBad : kText), 0);

    bool home = app_id_ == "home";
    if (home) lv_obj_remove_flag(sb_home_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(sb_home_, LV_OBJ_FLAG_HIDDEN);

    // The clock gives way as the bar fills: first it drops the date, then it
    // moves next to the status icons, and last it takes the title's place.
    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    lv_obj_update_layout(sb_icons_);
    lv_obj_update_layout(sb_title_);
    const int gap = 8;
    int left_end = home ? 26 : lv_obj_get_width(sb_title_);
    int right_start = lv_obj_get_x(sb_icons_);
    auto fits = [&](const char* f) {
        strftime(buf, sizeof buf, f, &lt);
        lv_label_set_text(sb_time_, buf);
        lv_obj_align(sb_time_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_update_layout(sb_time_);
        int x = lv_obj_get_x(sb_time_);
        return x >= left_end + gap && x + lv_obj_get_width(sb_time_) + gap <= right_start;
    };
    bool title = true;
    if (!fits("%b %-d  %H:%M") && !fits("%H:%M")) {
        int x = right_start - gap - lv_obj_get_width(sb_time_);
        title = x >= left_end + gap;
        lv_obj_align(sb_time_, LV_ALIGN_LEFT_MID, title ? x : 0, 0);
    }
    if (title || home) lv_obj_remove_flag(sb_title_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(sb_title_, LV_OBJ_FLAG_HIDDEN);
    if (!title && home) lv_obj_add_flag(sb_home_, LV_OBJ_FLAG_HIDDEN);
}

void Shell::tick_cb(lv_timer_t* t) {
    auto* s = static_cast<Shell*>(lv_timer_get_user_data(t));
    hal().poll();
    power().tick(lv_tick_get());
    remote_tick(lv_tick_get());
    sync_tick(lv_tick_get());
    // New mesh message while you're elsewhere: show it.
    static int mesh_seen = 0, mesh_n = 0;
    if (++mesh_n % 5 == 0) {
        int seq = hal().mesh_seq();
        if (seq > mesh_seen) {
            auto msgs = hal().mesh_messages();
            if (s->current_app() != "mesh" && !msgs.empty() && !msgs.back().mine) {
                const MeshMessage& m = msgs.back();
                s->toast(LV_SYMBOL_ENVELOPE " " + m.from + ": " + m.text.substr(0, 60));
                hal().beep(1320, 60);
            }
        }
        mesh_seen = seq;
    }
    static int n = 0;
    // Terminal closed (exit, or tmux detach with Alt+`): take the screen back.
    if (s->handoff_ && lv_tick_elaps(s->handoff_since_) > 1500 && !hal().terminal_active()) {
        s->handoff_ = false;
        power().set_handoff(false);
        s->screen_changed();
    }
    // Screen off or terminal: the timer runs every 500 ms and nothing is drawn.
    if (!power().screen_off() && !s->handoff_ && ++n % 10 == 0) s->update_statusbar();
    if (s->app_) s->app_->tick();
    s->apply_pending();
    if (s->toast_until_ && lv_tick_get() > s->toast_until_) {
        lv_obj_add_flag(s->toast_, LV_OBJ_FLAG_HIDDEN);
        s->toast_until_ = 0;
    }
}

void Shell::switch_to(std::unique_ptr<App> app, const std::string& title) {
    // Tear down the old app first so its widgets and group go away together.
    if (group_) {
        if (keypad_) lv_indev_set_group(keypad_, nullptr);
        lv_group_delete(group_);
        group_ = nullptr;
    }
    lv_obj_clean(content_);
    app_ = std::move(app);

    group_ = lv_group_create();
    lv_group_set_wrap(group_, true);
    app_->group = group_;
    if (keypad_) lv_indev_set_group(keypad_, group_);

    lv_obj_t* root = lv_obj_create(content_);
    style_plain(root);
    lv_obj_set_size(root, kScreenW, kContentH);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    app_->build(root);

    lv_label_set_text(sb_title_, title.c_str());
    update_statusbar();
}

void Shell::open_app(const std::string& id) {
    // Deferred: apps often call this from their own on_key(), and switching
    // destroys the calling app. apply_pending() runs once the call returns.
    pending_ = id;
}

void Shell::go_home() { pending_ = "home"; }

void Shell::apply_pending() {
    if (pending_.empty()) return;
    std::string id;
    id.swap(pending_);
    if (id == "home") {
        app_id_ = "home";
        switch_to(make_launcher(), "");
        return;
    }
    for (auto& info : app_registry()) {
        if (info.id == id) {
            app_id_ = id;
            switch_to(info.make(), info.name);
            return;
        }
    }
    toast("Unknown app: " + id);
}

void Shell::toast(const std::string& msg) {
    lv_label_set_text(toast_, msg.c_str());
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_);
    toast_until_ = lv_tick_get() + 2000;
}

void Shell::add_focus(lv_obj_t* obj) {
    if (group_) lv_group_add_obj(group_, obj);
}

bool Shell::handle_key(uint32_t key) {
    bool used = false;
    lv_obj_t* focused = group_ ? lv_group_get_focused(group_) : nullptr;
    bool in_text = focused && lv_obj_check_type(focused, &lv_textarea_class);
    // The Cardputer has no Esc key on the base layer; ` (top-left) doubles as Back.
    if (key == '`' && !in_text) key = LV_KEY_ESC;
    if (app_ && app_->on_key(key)) {
        used = true;
    } else if ((key == LV_KEY_UP || key == LV_KEY_DOWN) && group_) {
        // Up/Down move focus between widgets, except inside multi-line text.
        lv_obj_t* f = lv_group_get_focused(group_);
        if (!(f && lv_obj_check_type(f, &lv_textarea_class) && !lv_textarea_get_one_line(f))) {
            if (key == LV_KEY_UP) lv_group_focus_prev(group_);
            else lv_group_focus_next(group_);
            used = true;
        }
    } else if (key == LV_KEY_ESC) {
        if (app_ && app_->on_back()) {
            used = true;
        } else if (app_id_ != "home") {
            go_home();
            used = true;
        }
    }
    apply_pending();
    return used;
}

bool Shell::open_terminal(const std::string& command) {
    if (!hal().terminal_open(command)) return false;
    handoff_ = true;
    handoff_since_ = lv_tick_get();
    power().set_handoff(true);
    screen_changed();
    return true;
}

void Shell::screen_changed() {
    lv_display_t* disp = lv_display_get_default();
    bool off = power().screen_off();
    bool idle = off || handoff_;   // nothing of ours on screen
    platform_set_low_power(idle);
    if (!idle) {
        lv_display_enable_invalidation(disp, true);
        lv_timer_set_period(tick_timer_, 100);
    }
    if (!hal().has_backlight()) {
        // Draw what the backlight would do: dark when off, shaded when dim.
        if (off || power().dimmed()) {
            lv_obj_set_style_bg_opa(blank_, off ? LV_OPA_COVER : LV_OPA_50, 0);
            lv_obj_remove_flag(blank_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(blank_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (idle) {
        // Draw the last frame (not over the console), then stop drawing.
        if (!handoff_) lv_refr_now(disp);
        lv_display_enable_invalidation(disp, false);
        lv_timer_set_period(tick_timer_, 500);
    } else {
        update_statusbar();
        lv_obj_invalidate(screen_);
    }
}

// Service functions declared in app.h
void shell_toast(const std::string& msg) { Shell::get().toast(msg); }
void shell_open_app(const std::string& id) { Shell::get().open_app(id); }
void shell_go_home() { Shell::get().go_home(); }
void shell_add_focus(lv_obj_t* obj) { Shell::get().add_focus(obj); }
void shell_screen_changed() { Shell::get().screen_changed(); }
bool shell_open_terminal(const std::string& command) { return Shell::get().open_terminal(command); }

}  // namespace zs
