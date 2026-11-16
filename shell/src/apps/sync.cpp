// Sync: Notes, Documents and Recordings, the same on your PC and phone
// (Syncthing; see docs/SYNC.md).
//
// Left: status. Right: requests to accept, folders, devices, actions.
//   Enter on a request   accept it     Del: ignore it
//   Enter on a folder    look for changes now
//   Del twice on device  stop syncing with it
//   Show my device ID    QR code + the ID, to add this Cardputer elsewhere
#include <vector>

#include "apps/common.h"
#include "ui/sync.h"

namespace zs {
namespace {
using namespace theme;

const char* mode_label(const std::string& m) {
    return m == "always" ? "Always on" : m == "manual" ? "Only when asked" : "While charging";
}

class SyncApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        if (!hal().sync_running()) hal().sync_start();
        rebuild();
    }

    void tick() override {
        if (dirty_) {
            dirty_ = false;
            rebuild();
            return;
        }
        if (++n_ % 20) return;   // every 2 s; the device caches for 4 s
        SyncStatus s = hal().sync();
        if (signature(s) != sig_) rebuild();
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;
        if (key != LV_KEY_DEL && key != LV_KEY_BACKSPACE) return false;
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        for (size_t i = 0; i < req_btns_.size(); ++i) {
            if (req_btns_[i] != f) continue;
            hal().sync_answer(st_.requests[i], false);
            shell_toast("Ignored");
            dirty_ = true;
            return true;
        }
        for (size_t i = 0; i < dev_btns_.size(); ++i) {
            if (dev_btns_[i] != f) continue;
            const SyncDevice& d = st_.devices[i];
            if (confirm_ != d.id) {
                confirm_ = d.id;
                shell_toast("Del again to stop syncing with " + d.name);
                return true;
            }
            confirm_.clear();
            hal().sync_remove_device(d.id);
            shell_toast("Stopped syncing with " + d.name);
            dirty_ = true;
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (input_) {
            close_overlay();
            return true;
        }
        return false;
    }

private:
    static std::string signature(const SyncStatus& s) {
        std::string k = std::to_string(s.running) + std::to_string(s.ready) + std::to_string(sync_now_active());
        for (auto& f : s.folders) k += "|" + f.id + f.state + std::to_string(f.percent);
        for (auto& d : s.devices) k += "|" + d.id + (d.connected ? "+" : "-") + std::to_string(d.percent);
        for (auto& r : s.requests) k += "|" + r.device_id + r.folder_id;
        return k;
    }

    // One line for the top of the panel.
    std::string headline(uint32_t& color) const {
        color = kText;
        if (!st_.installed) return "Not installed";
        if (!st_.running) return "Sync off";
        if (!st_.ready) return "Starting...";
        for (auto& f : st_.folders)
            if (!f.error.empty() || f.state == "error") {
                color = kBad;
                return "Problem";
            }
        int need = 0, pct = 100;
        for (auto& f : st_.folders) {
            need += f.need_files;
            if (f.percent < pct) pct = f.percent;
            if (f.state == "syncing" || f.state == "scanning") need += need ? 0 : 1;
        }
        int online = 0;
        for (auto& d : st_.devices) online += d.connected;
        if (st_.devices.empty()) return "No devices yet";
        if (need) return fmt("Syncing %d%%", pct);
        if (!online) return "Waiting for devices";
        color = kGood;
        return LV_SYMBOL_OK " Up to date";
    }

    void rebuild() {
        st_ = hal().sync();
        sig_ = signature(st_);
        // Keep the highlighted row across refreshes (percentages change
        // every couple of seconds while syncing).
        int keep = 0;
        lv_obj_t* f = group && !input_ ? lv_group_get_focused(group) : nullptr;
        if (f && lv_obj_get_parent(f) == list_) keep = int(lv_obj_get_index(f));
        lv_obj_clean(root_);
        input_ = nullptr;
        req_btns_.clear();
        dev_btns_.clear();
        if (group) lv_group_remove_all_objs(group);

        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 150, kContentH - 10);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, 0);
        uint32_t color;
        std::string head = headline(color);
        label(panel, head.c_str(), &lv_font_montserrat_16, color);
        if (st_.ready) {
            int online = 0;
            for (auto& d : st_.devices) online += d.connected;
            kv_row(panel, "Online", fmt("%d of %zu", online, st_.devices.size()).c_str());
            kv_row(panel, "Runs", mode_label(sync_mode()));
            kv_row(panel, "ID", st_.device_id.substr(0, 7).c_str());
        }
        const char* hint = !st_.installed ? "Syncthing comes with ZeroOS images."
                           : !st_.requests.empty() ? "Enter: accept\nDel: ignore"
                           : st_.devices.empty() ? "Add this Cardputer in Syncthing on your PC or phone: Show my device ID."
                           : "Notes, Documents and Recordings stay the same everywhere.";
        lv_obj_t* h = label(panel, hint, &lv_font_montserrat_12, st_.requests.empty() ? kMuted : kAccent);
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(h, 136);

