#include "ui/remote.h"

#include "hal/hal.h"
#include "ui/power.h"
#include "ui/prefs.h"
#include "ui/shell.h"

namespace zs {
namespace {
constexpr uint32_t kIdleStopMs = 15 * 60 * 1000;
uint32_t g_last_use = 0, g_last_check = 0;
bool g_boot_done = false, g_use_now = false;
std::string g_send;
}  // namespace

std::string tailscale_mode() { return prefs::get_str("tailscale.mode", "needed"); }
void set_tailscale_mode(const std::string& m) { prefs::set_str("tailscale.mode", m); }

void remote_note_use() { g_use_now = true; }

void remote_tick(uint32_t now_ms) {
    auto& sh = Shell::get();
    if (g_use_now || sh.current_app() == "remote" || sh.in_terminal() || power().docked()) g_last_use = now_ms;
    g_use_now = false;
    if (now_ms - g_last_check < 30000 && g_boot_done) return;
    g_last_check = now_ms;
    std::string mode = tailscale_mode();
    if (!g_boot_done) {
        g_boot_done = true;
        if (mode == "always" && !hal().tailscale().running) hal().tailscale_start();
        return;
    }
    if (mode == "needed" && now_ms - g_last_use > kIdleStopMs && hal().tailscale().running) hal().tailscale_stop();
}

void remote_set_send(const std::string& path) { g_send = path; }
std::string remote_take_send() {
    std::string s;
    s.swap(g_send);
    return s;
}
const std::string& remote_pending_send() { return g_send; }

std::string ssh_user() { return prefs::get_str("remote.user", ""); }
void set_ssh_user(const std::string& u) { prefs::set_str("remote.user", u); }
bool use_mosh() { return prefs::get_int("remote.mosh", 0) != 0; }
void set_use_mosh(bool on) { prefs::set_int("remote.mosh", on); }

std::string ssh_command(const std::string& host) {
    std::string target = ssh_user().empty() ? host : ssh_user() + "@" + host;
    return std::string(use_mosh() ? "mosh " : "ssh ") + target;
}

}  // namespace zs
