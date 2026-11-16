#include "ui/power.h"

#include <algorithm>
#include <cmath>

#include "ui/app.h"
#include "ui/prefs.h"

namespace zs {

namespace {
constexpr int kDimPct = 10;
constexpr int kLowBattery = 15, kCriticalBattery = 3;
constexpr uint32_t kWifiSleepAfterMs = 10 * 60 * 1000;
constexpr double kNominalV = 3.7;        // for mAh -> mWh
constexpr int kDesignMwh = 6475;         // 1750 mAh x 3.7 V
// Motion. g = 9.81 m/s^2.
constexpr float kFaceDownZ = -8.0f;      // screen facing the table
constexpr uint32_t kFaceDownMs = 1500;   // held that long
constexpr float kFacingYouZ = 3.0f;      // screen up enough to read (at most ~70 deg from flat)
constexpr float kFlatZ = 9.3f;           // flatter than ~18 deg: put down, not picked up
constexpr float kPickupDelta = 3.0f;     // m/s^2 change in gravity from rest (~18 deg)
constexpr float kPickupGyro = 60.0f;     // deg/s
constexpr uint32_t kPickupGraceMs = 8000;      // no key after a pickup wake: off again
constexpr uint32_t kFalseWakeWindowMs = 5 * 60 * 1000;
constexpr uint32_t kPickupPauseMs = 15 * 60 * 1000;   // 3 false wakes (a bag, a pocket): pause
}  // namespace

Power& Power::get() {
    static Power p;
    return p;
}

const char* Power::profile_name(Profile p) {
    switch (p) {
    case Profile::Saver: return "Saver";
    case Profile::Performance: return "Performance";
    default: return "Balanced";
    }
}

void Power::start() {
    profile_ = Profile(std::clamp(prefs::get_int("power.profile", 1), 0, 2));
    brightness_ = std::clamp(prefs::get_int("display.brightness", 80), 5, 100);
    dim_after_s_ = prefs::get_int("display.dim_after", 30);
    off_after_s_ = prefs::get_int("display.off_after", 60);
    bt_auto_off_min_ = prefs::get_int("bluetooth.auto_off", 10);
    wifi_sleep_ = prefs::get_int("wifi.sleep", 0) != 0;
    show_time_left_ = prefs::get_int("statusbar.time_left", 1) != 0;
    wake_on_pickup_ = prefs::get_int("motion.wake", 1) != 0;
    sleep_face_down_ = prefs::get_int("motion.face_down", 1) != 0;
    z_sign_ = prefs::get_int("motion.z_sign", 1) < 0 ? -1 : 1;
    hal().set_brightness(brightness_);
    apply_cpu();
}

Profile Power::effective_profile() const { return low_saver_ ? Profile::Saver : profile_; }

void Power::apply_cpu() {
    if (state_ == State::Off && !docked_) {
        hal().set_cpu_mode(CpuMode::ScreenOff);
        return;
    }
    Profile p = effective_profile();
    if (docked_ && p == Profile::Saver) p = Profile::Balanced;   // a desktop needs the speed
    switch (p) {
    case Profile::Saver: hal().set_cpu_mode(CpuMode::Saver); break;
    case Profile::Performance: hal().set_cpu_mode(CpuMode::Performance); break;
    default: hal().set_cpu_mode(CpuMode::Balanced); break;
    }
}

void Power::set_profile(Profile p) {
    profile_ = p;
    if (p != Profile::Saver) low_saver_ = false;   // the user overrides the low-battery saver
    prefs::set_int("power.profile", int(p));
    apply_cpu();
}

void Power::cycle_profile() { set_profile(Profile((int(profile_) + 1) % 3)); }

void Power::set_user_brightness(int pct) {
    brightness_ = std::clamp(pct, 5, 100);
    prefs::set_int("display.brightness", brightness_);
    if (state_ == State::On) hal().set_brightness(brightness_);
}

void Power::set_dim_after_s(int s) { dim_after_s_ = s; prefs::set_int("display.dim_after", s); }
void Power::set_off_after_s(int s) { off_after_s_ = s; prefs::set_int("display.off_after", s); }
void Power::set_bt_auto_off_min(int m) { bt_auto_off_min_ = m; prefs::set_int("bluetooth.auto_off", m); }
void Power::set_wifi_sleep(bool on) { wifi_sleep_ = on; prefs::set_int("wifi.sleep", on); }
void Power::set_show_time_left(bool on) { show_time_left_ = on; prefs::set_int("statusbar.time_left", on); }

void Power::set_wake_on_pickup(bool on) { wake_on_pickup_ = on; prefs::set_int("motion.wake", on); }
void Power::set_sleep_face_down(bool on) { sleep_face_down_ = on; prefs::set_int("motion.face_down", on); }

// Face down: screen off at once. Picked up (moved, and ending with the screen
// tilted up toward you, not flat on a table) while dim or off: screen on. A pickup wake with no
// key within 8 s turns the screen off again; three of those in 5 minutes (a
// bag, a pocket, a car seat) pause pickup wake for 15 minutes.
void Power::motion_tick(uint32_t now_ms) {
    if (docked_ || (!wake_on_pickup_ && !sleep_face_down_)) return;
    ImuSample m = hal().imu();
    if (!m.available) return;
    float x = m.ax, y = m.ay, z = m.az * float(z_sign_);
    bool face_down = z < kFaceDownZ;

    if (sleep_face_down_ && state_ != State::Off && face_down && !handoff_) {
        if (!face_down_since_) face_down_since_ = now_ms;
        if (now_ms - face_down_since_ >= kFaceDownMs) {
            set_state(State::Off, now_ms);
            face_down_off_ = true;
            motion_woke_ = false;
        }
    } else if (!face_down) {
        face_down_since_ = 0;
    }

    if (state_ == State::On) {
        have_rest_ = false;
        if (motion_woke_ && now_ms - motion_woke_at_ >= kPickupGraceMs) {
            motion_woke_ = false;
            set_state(State::Off, now_ms);
            if (now_ms - false_wake_window_ > kFalseWakeWindowMs) {
                false_wake_window_ = now_ms;
                false_wakes_ = 0;
            }
            if (++false_wakes_ >= 3) {
                pickup_paused_until_ = now_ms + kPickupPauseMs;
                false_wakes_ = 0;
            }
        }
        return;
    }
    // Dim or off: remember how it lies, wake when that changes toward you.
    if (!have_rest_) {
        rest_x_ = x, rest_y_ = y, rest_z_ = z;
        have_rest_ = true;
        return;
    }
    float dx = x - rest_x_, dy = y - rest_y_, dz = z - rest_z_;
    float moved = std::sqrt(dx * dx + dy * dy + dz * dz);
    float spin = std::sqrt(m.gx * m.gx + m.gy * m.gy + m.gz * m.gz);
    bool facing_you = z > kFacingYouZ && z < kFlatZ;
    bool paused = pickup_paused_until_ && int32_t(now_ms - pickup_paused_until_) < 0;
    if (wake_on_pickup_ && !paused && facing_you && (moved > kPickupDelta || spin > kPickupGyro)) {
        bool was_off = state_ == State::Off;
        set_state(State::On, now_ms);
        last_input_ = now_ms;
        have_rest_ = false;
        // Turned over from face down, or lifted from off: give it the grace
        // period. From dim the user was just using it; treat it as a key.
        motion_woke_ = was_off;
        motion_woke_at_ = now_ms;
        face_down_off_ = false;
        return;
    }
    // Slow drift (sliding on a table): follow it so it doesn't add up.
    if (moved < 0.5f) {
        rest_x_ += 0.2f * dx, rest_y_ += 0.2f * dy, rest_z_ += 0.2f * dz;
    }
}

void Power::set_state(State s, uint32_t now_ms) {
    if (s == state_) return;
    State old = state_;
    state_ = s;
    switch (s) {
    case State::On:
        if (old == State::Off) {
            hal().set_screen_on(true);
            if (wifi_slept_) {
                hal().set_wifi_enabled(true);
                wifi_slept_ = false;
            }
        }
        hal().set_brightness(brightness_);
        break;
    case State::Dim:
        hal().set_brightness(std::min(brightness_, kDimPct));
        break;
    case State::Off:
        off_since_ = now_ms;
        hal().set_screen_on(false);
        break;
    }
    apply_cpu();
    shell_screen_changed();
}

bool Power::on_key(uint32_t now_ms) {
    if (docked_) return true;   // typing on the monitor; ZeroShell stays out of it
    last_input_ = now_ms;
    bool was_off = state_ == State::Off;
    if (motion_woke_) {   // picked up and then used: a real wake
        motion_woke_ = false;
        false_wakes_ = 0;
        was_off = false;  // the screen is already on; this key counts
    }
    set_state(State::On, now_ms);
    return was_off || handoff_;   // the key that wakes the screen does nothing else
}

void Power::update_estimate(const BatteryInfo& b, uint32_t now_ms) {
    if (b.external_power != was_external_) {   // plugged or unplugged: start over
        was_external_ = b.external_power;
        avg_ms_ = 0;
    }
    uint32_t dt = last_sample_ ? now_ms - last_sample_ : 0;
    last_sample_ = now_ms;
    if (b.power_mw <= 0) return;
    if (avg_ms_ == 0) {
        avg_mw_ = b.power_mw;
        avg_ms_ = 1;
    } else if (dt > 0) {
        // Exponential average with a 3-minute time constant: steady enough
        // to read, quick enough to follow the screen turning off.
        double a = 1.0 - std::exp(-double(dt) / 180000.0);
        avg_mw_ += a * (b.power_mw - avg_mw_);
        avg_ms_ += dt;
    }

    minutes_left_ = -1;
    if (b.external_power) {
        minutes_left_ = b.charging ? b.minutes_left : -1;   // the gauge knows charging best
        return;
    }
    double left_mwh = b.charge_now_mah >= 0 ? b.charge_now_mah * kNominalV
                                            : kDesignMwh * std::max(b.percent, 0) / 100.0;
    if (avg_ms_ >= 30000 && avg_mw_ > 50)
        minutes_left_ = int(left_mwh / avg_mw_ * 60.0);
    else
        minutes_left_ = b.minutes_left;   // the gauge's own figure until ours settles
}

void Power::set_docked(bool docked, uint32_t now_ms) {
    if (docked == docked_) return;
    docked_ = docked;
    last_input_ = now_ms;
    if (docked) shell_open_app("dock");   // the LCD shows what's on the monitor
    set_state(State::On, now_ms);
    apply_cpu();
    shell_screen_changed();
}

// HDMI plugged in: open the Dock app, or start Dock mode (dock.on_hdmi =
// ask | auto | off). Monitor unplugged for 3 s: stop Dock mode.
void Power::watch_dock(uint32_t now_ms) {
    if (now_ms - last_dock_check_ < 1000) return;
    last_dock_check_ = now_ms;
    auto& h = hal();
    DisplayStatus d = h.display();
    if (d.hdmi_connected && !hdmi_was_ && !d.dock_running) {
        std::string mode = prefs::get_str("dock.on_hdmi", "ask");
        if (mode == "auto") {
            h.dock_start();
            shell_toast("Monitor connected: starting Dock mode");
        } else if (mode == "ask") {
            set_state(State::On, now_ms);
            last_input_ = now_ms;
            shell_open_app("dock");
        }
    }
    hdmi_was_ = d.hdmi_connected;
    if (d.dock_running && !d.hdmi_connected) {
        if (!hdmi_lost_) hdmi_lost_ = now_ms;
        if (now_ms - hdmi_lost_ >= 3000) {
            h.dock_stop();
            hdmi_lost_ = 0;
            d.dock_running = false;
            shell_toast("Monitor unplugged: Dock mode off");
        }
    } else {
        hdmi_lost_ = 0;
    }
    set_docked(d.dock_running, now_ms);
}

void Power::tick(uint32_t now_ms) {
    auto& h = hal();
    if (!last_input_) last_input_ = now_ms;
    watch_dock(now_ms);
    BatteryInfo b = h.battery();
    update_estimate(b, now_ms);
    motion_tick(now_ms);

    // Screen: dim, then off. Plugged in: dim only, never off.
    uint32_t idle = now_ms - last_input_;
    if (state_ != State::Off) {
        if (off_after_s_ > 0 && (docked_ || !b.external_power) && idle >= uint32_t(off_after_s_) * 1000)
            set_state(State::Off, now_ms);
        else if (dim_after_s_ > 0 && idle >= uint32_t(dim_after_s_) * 1000)
            set_state(State::Dim, now_ms);
    }

    // Wi-Fi sleep: off after a long screen-off; nothing is using it.
    if (state_ == State::Off && wifi_sleep_ && !wifi_slept_ && now_ms - off_since_ >= kWifiSleepAfterMs &&
        !h.playback().playing) {
        NetStatus n = h.net();
        if (n.wifi_enabled) {
            h.set_wifi_enabled(false);
            wifi_slept_ = true;
        }
    }

    // Bluetooth: off when nothing has been connected for a while.
    NetStatus n = h.net();
    if (n.bt_enabled && n.bt_connected == 0 && bt_auto_off_min_ > 0) {
        if (!bt_idle_since_) bt_idle_since_ = now_ms;
        if (now_ms - bt_idle_since_ >= uint32_t(bt_auto_off_min_) * 60000) {
            h.set_bt_enabled(false);
            bt_idle_since_ = 0;
            shell_toast("Bluetooth off: nothing connected");
        }
    } else {
        bt_idle_since_ = 0;
    }

    // Low battery.
    if (b.percent >= 0 && !b.external_power) {
        if (b.percent <= kLowBattery && !low_saver_ && profile_ != Profile::Saver) {
            low_saver_ = true;
            apply_cpu();
            shell_toast("Battery low: Saver on");
        }
        if (b.percent <= kCriticalBattery) {
            if (!shutdown_warned_) {
                shutdown_warned_ = true;
                set_state(State::On, now_ms);
                shell_toast("Battery empty: shutting down");
            } else {
                h.power_off();
            }
        }
    } else if (b.external_power) {
        shutdown_warned_ = false;
        if (low_saver_ && b.percent > kLowBattery + 5) {
            low_saver_ = false;
            apply_cpu();
        }
    }
}

}  // namespace zs