        lv_obj_t* list = list_ = make_list(root_, 162, 4, 152, kContentH - 10);
        for (auto& r : st_.requests) {
            std::string t = r.is_folder ? r.device_name + ": " + r.folder_label : r.device_name + " asks";
            lv_obj_t* b = list_button(list, r.is_folder ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_PLUS, t.c_str(), on_request, this);
            lv_obj_set_style_text_color(b, c(kAccent), 0);
            req_btns_.push_back(b);
        }
        for (auto& f : st_.folders) {
            std::string t = f.label + "  " +
                            (!f.error.empty() || f.state == "error" ? std::string("error")
                             : f.percent < 100 || f.need_files ? fmt("%d%%", f.percent)
                             : f.state == "scanning"           ? std::string("...")
                                                               : std::string(LV_SYMBOL_OK));
            list_button(list, LV_SYMBOL_DIRECTORY, t.c_str(), [](lv_event_t*) {
                hal().sync_rescan();
                shell_toast("Looking for changes");
            }, nullptr);
        }
        for (auto& d : st_.devices) {
            std::string t = d.name + (!d.connected ? "  (off)" : d.percent < 100 ? fmt("  %d%%", d.percent) : "");
            lv_obj_t* b = list_button(list, LV_SYMBOL_DRIVE, t.c_str(), [](lv_event_t*) {
                shell_toast("Del twice to stop syncing with it");
            }, nullptr);
            if (!d.connected) lv_obj_set_style_text_color(b, c(kMuted), 0);
            dev_btns_.push_back(b);
        }
        if (st_.installed) {
            list_button(list, LV_SYMBOL_REFRESH, sync_now_active() ? "Syncing now..." : "Sync now", [](lv_event_t* e) {
                sync_now();
                shell_toast("Syncing until up to date");
                static_cast<SyncApp*>(lv_event_get_user_data(e))->dirty_ = true;
            }, this);
            list_button(list, LV_SYMBOL_EYE_OPEN, "Show my device ID", [](lv_event_t* e) {
                static_cast<SyncApp*>(lv_event_get_user_data(e))->show_id();
            }, this);
            list_button(list, LV_SYMBOL_KEYBOARD, "Add device by ID", [](lv_event_t* e) {
                static_cast<SyncApp*>(lv_event_get_user_data(e))->ask_id();
            }, this);
            std::string when = std::string("Runs: ") + mode_label(sync_mode());
            list_button(list, LV_SYMBOL_SETTINGS, when.c_str(), [](lv_event_t* e) {
                std::string m = sync_mode();
                set_sync_mode(m == "charging" ? "always" : m == "always" ? "manual" : "charging");
                static_cast<SyncApp*>(lv_event_get_user_data(e))->dirty_ = true;
            }, this);
        }
        finish_list(list);
        uint32_t n = lv_obj_get_child_count(list);
        if (keep > 0 && n) {
            lv_obj_t* k = lv_obj_get_child(list, keep < int(n) ? keep : int(n) - 1);
            lv_group_focus_obj(k);
            lv_obj_scroll_to_view(k, LV_ANIM_OFF);
        }
    }

    static void on_request(lv_event_t* e) {
        auto* self = static_cast<SyncApp*>(lv_event_get_user_data(e));
        for (size_t i = 0; i < self->req_btns_.size(); ++i) {
            if (self->req_btns_[i] != lv_event_get_target_obj(e)) continue;
            SyncRequest r = self->st_.requests[i];
            bool ok = hal().sync_answer(r, true);
            shell_toast(!ok         ? std::string("Couldn't accept")
                        : r.is_folder ? "Syncing " + r.folder_label + " into ~/Sync"
                                      : "Now syncing with " + r.device_name);
            self->dirty_ = true;
            return;
        }
    }

    lv_obj_t* overlay(int h) {
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 308, h);
        lv_obj_center(input_);
        lv_obj_remove_flag(input_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        return input_;
    }

    // QR code and the ID in four lines of two groups (XXXXXXX-XXXXXXX).
    void show_id() {
        if (st_.device_id.empty()) {
            shell_toast("Still starting; try again in a moment");
            return;
        }
        lv_obj_t* o = overlay(kContentH - 6);
        lv_obj_t* qr = lv_qrcode_create(o);
        lv_qrcode_set_size(qr, 112);
        lv_qrcode_set_dark_color(qr, c(0x000000));
        lv_qrcode_set_light_color(qr, c(0xffffff));
        lv_qrcode_update(qr, st_.device_id.c_str(), uint32_t(st_.device_id.size()));
        lv_obj_set_style_border_color(qr, c(0xffffff), 0);
        lv_obj_set_style_border_width(qr, 4, 0);
        lv_obj_align(qr, LV_ALIGN_LEFT_MID, 0, 0);
        std::string lines;
        for (size_t i = 0; i < st_.device_id.size(); i += 16)
            lines += st_.device_id.substr(i, 15) + "\n";
        lv_obj_t* t = label(o, "This Cardputer's ID", &lv_font_montserrat_12, kMuted);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 128, 0);
        lv_obj_t* id = label(o, lines.c_str(), &lv_font_montserrat_14, kText);
        lv_obj_align(id, LV_ALIGN_TOP_LEFT, 128, 18);
        lv_obj_t* hint = label(o, "Syncthing: Add Remote Device. Esc closes.", &lv_font_montserrat_12, kMuted);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, 160);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 128, 0);
        // Keep keys here until Esc.
        lv_obj_t* sink = lv_obj_create(o);
        lv_obj_set_size(sink, 1, 1);
        lv_obj_set_style_opa(sink, LV_OPA_TRANSP, 0);
        lv_group_add_obj(group, sink);
        lv_group_focus_obj(sink);
    }

    void ask_id() {
        lv_obj_t* o = overlay(74);
        label(o, "Device ID (from Syncthing: Actions > Show ID)", &lv_font_montserrat_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(o);
        lv_textarea_set_one_line(ta, true);
        lv_obj_set_width(ta, 288);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<SyncApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            self->close_overlay();
            if (!v.empty()) shell_toast(hal().sync_add_device(v, "") ? "Added; accept it on the other device too"
                                                                       : "That isn't a device ID");
            self->dirty_ = true;
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    void close_overlay() {
        if (!input_) return;
        lv_group_set_editing(group, false);
        lv_obj_t* in = input_;
        input_ = nullptr;
        lv_obj_delete_async(in);
        dirty_ = true;
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* input_ = nullptr;
    lv_obj_t* list_ = nullptr;
    SyncStatus st_;
    std::string sig_, confirm_;
    std::vector<lv_obj_t*> req_btns_, dev_btns_;
    bool dirty_ = false;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_sync_app() { return std::make_unique<SyncApp>(); }

}  // namespace zs
