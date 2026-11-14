// Settings: radios, display, sound, power.
#include <functional>
#include <vector>

#include "apps/common.h"
#include "ui/power.h"
#include "ui/remote.h"

namespace zs {
namespace {
using namespace theme;

class SettingsApp : public App {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = lv_obj_create(root);
        style_plain(col);
        lv_obj_set_size(col, kScreenW, kContentH);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(col, 6, 0);
        lv_obj_set_style_pad_row(col, 4, 0);
        lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_ACTIVE);

        auto& h = hal();
        NetStatus n = h.net();
        wifi_ = switch_row(col, LV_SYMBOL_WIFI "  Wi-Fi", n.wifi_enabled, [](lv_event_t* e) {
            hal().set_wifi_enabled(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
        });
        bt_ = switch_row(col, LV_SYMBOL_BLUETOOTH "  Bluetooth", n.bt_enabled, [](lv_event_t* e) {
            hal().set_bt_enabled(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
        });
        slider_row(col, LV_SYMBOL_EYE_OPEN "  Brightness", power().user_brightness(), [](lv_event_t* e) {
            power().set_user_brightness(lv_slider_get_value(lv_event_get_target_obj(e)));
        });
        slider_row(col, LV_SYMBOL_VOLUME_MAX "  Volume", h.volume(), [](lv_event_t* e) {
            hal().set_volume(lv_slider_get_value(lv_event_get_target_obj(e)));
        });
        switch_row(col, LV_SYMBOL_MUTE "  Mute", h.muted(), [](lv_event_t* e) {
            hal().set_muted(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
        });

        // Battery life
        auto& p = power();
        cycle_row(col, LV_SYMBOL_CHARGE "  Power profile",
                  {{"Saver", 0}, {"Balanced", 1}, {"Performance", 2}}, int(p.profile()),
                  [](int v) { power().set_profile(Profile(v)); });
        cycle_row(col, LV_SYMBOL_EYE_CLOSE "  Dim screen after",
                  {{"15 s", 15}, {"30 s", 30}, {"1 min", 60}, {"2 min", 120}, {"Never", 0}}, p.dim_after_s(),
                  [](int v) { power().set_dim_after_s(v); });
        cycle_row(col, LV_SYMBOL_POWER "  Screen off after",
                  {{"30 s", 30}, {"1 min", 60}, {"2 min", 120}, {"5 min", 300}, {"10 min", 600}, {"Never", 0}},
                  p.off_after_s(), [](int v) { power().set_off_after_s(v); });
        cycle_row(col, LV_SYMBOL_BLUETOOTH "  Bluetooth auto-off",
                  {{"5 min", 5}, {"10 min", 10}, {"30 min", 30}, {"Never", 0}}, p.bt_auto_off_min(),
                  [](int v) { power().set_bt_auto_off_min(v); });
        switch_row(col, LV_SYMBOL_WIFI "  Wi-Fi off with screen", p.wifi_sleep(), [](lv_event_t* e) {
            power().set_wifi_sleep(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
        });
        switch_row(col, LV_SYMBOL_BATTERY_2 "  Time left in top bar", p.show_time_left(), [](lv_event_t* e) {
            power().set_show_time_left(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
        });

        // Remote
        int ts = tailscale_mode() == "always" ? 1 : tailscale_mode() == "off" ? 2 : 0;
        cycle_row(col, LV_SYMBOL_SHUFFLE "  Tailscale", {{"When needed", 0}, {"Always on", 1}, {"Off", 2}}, ts,
                  [](int v) {
                      set_tailscale_mode(v == 1 ? "always" : v == 2 ? "off" : "needed");
                      if (v == 1) hal().tailscale_start();
                      if (v == 2) hal().tailscale_stop();
                  });

        button_row(col, LV_SYMBOL_WIFI "  Wi-Fi networks", [](lv_event_t*) { shell_open_app("wifi"); });
        button_row(col, LV_SYMBOL_LIST "  About this device", [](lv_event_t*) { shell_open_app("sysmon"); });
        button_row(col, LV_SYMBOL_REFRESH "  Restart", [](lv_event_t*) {
            shell_toast("Restarting...");
            hal().reboot();
        });
        button_row(col, LV_SYMBOL_POWER "  Shut down", [](lv_event_t*) {
            shell_toast("Shutting down...");
            hal().power_off();
        });
    }

private:
    lv_obj_t* row(lv_obj_t* parent, const char* text) {
        lv_obj_t* r = lv_obj_create(parent);
        style_panel(r);
        lv_obj_set_size(r, LV_PCT(100), 30);
        lv_obj_set_style_pad_ver(r, 2, 0);
        lv_obj_set_style_pad_hor(r, 8, 0);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* l = label(r, text, &lv_font_montserrat_12);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        return r;
    }

    static void focus_style(lv_obj_t* o) {
        lv_obj_set_style_outline_color(o, c(kAccent), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_width(o, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_pad(o, 2, LV_STATE_FOCUS_KEY);
    }

    lv_obj_t* switch_row(lv_obj_t* parent, const char* text, bool on, lv_event_cb_t cb) {
        lv_obj_t* r = row(parent, text);
        lv_obj_t* sw = lv_switch_create(r);
        lv_obj_set_size(sw, 40, 20);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(sw, c(kAccent), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
        focus_style(sw);
        shell_add_focus(sw);
        return sw;
    }

    lv_obj_t* slider_row(lv_obj_t* parent, const char* text, int value, lv_event_cb_t cb) {
        lv_obj_t* r = row(parent, text);
        lv_obj_t* s = lv_slider_create(r);
        lv_obj_set_size(s, 120, 8);
        lv_obj_align(s, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_slider_set_range(s, 0, 100);
        lv_slider_set_value(s, value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s, c(kAccent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s, c(kText), LV_PART_KNOB);
        lv_obj_set_style_pad_all(s, 3, LV_PART_KNOB);
        lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, nullptr);
        focus_style(s);
        shell_add_focus(s);
        return s;
    }

    lv_obj_t* button_row(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user = nullptr) {
        lv_obj_t* b = lv_button_create(parent);
        lv_obj_set_size(b, LV_PCT(100), 28);
        lv_obj_set_style_bg_color(b, c(kSurface), 0);
        lv_obj_set_style_bg_color(b, c(kAccent), LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(b, c(0x000000), LV_STATE_FOCUSED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_text_color(b, c(kText), 0);
        lv_obj_t* l = lv_label_create(b);  // inherits the button's text color
        lv_label_set_text(l, text);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
        shell_add_focus(b);
        return b;
    }

    // A row whose value steps through choices: Enter or Right = next, Left = previous.
    struct Choice {
        const char* text;
        int value;
    };
    struct Cycle {
        std::vector<Choice> choices;
        size_t at = 0;
        std::function<void(int)> set;
        lv_obj_t* value = nullptr;
        void step(int d) {
            int n = int(choices.size());
            at = size_t((int(at) + d + n) % n);
            lv_label_set_text(value, choices[at].text);
            set(choices[at].value);
        }
    };

    void cycle_row(lv_obj_t* parent, const char* text, std::vector<Choice> choices, int current,
                   std::function<void(int)> set) {
        auto* cy = new Cycle{std::move(choices), 0, std::move(set)};
        for (size_t i = 0; i < cy->choices.size(); ++i)
            if (cy->choices[i].value == current) cy->at = i;
        lv_obj_t* b = button_row(parent, text, [](lv_event_t* e) {
            static_cast<Cycle*>(lv_event_get_user_data(e))->step(1);
        }, cy);
        cy->value = lv_label_create(b);
        lv_label_set_text(cy->value, cy->choices[cy->at].text);
        lv_obj_align(cy->value, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            uint32_t k = lv_event_get_key(e);
            auto* c = static_cast<Cycle*>(lv_event_get_user_data(e));
            if (k == LV_KEY_RIGHT) c->step(1);
            else if (k == LV_KEY_LEFT) c->step(-1);
        }, LV_EVENT_KEY, cy);
        lv_obj_add_event_cb(b, [](lv_event_t* e) { delete static_cast<Cycle*>(lv_event_get_user_data(e)); },
                            LV_EVENT_DELETE, cy);
    }

    lv_obj_t *wifi_ = nullptr, *bt_ = nullptr;
};
}  // namespace

std::unique_ptr<App> make_settings_app() { return std::make_unique<SettingsApp>(); }

}  // namespace zs
