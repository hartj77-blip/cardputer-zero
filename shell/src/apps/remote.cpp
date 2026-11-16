// Remote: Tailscale and your other devices.
//
//   Off          -> Turn on Tailscale
//   Sign-in      -> QR code + link; approve on your phone
//   Connected    -> your devices (online first). Enter = SSH (or mosh) in the
//                   terminal, D = remote desktop on the monitor (Dock mode).
//                   Files' S key sends a file here: Enter then sends it.
// Also: Connect to any host, the SSH user name, mosh on/off, sign out.
#include <vector>

#include "apps/common.h"
#include "ui/power.h"
#include "ui/remote.h"

namespace zs {
void terminal_run(const std::string& cmd);

namespace {
using namespace theme;

const char* os_symbol(const std::string& os) {
    if (os == "windows") return LV_SYMBOL_IMAGE;
    if (os == "linux") return LV_SYMBOL_DRIVE;
    if (os == "iOS" || os == "android") return LV_SYMBOL_CALL;
    return LV_SYMBOL_HOME;
}

class RemoteApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        remote_note_use();
        if (tailscale_mode() != "off" && !hal().tailscale().running && hal().tailscale().installed) {
            hal().tailscale_start();
            starting_ = true;
        }
        rebuild();
    }

    void tick() override {
        remote_note_use();
        if (dirty_) {
            dirty_ = false;
            rebuild();
            return;
        }
        if (++n_ % 10) return;
        TsStatus t = hal().tailscale();
        if (signature(t) != sig_) rebuild();
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;   // typing in the text box
        if (key == 'd' || key == 'D') {
            int i = focused_peer();
            if (i < 0) return false;
            if (!power().docked()) {
                shell_toast("Remote desktop needs Dock mode (a monitor)");
            } else {
                hal().dock_open("rdp", ts_.peers[size_t(i)].name);
                shell_toast("Opening " + ts_.peers[size_t(i)].name + " on the monitor");
            }
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (!remote_pending_send().empty()) {
            remote_take_send();
            rebuild();
            return true;
        }
        return false;
    }

private:
    static std::string signature(const TsStatus& t) {
        std::string s = t.state + "|" + t.auth_url + "|" + std::to_string(t.running);
        for (auto& p : t.peers) s += "|" + p.name + (p.online ? "+" : "-");
        return s;
    }

    int focused_peer() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        for (size_t i = 0; i < peer_btns_.size(); ++i)
            if (peer_btns_[i] == f) return int(i);
        return -1;
    }

