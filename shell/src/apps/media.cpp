// Media: music from ~/Music and voice notes from the mic, in two tabs.
//
//   Left/Right   switch tab          Up/Down   choose a track
//   Enter        play                Space     pause / resume
//   N / P        next / previous     S         stop
//   + / -        volume              R         record a voice note (Voice tab)
//   X            delete the selected voice note
//
// The current track keeps playing when you leave the app (the status bar shows
// a note icon); moving on to the next track happens while the app is open.
#include <cstdlib>

#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

constexpr int kListW = 178;
constexpr int kBars = 9;

std::string mmss(int ms) {
    if (ms < 0) return "--:--";
    int s = ms / 1000;
    return fmt("%d:%02d", s / 60, s % 60);
}

class MediaApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;

        // Tabs
        for (int i = 0; i < 2; ++i) {
            tabs_[i] = label(root, i == 0 ? LV_SYMBOL_AUDIO "  Music" : LV_SYMBOL_AUDIO "  Voice notes",
                             &zs_font_12, kMuted);
            lv_obj_set_pos(tabs_[i], i == 0 ? 10 : 84, 3);
        }
        tab_line_ = lv_obj_create(root);
        style_plain(tab_line_);
        lv_obj_set_style_bg_color(tab_line_, c(kAccent), 0);
        lv_obj_set_style_bg_opa(tab_line_, LV_OPA_COVER, 0);
        lv_obj_set_size(tab_line_, 52, 2);

        // Now-playing panel (shared by both tabs)
        panel_ = lv_obj_create(root);
        style_panel(panel_);
        lv_obj_set_pos(panel_, kListW + 8, 6);
        lv_obj_set_size(panel_, kScreenW - kListW - 14, kContentH - 12);
        lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

        state_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(state_, 0, 0);
        title_ = label(panel_, "", &zs_font_14, kText);
        lv_obj_set_width(title_, kScreenW - kListW - 26);
        lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(title_, 0, 16);
        artist_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_set_width(artist_, kScreenW - kListW - 26);
        lv_label_set_long_mode(artist_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(artist_, 0, 34);

        // Level meter (visual only: animates while audio is running)
        for (int i = 0; i < kBars; ++i) {
            bars_[i] = lv_obj_create(panel_);
            style_plain(bars_[i]);
            lv_obj_set_style_bg_opa(bars_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(bars_[i], 2, 0);
            lv_obj_set_size(bars_[i], 9, 3);
        }

        progress_ = thin_bar(panel_, kScreenW - kListW - 26, 5, kAccent);
        lv_obj_set_pos(progress_, 0, 88);
        lv_bar_set_range(progress_, 0, 1000);
        time_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(time_, 0, 95);
        hint_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_align(hint_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        show_tab(0);
    }

    void tick() override {
        auto& h = hal();
        PlaybackState pb = h.playback();
        if (pb.finished && autoplay_ >= 0) {
            // Track ended: continue down the music list.
            int next = autoplay_ + 1;
            autoplay_ = -1;
            if (next < int(music_.size())) play_index(next, false);
            pb = h.playback();
        }

        RecordState rs = h.record_state();
        if (rs.recording) {
            lv_label_set_text(state_, LV_SYMBOL_STOP "  RECORDING");
            lv_obj_set_style_text_color(state_, c(kBad), 0);
            lv_label_set_text(title_, rs.path.substr(rs.path.rfind('/') + 1).c_str());
            lv_label_set_text(artist_, "Mic, 16 kHz mono");
            lv_bar_set_value(progress_, 0, LV_ANIM_OFF);
            lv_label_set_text(time_, mmss(rs.elapsed_ms).c_str());
        } else if (!pb.path.empty()) {
            lv_label_set_text(state_, pb.paused ? LV_SYMBOL_PAUSE "  PAUSED" : LV_SYMBOL_PLAY "  NOW PLAYING");
            lv_obj_set_style_text_color(state_, c(pb.paused ? kWarn : kGood), 0);
            const Track* t = find(pb.path);
            std::string name = t ? t->title : pb.path.substr(pb.path.rfind('/') + 1);
            if (name != shown_title_) {
                lv_label_set_text(title_, name.c_str());
                shown_title_ = name;
            }
            lv_label_set_text(artist_, t ? t->artist.c_str() : "");
            int permille = pb.dur_ms > 0 ? int(1000LL * pb.pos_ms / pb.dur_ms) : 0;
            lv_bar_set_value(progress_, permille, LV_ANIM_OFF);
            lv_label_set_text(time_, (mmss(pb.pos_ms) + " / " + mmss(pb.dur_ms) +
                                      fmt("   " LV_SYMBOL_VOLUME_MID " %d%%", h.volume()))
                                         .c_str());
        } else {
            lv_label_set_text(state_, LV_SYMBOL_STOP "  STOPPED");
            lv_obj_set_style_text_color(state_, c(kMuted), 0);
            lv_label_set_text(title_, tab_ == 0 ? "Pick a track" : "No note playing");
            shown_title_.clear();
            lv_label_set_text(artist_, tab_ == 0 ? fmt("%zu tracks in ~/Music", music_.size()).c_str()
                                                 : fmt("%zu notes in ~/Recordings", notes_.size()).c_str());
            lv_bar_set_value(progress_, 0, LV_ANIM_OFF);
            lv_label_set_text(time_, fmt(LV_SYMBOL_VOLUME_MID " %d%%", h.volume()).c_str());
        }

        // Meter: lively while playing or recording, flat otherwise.
        bool live = rs.recording || (!pb.path.empty() && !pb.paused);
        uint32_t col = rs.recording ? kBad : kAccent;
        for (int i = 0; i < kBars; ++i) {
            int target = live ? 4 + std::rand() % 26 : 3;
            levels_[i] = (levels_[i] * 2 + target) / 3;
            lv_obj_set_size(bars_[i], 9, levels_[i]);
            lv_obj_set_pos(bars_[i], i * 13, 82 - levels_[i]);
            lv_obj_set_style_bg_color(bars_[i], c(live ? col : kSurface2), 0);
        }

        if (rs.recording != was_recording_) {
            was_recording_ = rs.recording;
            if (!rs.recording && tab_ == 1) rebuild_list();  // new note appears
        }
    }

    bool on_key(uint32_t key) override {
        auto& h = hal();
        switch (key) {
            case LV_KEY_LEFT:
            case LV_KEY_RIGHT:
            case 9:  // Tab
                show_tab(1 - tab_);
                return true;
            case ' ': {
                PlaybackState pb = h.playback();
                if (!pb.path.empty()) h.set_paused(!pb.paused);
                else play_focused();
                return true;
            }
            case 'n': case 'N': step(+1); return true;
            case 'p': case 'P': step(-1); return true;
            case 's': case 'S':
                autoplay_ = -1;
                h.stop();
                return true;
            case '+': case '=':
                h.set_volume(std::min(100, h.volume() + 5));
                return true;
            case '-': case '_':
                h.set_volume(std::max(0, h.volume() - 5));
                return true;
            case 'r': case 'R':
                if (tab_ != 1) show_tab(1);
                if (h.record_state().recording) {
                    h.record_stop();
                    shell_toast("Voice note saved");
                } else {
                    autoplay_ = -1;
                    shell_toast(h.record_start() ? "Recording... press R to stop" : "No microphone found");
                }
                return true;
            case 'x': case 'X': case LV_KEY_DEL:
                if (tab_ == 1) delete_focused();
                return true;
        }
        return false;
    }

private:
    const Track* find(const std::string& path) const {
        for (auto& t : music_) if (t.path == path) return &t;
        for (auto& t : notes_) if (t.path == path) return &t;
        return nullptr;
    }

    void show_tab(int t) {
        tab_ = t;
        for (int i = 0; i < 2; ++i) lv_obj_set_style_text_color(tabs_[i], c(i == t ? kText : kMuted), 0);
        lv_obj_set_pos(tab_line_, t == 0 ? 10 : 84, 18);
        lv_obj_set_width(tab_line_, t == 0 ? 54 : 88);
        lv_label_set_text(hint_, t == 0 ? "Space pause   N next" : "R record   X delete");
        rebuild_list();
    }

    void rebuild_list() {
        if (list_) {
            lv_group_remove_all_objs(group);
            lv_obj_delete(list_);
        }
        music_ = hal().music_library();
        notes_ = hal().recordings();
        const auto& items = tab_ == 0 ? music_ : notes_;

        list_ = make_list(root_, 4, 24, kListW, kContentH - 28);
        if (items.empty())
            lv_list_add_text(list_, tab_ == 0 ? "Copy music to ~/Music" : "Press R to record a note");
        std::string playing = hal().playback().path;
        lv_obj_t* focus = nullptr;
        for (size_t i = 0; i < items.size(); ++i) {
            const Track& t = items[i];
            std::string text = t.title;
            if (tab_ == 1 && t.duration_s >= 0) text += "  " + mmss(t.duration_s * 1000);
            bool now = t.path == playing;
            lv_obj_t* b = list_button(list_, now ? LV_SYMBOL_PLAY : (tab_ == 0 ? LV_SYMBOL_AUDIO : LV_SYMBOL_FILE),
                                      text.c_str(), on_click, this);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
            lv_obj_t* lbl = lv_obj_get_child(b, 1);
            if (lbl) lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
            if (now) focus = b;
        }
        finish_list(list_);
        if (focus) lv_group_focus_obj(focus);
    }

    int focused_index() {
        lv_obj_t* f = lv_group_get_focused(group);
        if (!f || lv_obj_get_parent(f) != list_) return -1;
        return int((intptr_t)lv_obj_get_user_data(f));
    }

    void play_index(int i, bool from_user) {
        const auto& items = tab_ == 0 ? music_ : notes_;
        if (i < 0 || i >= int(items.size())) return;
        if (hal().record_state().recording) hal().record_stop();
        if (!hal().play(items[i].path)) {
            shell_toast("Can't play " + items[i].title);
            return;
        }
        autoplay_ = tab_ == 0 ? i : -1;  // music continues to the next track
        shown_title_.clear();
        rebuild_list();
        (void)from_user;
    }

    void play_focused() {
        int i = focused_index();
        play_index(i < 0 ? 0 : i, true);
    }

    void step(int dir) {
        const auto& items = tab_ == 0 ? music_ : notes_;
        if (items.empty()) return;
        std::string cur = hal().playback().path;
        int i = 0;
        for (int k = 0; k < int(items.size()); ++k)
            if (items[k].path == cur) i = k;
        int n = int(items.size());
        play_index(((cur.empty() ? -dir : i) + dir + n) % n, true);
    }

    void delete_focused() {
        int i = focused_index();
        if (i < 0 || i >= int(notes_.size())) return;
        std::string name = notes_[i].title;
        if (hal().delete_recording(notes_[i].path)) {
            shell_toast("Deleted " + name);
            rebuild_list();
        }
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<MediaApp*>(lv_event_get_user_data(e));
        self->pending_ = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        // Deferred: playing rebuilds the list, which deletes the clicked button.
        lv_async_call([](void* p) {
            auto* s = static_cast<MediaApp*>(p);
            s->play_index(s->pending_, true);
        }, self);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* tabs_[2] = {};
    lv_obj_t* tab_line_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t *panel_, *state_, *title_, *artist_, *progress_, *time_, *hint_;
    lv_obj_t* bars_[kBars] = {};
    int levels_[kBars] = {};
    std::vector<Track> music_, notes_;
    std::string shown_title_;
    int tab_ = 0;
    int autoplay_ = -1;
    int pending_ = -1;
    bool was_recording_ = false;
};
}  // namespace

std::unique_ptr<App> make_media_app() { return std::make_unique<MediaApp>(); }

}  // namespace zs
