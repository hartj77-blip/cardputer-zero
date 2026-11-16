// Mesh: the Cardputer as a Meshtastic LoRa node (Cap LoRa-1262), for text
// messages with no phone signal or internet. meshtasticd runs the radio.
//   Chat page (when the mesh is on): messages on the primary channel; type
//     and press Enter to send to everyone. Tab: the info page.
//   Info page: on/off, QR code for the web client (chat from a phone),
//     open it on the monitor in Dock mode, GPS. Tab or Esc: back to chat.
// See docs/EXPANSION.md.
#include "apps/common.h"
#include "ui/power.h"

namespace zs {
namespace {
using namespace theme;

class MeshApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        rebuild();
    }

    void tick() override {
        if (dirty_) {
            dirty_ = false;
            rebuild();
            return;
        }
        if (++n_ % 5) return;
        if (chat_ && hal().mesh_seq() != seq_) fill_messages();
        if (n_ % 10) return;
        MeshStatus m = hal().mesh();
        if (sig(m) != sig_) rebuild();
    }

    bool on_key(uint32_t key) override {
        if (key != 9 || !can_chat()) return false;   // Tab: chat <-> info
        info_ = !info_;
        dirty_ = true;
        return true;
    }

    bool on_back() override {
        if (info_ && can_chat()) {
            info_ = false;
            dirty_ = true;
            return true;
        }
        return false;
    }

private:
    static std::string sig(const MeshStatus& m) {
        return std::to_string(m.running) + std::to_string(m.no_radio) + std::to_string(m.connected) + m.web_url;
    }
    bool can_chat() const { return st_.running && st_.connected && !st_.no_radio; }

    void rebuild() {
        st_ = hal().mesh();
        sig_ = sig(st_);
        if (st_.running) starting_ = false;
        if (ta_) draft_ = lv_textarea_get_text(ta_);
        lv_obj_clean(root_);
        chat_ = ta_ = nullptr;
        if (group) lv_group_remove_all_objs(group);
        if (can_chat() && !info_) {
            build_chat();
            return;
        }

        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 150, kContentH - 10);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(panel, 3, 0);

        if (st_.running && !st_.web_url.empty() && !st_.no_radio) {
            lv_obj_t* qr = lv_qrcode_create(panel);
            lv_qrcode_set_size(qr, 84);
            lv_qrcode_set_dark_color(qr, c(0x000000));
            lv_qrcode_set_light_color(qr, c(0xffffff));
            lv_qrcode_update(qr, st_.web_url.c_str(), uint32_t(st_.web_url.size()));
            lv_obj_set_style_border_color(qr, c(0xffffff), 0);
            lv_obj_set_style_border_width(qr, 4, 0);
            label(panel, "Scan to chat", &lv_font_montserrat_12, kText);
            lv_obj_t* a = label(panel, ("App: " + st_.api).c_str(), &lv_font_montserrat_12, kMuted);
            lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
            lv_obj_set_width(a, 136);
        } else {
            const char* head = !st_.installed ? "Not installed"
                               : st_.running && st_.no_radio ? "No radio"
                               : st_.running ? LV_SYMBOL_OK " Mesh on"
                               : starting_   ? "Starting..."
                                             : "Mesh off";
            lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            label(panel, head, &lv_font_montserrat_16, st_.running && st_.no_radio ? kBad : kText);
            const char* hint = !st_.installed ? "meshtasticd comes with ZeroOS images."
                               : st_.no_radio ? "Plug in the Cap LoRa-1262 with the power off, then turn the mesh on."
                               : st_.running ? "Connect to Wi-Fi to chat from your phone."
                                             : "Text over LoRa radio, miles away, with no signal or internet.";
            lv_obj_t* h = label(panel, hint, &lv_font_montserrat_12, kMuted);
            lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(h, 136);
        }

