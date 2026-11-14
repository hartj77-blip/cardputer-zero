// Notes: a plain-text editor. Saves to ~/notes.txt (or the file opened from
// Files) automatically when you leave.
#include <fstream>
#include <sstream>

#include "apps/common.h"

namespace zs {

namespace {
std::string g_pending_file;
}
void notes_set_pending_file(const std::string& path) { g_pending_file = path; }

namespace {
using namespace theme;

class NotesApp : public App {
public:
    ~NotesApp() override { save(); }

    void build(lv_obj_t* root) override {
        path_ = g_pending_file.empty() ? home_dir() + "/notes.txt" : g_pending_file;
        g_pending_file.clear();

        std::string name = path_.substr(path_.find_last_of('/') + 1);
        lv_obj_t* t = label(root, name.c_str(), &lv_font_montserrat_12, kAccent);
        lv_obj_set_pos(t, 8, 3);
        status_ = label(root, "", &lv_font_montserrat_12, kMuted);
        lv_obj_align(status_, LV_ALIGN_TOP_RIGHT, -8, 3);

        ta_ = lv_textarea_create(root);
        lv_obj_set_pos(ta_, 4, 20);
        lv_obj_set_size(ta_, kScreenW - 8, kContentH - 24);
        lv_obj_set_style_bg_color(ta_, c(kSurface), 0);
        lv_obj_set_style_border_width(ta_, 0, 0);
        lv_obj_set_style_text_font(ta_, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_all(ta_, 6, 0);
        lv_obj_set_style_bg_color(ta_, c(kAccent), LV_PART_CURSOR | LV_STATE_FOCUSED);

        std::ifstream f(path_);
        std::stringstream ss;
        ss << f.rdbuf();
        text_ = ss.str();
        if (text_.empty() && !f) {
            text_ = "Welcome to ZeroOS Notes.\n\nType anything. It saves when you leave.\n";
        }
        lv_textarea_set_text(ta_, text_.c_str());
        lv_obj_add_event_cb(ta_, on_change, LV_EVENT_VALUE_CHANGED, this);
        shell_add_focus(ta_);
        lv_group_focus_obj(ta_);
        update_status();
    }

private:
    static void on_change(lv_event_t* e) {
        auto* self = static_cast<NotesApp*>(lv_event_get_user_data(e));
        self->dirty_ = true;
        self->update_status();
    }

    void update_status() {
        std::string t = lv_textarea_get_text(ta_);
        size_t words = 0;
        bool in = false;
        for (char ch : t) {
            bool ws = ch == ' ' || ch == '\n' || ch == '\t';
            if (!ws && !in) ++words;
            in = !ws;
        }
        lv_label_set_text(status_, fmt("%zu words%s", words, dirty_ ? "  *" : "").c_str());
    }

    void save() {
        if (!dirty_ || !ta_) return;
        std::ofstream f(path_);
        f << lv_textarea_get_text(ta_);
        dirty_ = false;
    }

    std::string path_, text_;
    lv_obj_t* ta_ = nullptr;
    lv_obj_t* status_ = nullptr;
    bool dirty_ = false;
};
}  // namespace

std::unique_ptr<App> make_notes_app() { return std::make_unique<NotesApp>(); }

}  // namespace zs
