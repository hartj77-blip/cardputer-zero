// Home screen, after Ubuntu's: wallpaper, a dock of favourites down the left
// edge and the app grid in pages beside it.
// Arrows move, Enter opens, 1-5 open the dock's apps (Ubuntu's Super+1..5),
// typing a letter jumps to the next app that starts with it.
#include <cctype>

#include "ui/shell.h"
#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {

constexpr int kDockW = 32;
constexpr int kCols = 4;
constexpr int kRows = 2;
constexpr int kPerPage = kCols * kRows;
constexpr int kCellW = (kScreenW - kDockW) / kCols;   // 72
constexpr int kCellH = 66;
constexpr int kGridY = 3;
constexpr int kDotsY = kGridY + kRows * kCellH + 5;

// The dock's favourites, top to bottom.
const char* const kFavourites[] = {"terminal", "files", "notes", "remote", "settings"};
constexpr int kFavCount = int(sizeof kFavourites / sizeof kFavourites[0]);

// A Yaru-style icon: a white glyph on a coloured rounded square.
lv_obj_t* app_icon(lv_obj_t* parent, const AppInfo& info, int size) {
    lv_obj_t* box = lv_obj_create(parent);
    style_plain(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_style_radius(box, size * 9 / 32, 0);
    lv_obj_set_style_bg_color(box, c(info.color), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* glyph = label(box, info.symbol, size >= 30 ? &zs_font_16 : &zs_font_12, 0xffffff);
    lv_obj_center(glyph);
    return box;
}

// Translucent white behind the selected item, as in GNOME's app grid.
void style_highlight(lv_obj_t* o, int radius) {
    style_plain(o);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, c(0xffffff), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_30, LV_STATE_USER_1);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

class Launcher : public App {
public:
    void build(lv_obj_t* root) override {
        // Wallpaper: aubergine fading up from the bottom left, like Ubuntu's.
        lv_obj_set_style_bg_color(root, c(kAubergine), 0);
        lv_obj_set_style_bg_grad_color(root, c(kAubergineLight), 0);
        lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

        const auto& apps = app_registry();

        lv_obj_t* dock = lv_obj_create(root);
        style_plain(dock);
        lv_obj_set_size(dock, kDockW, kContentH);
        lv_obj_set_style_bg_color(dock, c(0x000000), 0);
        lv_obj_set_style_bg_opa(dock, LV_OPA_60, 0);
        lv_obj_remove_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
        const int pitch = kContentH / kFavCount;
        for (int f = 0; f < kFavCount; ++f) {
            for (size_t i = 0; i < apps.size(); ++i) {
                if (apps[i].id != kFavourites[f]) continue;
                lv_obj_t* slot = lv_obj_create(dock);
                style_highlight(slot, 7);
                lv_obj_set_size(slot, 28, 28);
                lv_obj_set_pos(slot, (kDockW - 28) / 2, f * pitch + (pitch - 28) / 2);
                lv_obj_center(app_icon(slot, apps[i], 22));
                dock_.push_back(slot);
                dock_app_.push_back(int(i));
            }
        }

        int n = int(apps.size());
        pages_ = (n + kPerPage - 1) / kPerPage;
        for (int p = 0; p < pages_; ++p) {
            lv_obj_t* page = lv_obj_create(root);
            style_plain(page);
            lv_obj_set_pos(page, kDockW, 0);
            lv_obj_set_size(page, kScreenW - kDockW, kDotsY);
            lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
            page_objs_.push_back(page);
        }
        for (int i = 0; i < n; ++i) {
            int slot = i % kPerPage;
            lv_obj_t* cell = lv_obj_create(page_objs_[i / kPerPage]);
            style_highlight(cell, 10);
            lv_obj_set_size(cell, kCellW - 4, kCellH - 4);
            lv_obj_set_pos(cell, (slot % kCols) * kCellW + 2, kGridY + (slot / kCols) * kCellH + 2);
            lv_obj_align(app_icon(cell, apps[i], 32), LV_ALIGN_TOP_MID, 0, 5);
            lv_obj_t* name = label(cell, apps[i].name.c_str(), &zs_font_12, 0xffffff);
            lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -5);
            cells_.push_back(cell);
        }

        // Page dots, centred under the grid.
        for (int p = 0; p < pages_ && pages_ > 1; ++p) {
            lv_obj_t* dot = lv_obj_create(root);
            style_plain(dot);
            lv_obj_set_size(dot, 5, 5);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, c(0xffffff), 0);
            lv_obj_set_pos(dot, kDockW + (kScreenW - kDockW) / 2 - pages_ * 5 + 3 + p * 10, kDotsY);
            dots_.push_back(dot);
        }
        select(0);
    }

    bool on_key(uint32_t key) override {
        int n = int(cells_.size());
        if (key >= '1' && key < uint32_t('1' + dock_.size())) {
            shell_open_app(app_registry()[dock_app_[key - '1']].id);
            return true;
        }
        if (in_dock_) return dock_key(key);
        int col = sel_ % kCols;
        switch (key) {
            case LV_KEY_RIGHT: select((sel_ + 1) % n); return true;
            case LV_KEY_LEFT:
                // Off the left edge of the first page is the dock.
                if (col == 0 && sel_ < kPerPage && !dock_.empty()) enter_dock();
                else select((sel_ + n - 1) % n);
                return true;
            case LV_KEY_DOWN: {
                int t = sel_ + kCols;
                select(t < n ? t : col);
                return true;
            }
            case LV_KEY_UP: {
                int t = sel_ - kCols;
                if (t < 0) {
                    t = col + ((n - 1) / kCols) * kCols;
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
    bool dock_key(uint32_t key) {
        int n = int(dock_.size());
        switch (key) {
            case LV_KEY_DOWN: dock_select((dock_sel_ + 1) % n); return true;
            case LV_KEY_UP: dock_select((dock_sel_ + n - 1) % n); return true;
            case LV_KEY_RIGHT:
            case LV_KEY_ESC:
                in_dock_ = false;
                lv_obj_remove_state(dock_[dock_sel_], LV_STATE_USER_1);
                select(sel_);
                return true;
            case LV_KEY_LEFT: return true;
            case LV_KEY_ENTER:
            case '\r':
                shell_open_app(app_registry()[dock_app_[dock_sel_]].id);
                return true;
            default:
                return key < 128 && std::isalpha(int(key));
        }
    }

    void enter_dock() {
        lv_obj_remove_state(cells_[sel_], LV_STATE_USER_1);
        in_dock_ = true;
        // Land on the dock icon level with the row you came from.
        int row = (sel_ % kPerPage) / kCols;
        dock_select(row == 0 ? 0 : int(dock_.size()) - 1);
    }

    void dock_select(int i) {
        lv_obj_remove_state(dock_[dock_sel_], LV_STATE_USER_1);
        dock_sel_ = i;
        lv_obj_add_state(dock_[dock_sel_], LV_STATE_USER_1);
    }

    void select(int i) {
        if (sel_ >= 0 && sel_ < int(cells_.size())) lv_obj_remove_state(cells_[sel_], LV_STATE_USER_1);
        sel_ = i;
        lv_obj_add_state(cells_[sel_], LV_STATE_USER_1);
        int page = sel_ / kPerPage;
        for (int p = 0; p < pages_; ++p) {
            if (p == page) lv_obj_remove_flag(page_objs_[p], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(page_objs_[p], LV_OBJ_FLAG_HIDDEN);
            if (p < int(dots_.size())) lv_obj_set_style_bg_opa(dots_[p], p == page ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }

    std::vector<lv_obj_t*> cells_, dock_, page_objs_, dots_;
    std::vector<int> dock_app_;
    int pages_ = 0;
    int sel_ = -1;
    int dock_sel_ = 0;
    bool in_dock_ = false;
};

}  // namespace

std::unique_ptr<App> make_launcher() { return std::make_unique<Launcher>(); }

}  // namespace zs
