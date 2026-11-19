// Compass and spirit level from the BMI270 + BMM150.
#include <cmath>

#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

const char* cardinal(float h) {
    static const char* n[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return n[int((h + 22.5f) / 45.0f) % 8];
}

class CompassApp : public App {
public:
    void build(lv_obj_t* root) override {
        // Dial
        dial_ = lv_obj_create(root);
        lv_obj_set_size(dial_, 136, 136);
        lv_obj_set_pos(dial_, 10, 7);
        lv_obj_set_style_radius(dial_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dial_, c(kSurface), 0);
        lv_obj_set_style_border_color(dial_, c(kSurface2), 0);
        lv_obj_set_style_border_width(dial_, 3, 0);
        lv_obj_remove_flag(dial_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(dial_, 0, 0);

        static const char* marks[] = {"N", "E", "S", "W"};
        for (int i = 0; i < 4; ++i) {
            marks_[i] = label(dial_, marks[i], &zs_font_16, i == 0 ? kBad : kMuted);
        }
        needle_ = lv_line_create(dial_);
        lv_obj_set_style_line_width(needle_, 4, 0);
        lv_obj_set_style_line_color(needle_, c(kAccent), 0);
        lv_obj_set_style_line_rounded(needle_, true, 0);

        hub_ = lv_obj_create(dial_);
        lv_obj_set_size(hub_, 10, 10);
        lv_obj_set_style_radius(hub_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(hub_, c(kText), 0);
        lv_obj_set_style_border_width(hub_, 0, 0);
        lv_obj_center(hub_);

        heading_ = label(root, "---", &zs_font_28);
        lv_obj_set_pos(heading_, 160, 10);
        card_ = label(root, "", &zs_font_20, kAccent);
        lv_obj_set_pos(card_, 160, 44);

        // Level: bubble moves inside a ring.
        lv_obj_t* ring = lv_obj_create(root);
        lv_obj_set_size(ring, 70, 70);
        lv_obj_set_pos(ring, 240, 74);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ring, c(kSurface), 0);
        lv_obj_set_style_border_color(ring, c(kSurface2), 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(ring, 0, 0);
        bubble_ = lv_obj_create(ring);
        lv_obj_set_size(bubble_, 14, 14);
        lv_obj_set_style_radius(bubble_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bubble_, c(kGood), 0);
        lv_obj_set_style_border_width(bubble_, 0, 0);
        lv_obj_center(bubble_);

        tilt_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_set_pos(tilt_, 160, 80);
        refresh();
    }

    void tick() override { refresh(); }

private:
    void refresh() {
        ImuSample s = hal().imu();
        if (!s.available) {
            lv_label_set_text(heading_, "No IMU");
            lv_label_set_text(card_, "Lite model");
            return;
        }
        float h = s.heading_deg;
        lv_label_set_text(heading_, fmt("%03d\xC2\xB0", int(h + 0.5f) % 360).c_str());
        lv_label_set_text(card_, cardinal(h));

        // Rotate the card so north points at magnetic north; needle stays up.
        const int cx = 65, cy = 65, r = 52;
        for (int i = 0; i < 4; ++i) {
            double a = (i * 90 - h) * M_PI / 180.0;
            lv_obj_set_pos(marks_[i], int(cx + r * std::sin(a)) - 6, int(cy - r * std::cos(a)) - 9);
        }
        pts_[0] = {cx, cy + 22};
        pts_[1] = {cx, cy - 40};
        lv_line_set_points(needle_, pts_, 2);

        float px = std::fmax(-25.f, std::fmin(25.f, s.roll_deg * 2.0f));
        float py = std::fmax(-25.f, std::fmin(25.f, s.pitch_deg * 2.0f));
        lv_obj_align(bubble_, LV_ALIGN_CENTER, int(px), int(py));
        bool level = std::fabs(s.pitch_deg) < 1.0f && std::fabs(s.roll_deg) < 1.0f;
        lv_obj_set_style_bg_color(bubble_, c(level ? kGood : kWarn), 0);
        lv_label_set_text(tilt_, fmt("Pitch %+.1f\xC2\xB0\nRoll  %+.1f\xC2\xB0", s.pitch_deg, s.roll_deg).c_str());
    }

    lv_obj_t *dial_, *needle_, *hub_, *heading_, *card_, *bubble_, *tilt_;
    lv_obj_t* marks_[4];
    lv_point_precise_t pts_[2];
};
}  // namespace

std::unique_ptr<App> make_compass_app() { return std::make_unique<CompassApp>(); }

}  // namespace zs
