// Clock with a stopwatch. Time comes from the RX8130CE RTC, so it is right
// even with no network. Space starts/stops the stopwatch, R resets it.
#include <ctime>

#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

class ClockApp : public App {
public:
    void build(lv_obj_t* root) override {
        time_ = label(root, "", &zs_font_28);
        lv_obj_align(time_, LV_ALIGN_TOP_MID, 0, 14);
        date_ = label(root, "", &zs_font_14, kMuted);
        lv_obj_align(date_, LV_ALIGN_TOP_MID, 0, 50);

        lv_obj_t* sw = lv_obj_create(root);
        style_panel(sw);
        lv_obj_set_size(sw, 220, 58);
        lv_obj_align(sw, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_remove_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* t = label(sw, LV_SYMBOL_BELL "  Stopwatch", &zs_font_12, kMuted);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
        sw_ = label(sw, "00:00.0", &zs_font_20, kAccent);
        lv_obj_align(sw_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_t* h = label(sw, "Space start/stop\nR reset", &zs_font_12, kMuted);
        lv_obj_align(h, LV_ALIGN_RIGHT_MID, 0, 0);
        tick();
    }

    void tick() override {
        time_t now = ::time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        char buf[64];
        strftime(buf, sizeof buf, "%H:%M:%S", &lt);
        lv_label_set_text(time_, buf);
        strftime(buf, sizeof buf, "%A, %d %B %Y", &lt);
        lv_label_set_text(date_, buf);

        uint32_t ms = elapsed_ + (running_ ? lv_tick_elaps(start_) : 0);
        lv_label_set_text(sw_, fmt("%02u:%02u.%u", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10).c_str());
    }

    bool on_key(uint32_t key) override {
        if (key == ' ' || key == LV_KEY_ENTER) {
            if (running_) elapsed_ += lv_tick_elaps(start_);
            else start_ = lv_tick_get();
            running_ = !running_;
            return true;
        }
        if (key == 'r' || key == 'R') {
            running_ = false;
            elapsed_ = 0;
            return true;
        }
        return false;
    }

private:
    lv_obj_t *time_, *date_, *sw_;
    bool running_ = false;
    uint32_t start_ = 0, elapsed_ = 0;
};
}  // namespace

std::unique_ptr<App> make_clock_app() { return std::make_unique<ClockApp>(); }

}  // namespace zs
