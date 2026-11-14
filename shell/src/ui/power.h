// Power management: screen dimming and blanking, CPU profile, radios, low
// battery handling, and a smoothed time-left estimate.
//
// Policy (all adjustable in Settings, saved in prefs):
//   idle dim_after seconds   -> backlight down to 10%
//   idle off_after seconds   -> backlight and panel off, CPU to lowest speed,
//                               UI stops redrawing; the first key only wakes
//   Bluetooth on, nothing connected for bt_auto_off minutes -> Bluetooth off
//   wifi_sleep on: Wi-Fi off after 10 minutes of screen off, back on at wake
//   battery <= 15% on battery power -> Saver profile until charged
//   battery <= 3% on battery power  -> clean shutdown
#pragma once

#include <cstdint>
#include <string>

#include "hal/hal.h"

namespace zs {

enum class Profile { Saver = 0, Balanced = 1, Performance = 2 };

class Power {
public:
    static Power& get();

    void start();                 // apply saved settings
    void tick(uint32_t now_ms);   // from the shell's timer
    // A key went down. Returns true if it only woke the screen (swallow it).
    bool on_key(uint32_t now_ms);

    // Dock mode: the Cardputer's keys type on the monitor, so they neither
    // wake nor reach ZeroShell; the LCD dims and turns off on its own timer;
    // the CPU never drops to its screen-off speed.
    void set_docked(bool docked, uint32_t now_ms);
    bool docked() const { return docked_; }

    // Terminal on the built-in screen: keys belong to the console but still
    // count as activity (and wake the screen); ZeroShell doesn't act on them.
    void set_handoff(bool on) { handoff_ = on; }

    bool screen_off() const { return state_ == State::Off; }
    bool dimmed() const { return state_ == State::Dim; }

    Profile profile() const { return profile_; }
    Profile effective_profile() const;   // Saver while low-battery saver is active
    void set_profile(Profile p);
    void cycle_profile();
    static const char* profile_name(Profile p);

    int user_brightness() const { return brightness_; }
    void set_user_brightness(int pct);

    // Smoothed draw (mW) and minutes left; -1 when not known yet.
    int avg_power_mw() const { return int(avg_mw_ + 0.5); }
    int minutes_left() const { return minutes_left_; }

    // Settings, in seconds (0 = never) and minutes (0 = never).
    int dim_after_s() const { return dim_after_s_; }
    int off_after_s() const { return off_after_s_; }
    int bt_auto_off_min() const { return bt_auto_off_min_; }
    bool wifi_sleep() const { return wifi_sleep_; }
    bool show_time_left() const { return show_time_left_; }
    void set_dim_after_s(int s);
    void set_off_after_s(int s);
    void set_bt_auto_off_min(int m);
    void set_wifi_sleep(bool on);
    void set_show_time_left(bool on);

private:
    enum class State { On, Dim, Off };
    void set_state(State s, uint32_t now_ms);
    void apply_cpu();
    void update_estimate(const BatteryInfo& b, uint32_t now_ms);
    void watch_dock(uint32_t now_ms);
    bool hdmi_was_ = false;
    uint32_t hdmi_lost_ = 0, last_dock_check_ = 0;

    State state_ = State::On;
    Profile profile_ = Profile::Balanced;
    bool low_saver_ = false;     // forced Saver by low battery
    bool shutdown_warned_ = false;
    int brightness_ = 80;
    int dim_after_s_ = 30, off_after_s_ = 60, bt_auto_off_min_ = 10;
    bool wifi_sleep_ = false, show_time_left_ = true;
    bool wifi_slept_ = false;
    bool docked_ = false;
    bool handoff_ = false;    // we turned Wi-Fi off; turn it back on at wake
    uint32_t last_input_ = 0, off_since_ = 0, bt_idle_since_ = 0, last_sample_ = 0;
    double avg_mw_ = 0;
    uint32_t avg_ms_ = 0;        // how long the average has been running
    bool was_external_ = false;
    int minutes_left_ = -1;
};

inline Power& power() { return Power::get(); }

}  // namespace zs