        lv_obj_t* list = make_list(root_, 162, 4, 152, kContentH - 10);
        if (st_.installed) {
            if (!st_.running && !starting_) {
                list_button(list, LV_SYMBOL_POWER, "Turn on mesh", [](lv_event_t* e) {
                    hal().mesh_start();
                    auto* self = static_cast<MeshApp*>(lv_event_get_user_data(e));
                    self->starting_ = true;
                    self->dirty_ = true;
                    shell_toast("Starting the radio...");
                }, this);
            } else {
                list_button(list, LV_SYMBOL_POWER, "Turn off mesh", [](lv_event_t* e) {
                    hal().mesh_stop();
                    hal().set_ext_power(false);
                    auto* self = static_cast<MeshApp*>(lv_event_get_user_data(e));
                    self->starting_ = false;
                    self->dirty_ = true;
                    shell_toast("Mesh off");
                }, this);
            }
            if (st_.running) {
                list_button(list, LV_SYMBOL_IMAGE, "Open on monitor", [](lv_event_t* e) {
                    auto* self = static_cast<MeshApp*>(lv_event_get_user_data(e));
                    if (!power().docked()) {
                        shell_toast("Needs Dock mode (a monitor)");
                        return;
                    }
                    hal().dock_open("browser", "https://localhost:9443");
                    (void)self;
                    shell_toast("Opening Meshtastic on the monitor");
                }, this);
            }
        }
        if (can_chat()) {
            list_button(list, LV_SYMBOL_ENVELOPE, "Chat (Tab)", [](lv_event_t* e) {
                auto* self = static_cast<MeshApp*>(lv_event_get_user_data(e));
                self->info_ = false;
                self->dirty_ = true;
            }, this);
        }
        list_button(list, LV_SYMBOL_GPS, "GPS", [](lv_event_t*) { shell_open_app("gps"); }, nullptr);
        finish_list(list);
    }

    void build_chat() {
        std::string head = st_.my_name.empty() ? std::string("Mesh") : st_.my_name;
        head += fmt("  |  %d nodes  |  Tab: info", st_.nodes);
        lv_obj_t* h = label(root_, head.c_str(), &lv_font_montserrat_12, kMuted);
        lv_obj_set_pos(h, 8, 2);

        chat_ = lv_obj_create(root_);
        style_panel(chat_);
        lv_obj_set_pos(chat_, 4, 18);
        lv_obj_set_size(chat_, kScreenW - 8, kContentH - 58);
        lv_obj_set_flex_flow(chat_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(chat_, 4, 0);
        lv_obj_set_style_pad_row(chat_, 2, 0);
        fill_messages();

        ta_ = lv_textarea_create(root_);
        lv_textarea_set_one_line(ta_, true);
        lv_textarea_set_placeholder_text(ta_, "Message everyone, Enter to send");
        lv_textarea_set_max_length(ta_, 200);
        lv_textarea_set_text(ta_, draft_.c_str());
        lv_obj_set_width(ta_, kScreenW - 8);
        lv_obj_set_pos(ta_, 4, kContentH - 37);
        lv_obj_add_event_cb(ta_, [](lv_event_t* e) {
            auto* self = static_cast<MeshApp*>(lv_event_get_user_data(e));
            std::string text = lv_textarea_get_text(self->ta_);
            if (text.empty()) return;
            if (hal().mesh_send(text)) {
                lv_textarea_set_text(self->ta_, "");
                self->fill_messages();
            } else {
                shell_toast("Not sent: mesh not ready");
            }
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta_);
        lv_group_focus_obj(ta_);
        lv_group_set_editing(group, true);
    }

    void fill_messages() {
        if (!chat_) return;
        seq_ = hal().mesh_seq();
        lv_obj_clean(chat_);
        auto msgs = hal().mesh_messages();
        size_t first = msgs.size() > 30 ? msgs.size() - 30 : 0;   // keep the screen light
        if (msgs.empty()) {
            label(chat_, "No messages yet. Say hello!", &lv_font_montserrat_12, kMuted);
        }
        for (size_t i = first; i < msgs.size(); ++i) {
            const MeshMessage& m = msgs[i];
            std::string who = m.mine ? "You" : m.from + (m.direct ? " (to you)" : "");
            lv_obj_t* l = lv_label_create(chat_);
            lv_label_set_text(l, (who + ": " + m.text).c_str());
            lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(l, c(m.mine ? kGood : m.direct ? kAccent : kText), 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, kScreenW - 24);
        }
        lv_obj_update_layout(chat_);
        lv_obj_scroll_to_y(chat_, LV_COORD_MAX, LV_ANIM_OFF);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* chat_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    MeshStatus st_;
    std::string sig_, draft_;
    bool starting_ = false, dirty_ = false, info_ = false;
    int n_ = 0, seq_ = -1;
};
}  // namespace

std::unique_ptr<App> make_mesh_app() { return std::make_unique<MeshApp>(); }

}  // namespace zs
