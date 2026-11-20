// Tether: get online through your phone (docs/TETHER.md).
//
//   Wi-Fi hotspot   it's a Wi-Fi network: pick it in Wi-Fi, or scan the
//                   hotspot's QR code with Camera
//   USB             plug the phone in and turn on USB tethering; nothing to
//                   set up here
//   Bluetooth       slower, but far easier on both batteries. Pair once
//                   (Add a phone), then Enter on the phone goes online or
//                   offline. Del twice forgets a phone.
#include <vector>

#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

class TetherApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        rebuild();
    }

    void tick() override {
        if (++n_ % 5 && !dirty_) return;   // twice a second
        TetherStatus s = hal().tether();
        if (!s.error.empty() && s.error != shown_error_ && page_ != Page::Pair) shell_toast(s.error);
        shown_error_ = s.error;
        // Pairing ended: say how it went and return to the list of phones.
        if (page_ == Page::Pair && s.busy.empty() && started_ && s.error.empty()) {
            for (auto& p : s.phones)
                if (p.mac == mac_) shell_toast(p.online ? "Online through " + p.name : "Paired with " + p.name);
            page_ = Page::Main;
            dirty_ = true;
        }
        if (!s.busy.empty()) started_ = true;
        if (dirty_ || signature(s) != sig_) rebuild();
    }

    bool on_key(uint32_t key) override {
        if (page_ != Page::Main || (key != LV_KEY_DEL && key != LV_KEY_BACKSPACE)) return false;
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        for (size_t i = 0; i < phone_btns_.size(); ++i) {
            if (phone_btns_[i] != f) continue;
            const TetherPhone& p = st_.phones[i];
            if (confirm_ != p.mac) {
                confirm_ = p.mac;
                shell_toast("Del again to forget " + p.name);
                return true;
            }
            confirm_.clear();
            hal().tether_forget(p.mac);
            shell_toast("Forgot " + p.name);
            dirty_ = true;
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (page_ == Page::Main) return false;
        page_ = Page::Main;
        dirty_ = true;
        return true;
    }

private:
    enum class Page { Main, Search, Pair };

    std::string signature(const TetherStatus& s) const {
        NetStatus n = hal().net();
        std::string k = std::to_string(int(page_)) + s.busy + s.busy_mac + s.passkey + s.error + s.usb_name + s.usb_ip +
                        n.wifi_ssid + n.tether;
        for (auto& p : s.phones) k += "|" + p.mac + (p.link ? "+" : "-") + (p.online ? "+" : "-");
        for (auto& p : s.found) k += "/" + p.mac;
        return k;
    }

    void rebuild() {
        st_ = hal().tether();
        sig_ = signature(st_);
        dirty_ = false;
        int keep = 0;
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (f && list_ && lv_obj_get_parent(f) == list_ && page_ == built_) keep = int(lv_obj_get_index(f));
        lv_obj_clean(root_);
        list_ = nullptr;
        phone_btns_.clear();
        if (group) lv_group_remove_all_objs(group);
        built_ = page_;
        switch (page_) {
        case Page::Main: build_main(); break;
        case Page::Search: build_search(); break;
        case Page::Pair: build_pair(); break;
        }
        if (list_) {
            finish_list(list_);
            int n = int(lv_obj_get_child_count(list_));
            if (n > 0) lv_group_focus_obj(lv_obj_get_child(list_, keep < n ? keep : n - 1));
        }
    }

    // What we're online through, for the top line.
    std::string headline(uint32_t& color) const {
        NetStatus n = hal().net();
        color = kGood;
        if (st_.usb && !st_.usb_ip.empty()) return "Online through " + st_.usb_name + " (USB)";
        for (auto& p : st_.phones)
            if (p.online) return "Online through " + p.name + " (Bluetooth)";
        color = kMuted;
        if (n.wifi_connected) return "On Wi-Fi: " + n.wifi_ssid;
        return "Not online";
    }

    void build_main() {
        uint32_t color;
        std::string head = headline(color);
        lv_obj_t* h = label(root_, head.c_str(), &zs_font_12, color);
        lv_obj_set_pos(h, 8, 4);
        lv_obj_set_width(h, kScreenW - 16);
        lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);

        list_ = make_list(root_, 4, 22, kScreenW - 8, kContentH - 26);
        list_button(list_, LV_SYMBOL_WIFI, "Wi-Fi hotspot: pick it in Wi-Fi",
                    [](lv_event_t*) { shell_open_app("wifi"); }, nullptr);
        list_button(list_, ZS_ICON_CAMERA, "Wi-Fi hotspot: scan its QR code",
                    [](lv_event_t*) { shell_open_app("camera"); }, nullptr);

        std::string usb = "USB: plug in the phone";
        if (st_.usb) usb = "USB: " + st_.usb_name + (st_.usb_ip.empty() ? "   not sharing yet" : "   online");
        list_button(list_, LV_SYMBOL_USB, usb.c_str(), [](lv_event_t* e) {
            auto* self = static_cast<TetherApp*>(lv_event_get_user_data(e));
            if (self->st_.usb && !self->st_.usb_ip.empty()) shell_toast("Online at " + self->st_.usb_ip);
            else shell_toast("On the phone: turn on USB tethering (iPhone: Personal Hotspot)");
        }, this);

        for (size_t i = 0; i < st_.phones.size(); ++i) {
            const TetherPhone& p = st_.phones[i];
            bool busy = st_.busy == "connecting" && st_.busy_mac == p.mac;
            std::string text = p.name + (busy ? "   connecting..." : p.online ? "   online" : "   off");
            lv_obj_t* b = list_button(list_, p.online ? LV_SYMBOL_OK : LV_SYMBOL_BLUETOOTH, text.c_str(), on_phone, this);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
            phone_btns_.push_back(b);
        }
        list_button(list_, LV_SYMBOL_PLUS, "Add a phone over Bluetooth", [](lv_event_t* e) {
            auto* self = static_cast<TetherApp*>(lv_event_get_user_data(e));
            hal().tether_search();
            self->page_ = Page::Search;
            self->dirty_ = true;
        }, this);
    }

    static void on_phone(lv_event_t* e) {
        auto* self = static_cast<TetherApp*>(lv_event_get_user_data(e));
        size_t i = size_t((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        if (i >= self->st_.phones.size()) return;
        const TetherPhone& p = self->st_.phones[i];
        if (!self->st_.busy.empty()) {
            shell_toast("Busy: " + self->st_.busy);
            return;
        }
        if (p.online) {
            hal().tether_disconnect(p.mac);
            shell_toast("Offline");
        } else {
            hal().tether_connect(p.mac);
            shell_toast("Connecting through " + p.name + "...");
        }
        self->dirty_ = true;
    }

    void build_search() {
        bool searching = st_.busy == "searching";
        lv_obj_t* h = label(root_, searching ? "Looking for phones..." : st_.found.empty() ? "No phones found" : "Pick your phone",
                            &zs_font_12, searching ? kInfo : kText);
        lv_obj_set_pos(h, 8, 4);
        lv_obj_t* tip = label(root_, "On the phone, open Bluetooth settings and keep them open.", &zs_font_12, kMuted);
        lv_obj_set_pos(tip, 8, 20);
        lv_obj_set_width(tip, kScreenW - 16);
        lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);

        list_ = make_list(root_, 4, 54, kScreenW - 8, kContentH - 58);
        for (size_t i = 0; i < st_.found.size(); ++i) {
            lv_obj_t* b = list_button(list_, ZS_ICON_PHONE, st_.found[i].name.c_str(), [](lv_event_t* e) {
                auto* self = static_cast<TetherApp*>(lv_event_get_user_data(e));
                size_t i = size_t((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
                if (i >= self->st_.found.size()) return;
                self->mac_ = self->st_.found[i].mac;
                self->name_ = self->st_.found[i].name;
                hal().tether_pair(self->mac_);
                self->started_ = true;
                self->page_ = Page::Pair;
                self->dirty_ = true;
            }, this);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
        }
        if (!searching)
            list_button(list_, LV_SYMBOL_REFRESH, "Search again", [](lv_event_t* e) {
                hal().tether_search();
                static_cast<TetherApp*>(lv_event_get_user_data(e))->dirty_ = true;
            }, this);
    }

    void build_pair() {
        lv_obj_t* h = label(root_, ("Pairing with " + name_).c_str(), &zs_font_14, kText);
        lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 8);
        std::string big, text;
        uint32_t color = kMuted;
        if (!st_.error.empty() && st_.busy.empty()) {
            text = st_.error + "\nEsc goes back.";
            color = kBad;
        } else if (st_.busy == "connecting") {
            text = "Paired. Going online...";
            color = kGood;
        } else if (!st_.passkey.empty()) {
            big = st_.passkey.size() == 6 ? st_.passkey.substr(0, 3) + " " + st_.passkey.substr(3) : st_.passkey;
            text = "If the phone shows the same number, tap Pair on the phone. If not, tap Cancel.";
        } else {
            text = "Waiting for the phone...";
        }
        if (!big.empty()) {
            lv_obj_t* n = label(root_, big.c_str(), &zs_font_28, kAccent);
            lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 34);
        }
        lv_obj_t* t = label(root_, text.c_str(), &zs_font_12, color);
        lv_obj_set_width(t, kScreenW - 32);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, big.empty() ? 54 : 78);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* list_ = nullptr;
    std::vector<lv_obj_t*> phone_btns_;
    TetherStatus st_;
    std::string sig_, confirm_, shown_error_, mac_, name_;
    Page page_ = Page::Main, built_ = Page::Main;
    bool dirty_ = false;
    bool started_ = false;   // the pairing we asked for has been seen running
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_tether_app() { return std::make_unique<TetherApp>(); }

}  // namespace zs
