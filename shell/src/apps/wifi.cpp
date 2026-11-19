// Wi-Fi: scan, pick a network, type the password, connect (NetworkManager).
#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

class WifiApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        show_list();
    }

    bool on_back() override {
        if (pw_page_) {
            show_list();
            return true;
        }
        return false;
    }

    bool on_key(uint32_t key) override {
        if (!pw_page_ && (key == 'r' || key == 'R')) {
            show_list();
            shell_toast("Rescanned");
            return true;
        }
        return false;
    }

private:
    void clear() {
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        pw_page_ = nullptr;
    }

    void show_list() {
        clear();
        nets_ = hal().wifi_scan();
        NetStatus st = hal().net();

        lv_obj_t* hdr = label(root_, st.wifi_connected ? ("Connected to " + st.wifi_ssid).c_str() : "Not connected",
                              &zs_font_12, st.wifi_connected ? kGood : kMuted);
        lv_obj_set_pos(hdr, 8, 4);
        lv_obj_t* hint = label(root_, "R rescan", &zs_font_12, kMuted);
        lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -8, 4);

        lv_obj_t* list = make_list(root_, 4, 22, kScreenW - 8, kContentH - 26);
        for (size_t i = 0; i < nets_.size(); ++i) {
            auto& n = nets_[i];
            std::string text = n.ssid + fmt("   %d%%", n.signal) + (n.secure ? "" : "   open");
            lv_obj_t* b = list_button(list, n.connected ? LV_SYMBOL_OK : LV_SYMBOL_WIFI, text.c_str(), on_pick, this);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
        }
        finish_list(list);
    }

    static void on_pick(lv_event_t* e) {
        auto* self = static_cast<WifiApp*>(lv_event_get_user_data(e));
        size_t i = (size_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
        // Defer: we're inside an event of a widget that show_password() deletes.
        self->pick_ = int(i);
        lv_async_call([](void* p) { static_cast<WifiApp*>(p)->show_password(); }, self);
    }

    void show_password() {
        if (pick_ < 0 || pick_ >= int(nets_.size())) return;
        WifiNetwork n = nets_[pick_];
        if (!n.secure) {
            connect(n.ssid, "");
            return;
        }
        clear();
        pw_page_ = lv_obj_create(root_);
        style_plain(pw_page_);
        lv_obj_set_size(pw_page_, kScreenW, kContentH);
        lv_obj_t* t = label(pw_page_, ("Password for " + n.ssid).c_str(), &zs_font_14);
        lv_obj_set_pos(t, 10, 10);
        ta_ = lv_textarea_create(pw_page_);
        lv_textarea_set_one_line(ta_, true);
        lv_textarea_set_password_mode(ta_, true);
        lv_obj_set_size(ta_, kScreenW - 20, 36);
        lv_obj_set_pos(ta_, 10, 40);
        lv_obj_set_style_bg_color(ta_, c(kSurface), 0);
        lv_obj_set_style_border_color(ta_, c(kAccent), 0);
        lv_obj_add_event_cb(ta_, on_submit, LV_EVENT_READY, this);
        shell_add_focus(ta_);
        lv_obj_t* h = label(pw_page_, "Enter to connect   ` or Esc to go back", &zs_font_12, kMuted);
        lv_obj_set_pos(h, 10, 90);
    }

    static void on_submit(lv_event_t* e) {
        auto* self = static_cast<WifiApp*>(lv_event_get_user_data(e));
        self->pw_ = lv_textarea_get_text(self->ta_);
        lv_async_call([](void* p) {
            auto* s = static_cast<WifiApp*>(p);
            s->connect(s->nets_[s->pick_].ssid, s->pw_);
        }, self);
    }

    void connect(const std::string& ssid, const std::string& pw) {
        shell_toast("Connecting to " + ssid + "...");
        bool ok = hal().wifi_connect(ssid, pw);
        shell_toast(ok ? "Connected to " + ssid : "Could not connect");
        show_list();
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* pw_page_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    std::vector<WifiNetwork> nets_;
    int pick_ = -1;
    std::string pw_;
};
}  // namespace

std::unique_ptr<App> make_wifi_app() { return std::make_unique<WifiApp>(); }

}  // namespace zs
