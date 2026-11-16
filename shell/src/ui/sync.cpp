#include "ui/sync.h"

#include "hal/hal.h"
#include "ui/app.h"
#include "ui/prefs.h"
#include "ui/shell.h"

namespace zs {
namespace {
constexpr uint32_t kCheckMs = 10 * 1000;
constexpr uint32_t kGraceMs = 60 * 1000;          // keep running a minute after it was last wanted
constexpr uint32_t kNowLimitMs = 10 * 60 * 1000;   // "Sync now" gives up after 10 minutes
constexpr uint32_t kNoPeerMs = 3 * 60 * 1000;      // ...or 3 minutes with nobody online
constexpr uint32_t kSettleMs = 20 * 1000;          // and needs this long to see changes arrive

uint32_t g_last_check = 0, g_last_want = 0, g_now_since = 0;
bool g_checked = false, g_now = false, g_running = false, g_now_started = false;

bool up_to_date(const SyncStatus& s, bool& anyone) {
    anyone = false;
    for (auto& d : s.devices) {
        if (!d.connected) continue;
        anyone = true;
        if (d.percent < 100) return false;
    }
    for (auto& f : s.folders)
        if (f.state != "idle" || f.percent < 100 || f.need_files > 0) return false;
    return s.ready && anyone;
}
}  // namespace

std::string sync_mode() { return prefs::get_str("sync.mode", "charging"); }
void set_sync_mode(const std::string& m) {
    prefs::set_str("sync.mode", m);
    g_checked = false;   // act on it at the next tick
}

void sync_now() {
    g_now = true;
    g_now_started = false;
    g_checked = false;
}
bool sync_now_active() { return g_now; }
bool sync_running_cached() { return g_running; }

void sync_tick(uint32_t now_ms) {
    if (g_checked && now_ms - g_last_check < kCheckMs) return;
    bool first = !g_checked;
    g_checked = true;
    g_last_check = now_ms;
    Hal& h = hal();

    if (g_now && !g_now_started) {
        g_now_started = true;
        g_now_since = now_ms;
    }
    std::string mode = sync_mode();
    bool app = Shell::get().current_app() == "sync";
    bool want = mode == "always" || app || g_now;
    if (mode == "charging" && h.battery().external_power) {
        NetStatus n = h.net();
        want = want || n.wifi_connected || n.ethernet || n.usb_gadget;
    }
    if (want || first) g_last_want = now_ms;

    g_running = h.sync_running();
    if (want && !g_running) {
        h.sync_start();
        g_running = true;
    } else if (!want && g_running && now_ms - g_last_want > kGraceMs) {
        h.sync_stop();
        g_running = false;
    }

    if (g_now && g_running && now_ms - g_now_since > kSettleMs) {
        bool anyone = false;
        SyncStatus s = h.sync();
        uint32_t t = now_ms - g_now_since;
        if (up_to_date(s, anyone)) {
            g_now = false;
            shell_toast(LV_SYMBOL_OK " Sync done");
        } else if (!anyone && t > kNoPeerMs) {
            g_now = false;
            shell_toast("Sync: none of your devices are online");
        } else if (t > kNowLimitMs) {
            g_now = false;
            shell_toast("Sync: still going; stopped after 10 minutes");
        }
    }
}

}  // namespace zs
