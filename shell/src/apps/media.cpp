// Media: music from ~/Music, voice notes from the mic, and music from a
// Jellyfin server, in three tabs.
//
//   Left/Right   switch tab          Up/Down   choose
//   Enter        play / open         Space     pause / resume
//   N / P        next / previous     S         stop
//   + / -        volume              R         record a voice note
//   X            delete the selected voice note
//   Esc          back one level in Jellyfin
//
// Playing goes through the shell's queue (ui/player.h), so it carries on to
// the next track after you leave the app or the screen turns off.
#include <cstdlib>
#include <functional>
#include <random>

#include "apps/common.h"
#include "ui/player.h"
#include "ui/prefs.h"

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
        static const char* const names[kTabs] = {LV_SYMBOL_AUDIO " Music", LV_SYMBOL_AUDIO " Voice",
                                                 LV_SYMBOL_AUDIO " Jellyfin"};
        int x = 8;
        for (int i = 0; i < kTabs; ++i) {
            tabs_[i] = label(root, names[i], &zs_font_12, kMuted);
            lv_obj_set_pos(tabs_[i], x, 3);
            lv_obj_update_layout(tabs_[i]);
            x += lv_obj_get_width(tabs_[i]) + 12;
        }
        tab_line_ = lv_obj_create(root);
        style_plain(tab_line_);
        lv_obj_set_style_bg_color(tab_line_, c(kAccent), 0);
        lv_obj_set_style_bg_opa(tab_line_, LV_OPA_COVER, 0);
        lv_obj_set_size(tab_line_, 52, 2);

        jf_.server = prefs::get_str("jellyfin.server", "");
        jf_.user = prefs::get_str("jellyfin.user", "");
        jf_.user_id = prefs::get_str("jellyfin.user_id", "");
        jf_.token = prefs::get_str("jellyfin.token", "");
        jf_.device_id = prefs::get_str("jellyfin.device", "");
        if (jf_.device_id.empty()) {
            std::random_device rd;
            jf_.device_id = fmt("zeroos-%08x%08x", unsigned(rd()), unsigned(rd()));
            prefs::set_str("jellyfin.device", jf_.device_id);
        }

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
            const QueueItem* q = player_current();
            std::string name = q ? q->title : pb.path.substr(pb.path.rfind('/') + 1);
            if (name != shown_title_) {
                lv_label_set_text(title_, name.c_str());
                shown_title_ = name;
            }
            lv_label_set_text(artist_, q ? q->artist.c_str() : "");
            if (pb.path != listed_playing_) rebuild_list();   // the play mark follows the queue
            int permille = pb.dur_ms > 0 ? int(1000LL * pb.pos_ms / pb.dur_ms) : 0;
            lv_bar_set_value(progress_, permille, LV_ANIM_OFF);
            lv_label_set_text(time_, (mmss(pb.pos_ms) + " / " + mmss(pb.dur_ms) +
                                      fmt("   " LV_SYMBOL_VOLUME_MID " %d%%", h.volume()))
                                         .c_str());
        } else {
            lv_label_set_text(state_, LV_SYMBOL_STOP "  STOPPED");
            lv_obj_set_style_text_color(state_, c(kMuted), 0);
            static const char* const idle[kTabs] = {"Pick a track", "No note playing", "Jellyfin"};
            lv_label_set_text(title_, idle[tab_]);
            shown_title_.clear();
            std::string sub = tab_ == 0   ? fmt("%zu tracks in ~/Music", music_.size())
                              : tab_ == 1 ? fmt("%zu notes in ~/Recordings", notes_.size())
                              : jf_.signed_in() ? jf_.user + " on " + host_of(jf_.server)
                                                : std::string("Not signed in");
            lv_label_set_text(artist_, sub.c_str());
            if (!listed_playing_.empty() && !input_) rebuild_list();   // clear the play mark
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
        if (input_) return false;   // typing in the text box
        auto& h = hal();
        switch (key) {
            case LV_KEY_LEFT: show_tab((tab_ + kTabs - 1) % kTabs); return true;
            case LV_KEY_RIGHT:
            case 9:  // Tab
                show_tab((tab_ + 1) % kTabs);
                return true;
            case ' ': {
                PlaybackState pb = h.playback();
                if (!pb.path.empty()) h.set_paused(!pb.paused);
                else activate(focused_index());
                return true;
            }
            case 'n': case 'N': step(+1); return true;
            case 'p': case 'P': step(-1); return true;
            case 's': case 'S':
                player_stop();
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
                    player_stop();
                    shell_toast(h.record_start() ? "Recording... press R to stop" : "No microphone found");
                }
                return true;
            case 'x': case 'X': case LV_KEY_DEL:
                if (tab_ == 1) delete_focused();
                return true;
            case '/':
                if (tab_ == 2 && jf_.signed_in()) {
                    ask_search();
                    return true;
                }
                break;
        }
        return false;
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (tab_ == 2 && !stack_.empty()) {
            stack_.pop_back();
            rebuild_list();
            return true;
        }
        return false;
    }

