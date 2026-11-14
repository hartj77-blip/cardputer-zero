// Simulated hardware for running ZeroShell on a PC.
#include <cmath>
#include <cstdio>
#include <ctime>

#include "hal/hal.h"
#include "hal/play_clock.h"
#include "hal/proc_stats.h"

namespace zs {
namespace {

class MockHal : public Hal {
public:
    void poll() override { ++ticks_; }

    BatteryInfo battery() override {
        BatteryInfo b;
        b.percent = 78;
        b.charging = false;
        b.voltage_mv = 3912;
        b.current_ma = -540;
        b.temp_c10 = 296;
        b.minutes_left = 152;
        b.health_percent = 99;
        b.cycle_count = 3;
        return b;
    }

    ImuSample imu() override {
        ImuSample s;
        s.available = true;
        double t = ticks_ * 0.02;
        s.heading_deg = std::fmod(312.0 + 20.0 * std::sin(t), 360.0);
        s.pitch_deg = 4.0f * std::sin(t * 1.3);
        s.roll_deg = -2.5f * std::cos(t * 0.9);
        s.ax = 0.1f; s.ay = 0.05f; s.az = 9.79f;
        return s;
    }

    NetStatus net() override {
        NetStatus n;
        n.wifi_enabled = wifi_on_;
        n.wifi_connected = wifi_on_;
        n.wifi_ssid = wifi_on_ ? connected_ssid_ : "";
        n.wifi_signal = 72;
        n.ip = wifi_on_ ? "192.168.1.42" : "";
        n.bt_enabled = bt_on_;
        n.bt_connected = bt_on_ ? 1 : 0;
        return n;
    }

    SystemInfo system() override {
        SystemInfo s;
        s.hostname = "cardputer";
        s.model = "CardputerZero";
        s.kernel = "6.18.39+rpt-rpi-v8 (simulated)";
        s.os_version = "ZeroOS " ZS_VERSION;
        s.simulated = true;
        s.cpu_temp_c = 47;
        s.cpu_load_percent = 9 + int(ticks_ / 7 % 5);
        s.cpu_mhz = s.cpu_load_percent > 10 ? 1000 : 600;
        s.cpu_max_mhz = 1000;
        s.cpu_governor = "ondemand";
        // Illustrative numbers (not measured): ZeroOS idle on a 512 MB unit.
        s.mem_total_mb = 438;
        s.mem_used_mb = 96;
        s.mem_avail_mb = 342;
        s.mem_cache_mb = 71;
        s.zram_stored_mb = 18.4f;
        s.zram_ram_mb = 6.6f;
        s.zram_capacity_mb = 657;
        s.disk_swap_total_mb = 0;   // zram+file: the SD file is writeback, not swap
        s.disk_swap_used_mb = 0;
        // The shell's own memory is real when the platform can report it.
        if (!read_self_mem(s.self_rss_kb, s.self_peak_kb)) {
            s.self_rss_kb = 9800;
            s.self_peak_kb = 10400;
        }
        s.top_procs = {{"zeroshell", s.self_rss_kb}, {"NetworkManager", 14200}, {"pipewire", 9100},
                       {"wireplumber", 8700}, {"systemd", 8300}};
        s.disk_used_gb10 = 31;
        s.disk_total_gb10 = 290;
        s.uptime_s = 3600 + ticks_ / 10;
        return s;
    }

    std::vector<WifiNetwork> wifi_scan() override {
        return {
            {connected_ssid_, 72, true, wifi_on_},
            {"Workshop-5G", 61, true, false},
            {"CoffeeBar Guest", 44, false, false},
            {"DIRECT-printer", 30, true, false},
            {"neighbors_net", 18, true, false},
        };
    }
    bool wifi_connect(const std::string& ssid, const std::string&) override {
        connected_ssid_ = ssid;
        wifi_on_ = true;
        return true;
    }
    void set_wifi_enabled(bool on) override { wifi_on_ = on; }
    void set_bt_enabled(bool on) override { bt_on_ = on; }

    int brightness() override { return brightness_; }
    void set_brightness(int p) override { brightness_ = p; }
    int volume() override { return volume_; }
    void set_volume(int p) override { volume_ = p; }
    bool muted() override { return muted_; }
    void set_muted(bool m) override { muted_ = m; }

