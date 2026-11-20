// Simulated hardware for running ZeroShell on a PC.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "hal/hal.h"
#include "hal/play_clock.h"
#include "hal/proc_stats.h"

namespace zs {
namespace {
bool g_hdmi = false;
int g_pose = 0, g_pose_ticks = 0;   // see mock_set_pose

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
        // A plausible draw that follows what the shell asked for.
        // Illustrative (measure on the unit): ~0.6 W base + backlight.
        int mw = cpu_ == CpuMode::Performance ? 780 : cpu_ == CpuMode::Saver ? 560 : 620;
        if (cpu_ == CpuMode::ScreenOff) mw = 430;
        if (screen_on_) mw += brightness_ * 4;
        if (bt_on_) mw += 40;
        if (pb_.playing) mw += 180;
        mw += int(30 * std::sin(ticks_ * 0.05));
        b.power_mw = mw;
        b.current_ma = -mw * 1000 / b.voltage_mv;
        b.charge_full_mah = 1730;
        b.charge_now_mah = 1730 * b.percent / 100;
        return b;
    }

    ImuSample imu() override {
        ImuSample s;
        s.available = true;
        double t = ticks_ * 0.02;
        s.heading_deg = std::fmod(312.0 + 20.0 * std::sin(t), 360.0);
        s.pitch_deg = 4.0f * std::sin(t * 1.3);
        s.roll_deg = -2.5f * std::cos(t * 0.9);
        // Gravity for the pretend pose; a short spin right after it changes.
        switch (g_pose) {
        case 1: s.ax = 0.1f; s.ay = -0.05f; s.az = -9.79f; break;   // face down
        case 2: s.ax = 0.2f; s.ay = 6.3f; s.az = 7.5f; break;       // in hand, ~40 deg toward you
        case 3: s.ax = 0.3f; s.ay = 9.7f; s.az = 0.8f; break;       // upright in a pocket
        default: s.ax = 0.1f; s.ay = 0.05f; s.az = 9.79f; break;    // flat, face up
        }
        if (g_pose_ticks > 0) {
            --g_pose_ticks;
            s.gx = 90.0f;
        }
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
        n.bt_connected = bt_on_ && bt_paired_ ? 1 : 0;
        if (ts_on_ && ts_signed_in_) n.tailscale_ip = "100.101.42.7";
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
    void set_bt_enabled(bool on) override { bt_on_ = on; bt_paired_ = false; }

    int brightness() override { return brightness_; }
    void set_brightness(int p) override { brightness_ = p; }
    void set_screen_on(bool on) override { screen_on_ = on; }
    bool has_backlight() override { return false; }
    void set_cpu_mode(CpuMode m) override { cpu_ = m; }

    DisplayStatus display() override {
        if (!g_hdmi) dock_ = false;   // unplugging ends the pretend desktop
        DisplayStatus d;
        d.hdmi_connected = g_hdmi;
        d.hdmi_mode = g_hdmi ? "1920x1080" : "";
        d.dock_running = dock_;
        return d;
    }
    // Tailscale, simulated: sign-in "completes" 8 s after the QR code shows.
    TsStatus tailscale() override {
        TsStatus t;
        t.installed = true;
        t.running = ts_on_;
        if (!ts_on_) {
            t.state = "Stopped";
            return t;
        }
        if (!ts_signed_in_ && ts_clock_.ms() > 8000) ts_signed_in_ = true;
        if (!ts_signed_in_) {
            t.state = "NeedsLogin";
            t.auth_url = "https://login.tailscale.com/a/5f1c2e9a7b3d";
            return t;
        }
        t.state = "Running";
        t.self_name = "cardputer";
        t.self_ip = "100.101.42.7";
        t.tailnet = "tail4c2e.ts.net";
        t.peers = {{"desktop", "100.88.12.4", "windows", true},
                   {"homelab", "100.71.3.20", "linux", true},
                   {"phone", "100.90.7.33", "iOS", true},
                   {"macbook", "100.64.9.12", "macOS", false}};
        return t;
    }
    void tailscale_start() override {
        ts_on_ = true;
        ts_clock_.start();
    }
    void tailscale_stop() override { ts_on_ = false; }
    void tailscale_logout() override { ts_signed_in_ = false; ts_clock_.start(); }
    bool taildrop_send(const std::string& path, const std::string& peer) override {
        std::printf("[mock-taildrop] %s -> %s\n", path.c_str(), peer.c_str());
        return ts_on_ && ts_signed_in_;
    }
    // Sync, simulated: ready after 2 s, "desktop" connects and Documents
    // catches up over ~12 s, and "phone" asks to connect after 6 s.
    SyncStatus sync() override {
        SyncStatus s;
        s.installed = true;
        s.running = sync_on_;
        if (!sync_on_ || sync_clock_.ms() < 2000) return s;
        long t = sync_clock_.ms();
        s.ready = true;
        s.device_id = "K7QX2FA-MZ4D6VR-3TNHB5L-PW2YC7E-JQ6U4SG-XA5LKD3-RV7ME2N-HC4ZT6B";
        int docs = int(std::min<long>(100, std::max<long>(0, (t - 4000) / 120)));
        s.folders = {{"zeroos-notes", "Notes", "/home/pi/Notes", "idle", 100, 0, ""},
                     {"zeroos-documents", "Documents", "/home/pi/Documents", docs < 100 ? "syncing" : "idle", docs,
                      docs < 100 ? (100 - docs) / 8 + 1 : 0, ""},
                     {"zeroos-recordings", "Recordings", "/home/pi/Recordings", "idle", 100, 0, ""}};
        if (!sync_desktop_removed_) s.devices.push_back({"DSK7Q2A-...", "desktop", t > 4000, 100});
        if (sync_phone_ == 1) s.devices.push_back({"PHN3K5D-...", "phone", true, int(std::min<long>(100, (t - sync_phone_at_) / 80))});
        if (sync_phone_ == 0 && t > 6000) s.requests.push_back({false, "PHN3K5D-...", "phone", "", ""});
        return s;
    }
    void sync_start() override {
        if (!sync_on_) sync_clock_.start();
        sync_on_ = true;
    }
    void sync_stop() override { sync_on_ = false; }
    bool sync_running() override { return sync_on_; }
    bool sync_answer(const SyncRequest& r, bool accept) override {
        std::printf("[mock-sync] %s %s\n", accept ? "accept" : "ignore", r.device_name.c_str());
        sync_phone_ = accept ? 1 : 2;
        sync_phone_at_ = sync_clock_.ms();
        return true;
    }
    bool sync_add_device(const std::string& id, const std::string& name) override {
        std::printf("[mock-sync] add %s %s\n", id.c_str(), name.c_str());
        return id.size() == 63;
    }
    bool sync_remove_device(const std::string& id) override {
        if (id.rfind("DSK", 0) == 0) sync_desktop_removed_ = true;
        if (id.rfind("PHN", 0) == 0) sync_phone_ = 2;
        return true;
    }
    void sync_rescan() override {}

    // Expansion, simulated: a Cap LoRa-1262 is plugged in. The GPS gets a
    // fix 6 s after it is first asked; the mesh starts in 2 s.
    bool ext_power() override { return ext_on_; }
    void set_ext_power(bool on) override { ext_on_ = on; }
    GpsFix gps() override {
        if (!gps_on_) {
            gps_on_ = true;
            gps_clock_.start();
        }
        GpsFix f;
        f.device = true;
        long t = gps_clock_.ms();
        f.sats_seen = int(std::min<long>(14, 3 + t / 700));
        f.mode = t < 6000 ? 1 : 3;
        f.time = "2026-11-16T14:20:31.000Z";
        if (f.mode >= 2) {
            double w = t / 4000.0;
            f.lat = 51.500729 + 0.00002 * std::sin(w);
            f.lon = -0.124625 + 0.00003 * std::cos(w);
            f.alt_m = 18.4;
            f.speed_mps = 1.3;
            f.track_deg = 64;
            f.error_m = 3.2;
            f.sats_used = std::max(4, f.sats_seen - 4);
        }
        return f;
    }
    void gps_stop() override { gps_on_ = false; }
    MeshStatus mesh() override {
        MeshStatus m;
        m.installed = true;
        m.running = mesh_on_ && mesh_clock_.ms() > 2000;
        if (m.running) {
            m.web_url = "https://192.168.1.42:9443";
            m.api = "192.168.1.42";
            m.connected = true;
            m.my_name = "CZ01";
            m.nodes = 3;
        }
        return m;
    }
    // Simulated chat: a hiker says hello after 3 s; replies come back 4 s
    // after you send.
    void mesh_chat_tick() {
        if (!mesh_on_) return;
        long t = mesh_clock_.ms();
        if (!mesh_hello_ && t > 3000) {
            mesh_hello_ = true;
            mesh_log_.push_back({"HIKE", "Anyone near the north trailhead?", false, false, long(std::time(nullptr))});
            ++mesh_seq_;
        }
        if (mesh_reply_at_ && t > mesh_reply_at_) {
            mesh_reply_at_ = 0;
            mesh_log_.push_back({"BASE", "Copy that. Heading your way.", false, false, long(std::time(nullptr))});
            ++mesh_seq_;
        }
    }
    std::vector<MeshMessage> mesh_messages() override {
        mesh_chat_tick();
        return mesh_log_;
    }
    bool mesh_send(const std::string& text) override {
        if (!mesh_on_ || text.empty()) return false;
        mesh_log_.push_back({"", text, true, false, long(std::time(nullptr))});
        mesh_reply_at_ = mesh_clock_.ms() + 4000;
        return true;
    }
    int mesh_seq() override {
        mesh_chat_tick();
        return mesh_seq_;
    }
    void mesh_start() override {
        ext_on_ = true;
        if (!mesh_on_) mesh_clock_.start();
        mesh_on_ = true;
    }
    void mesh_stop() override { mesh_on_ = false; }

    bool terminal_open(const std::string&) override { return false; }   // no real console here
    bool terminal_active() override { return false; }

    void dock_start() override { dock_ = g_hdmi; }
    void dock_stop() override { dock_ = false; }
    bool dock_open(const std::string& what, const std::string& path) override {
        std::printf("[mock-dock] open %s %s\n", what.c_str(), path.c_str());
        return dock_;
    }
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
    bool play_stream(const std::string& url, const std::string&, int duration_s) override {
        pb_ = PlaybackState{};
        pb_.path = url;
        pb_.playing = true;
        pb_.dur_ms = duration_s > 0 ? duration_s * 1000 : 30000;
        clock_.start();
        return true;
    }
    // A pretend Jellyfin server: any address and user sign in.
    bool jellyfin_login(const std::string& server, const std::string& user, const std::string& password,
                        const std::string& device_id, JfSession& out, std::string& error) override {
        if (password == "wrong") {
            error = "Wrong user name or password";
            return false;
        }
        out = JfSession{jf::normalize_server(server), user, "0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f", "simulated", device_id};
        return true;
    }
    bool jellyfin_list(const JfSession&, JfView view, const std::string& arg, std::vector<JfItem>& out,
                       std::string&) override {
        using K = JfItem;
        static const std::vector<JfItem> albums = {
            {K::Album, "a1", "Bench Tapes Vol. 2", "Bench Tapes", -1},
            {K::Album, "a2", "Chip Sessions", "Various Artists", -1},
            {K::Album, "a3", "Night Drive", "Synthwave Demos", -1},
            {K::Album, "a4", "Solder & Flux", "Bench Tapes", -1},
        };
        static const std::vector<JfItem> tracks = {
            {K::Track, "c1", "Low Battery Blues", "Bench Tapes", 243},
            {K::Track, "c2", "Pocket Computer", "Bench Tapes", 162},
            {K::Track, "c3", "Forty-Six Keys", "Bench Tapes", 201},
            {K::Track, "c4", "Interrupt Request", "Bench Tapes", 199},
            {K::Track, "c5", "Brownout", "Bench Tapes", 177},
        };
        switch (view) {
            case JfView::Latest:
            case JfView::Albums:
            case JfView::ArtistAlbums: out = albums; break;
            case JfView::Artists:
                out = {{K::Artist, "b1", "Bench Tapes", "", -1}, {K::Artist, "b2", "Synthwave Demos", "", -1}};
                break;
            case JfView::Playlists: out = {{K::Playlist, "d1", "Soldering", "", -1}}; break;
            case JfView::Search:
                out.clear();
                for (auto& t : tracks)
                    if (lower(t.name).find(lower(arg)) != std::string::npos) out.push_back(t);
                break;
            case JfView::AlbumTracks:
            case JfView::PlaylistTracks: out = tracks; break;
        }
        return true;
    }
    static std::string lower(std::string s) {
        for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
        return s;
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

    CameraStatus camera() override { return {true, true, true}; }
    bool camera_open() override { return false; }
    bool camera_active() override { return false; }
    void camera_close() override {}

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
    bool bt_paired_ = true;   // headphones "connected" until Bluetooth is toggled
    std::string connected_ssid_ = "HomeNet";
    int brightness_ = 80;
    int volume_ = 60;
    bool muted_ = false;
    bool screen_on_ = true;
    bool dock_ = false;
    bool ts_on_ = false, ts_signed_in_ = false;
    PlayClock ts_clock_;
    bool sync_on_ = false, sync_desktop_removed_ = false;
    int sync_phone_ = 0;   // 0 asking, 1 accepted, 2 ignored
    long sync_phone_at_ = 0;
    PlayClock sync_clock_;
    bool ext_on_ = false, gps_on_ = false, mesh_on_ = false;
    PlayClock gps_clock_, mesh_clock_;
    std::vector<MeshMessage> mesh_log_;
    bool mesh_hello_ = false;
    long mesh_reply_at_ = 0;
    int mesh_seq_ = 0;
    CpuMode cpu_ = CpuMode::Balanced;
};

}  // namespace

void mock_set_hdmi(bool connected) { g_hdmi = connected; }
void mock_set_pose(int pose) {
    g_pose = pose;
    g_pose_ticks = 3;
}

std::unique_ptr<Hal> make_mock_hal() { return std::make_unique<MockHal>(); }

}  // namespace zs
