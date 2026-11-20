// Car: two ways to play music through a car stereo (docs/CAR.md).
//
//   USB stick mode   the Cardputer shows up as a USB stick holding ~/Music;
//                    the car plays the files and its own buttons work.
//   Bluetooth        the Cardputer plays (Jellyfin too) and the car, a
//                    speaker or headphones is the loudspeaker; the car's
//                    play and skip buttons drive the play queue.
//
//   Enter  turn on/off, connect/disconnect      X  forget a Bluetooth device
#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

constexpr int kListW = 184;

class CarApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        panel_ = lv_obj_create(root);
        style_panel(panel_);
        lv_obj_set_pos(panel_, kListW + 8, 6);
        lv_obj_set_size(panel_, kScreenW - kListW - 14, kContentH - 12);
        lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
        head_ = label(panel_, "", &zs_font_14, kText);
        lv_obj_set_width(head_, kScreenW - kListW - 26);
        lv_label_set_long_mode(head_, LV_LABEL_LONG_WRAP);
        help_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_set_width(help_, kScreenW - kListW - 26);
        lv_label_set_long_mode(help_, LV_LABEL_LONG_WRAP);

        devices_ = hal().bt_devices();
        stick_ = hal().usb_stick();
        rebuild(0);
    }

    void tick() override {
        if (++n_ % 10 == 0) {   // once a second
            UsbStickStatus st = hal().usb_stick();
            bool changed = st.state != stick_.state || st.percent != stick_.percent;
            stick_ = st;
            if (changed) {
                if (usb_row_) lv_label_set_text(lv_obj_get_child(usb_row_, 1), usb_text().c_str());
                show_panel(focused());
            }
        }
        if (n_ % 50 == 0) refresh_devices();   // a car connects by itself when it starts
    }

    bool on_key(uint32_t key) override {
        if (key == 'x' || key == 'X' || key == LV_KEY_DEL) {
            int i = focused();
            if (i >= 0 && rows_[i].kind == Row::Device && rows_[i].dev.paired) {
                hal().bt_forget(rows_[i].dev.address);
                shell_toast("Forgot " + rows_[i].dev.name);
                devices_ = hal().bt_devices();
                rebuild(i);
            }
            return true;
        }
        return false;
    }

