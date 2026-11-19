// GPS: where you are, from the Cap LoRa-1262's GPS or a GPS unit on the
// Grove port (through gpsd; see docs/EXPANSION.md).
//   Enter  save this spot to ~/Documents/waypoints.txt (synced by Sync)
// The expansion port's 5 V is on while the app is open (and while the mesh
// runs); leaving turns it off again, so each open is a fresh start (~30 s
// outdoors for a first fix).
#include <cmath>
#include <ctime>
#include <fstream>

#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

const char* compass_point(double deg) {
    static const char* k[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return k[int(std::fmod(deg + 22.5 + 360.0, 360.0) / 45.0) % 8];
}

class GpsApp : public App {
public:
    void build(lv_obj_t* root) override {
        powered_here_ = !hal().ext_power();
        if (powered_here_) hal().set_ext_power(true);
        opened_ = std::time(nullptr);

        status_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_set_pos(status_, 8, 4);
        lat_ = label(root, "", &zs_font_16, kText);
        lv_obj_set_pos(lat_, 8, 22);
        lon_ = label(root, "", &zs_font_16, kText);
        lv_obj_set_pos(lon_, 8, 42);

        lv_obj_t* panel = lv_obj_create(root);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 66);
        lv_obj_set_size(panel, kScreenW - 12, kContentH - 72);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN_WRAP);
        lv_obj_set_style_pad_row(panel, 2, 0);
        lv_obj_set_style_pad_column(panel, 14, 0);
        auto row = [&](const char* k) {
            lv_obj_t* r = lv_obj_create(panel);
            lv_obj_remove_style_all(r);
            lv_obj_set_size(r, 140, 18);
            label(r, k, &zs_font_12, kMuted);
            lv_obj_t* v = label(r, "-", &zs_font_12, kText);
            lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
            return v;
        };
        alt_ = row("Altitude");
        speed_ = row("Speed");
        head_ = row("Heading");
        acc_ = row("Accuracy");
        sats_ = row("Satellites");
        time_ = row("UTC");
        hint_ = label(root, "Enter: save spot", &zs_font_12, kMuted);
        lv_obj_align(hint_, LV_ALIGN_TOP_RIGHT, -8, 4);
        update();
    }

    ~GpsApp() override {
        hal().gps_stop();
        if (powered_here_ && !hal().mesh().running) hal().set_ext_power(false);
    }

    void tick() override {
        if (++n_ % 5 == 0) update();
    }

    bool on_key(uint32_t key) override {
        if (key != LV_KEY_ENTER) return false;
        if (fix_.mode < 2) {
            shell_toast("No fix yet");
            return true;
        }
        std::string dir = home_dir() + "/Documents";
        std::ofstream f(dir + "/waypoints.txt", std::ios::app);
        if (!f) {
            shell_toast("Couldn't save to ~/Documents");
            return true;
        }
        char line[160];
        std::snprintf(line, sizeof line, "%s  %.6f, %.6f  %.0f m\n",
                      fix_.time.empty() ? "?" : fix_.time.substr(0, 19).c_str(), fix_.lat, fix_.lon, fix_.alt_m);
        f << line;
        shell_toast("Saved to waypoints.txt");
        return true;
    }

private:
    void update() {
        fix_ = hal().gps();
        long waited = long(std::time(nullptr) - opened_);
        if (!fix_.device) {
            lv_label_set_text(status_, waited < 6 ? "Starting GPS..." : "No GPS found");
            lv_label_set_text(lat_, waited < 6 ? "" : "Plug in a Cap LoRa-1262 or");
            lv_label_set_text(lon_, waited < 6 ? "" : "a Grove GPS unit (power off first)");
            lv_obj_set_style_text_font(lat_, &zs_font_12, 0);
            lv_obj_set_style_text_font(lon_, &zs_font_12, 0);
            return;
        }
        lv_obj_set_style_text_font(lat_, &zs_font_16, 0);
        lv_obj_set_style_text_font(lon_, &zs_font_16, 0);
        const char* st = fix_.mode >= 3 ? LV_SYMBOL_GPS " 3D fix" : fix_.mode == 2 ? LV_SYMBOL_GPS " 2D fix"
                                                                                    : "Looking for satellites...";
        lv_label_set_text(status_, st);
        lv_obj_set_style_text_color(status_, c(fix_.mode >= 2 ? kGood : kMuted), 0);
        if (fix_.mode >= 2) {
            lv_label_set_text(lat_, fmt("%.6f %s", std::fabs(fix_.lat), fix_.lat >= 0 ? "N" : "S").c_str());
            lv_label_set_text(lon_, fmt("%.6f %s", std::fabs(fix_.lon), fix_.lon >= 0 ? "E" : "W").c_str());
            lv_label_set_text(alt_, fix_.mode >= 3 ? fmt("%.0f m", fix_.alt_m).c_str() : "-");
            lv_label_set_text(speed_, fmt("%.1f km/h", fix_.speed_mps * 3.6).c_str());
            lv_label_set_text(head_, fix_.speed_mps > 0.5 ? fmt("%.0f %s", fix_.track_deg, compass_point(fix_.track_deg)).c_str() : "-");
            lv_label_set_text(acc_, fix_.error_m > 0 ? fmt("%.0f m", fix_.error_m).c_str() : "-");
        } else {
            lv_label_set_text(lat_, "--.------");
            lv_label_set_text(lon_, "--.------");
        }
        lv_label_set_text(sats_, fmt("%d of %d", fix_.sats_used, fix_.sats_seen).c_str());
        lv_label_set_text(time_, fix_.time.size() >= 19 ? fix_.time.substr(11, 8).c_str() : "-");
    }

    lv_obj_t *status_ = nullptr, *lat_ = nullptr, *lon_ = nullptr, *hint_ = nullptr;
    lv_obj_t *alt_ = nullptr, *speed_ = nullptr, *head_ = nullptr, *acc_ = nullptr, *sats_ = nullptr, *time_ = nullptr;
    GpsFix fix_;
    bool powered_here_ = false;
    std::time_t opened_ = 0;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_gps_app() { return std::make_unique<GpsApp>(); }

}  // namespace zs
