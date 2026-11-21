// Update: a newer ZeroOS over the network, with no SD card re-flash
// (docs/UPDATE.md). Any connection will do: Wi-Fi, Ethernet, a tethered
// phone. zeroos-update does the work as root; this screen starts it and
// shows how far it has got.
//
//   Enter  look for an update / install the one found
#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

class UpdateApp : public App {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* panel = lv_obj_create(root);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 6);
        lv_obj_set_size(panel, kScreenW - 12, kContentH - 12);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        const int w = kScreenW - 12 - 24;

        version_ = label(panel, "", &zs_font_12, kMuted);
        lv_obj_set_pos(version_, 0, 0);
        head_ = label(panel, "", &zs_font_16, kText);
        lv_obj_set_pos(head_, 0, 18);
        help_ = label(panel, "", &zs_font_12, kMuted);
        lv_obj_set_width(help_, w);
        lv_label_set_long_mode(help_, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(help_, 0, 42);
        bar_ = thin_bar(panel, w, 6, kAccent);
        lv_obj_align(bar_, LV_ALIGN_BOTTOM_LEFT, 0, -22);
        lv_bar_set_range(bar_, 0, 100);
        hint_ = label(panel, "", &zs_font_12, kText);
        lv_obj_align(hint_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        st_ = hal().update();
        show();
        // Look straight away, unless an update is in the middle of something
        // or has just finished.
        if (st_.state == UpdateStatus::Idle || st_.state == UpdateStatus::UpToDate ||
            st_.state == UpdateStatus::Offline)
            check();
    }

    void tick() override {
        if (++n_ % 5 != 0) return;   // twice a second
        bool waited = wait_ > 0 && --wait_ == 0;
        UpdateStatus st = hal().update();
        bool changed = st.state != st_.state || st.percent != st_.percent || st.current != st_.current ||
                       st.latest != st_.latest;
        st_ = st;
        if (changed || waited) show();
    }

    bool on_key(uint32_t key) override {
        if (key != LV_KEY_ENTER) return false;
        if (busy()) return true;
        if (st_.state == UpdateStatus::Available) install();
        else check();
        return true;
    }

private:
    // The service takes a moment to write its first state; until then the
    // old one is still in the file. wait_ covers that gap.
    bool busy() const { return wait_ > 0 || st_.busy(); }

    void check() {
        NetStatus n = hal().net();
        if (!n.wifi_connected && !n.ethernet && n.tether.empty()) {
            offline_ = true;
            show();
            return;
        }
        offline_ = false;
        asked_ = Asked::Check;
        wait_ = 4;
        hal().update_check();
        show();
    }

    void install() {
        BatteryInfo b = hal().battery();
        if (b.percent >= 0 && b.percent < 20 && !b.charging && !b.external_power) {
            shell_toast("Battery under 20%: plug in first");
            return;
        }
        asked_ = Asked::Install;
        wait_ = 4;
        hal().update_install();
        show();
    }

    void show() {
        std::string head, help, hint;
        uint32_t color = kText;
        int percent = -1;   // -1: no bar
        UpdateStatus::State s = st_.state;
        if (wait_ > 0) s = asked_ == Asked::Install ? UpdateStatus::Downloading : UpdateStatus::Checking;
        if (offline_ && wait_ == 0 && !st_.busy()) s = UpdateStatus::Offline;

        switch (s) {
            case UpdateStatus::Idle:
                head = "Software update";
                help = "Updates come over Wi-Fi, Ethernet or a tethered phone.";
                hint = "Enter: look for an update";
                break;
            case UpdateStatus::Checking:
                head = "Looking...";
                help = "Asking the update server for the newest version.";
                break;
            case UpdateStatus::UpToDate:
                head = "Up to date";
                color = kGood;
                help = "This is the newest version.";
                hint = "Enter: look again";
                break;
            case UpdateStatus::Available:
                head = "Version " + st_.latest + " is ready";
                color = kAccent;
                help = "Takes a minute or two. The screen restarts at the end; your files and settings stay.";
                hint = "Enter: install";
                break;
            case UpdateStatus::Offline:
                head = "Not online";
                color = kWarn;
                help = offline_ ? "Join a Wi-Fi network, plug in Ethernet or tether a phone first."
                                : "The update server did not answer. Check the connection.";
                hint = "Enter: try again";
                break;
            case UpdateStatus::Downloading:
                head = fmt("Downloading %d%%", wait_ > 0 ? 0 : st_.percent);
                color = kAccent;
                help = "Keep the Cardputer on.";
                percent = wait_ > 0 ? 0 : st_.percent;
                break;
            case UpdateStatus::Installing:
                head = "Installing " + st_.latest;
                color = kAccent;
                help = "Keep the Cardputer on.";
                percent = 100;
                break;
            case UpdateStatus::Restarting:
                head = "Restarting the screen";
                color = kAccent;
                help = "Back in a few seconds.";
                percent = 100;
                break;
            case UpdateStatus::Done:
                head = "Updated";
                color = kGood;
                help = st_.message.empty() ? "The new version is running." : st_.message + ".";
                hint = "Enter: look again";
                break;
            case UpdateStatus::RolledBack:
                head = "Update undone";
                color = kWarn;
                help = st_.message.empty() ? "The new version would not start. The old one is back." : st_.message + ".";
                hint = "Enter: look again";
                break;
            case UpdateStatus::Failed:
                head = "Update failed";
                color = kBad;
                help = st_.message.empty() ? "Something went wrong. Nothing was changed." : st_.message + ".";
                hint = "Enter: try again";
                break;
        }
        lv_label_set_text(version_, ("ZeroOS " + (st_.current.empty() ? std::string("?") : st_.current)).c_str());
        lv_label_set_text(head_, head.c_str());
        lv_obj_set_style_text_color(head_, c(color), 0);
        lv_label_set_text(help_, help.c_str());
        lv_label_set_text(hint_, hint.c_str());
        if (percent < 0) {
            lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(bar_, percent, LV_ANIM_OFF);
        }
    }

    enum class Asked { None, Check, Install };

    lv_obj_t *version_ = nullptr, *head_ = nullptr, *help_ = nullptr, *bar_ = nullptr, *hint_ = nullptr;
    UpdateStatus st_;
    Asked asked_ = Asked::None;
    bool offline_ = false;
    int wait_ = 0;   // half seconds
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_update_app() { return std::make_unique<UpdateApp>(); }

}  // namespace zs
