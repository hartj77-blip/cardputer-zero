// Files: browse the filesystem, starting in the home folder.
// Enter opens a folder (or a text file in Notes, music in the player, video
// on the monitor in Dock mode); Backspace goes up; S sends a file to one of
// your devices with Taildrop.
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>

#include <cctype>

#include "apps/common.h"
#include "ui/remote.h"

namespace zs {

void notes_set_pending_file(const std::string& path);

namespace {
using namespace theme;

struct Entry {
    std::string name;
    bool dir;
    long long size;
};

std::string human(long long b) {
    if (b < 1024) return fmt("%lld B", b);
    if (b < 1024 * 1024) return fmt("%.1f KB", b / 1024.0);
    if (b < 1024LL * 1024 * 1024) return fmt("%.1f MB", b / 1048576.0);
    return fmt("%.1f GB", b / 1073741824.0);
}

class FilesApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        cwd_ = home_dir();
        refresh();
    }

    bool on_key(uint32_t key) override {
        if (key == LV_KEY_BACKSPACE || key == 8) {
            up();
            return true;
        }
        if (key == 's' || key == 'S') {   // send the focused file with Taildrop
            lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
            int i = f ? int((intptr_t)lv_obj_get_user_data(f)) : -1;
            if (i < 0 || i >= int(entries_.size()) || entries_[size_t(i)].dir) return false;
            remote_set_send(cwd_ + (cwd_ == "/" ? "" : "/") + entries_[size_t(i)].name);
            shell_open_app("remote");
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (cwd_ != "/" && cwd_ != home_dir()) {
            up();
            return true;
        }
        return false;
    }

private:
    void up() {
        if (cwd_ == "/") return;
        auto p = cwd_.find_last_of('/');
        cwd_ = p == 0 ? "/" : cwd_.substr(0, p);
        refresh();
    }

    void refresh() {
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        entries_.clear();
        if (DIR* d = opendir(cwd_.c_str())) {
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n == "." || n == ".." || n[0] == '.') continue;
                struct stat st{};
                std::string full = cwd_ + (cwd_ == "/" ? "" : "/") + n;
                if (stat(full.c_str(), &st) != 0) continue;
                entries_.push_back({n, S_ISDIR(st.st_mode), (long long)st.st_size});
            }
            closedir(d);
        }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            if (a.dir != b.dir) return a.dir;
            return a.name < b.name;
        });

        std::string shown = cwd_;
        if (shown.rfind(home_dir(), 0) == 0) shown = "~" + shown.substr(home_dir().size());
        lv_obj_t* path = label(root_, shown.c_str(), &lv_font_montserrat_12, kAccent);
        lv_obj_set_pos(path, 8, 3);
        lv_obj_set_width(path, 230);
        lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
        lv_obj_t* cnt = label(root_, fmt("%zu items", entries_.size()).c_str(), &lv_font_montserrat_12, kMuted);
        lv_obj_align(cnt, LV_ALIGN_TOP_RIGHT, -8, 3);

        lv_obj_t* list = make_list(root_, 4, 21, kScreenW - 8, kContentH - 24);
        if (entries_.empty()) lv_list_add_text(list, "(empty folder)");
        for (size_t i = 0; i < entries_.size(); ++i) {
            auto& e = entries_[i];
            std::string text = e.dir ? e.name : e.name + "   " + human(e.size);
            lv_obj_t* b = list_button(list, e.dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, text.c_str(), on_open, this);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
        }
        finish_list(list);
    }

    static void on_open(lv_event_t* e) {
        auto* self = static_cast<FilesApp*>(lv_event_get_user_data(e));
        self->pick_ = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        lv_async_call([](void* p) { static_cast<FilesApp*>(p)->open(); }, self);
    }

    void open() {
        if (pick_ < 0 || pick_ >= int(entries_.size())) return;
        auto e = entries_[pick_];
        std::string full = cwd_ + (cwd_ == "/" ? "" : "/") + e.name;
        std::string ext = e.name.substr(e.name.rfind('.') == std::string::npos ? e.name.size() : e.name.rfind('.'));
        for (auto& ch : ext) ch = char(std::tolower((unsigned char)ch));
        auto is = [&](std::initializer_list<const char*> l) {
            for (auto x : l) if (ext == x) return true;
            return false;
        };
        if (e.dir) {
            cwd_ = full;
            refresh();
        } else if (is({".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v"})) {
            // Video plays on the HDMI monitor in Dock mode.
            if (hal().dock_open("video", full)) shell_toast("Playing on the monitor");
            else shell_toast("Videos play on a monitor: open Dock");
        } else if (is({".mp3", ".flac", ".ogg", ".wav", ".m4a", ".opus"})) {
            hal().play(full);
            shell_toast("Playing " + e.name);
        } else if (e.size < 64 * 1024) {
            notes_set_pending_file(full);
            shell_open_app("notes");
        } else {
            shell_toast("Too large to open (" + human(e.size) + ")");
        }
    }

    lv_obj_t* root_ = nullptr;
    std::string cwd_;
    std::vector<Entry> entries_;
    int pick_ = -1;
};
}  // namespace

std::unique_ptr<App> make_files_app() { return std::make_unique<FilesApp>(); }

}  // namespace zs