    void rebuild() {
        ts_ = hal().tailscale();
        sig_ = signature(ts_);
        if (ts_.state != "Stopped") starting_ = false;
        lv_obj_clean(root_);
        input_ = nullptr;
        peer_btns_.clear();
        if (group) lv_group_remove_all_objs(group);

        // Left: status, or the sign-in QR code.
        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 150, kContentH - 10);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, 0);

        bool signin = ts_.state == "NeedsLogin" && !ts_.auth_url.empty();
        if (signin) {
            lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t* qr = lv_qrcode_create(panel);
            lv_qrcode_set_size(qr, 96);
            lv_qrcode_set_dark_color(qr, c(0x000000));
            lv_qrcode_set_light_color(qr, c(0xffffff));
            lv_qrcode_update(qr, ts_.auth_url.c_str(), uint32_t(ts_.auth_url.size()));
            lv_obj_set_style_border_color(qr, c(0xffffff), 0);
            lv_obj_set_style_border_width(qr, 4, 0);
            lv_obj_t* l = label(panel, "Scan to sign in", &lv_font_montserrat_12, kText);
            (void)l;
        } else {
            const char* head = !ts_.installed            ? "Not installed"
                               : ts_.state == "Running"  ? LV_SYMBOL_OK " Connected"
                               : ts_.state == "Stopped" && !starting_ ? "Tailscale off"
                               : ts_.state == "NeedsLogin" ? "Signing in..."
                               : "Starting...";
            label(panel, head, &lv_font_montserrat_16, ts_.state == "Running" ? kGood : kText);
            if (ts_.state == "Running") {
                kv_row(panel, "Name", ts_.self_name.c_str());
                kv_row(panel, "IP", ts_.self_ip.c_str());
                lv_obj_t* tn = label(panel, ts_.tailnet.c_str(), &lv_font_montserrat_12, kMuted);
                lv_label_set_long_mode(tn, LV_LABEL_LONG_DOT);
                lv_obj_set_width(tn, 136);
                const std::string& send = remote_pending_send();
                std::string hint = send.empty() ? "Enter: SSH\nD: desktop on monitor"
                                                : "Send " + send.substr(send.rfind('/') + 1) + ":\npick a device";
                lv_obj_t* h = label(panel, hint.c_str(), &lv_font_montserrat_12, send.empty() ? kMuted : kAccent);
                lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(h, 136);
            } else {
                lv_obj_t* h = label(panel,
                                    ts_.installed ? "Reach your PC, phone and servers from anywhere."
                                                  : "Tailscale comes with ZeroOS images.",
                                    &lv_font_montserrat_12, kMuted);
                lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(h, 136);
            }
        }

        // Right: actions and devices.
        lv_obj_t* list = make_list(root_, 162, 4, 152, kContentH - 10);
        if (ts_.state == "Running") {
            for (auto& p : ts_.peers) {
                std::string text = p.name + (p.online ? "" : "  (off)");
                lv_obj_t* b = list_button(list, os_symbol(p.os), text.c_str(), on_peer, this);
                if (!p.online) lv_obj_set_style_text_color(b, c(kMuted), 0);
                peer_btns_.push_back(b);
            }
        } else if (ts_.installed && ts_.state == "Stopped" && !starting_) {
            list_button(list, LV_SYMBOL_POWER, "Turn on Tailscale", [](lv_event_t* e) {
                hal().tailscale_start();
                static_cast<RemoteApp*>(lv_event_get_user_data(e))->starting_ = true;
                shell_toast("Starting Tailscale...");
            }, this);
        }
        list_button(list, LV_SYMBOL_KEYBOARD, "Connect to...", [](lv_event_t* e) {
            static_cast<RemoteApp*>(lv_event_get_user_data(e))->ask("Host (user@host)", "", [](const std::string& v) {
                if (!v.empty()) terminal_run((use_mosh() ? "mosh " : "ssh ") + v);
            });
        }, this);
        std::string user = "SSH as: " + (ssh_user().empty() ? std::string("pi") : ssh_user());
        list_button(list, LV_SYMBOL_EDIT, user.c_str(), [](lv_event_t* e) {
            static_cast<RemoteApp*>(lv_event_get_user_data(e))->ask("SSH user name", ssh_user(), [](const std::string& v) {
                set_ssh_user(v);
            });
        }, this);
        list_button(list, LV_SYMBOL_SHUFFLE, use_mosh() ? "Use mosh: on" : "Use mosh: off", [](lv_event_t* e) {
            set_use_mosh(!use_mosh());
            auto* self = static_cast<RemoteApp*>(lv_event_get_user_data(e));
            self->dirty_ = true;
        }, this);
        if (ts_.running) {
            list_button(list, LV_SYMBOL_POWER, "Turn off Tailscale", [](lv_event_t* e) {
                hal().tailscale_stop();
                shell_toast("Tailscale off");
                auto* self = static_cast<RemoteApp*>(lv_event_get_user_data(e));
                self->dirty_ = true;
            }, this);
        }
        if (ts_.state == "Running") {
            list_button(list, LV_SYMBOL_CLOSE, "Sign out", [](lv_event_t*) {
                hal().tailscale_logout();
                shell_toast("Signed out of Tailscale");
            }, nullptr);
        }
        finish_list(list);
    }

    static void on_peer(lv_event_t* e) {
        auto* self = static_cast<RemoteApp*>(lv_event_get_user_data(e));
        int i = -1;
        for (size_t k = 0; k < self->peer_btns_.size(); ++k)
            if (self->peer_btns_[k] == lv_event_get_target_obj(e)) i = int(k);
        if (i < 0) return;
        const TsPeer& p = self->ts_.peers[size_t(i)];
        if (!remote_pending_send().empty()) {
            std::string path = remote_take_send();
            bool ok = hal().taildrop_send(path, p.name);
            shell_toast(ok ? "Sent to " + p.name : "Couldn't send to " + p.name);
            self->dirty_ = true;
            return;
        }
        if (!p.online) shell_toast(p.name + " looks offline; trying anyway");
        terminal_run(ssh_command(p.name));
    }

    // One-line text box over the screen. Enter saves, Esc cancels.
    using Done = void (*)(const std::string&);
    void ask(const char* title, const std::string& initial, Done done) {
        done_ = done;
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        label(input_, title, &lv_font_montserrat_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<RemoteApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = self->done_;
            self->close_input();
            d(v);
            self->dirty_ = true;
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
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* input_ = nullptr;
    Done done_ = nullptr;
    TsStatus ts_;
    std::string sig_;
    std::vector<lv_obj_t*> peer_btns_;
    bool starting_ = false;
    bool dirty_ = false;   // rebuild on the next tick (never inside a widget's own event)
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_remote_app() { return std::make_unique<RemoteApp>(); }

}  // namespace zs
