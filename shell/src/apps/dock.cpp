// Dock: use an HDMI monitor as a desktop (labwc), with web browser, video,
// terminal and files there, while this screen shows the status.
//
// Not docked: Start Dock mode, the monitor resolution, and what happens when
// a monitor is plugged in. Docked: the Cardputer's keys type on the monitor,
// so this screen only shows status; leave from the monitor's top bar or menu
// (or Ctrl+Alt+Del there), or unplug the monitor.
#include <functional>
#include <vector>

#include "apps/common.h"
#include "ui/power.h"
#include "ui/prefs.h"

namespace zs {
namespace {
using namespace theme;

struct Choice {
    const char* label;
    const char* value;
};
const std::vector<Choice> kOnHdmi = {{"Ask", "ask"}, {"Start dock", "auto"}, {"Do nothing", "off"}};
const std::vector<Choice> kRes = {{"720p", "720p"}, {"1080p", "1080p"}, {"Monitor's best", "auto"}};

class DockApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        rebuild();
    }

    void tick() override {
        if (++n_ % 5) return;
        DisplayStatus d = hal().display();
        if (d.dock_running != docked_ || d.hdmi_connected != hdmi_) rebuild();
        else refresh(d);
    }

private:
    void rebuild() {
        lv_obj_clean(root_);
        hint_ = nullptr;
        if (group) lv_group_remove_all_objs(group);
        DisplayStatus d = hal().display();
        docked_ = d.dock_running;
        hdmi_ = d.hdmi_connected;

        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, docked_ ? 308 : 150, kContentH - 10);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, 0);

        headline_ = label(panel, "", &zs_font_16, docked_ ? kGood : kText);
        monitor_ = kv_row(panel, "Monitor", "");
        mem_ = kv_row(panel, "Free memory", "");
        if (docked_) {
            lv_obj_t* hint = label(panel,
                                   "The keyboard now types on the monitor.\n"
                                   "Leave from the monitor's top bar, or\n"
                                   "unplug the monitor.",
                                   &zs_font_12, kMuted);
            lv_obj_set_width(hint, 290);
        } else {
            hint_ = label(panel, "", &zs_font_12, kMuted);
            lv_label_set_long_mode(hint_, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(hint_, 136);
            build_controls();
        }
        refresh(d);
    }

    void build_controls() {
        lv_obj_t* list = make_list(root_, 162, 4, 152, kContentH - 10);
        start_ = list_button(list, LV_SYMBOL_PLAY, "Start Dock mode", [](lv_event_t* e) {
            if (!hal().display().hdmi_connected) {
                shell_toast("Plug in a monitor first");
                return;
            }
            hal().dock_start();
            shell_toast("Starting the desktop...");   // tick() redraws once it runs
        }, this);
        on_hdmi_ = cycle_button(list, "On plug-in", kOnHdmi, "dock.on_hdmi", "ask");
        res_ = cycle_button(list, "Size", kRes, "dock.resolution", "720p");
        finish_list(list);
    }

    // A list button whose value steps through choices on Enter.
    lv_obj_t* cycle_button(lv_obj_t* list, const char* name, const std::vector<Choice>& choices,
                           const char* key, const char* def) {
        std::string cur = prefs::get_str(key, def);
        size_t at = 0;
        for (size_t i = 0; i < choices.size(); ++i)
            if (cur == choices[i].value) at = i;
        auto* st = new CycleState{name, &choices, key, at, nullptr};
        lv_obj_t* b = list_button(list, LV_SYMBOL_SETTINGS, "", [](lv_event_t* e) {
            auto* s = static_cast<CycleState*>(lv_event_get_user_data(e));
            s->at = (s->at + 1) % s->choices->size();
            prefs::set_str(s->key, (*s->choices)[s->at].value);
            s->show();
        }, st);
        st->btn = b;
        st->show();
        lv_obj_add_event_cb(b, [](lv_event_t* e) { delete static_cast<CycleState*>(lv_event_get_user_data(e)); },
                            LV_EVENT_DELETE, st);
        return b;
    }

    struct CycleState {
        const char* name;
        const std::vector<Choice>* choices;
        const char* key;
        size_t at;
        lv_obj_t* btn;
        void show() {
            // list_button's label is the button's last child.
            lv_obj_t* l = lv_obj_get_child(btn, int32_t(lv_obj_get_child_count(btn)) - 1);
            lv_label_set_text(l, fmt("%s: %s", name, (*choices)[at].label).c_str());
        }
    };

    void refresh(const DisplayStatus& d) {
        SystemInfo s = hal().system();
        lv_label_set_text(headline_, docked_ ? LV_SYMBOL_OK " Docked" : hdmi_ ? "Monitor ready" : "No monitor");
        lv_label_set_text(monitor_, d.hdmi_connected ? (d.hdmi_mode.empty() ? "connected" : d.hdmi_mode.c_str())
                                                     : "none");
        lv_label_set_text(mem_, fmt("%d MB", s.mem_avail_mb).c_str());
        if (hint_)
            lv_label_set_text(hint_, hdmi_ ? "Opens a desktop with web, video, terminal and files on the monitor."
                                           : "Plug a monitor into the HDMI port. A USB keyboard and mouse help.");
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t *headline_ = nullptr, *monitor_ = nullptr, *mem_ = nullptr, *hint_ = nullptr;
    lv_obj_t *start_ = nullptr, *on_hdmi_ = nullptr, *res_ = nullptr;
    bool docked_ = false, hdmi_ = false;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_dock_app() { return std::make_unique<DockApp>(); }

}  // namespace zs