private:
    struct Row {
        enum Kind { Usb, Device, Scan, Visible } kind;
        BtDevice dev;
    };

    std::string usb_text() const {
        switch (stick_.state) {
            case UsbStickStatus::Off: return "USB stick mode: off";
            case UsbStickStatus::Building: return fmt("Copying music %d%%", stick_.percent);
            case UsbStickStatus::Ready: return "USB stick mode: on";
            case UsbStickStatus::Attached: return "USB stick: in use";
            case UsbStickStatus::Error: return "USB stick mode: failed";
        }
        return "";
    }

    void rebuild(int focus_index) {
        if (list_) {
            lv_group_remove_all_objs(group);
            lv_obj_delete(list_);
        }
        rows_.clear();
        list_ = make_list(root_, 4, 4, kListW, kContentH - 8);
        usb_row_ = add(LV_SYMBOL_USB, usb_text(), {Row::Usb, {}});
        lv_list_add_text(list_, "Bluetooth");
        for (auto& d : devices_) {
            std::string text = d.name + (d.connected ? "  " LV_SYMBOL_OK : "");
            lv_obj_t* b = add(d.connected ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_BLUETOOTH, text, {Row::Device, d});
            if (!d.paired) lv_obj_set_style_text_color(b, c(kMuted), 0);
        }
        add(ZS_ICON_SEARCH, "Scan for devices", {Row::Scan, {}});
        add(LV_SYMBOL_EYE_OPEN, "Let the car find me", {Row::Visible, {}});
        finish_list(list_);
        if (focus_index >= int(buttons_.size())) focus_index = int(buttons_.size()) - 1;
        if (focus_index >= 0) lv_group_focus_obj(buttons_[focus_index]);
        show_panel(focus_index);
    }

    lv_obj_t* add(const char* symbol, const std::string& text, Row row) {
        if (rows_.empty()) buttons_.clear();
        lv_obj_t* b = list_button(list_, symbol, text.c_str(), on_click, this);
        lv_obj_set_user_data(b, (void*)(intptr_t)rows_.size());
        lv_obj_add_event_cb(b, on_focus, LV_EVENT_FOCUSED, this);
        lv_obj_t* lbl = lv_obj_get_child(b, 1);
        if (lbl) lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        rows_.push_back(std::move(row));
        buttons_.push_back(b);
        return b;
    }

    int focused() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (!f || !list_ || lv_obj_get_parent(f) != list_) return -1;
        return int((intptr_t)lv_obj_get_user_data(f));
    }

    // The panel on the right explains the row in focus.
    void show_panel(int i) {
        if (!head_ || i < 0 || i >= int(rows_.size())) return;
        const Row& r = rows_[i];
        std::string head, help;
        uint32_t color = kText;
        switch (r.kind) {
            case Row::Usb:
                switch (stick_.state) {
                    case UsbStickStatus::Off:
                        head = "USB stick";
                        help = "The car plays ~/Music as if from a USB stick.\nSwitch on the left: Slave. Cable in the "
                               "right USB-C port.";
                        break;
                    case UsbStickStatus::Building:
                        head = fmt("Copying %d%%", stick_.percent);
                        help = "Making the stick from ~/Music. Only needed when the music changed.";
                        color = kWarn;
                        break;
                    case UsbStickStatus::Ready:
                        head = "Ready";
                        help = "Plug the right USB-C port into the car. Switch on the left: Slave.";
                        color = kGood;
                        break;
                    case UsbStickStatus::Attached:
                        head = "In use";
                        help = "The car is reading the stick. Use the car's buttons.\nEnter turns it off.";
                        color = kGood;
                        break;
                    case UsbStickStatus::Error:
                        head = "Failed";
                        help = stick_.error;
                        color = kBad;
                        break;
                }
                break;
            case Row::Device:
                head = r.dev.name;
                color = r.dev.connected ? kGood : kText;
                help = r.dev.connected  ? "Connected: sound goes here. Its play and skip buttons work.\nEnter disconnects."
                       : r.dev.paired   ? "Paired.\nEnter connects.\nX forgets it."
                                        : "Found nearby.\nEnter pairs and connects.";
                break;
            case Row::Scan:
                head = "Scan";
                help = "Put the car, speaker or headphones in pairing mode first. Takes 8 seconds.";
                break;
            case Row::Visible:
                head = "Be found";
                help = "For cars that search for a phone: shows this Cardputer for 2 minutes. Pick it on the car's screen.";
                break;
        }
        lv_label_set_text(head_, head.c_str());
        lv_obj_set_style_text_color(head_, c(color), 0);
        lv_label_set_text(help_, help.c_str());
        lv_obj_update_layout(head_);
        lv_obj_set_pos(help_, 0, lv_obj_get_height(head_) + 4);
    }

    void refresh_devices() {
        std::vector<BtDevice> now = hal().bt_devices();
        bool same = now.size() == devices_.size();
        for (size_t i = 0; same && i < now.size(); ++i)
            same = now[i].address == devices_[i].address && now[i].connected == devices_[i].connected &&
                   now[i].paired == devices_[i].paired;
        if (same) return;
        devices_ = std::move(now);
        rebuild(focused());
    }

    // These wait on Bluetooth; say so before the screen stops updating.
    void busy(const std::string& what) {
        shell_toast(what);
        lv_refr_now(nullptr);
    }

    void activate(int i) {
        if (i < 0 || i >= int(rows_.size())) return;
        const Row r = rows_[i];   // a copy: rebuilding replaces rows_
        switch (r.kind) {
            case Row::Usb:
                if (stick_.state == UsbStickStatus::Off || stick_.state == UsbStickStatus::Error) {
                    hal().usb_stick_start();
                    shell_toast("USB stick mode on");
                } else {
                    hal().usb_stick_stop();
                    shell_toast("USB stick mode off");
                }
                n_ = 5;   // look at the result soon
                return;
            case Row::Device:
                if (r.dev.connected) {
                    busy("Disconnecting...");
                    hal().bt_disconnect(r.dev.address);
                    shell_toast("Disconnected " + r.dev.name);
                } else {
                    busy(r.dev.paired ? "Connecting..." : "Pairing...");
                    std::string error;
                    if (hal().bt_connect(r.dev.address, error)) shell_toast("Sound now goes to " + r.dev.name);
                    else shell_toast(error);
                }
                break;
            case Row::Scan:
                busy("Scanning for 8 seconds...");
                hal().bt_scan();
                break;
            case Row::Visible:
                hal().bt_set_visible(120);
                shell_toast("Visible as \"cardputer\" for 2 minutes");
                return;
        }
        devices_ = hal().bt_devices();
        rebuild(i);
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<CarApp*>(lv_event_get_user_data(e));
        self->pending_ = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        // Deferred: activating rebuilds the list, which deletes the clicked button.
        lv_async_call([](void* p) {
            auto* s = static_cast<CarApp*>(p);
            s->activate(s->pending_);
        }, self);
    }
    static void on_focus(lv_event_t* e) {
        auto* self = static_cast<CarApp*>(lv_event_get_user_data(e));
        self->show_panel(int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e))));
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* head_ = nullptr;
    lv_obj_t* help_ = nullptr;
    lv_obj_t* usb_row_ = nullptr;
    std::vector<Row> rows_;
    std::vector<lv_obj_t*> buttons_;
    std::vector<BtDevice> devices_;
    UsbStickStatus stick_;
    int n_ = 0;
    int pending_ = -1;
};
}  // namespace

std::unique_ptr<App> make_car_app() { return std::make_unique<CarApp>(); }

}  // namespace zs
