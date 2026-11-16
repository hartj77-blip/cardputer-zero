// Mesh: the Cardputer as a Meshtastic LoRa node (Cap LoRa-1262), for text
// messages with no phone signal or internet. meshtasticd runs the radio; you
// chat from the Meshtastic phone app (connect by IP) or its web page, shown
// here as a QR code, or on the monitor in Dock mode. See docs/EXPANSION.md.
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
        if (++n_ % 10) return;
        MeshStatus m = hal().mesh();
        if (sig(m) != sig_) rebuild();
    }

private:
    static std::string sig(const MeshStatus& m) {
        return std::to_string(m.running) + std::to_string(m.no_radio) + m.web_url;
    }

    void rebuild() {
        st_ = hal().mesh();
        sig_ = sig(st_);
        if (st_.running) starting_ = false;
        lv_obj_clean(root_);
        if (group) lv_group_remove_all_objs(group);

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
        list_button(list, LV_SYMBOL_GPS, "GPS", [](lv_event_t*) { shell_open_app("gps"); }, nullptr);
        finish_list(list);
    }

    lv_obj_t* root_ = nullptr;
    MeshStatus st_;
    std::string sig_;
    bool starting_ = false, dirty_ = false;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_mesh_app() { return std::make_unique<MeshApp>(); }

}  // namespace zs