    // --- audio (simulated: time advances, nothing is heard)
    std::vector<Track> music_library() override {
        return {
            {"/home/pi/Music/Night Drive.mp3", "Night Drive", "Synthwave Demos", 214},
            {"/home/pi/Music/Solder Fumes.flac", "Solder Fumes", "Synthwave Demos", 187},
            {"/home/pi/Music/Low Battery Blues.ogg", "Low Battery Blues", "Bench Tapes", 243},
            {"/home/pi/Music/Pocket Computer.mp3", "Pocket Computer", "Bench Tapes", 162},
            {"/home/pi/Music/320x170.mp3", "320x170", "Chip Sessions", 128},
            {"/home/pi/Music/Interrupt Request.wav", "Interrupt Request", "Chip Sessions", 199},
        };
    }
    bool play(const std::string& path) override {
        pb_ = PlaybackState{};
        pb_.path = path;
        pb_.playing = true;
        pb_.dur_ms = 30000;
        for (auto& t : music_library())
            if (t.path == path) pb_.dur_ms = t.duration_s * 1000;
        for (auto& t : recs_)
            if (t.path == path) pb_.dur_ms = t.duration_s * 1000;
        clock_.start();
        return true;
    }
    void set_paused(bool p) override {
        if (pb_.path.empty()) return;
        pb_.paused = p;
        pb_.playing = !p;
        if (p) clock_.pause(); else clock_.resume();
    }
    void stop() override { pb_ = PlaybackState{}; }
    PlaybackState playback() override {
        if (!pb_.path.empty()) {
            pb_.pos_ms = int(clock_.ms());
            if (pb_.dur_ms > 0 && pb_.pos_ms >= pb_.dur_ms) {
                pb_ = PlaybackState{};
                pb_.finished = true;
            }
        }
        return pb_;
    }

    std::vector<Track> recordings() override { return recs_; }
    bool record_start() override {
        if (rec_.recording) return false;
        stop();
        rec_.recording = true;
        char name[64];
        std::snprintf(name, sizeof name, "/home/pi/Recordings/note-%03d.wav", ++rec_count_);
        rec_.path = name;
        rec_clock_.start();
        return true;
    }
    void record_stop() override {
        if (!rec_.recording) return;
        int secs = int(rec_clock_.ms() / 1000);
        recs_.insert(recs_.begin(), {rec_.path, rec_.path.substr(rec_.path.rfind('/') + 1), "just now", secs < 1 ? 1 : secs});
        rec_ = RecordState{};
    }
    RecordState record_state() override {
        if (rec_.recording) rec_.elapsed_ms = int(rec_clock_.ms());
        return rec_;
    }
    bool delete_recording(const std::string& path) override {
        for (size_t i = 0; i < recs_.size(); ++i) {
            if (recs_[i].path == path) {
                recs_.erase(recs_.begin() + long(i));
                return true;
            }
        }
        return false;
    }

    void set_led(Led, bool) override {}

    bool ir_send(const IrCode& c) override {
        std::printf("[mock-ir] send %s %s 0x%x\n", c.name.c_str(), c.protocol.c_str(), c.scancode);
        return true;
    }
    bool ir_learn(IrCode& out, int) override {
        out.protocol = "nec";
        out.scancode = 0x40bf00 + (unsigned)(std::time(nullptr) & 0xff);
        return true;
    }

    void power_off() override { std::printf("[mock] power off\n"); }
    void reboot() override { std::printf("[mock] reboot\n"); }
    void beep(int, int) override {}

private:
    PlaybackState pb_;
    PlayClock clock_, rec_clock_;
    RecordState rec_;
    int rec_count_ = 2;
    std::vector<Track> recs_ = {
        {"/home/pi/Recordings/note-002.wav", "note-002.wav", "Today 09:14", 23},
        {"/home/pi/Recordings/note-001.wav", "note-001.wav", "Yesterday 18:40", 61},
    };
    long ticks_ = 0;
    bool wifi_on_ = true;
    bool bt_on_ = true;
    std::string connected_ssid_ = "HomeNet";
    int brightness_ = 80;
    int volume_ = 60;
    bool muted_ = false;
};

}  // namespace

std::unique_ptr<Hal> make_mock_hal() { return std::make_unique<MockHal>(); }

}  // namespace zs
