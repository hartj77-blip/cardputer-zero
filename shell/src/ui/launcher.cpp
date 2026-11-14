// Home screen: a 4-column grid of app tiles.
// Arrows move, Enter opens, typing a letter jumps to the first app with it.
#include <cctype>

#include "ui/shell.h"
#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {

constexpr int kCols = 4;
constexpr int kTileW = 72;
constexpr int kTileH = 62;
constexpr int kGap = 6;

class Launcher : public App {
public:
    void build(lv_obj_t* root) override {
        grid_ = lv_obj_create(root);
        style_plain(grid_);
        lv_obj_set_size(grid_, kScreenW, kContentH);
        lv_obj_set_style_pad_all(grid_, kGap, 0);
        lv_obj_set_style_pad_row(grid_, kGap, 0);
        lv_obj_set_style_pad_column(grid_, kGap, 0);
        lv_obj_set_flex_flow(grid_, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_scrollbar_mode(grid_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_snap_y(grid_, LV_SCROLL_SNAP_START);

        for (auto& info : app_registry()) {
            lv_obj_t* tile = lv_obj_create(grid_);
            lv_obj_set_size(tile, kTileW, kTileH);
            lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(tile, 10, 0);
            lv_obj_set_style_bg_color(tile, c(kSurface), 0);
            lv_obj_set_style_border_width(tile, 0, 0);
            lv_obj_set_style_pad_all(tile, 4, 0);
            lv_obj_set_style_border_color(tile, c(kAccent), LV_STATE_USER_1);
            lv_obj_set_style_border_width(tile, 2, LV_STATE_USER_1);
            lv_obj_set_style_bg_color(tile, c(kSurface2), LV_STATE_USER_1);

            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, info.symbol);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(icon, c(info.color), 0);
            lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 6);

            lv_obj_t* name = label(tile, info.name.c_str(), &lv_font_montserrat_12, kText);
            lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -2);
            tiles_.push_back(tile);
        }
        select(0);
    }

    bool on_key(uint32_t key) override {
        int n = int(tiles_.size());
        switch (key) {
            case LV_KEY_RIGHT: select((sel_ + 1) % n); return true;
            case LV_KEY_LEFT: select((sel_ + n - 1) % n); return true;
            case LV_KEY_DOWN: select(sel_ + kCols < n ? sel_ + kCols : sel_ % kCols); return true;
            case LV_KEY_UP: {
                int t = sel_ - kCols;
                if (t < 0) {
                    t = sel_ % kCols + ((n - 1) / kCols) * kCols;
                    if (t >= n) t -= kCols;
                }
                select(t);
                return true;
            }
            case LV_KEY_ENTER:
            case '\r':
                shell_open_app(app_registry()[sel_].id);
                return true;
            default:
                break;
        }
        if (key < 128 && std::isalpha(int(key))) {
            int lower = std::tolower(int(key));
            for (int i = 1; i <= n; ++i) {
                int j = (sel_ + i) % n;
                if (std::tolower(app_registry()[j].name[0]) == lower) {
                    select(j);
                    return true;
                }
            }
            return true;
        }
        return false;
    }

private:
    void select(int i) {
        if (sel_ >= 0 && sel_ < int(tiles_.size())) lv_obj_remove_state(tiles_[sel_], LV_STATE_USER_1);
        sel_ = i;
        lv_obj_add_state(tiles_[sel_], LV_STATE_USER_1);
        lv_obj_scroll_to_view(tiles_[sel_], LV_ANIM_ON);
    }

    lv_obj_t* grid_ = nullptr;
    std::vector<lv_obj_t*> tiles_;
    int sel_ = -1;
};

}  // namespace

std::unique_ptr<App> make_launcher() { return std::make_unique<Launcher>(); }

}  // namespace zs