private:
    static constexpr int kTabs = 3;

    // One screen of Jellyfin results; the stack of them is where you are.
    struct Level {
        std::string title;
        std::vector<JfItem> items;
    };
    // The Jellyfin tab's first screen.
    enum Root { kSearch, kLatest, kAlbums, kArtists, kPlaylists, kSignOut, kRootCount };

    static std::string host_of(const std::string& url) {
        auto a = url.find("://");
        std::string h = a == std::string::npos ? url : url.substr(a + 3);
        return h.substr(0, h.find_first_of(":/"));
    }

    void show_tab(int t) {
        tab_ = t;
        for (int i = 0; i < kTabs; ++i) lv_obj_set_style_text_color(tabs_[i], c(i == t ? kText : kMuted), 0);
        lv_obj_set_pos(tab_line_, lv_obj_get_x(tabs_[t]), 18);
        lv_obj_set_width(tab_line_, lv_obj_get_width(tabs_[t]));
        static const char* const hints[kTabs] = {"Space pause   N next", "R record   X delete",
                                                 "/ search   Esc back"};
        lv_label_set_text(hint_, hints[t]);
        rebuild_list();
    }

    lv_obj_t* add_row(const char* symbol, const std::string& text, int index) {
        lv_obj_t* b = list_button(list_, symbol, text.c_str(), on_click, this);
        lv_obj_set_user_data(b, (void*)(intptr_t)index);
        lv_obj_t* lbl = lv_obj_get_child(b, 1);
        if (lbl) lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        return b;
    }

    void rebuild_list() {
        int keep = focused_index();
        if (list_) {
            lv_group_remove_all_objs(group);
            lv_obj_delete(list_);
        }
        music_ = hal().music_library();
        notes_ = hal().recordings();
        list_ = make_list(root_, 4, 24, kListW, kContentH - 28);
        listed_playing_ = hal().playback().path;
        lv_obj_t* focus = nullptr;

        if (tab_ == 2) {
            focus = build_jellyfin(keep);
        } else {
            const auto& items = tab_ == 0 ? music_ : notes_;
            if (items.empty())
                lv_list_add_text(list_, tab_ == 0 ? "Copy music to ~/Music" : "Press R to record a note");
            for (size_t i = 0; i < items.size(); ++i) {
                const Track& t = items[i];
                std::string text = t.title;
                if (tab_ == 1 && t.duration_s >= 0) text += "  " + mmss(t.duration_s * 1000);
                bool now = t.path == listed_playing_;
                lv_obj_t* b = add_row(now ? LV_SYMBOL_PLAY : (tab_ == 0 ? LV_SYMBOL_AUDIO : LV_SYMBOL_FILE), text, int(i));
                if (now) focus = b;
            }
        }
        finish_list(list_);
        if (focus) lv_group_focus_obj(focus);
        if (input_) lv_obj_move_foreground(input_);
    }

    // Rows for the Jellyfin tab; returns the row to focus, if any.
    lv_obj_t* build_jellyfin(int keep) {
        lv_obj_t* focus = nullptr;
        if (!jf_.signed_in()) {
            lv_list_add_text(list_, "Music from your server");
            add_row(LV_SYMBOL_RIGHT, "Sign in...", 0);
            return nullptr;
        }
        if (stack_.empty()) {
            static const char* const rows[kRootCount] = {"Search...", "Latest albums", "Albums", "Artists",
                                                         "Playlists", "Sign out"};
            static const char* const icons[kRootCount] = {ZS_ICON_SEARCH,    LV_SYMBOL_BELL,  LV_SYMBOL_DIRECTORY,
                                                          LV_SYMBOL_DIRECTORY, LV_SYMBOL_LIST, LV_SYMBOL_CLOSE};
            for (int i = 0; i < kRootCount; ++i) {
                lv_obj_t* b = add_row(icons[i], rows[i], i);
                if (i == keep) focus = b;
            }
            return focus;
        }
        const Level& lv = stack_.back();
        lv_list_add_text(list_, lv.title.c_str());
        if (lv.items.empty()) lv_list_add_text(list_, "Nothing here");
        for (size_t i = 0; i < lv.items.size(); ++i) {
            const JfItem& it = lv.items[i];
            bool now = it.kind == JfItem::Track && jf::stream_url(jf_, it.id) == listed_playing_;
            const char* icon = now ? LV_SYMBOL_PLAY
                               : it.kind == JfItem::Track ? LV_SYMBOL_AUDIO
                               : it.kind == JfItem::Playlist ? LV_SYMBOL_LIST : LV_SYMBOL_DIRECTORY;
            lv_obj_t* b = add_row(icon, it.name, int(i));
            if (now || (!focus && int(i) == keep)) focus = b;
        }
        return focus;
    }

    int focused_index() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (!f || !list_ || lv_obj_get_parent(f) != list_) return -1;
        return int((intptr_t)lv_obj_get_user_data(f));
    }

    // Enter on row i of the current list.
    void activate(int i) {
        if (i < 0) i = 0;
        if (tab_ == 2) {
            activate_jellyfin(i);
            return;
        }
        const auto& items = tab_ == 0 ? music_ : notes_;
        if (i >= int(items.size())) return;
        std::vector<QueueItem> q;
        int at = 0;
        if (tab_ == 0) {   // music continues down the list
            for (auto& t : music_) q.push_back({t.path, t.title, t.artist, t.duration_s, ""});
            at = i;
        } else {
            q.push_back({notes_[i].path, notes_[i].title, notes_[i].artist, notes_[i].duration_s, ""});
        }
        if (!player_play(std::move(q), at)) shell_toast("Can't play " + items[i].title);
        shown_title_.clear();
        rebuild_list();
    }

    void activate_jellyfin(int i) {
        if (!jf_.signed_in()) {
            sign_in();
            return;
        }
        if (stack_.empty()) {
            switch (i) {
                case kSearch: ask_search(); break;
                case kLatest: open_level("Latest albums", JfView::Latest, ""); break;
                case kAlbums: open_level("Albums", JfView::Albums, ""); break;
                case kArtists: open_level("Artists", JfView::Artists, ""); break;
                case kPlaylists: open_level("Playlists", JfView::Playlists, ""); break;
                case kSignOut:
                    for (const char* k : {"jellyfin.user_id", "jellyfin.token"}) prefs::set_str(k, "");
                    jf_.user_id.clear();
                    jf_.token.clear();
                    shell_toast("Signed out of Jellyfin");
                    rebuild_list();
                    break;
            }
            return;
        }
        // Copies: opening a level reallocates the stack this item lives in.
        const std::vector<JfItem> items = stack_.back().items;
        if (i >= int(items.size())) return;
        const JfItem it = items[i];
        switch (it.kind) {
            case JfItem::Album: open_level(it.name, JfView::AlbumTracks, it.id); break;
            case JfItem::Artist: open_level(it.name, JfView::ArtistAlbums, it.id); break;
            case JfItem::Playlist: open_level(it.name, JfView::PlaylistTracks, it.id); break;
            case JfItem::Track: {
                // Queue every track on this screen, starting with this one.
                std::vector<QueueItem> q;
                int at = 0;
                std::string auth = jf::auth_header(jf_);
                for (size_t k = 0; k < items.size(); ++k) {
                    if (items[k].kind != JfItem::Track) continue;
                    if (int(k) == i) at = int(q.size());
                    q.push_back({jf::stream_url(jf_, items[k].id), items[k].name, items[k].detail,
                                 items[k].duration_s, auth});
                }
                if (!player_play(std::move(q), at)) shell_toast("Can't play " + it.name);
                shown_title_.clear();
                rebuild_list();
                break;
            }
        }
    }

    // Ask the server for a list and show it as a new level.
    void open_level(const std::string& title, JfView view, const std::string& arg) {
        shell_toast("Loading...");
        lv_refr_now(nullptr);   // the request blocks; show the notice first
        Level lv;
        lv.title = title;
        std::string error;
        if (!hal().jellyfin_list(jf_, view, arg, lv.items, error)) {
            shell_toast(error);
            return;
        }
        shell_toast(lv.items.empty() ? "Nothing found" : fmt("%zu found", lv.items.size()));
        stack_.push_back(std::move(lv));
        rebuild_list();
        if (lv_obj_get_child_count(list_) > 1) lv_group_focus_obj(lv_obj_get_child(list_, 1));
    }

    void ask_search() {
        ask("Search Jellyfin", "", false, [this](const std::string& text) {
            if (!text.empty()) open_level("\"" + text + "\"", JfView::Search, text);
        });
    }

    // Three questions, then sign in. Only the token is kept, not the password.
    void sign_in() {
        ask("Jellyfin server (name or address)", jf_.server, false, [this](const std::string& server) {
            if (server.empty()) return;
            typed_server_ = server;
            ask("User name", jf_.user, false, [this](const std::string& user) {
                if (user.empty()) return;
                typed_user_ = user;
                ask("Password", "", true, [this](const std::string& password) {
                    shell_toast("Signing in...");
                    lv_refr_now(nullptr);
                    JfSession s;
                    std::string error;
                    if (!hal().jellyfin_login(typed_server_, typed_user_, password, jf_.device_id, s, error)) {
                        shell_toast(error);
                        return;
                    }
                    jf_ = s;
                    prefs::set_str("jellyfin.server", s.server);
                    prefs::set_str("jellyfin.user", s.user);
                    prefs::set_str("jellyfin.user_id", s.user_id);
                    prefs::set_str("jellyfin.token", s.token);
                    stack_.clear();
                    shell_toast("Signed in as " + s.user);
                    rebuild_list();
                });
            });
        });
    }

    // One-line text box over the screen. Enter accepts, Esc cancels.
    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, bool password, Done done) {
        done_ = std::move(done);
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        label(input_, title, &zs_font_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, password);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<MediaApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = std::move(self->done_);
            // Enter is still down: without this its release would also
            // "click" whichever list row gets the focus next.
            if (lv_indev_t* in = lv_indev_active()) lv_indev_wait_release(in);
            self->close_input();
            if (d) d(v);   // may open the next box
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    void close_input() {
        if (!input_) return;
        lv_group_set_editing(group, false);
        lv_obj_t* in = input_;
        input_ = nullptr;
        lv_obj_delete_async(in);
        if (int n = int(lv_obj_get_child_count(list_))) {
            // Focus goes back to the list (its first button).
            for (int i = 0; i < n; ++i) {
                lv_obj_t* ch = lv_obj_get_child(list_, i);
                if (lv_obj_check_type(ch, &lv_list_button_class)) {
                    lv_group_focus_obj(ch);
                    break;
                }
            }
        }
    }

    void step(int dir) {
        if (player_step(dir)) {
            shown_title_.clear();
            rebuild_list();
        } else {
            activate(focused_index());
        }
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
        // Deferred: activating rebuilds the list, which deletes the clicked button.
        lv_async_call([](void* p) {
            auto* s = static_cast<MediaApp*>(p);
            s->activate(s->pending_);
        }, self);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* tabs_[kTabs] = {};
    lv_obj_t* tab_line_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t *panel_, *state_, *title_, *artist_, *progress_, *time_, *hint_;
    lv_obj_t* bars_[kBars] = {};
    int levels_[kBars] = {};
    std::vector<Track> music_, notes_;
    std::string shown_title_;
    std::string listed_playing_;   // what the list last marked as playing
    int tab_ = 0;
    int pending_ = -1;

    JfSession jf_;
    std::vector<Level> stack_;
    lv_obj_t* input_ = nullptr;
    Done done_;
    std::string typed_server_, typed_user_;
    bool was_recording_ = false;
};
}  // namespace

std::unique_ptr<App> make_media_app() { return std::make_unique<MediaApp>(); }

}  // namespace zs
