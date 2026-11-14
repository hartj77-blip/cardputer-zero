// Battery: fuel-gauge readings from the BQ27220.
#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

class BatteryApp : public App {
public:
    void build(lv_obj_t* root) override {
        arc_ = lv_arc_create(root);
        lv_obj_set_size(arc_, 110, 110);
        lv_obj_set_pos(arc_, 8, 18);
        lv_arc_set_bg_angles(arc_, 135, 45);
        lv_arc_set_range(arc_, 0, 100);
        lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
        lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(arc_, 10, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc_, 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_, c(kSurface2), LV_PART_MAIN);

        pct_ = label(arc_, "--", &lv_font_montserrat_28);
        lv_obj_align(pct_, LV_ALIGN_CENTER, 0, -4);
        state_ = label(arc_, "", &lv_font_montserrat_12, kMuted);
        lv_obj_align(state_, LV_ALIGN_CENTER, 0, 22);

        lv_obj_t* panel = lv_obj_create(root);
        style_panel(panel);
        lv_obj_set_pos(panel, 128, 6);
        lv_obj_set_size(panel, 184, 138);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, 0);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        time_ = kv_row(panel, "Time left", "");
        volt_ = kv_row(panel, "Voltage", "");
        curr_ = kv_row(panel, "Current", "");
        temp_ = kv_row(panel, "Temperature", "");
        health_ = kv_row(panel, "Health", "");
        cycles_ = kv_row(panel, "Cycles", "");
        cap_ = kv_row(panel, "Capacity", "1750 mAh");
        refresh();
    }

    void tick() override {
        if (++n_ % 10 == 0) refresh();
    }

private:
    void refresh() {
        BatteryInfo b = hal().battery();
        lv_arc_set_value(arc_, b.percent < 0 ? 0 : b.percent);
        uint32_t col = b.charging ? kGood : b.percent < 15 ? kBad : b.percent < 30 ? kWarn : kAccent;
        lv_obj_set_style_arc_color(arc_, c(col), LV_PART_INDICATOR);
        lv_label_set_text(pct_, b.percent < 0 ? "--" : fmt("%d%%", b.percent).c_str());
        lv_label_set_text(state_, b.charging ? "Charging" : b.external_power ? "Plugged in" : "On battery");
        if (b.minutes_left >= 0)
            lv_label_set_text(time_, fmt("%dh %02dm%s", b.minutes_left / 60, b.minutes_left % 60,
                                         b.charging ? " to full" : "").c_str());
        else
            lv_label_set_text(time_, "--");
        lv_label_set_text(volt_, fmt("%.2f V", b.voltage_mv / 1000.0).c_str());
        lv_label_set_text(curr_, fmt("%d mA", b.current_ma).c_str());
        lv_label_set_text(temp_, fmt("%.1f C", b.temp_c10 / 10.0).c_str());
        lv_label_set_text(health_, b.health_percent < 0 ? "--" : fmt("%d%%", b.health_percent).c_str());
        lv_label_set_text(cycles_, b.cycle_count < 0 ? "--" : fmt("%d", b.cycle_count).c_str());
    }

    lv_obj_t *arc_, *pct_, *state_, *time_, *volt_, *curr_, *temp_, *health_, *cycles_, *cap_;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_battery_app() { return std::make_unique<BatteryApp>(); }

}  // namespace zs
