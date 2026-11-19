// IR remote: a pad of buttons that send stored codes; L learns a new code
// for the selected button from any remote pointed at the receiver.
// Codes persist in ~/.config/zeroos/ir-remote.conf as "name protocol 0xSCANCODE".
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

std::string conf_path() { return home_dir() + "/.config/zeroos/ir-remote.conf"; }

class IrApp : public App {
public:
    void build(lv_obj_t* root) override {
        load();
        lv_obj_t* grid = lv_obj_create(root);
        style_plain(grid);
        lv_obj_set_size(grid, 220, kContentH);
        lv_obj_set_pos(grid, 0, 0);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_all(grid, 6, 0);
        lv_obj_set_style_pad_gap(grid, 6, 0);
        lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

        for (size_t i = 0; i < codes_.size(); ++i) {
            lv_obj_t* b = lv_button_create(grid);
            lv_obj_set_size(b, 64, 40);
            lv_obj_set_style_radius(b, 8, 0);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_set_style_bg_color(b, c(i == 0 ? 0x5a1d1d : kSurface), 0);
            lv_obj_set_style_bg_color(b, c(kAccent), LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(b, c(kText), 0);
            lv_obj_set_style_text_color(b, c(kOnAccent), LV_STATE_FOCUSED);
            lv_obj_t* l = lv_label_create(b);
            lv_label_set_text(l, codes_[i].name.c_str());
            lv_obj_center(l);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
            lv_obj_add_event_cb(b, on_click, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(b, on_focus, LV_EVENT_FOCUSED, this);
            shell_add_focus(b);
            buttons_.push_back(b);
        }

        lv_obj_t* side = lv_obj_create(root);
        style_panel(side);
        lv_obj_set_pos(side, 222, 6);
        lv_obj_set_size(side, 92, 138);
        lv_obj_remove_flag(side, LV_OBJ_FLAG_SCROLLABLE);
        info_ = label(side, "", &zs_font_12, kText);
        lv_obj_set_width(info_, 80);
        lv_label_set_long_mode(info_, LV_LABEL_LONG_WRAP);
        lv_obj_t* h = label(side, "Enter send\nL learn\nArrows move", &zs_font_12, kMuted);
        lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        show_info(0);
    }

    bool on_key(uint32_t key) override {
        int n = int(buttons_.size());
        lv_obj_t* f = lv_group_get_focused(group);
        int i = 0;
        for (int k = 0; k < n; ++k)
            if (buttons_[k] == f) i = k;
        switch (key) {
            case LV_KEY_RIGHT: lv_group_focus_obj(buttons_[(i + 1) % n]); return true;
            case LV_KEY_LEFT: lv_group_focus_obj(buttons_[(i + n - 1) % n]); return true;
            case LV_KEY_DOWN: lv_group_focus_obj(buttons_[(i + 3) % n]); return true;
            case LV_KEY_UP: lv_group_focus_obj(buttons_[(i + n - 3) % n]); return true;
            case 'l':
            case 'L': learn(i); return true;
        }
        return false;
    }

private:
    void load() {
        codes_ = {
            {"Power", "nec", 0x40bf12}, {"Mute", "nec", 0x40bf10}, {"Input", "nec", 0x40bf0f},
            {"Vol +", "nec", 0x40bf1a}, {"Vol -", "nec", 0x40bf1e}, {"OK", "nec", 0x40bf1f},
            {"Ch +", "nec", 0x40bf1b},  {"Ch -", "nec", 0x40bf1c},  {"Menu", "nec", 0x40bf0d},
        };
        std::ifstream f(conf_path());
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream in(line);
            std::string name, proto, sc;
            if (!(in >> std::quoted(name) >> proto >> sc)) continue;
            for (auto& c : codes_) {
                if (c.name == name) {
                    c.protocol = proto;
                    c.scancode = unsigned(std::strtoul(sc.c_str(), nullptr, 16));
                }
            }
        }
    }

    void save() {
        std::string dir = home_dir() + "/.config";
        mkdir(dir.c_str(), 0755);
        mkdir((dir + "/zeroos").c_str(), 0755);
        std::ofstream f(conf_path());
        for (auto& c : codes_) f << std::quoted(c.name) << ' ' << c.protocol << ' ' << fmt("0x%x", c.scancode) << '\n';
    }

    void show_info(int i) {
        if (!info_) return;  // focus events fire while the pad is still being built
        auto& c = codes_[i];
        lv_label_set_text(info_, fmt("%s\n%s\n0x%06x", c.name.c_str(), c.protocol.c_str(), c.scancode).c_str());
    }

    void learn(int i) {
        shell_toast("Point a remote and press " + codes_[i].name);
        lv_refr_now(nullptr);
        IrCode got;
        if (hal().ir_learn(got, 5000)) {
            codes_[i].protocol = got.protocol;
            codes_[i].scancode = got.scancode;
            save();
            show_info(i);
            shell_toast("Learned " + codes_[i].name);
        } else {
            shell_toast("Nothing received");
        }
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
        int i = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        bool ok = hal().ir_send(self->codes_[i]);
        shell_toast(ok ? "Sent " + self->codes_[i].name : "IR send failed");
    }
    static void on_focus(lv_event_t* e) {
        auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
        self->show_info(int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e))));
    }

    std::vector<IrCode> codes_;
    std::vector<lv_obj_t*> buttons_;
    lv_obj_t* info_ = nullptr;
};
}  // namespace

std::unique_ptr<App> make_ir_app() { return std::make_unique<IrApp>(); }

}  // namespace zs
