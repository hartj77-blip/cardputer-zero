// Battery: fuel-gauge readings from the BQ27220, power draw, and the power
// profile (P or Tab switches Saver / Balanced / Performance).
#include "apps/common.h"
#include "ui/power.h"

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

        pct_ = label(arc_, "--", &zs_font_28);
        lv_obj_align(pct_, LV_ALIGN_CENTER, 0, -4);
        state_ = label(arc_, "", &zs_font_12, kMuted);
        lv_obj_align(state_, LV_ALIGN_CENTER, 0, 22);

        profile_ = label(root, "", &zs_font_12, kAccent);
        lv_obj_set_width(profile_, 120);
        lv_obj_set_style_text_align(profile_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(profile_, 3, 124);

        lv_obj_t* panel = lv_obj_create(root);
        style_panel(panel);
        lv_obj_set_pos(panel, 128, 6);
        lv_obj_set_size(panel, 184, 138);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, 0);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        time_ = kv_row(panel, "Time left", "");
        draw_ = kv_row(panel, "Power draw", "");
        volt_ = kv_row(panel, "Voltage", "");
        curr_ = kv_row(panel, "Current", "");
        temp_ = kv_row(panel, "Temperature", "");
        health_ = kv_row(panel, "Health", "");
        cycles_ = kv_row(panel, "Cycles", "");
        refresh();
    }

    bool on_key(uint32_t key) override {
        if (key == 'p' || key == 'P' || key == 9) {
            power().cycle_profile();
            shell_toast(std::string("Power profile: ") + Power::profile_name(power().profile()));
            refresh();
            return true;
        }
        return false;
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
        int left = power().minutes_left();
        if (left >= 0)
            lv_label_set_text(time_, fmt("%dh %02dm%s", left / 60, left % 60, b.charging ? " to full" : "").c_str());
        else
            lv_label_set_text(time_, b.external_power ? "plugged in" : "--");
        int mw = power().avg_power_mw();
        lv_label_set_text(draw_, mw > 0 ? fmt("%.2f W", mw / 1000.0).c_str() : "--");
        Profile ep = power().effective_profile();
        bool forced = ep != power().profile();
        lv_label_set_text(profile_, fmt(LV_SYMBOL_CHARGE " %s%s  [P]", Power::profile_name(ep), forced ? " (low)" : "").c_str());
        lv_label_set_text(volt_, fmt("%.2f V", b.voltage_mv / 1000.0).c_str());
        lv_label_set_text(curr_, fmt("%d mA", b.current_ma).c_str());
        lv_label_set_text(temp_, fmt("%.1f C", b.temp_c10 / 10.0).c_str());
        if (b.health_percent < 0) lv_label_set_text(health_, "--");
        else if (b.charge_full_mah > 0) lv_label_set_text(health_, fmt("%d%% (%d mAh)", b.health_percent, b.charge_full_mah).c_str());
        else lv_label_set_text(health_, fmt("%d%%", b.health_percent).c_str());
        lv_label_set_text(cycles_, b.cycle_count < 0 ? "--" : fmt("%d", b.cycle_count).c_str());
    }

    lv_obj_t *arc_, *pct_, *state_, *profile_, *time_, *draw_, *volt_, *curr_, *temp_, *health_, *cycles_;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_battery_app() { return std::make_unique<BatteryApp>(); }

}  // namespace zs
