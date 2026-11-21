// Simulated hardware for running ZeroShell on a PC.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include <cstring>

#include "hal/hal.h"
#include "hal/play_clock.h"
#include "src/libs/qrcode/qrcodegen.h"   // LVGL's bundled QR encoder, for the pretend camera
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
        for (auto& p : tether_phones_) n.bt_connected += p.link;
        if (ts_on_ && ts_signed_in_) n.tailscale_ip = "100.101.42.7";
        for (auto& p : tether().phones)
            if (p.online) {
                n.tether = "bluetooth";
                if (n.ip.empty()) n.ip = "172.20.10.4";
            }
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
    void set_bt_enabled(bool on) override {
        bt_on_ = on;
        bt_paired_ = false;
        if (!on) {
            for (auto& p : tether_phones_) p.link = p.online = false;
            tether_job_.clear();
        }
    }

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
        long sec = 14 * 3600 + 20 * 60 + 31 + t / 1000;
        char when[40];
        std::snprintf(when, sizeof when, "2026-11-16T%02ld:%02ld:%02ld.000Z", sec / 3600 % 24, sec / 60 % 60, sec % 60);
        f.time = when;
        if (f.mode >= 2) {
            // Walking north-east at 1.3 m/s, with a little wander.
            double m = (t - 6000) / 1000.0 * 1.3, w = t / 4000.0;
            f.lat = 51.500729 + m * 0.438 / 111320.0 + 0.000004 * std::sin(w);
            f.lon = -0.124625 + m * 0.899 / 69300.0 + 0.000006 * std::cos(w);
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

    // Camera, simulated: a desk with a card that slides into view. The card
    // carries a real QR code (a different one each time the camera opens),
    // so the app's reader is exercised end to end.
    void scan_start() override {
        if (cam_on_) return;
        cam_on_ = true;
        cam_clock_.start();
        cam_seq_ = 0;
        cam_drawn_ = -1000;
        static const char* const kScenes[] = {
            "WIFI:S:Pixel 9;T:WPA;P:pocket-hotspot;;",
            "https://docs.m5stack.com/en/CardputerZero",
            "Locker 14, code 2580",
        };
        cam_text_ = kScenes[cam_scene_++ % 3];
    }
    void scan_stop() override { cam_on_ = false; }
    CamState scan_state() override {
        if (!cam_on_) return CamState::Off;
        return cam_clock_.ms() < 600 ? CamState::Starting : CamState::Running;
    }
    bool scan_frame(CameraFrame& out) override {
        if (scan_state() != CamState::Running) return false;
        long t = cam_clock_.ms();
        if (t - cam_drawn_ >= 100) {
            cam_drawn_ = t;
            draw_scene(t - 600);
            ++cam_seq_;
        }
        out.w = kCamW;
        out.h = kCamH;
        out.y = cam_.data();
        out.u = cam_.data() + kCamW * kCamH;
        out.v = out.u + kCamW * kCamH / 4;
        out.seq = cam_seq_;
        return true;
    }
    void draw_scene(long t) {
        cam_.resize(size_t(kCamW) * kCamH * 3 / 2);
        uint8_t* y = cam_.data();
        uint8_t* u = y + kCamW * kCamH;
        uint8_t* v = u + kCamW * kCamH / 4;
        // A wooden desk: warm brown, lighter toward the top left, with grain.
        for (int r = 0; r < kCamH; ++r)
            for (int col = 0; col < kCamW; ++col)
                y[r * kCamW + col] = uint8_t(118 - r / 12 - col / 20 + ((r * 7 + col / 9) % 13 < 2 ? -9 : 0) +
                                             int((unsigned(r * 131 + col * 71 + cam_seq_ * 17) * 2654435761u) >> 30));
        std::memset(u, 112, size_t(kCamW) * kCamH / 4);
        std::memset(v, 146, size_t(kCamW) * kCamH / 4);
        // The card slides in from the right during the first second, then
        // sways a little, as a hand-held shot does.
        static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(8)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
        if (!qrcodegen_encodeText(cam_text_.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 8, qrcodegen_Mask_AUTO, true)) return;
        int n = qrcodegen_getSize(qr), cell = 280 / n, card = n * cell + 48;
        int slide = t < 1000 ? int((1000 - t) * 0.5) : 0;
        int cx = (kCamW - card) / 2 + slide + int(5 * std::sin(t / 700.0));
        int cy = (kCamH - card) / 2 + int(4 * std::cos(t / 900.0));
        for (int r = 0; r < card; ++r) {
            for (int col = 0; col < card; ++col) {
                int px = cx + col, py = cy + r;
                if (px < 0 || px >= kCamW || py < 0 || py >= kCamH) continue;
                int mx = (col - 24) / cell, my = (r - 24) / cell;
                bool dark = col >= 24 && r >= 24 && mx < n && my < n && qrcodegen_getModule(qr, mx, my);
                y[py * kCamW + px] = dark ? 28 : 226;
                u[(py / 2) * (kCamW / 2) + px / 2] = 128;
                v[(py / 2) * (kCamW / 2) + px / 2] = 128;
            }
        }
    }

    // Tethering, simulated: a search finds two phones after 3 s; pairing shows
    // a number for 4 s, then the phone is online 2 s later.
    TetherStatus tether() override {
        long t = tether_clock_.ms();
        if (tether_job_ == "searching" && t > 3000) {
            tether_job_.clear();
            tether_found_.clear();
            for (auto& f : std::vector<TetherPhone>{{"3C:28:6D:11:42:A0", "Pixel 9"}, {"F0:98:9D:5E:07:1B", "John's iPhone"}}) {
                bool paired = false;
                for (auto& p : tether_phones_) paired = paired || p.mac == f.mac;
                if (!paired) tether_found_.push_back(f);
            }
        } else if (tether_job_ == "pairing" && t > 5000) {
            for (size_t i = 0; i < tether_found_.size(); ++i)
                if (tether_found_[i].mac == tether_mac_) {
                    tether_phones_.push_back(tether_found_[i]);
                    tether_found_.erase(tether_found_.begin() + long(i));
                    break;
                }
            tether_job_ = "connecting";
            tether_clock_.start();
        } else if (tether_job_ == "connecting" && t > 2000) {
            tether_job_.clear();
            for (auto& p : tether_phones_) p.link = p.online = p.mac == tether_mac_;
        }
        TetherStatus s;
        s.bt_on = bt_on_;
        s.phones = tether_phones_;
        s.found = tether_found_;
        s.busy = tether_job_;
        s.busy_mac = tether_job_.empty() ? "" : tether_mac_;
        if (tether_job_ == "pairing" && tether_clock_.ms() > 1000) s.passkey = "483920";
        return s;
    }
    void tether_job(const char* job, const std::string& mac) {
        bt_on_ = true;
        tether_job_ = job;
        tether_mac_ = mac;
        tether_clock_.start();
    }
    void tether_search() override { tether_job("searching", ""); }
    void tether_pair(const std::string& mac) override { tether_job("pairing", mac); }
    void tether_connect(const std::string& mac) override { tether_job("connecting", mac); }
    void tether_disconnect(const std::string& mac) override {
        for (auto& p : tether_phones_)
            if (p.mac == mac) p.link = p.online = false;
    }
    void tether_forget(const std::string& mac) override {
        for (size_t i = 0; i < tether_phones_.size(); ++i)
            if (tether_phones_[i].mac == mac) {
                tether_phones_.erase(tether_phones_.begin() + long(i));
                break;
            }
    }

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

    // A pretend car and headphones; pairing always works.
    std::vector<BtDevice> bt_devices() override { return bt_; }
    void bt_scan() override {
        if (bt_.size() < 3) bt_.push_back({"00:1A:7D:DA:71:13", "Kitchen speaker", false, false});
    }
    bool bt_connect(const std::string& address, std::string&) override {
        for (auto& d : bt_) {
            d.connected = d.address == address;
            d.paired = d.paired || d.connected;
        }
        return true;
    }
    void bt_disconnect(const std::string& address) override {
        for (auto& d : bt_) if (d.address == address) d.connected = false;
    }
    void bt_forget(const std::string& address) override {
        for (size_t i = 0; i < bt_.size(); ++i) if (bt_[i].address == address) { bt_.erase(bt_.begin() + long(i)); break; }
    }
    void bt_set_visible(int) override {}
    MediaKey media_key() override { return MediaKey::None; }

    // The copy takes a few seconds, then the "car" takes the stick.
    UsbStickStatus usb_stick() override {
        UsbStickStatus st;
        if (!stick_on_) return st;
        long ms = stick_clock_.ms();
        if (ms < 4000) {
            st.state = UsbStickStatus::Building;
            st.percent = int(ms / 40);
        } else {
            st.state = ms < 7000 ? UsbStickStatus::Ready : UsbStickStatus::Attached;
        }
        return st;
    }
    void usb_stick_start() override {
        stick_on_ = true;
        stick_clock_.start();
    }
    void usb_stick_stop() override { stick_on_ = false; }

    // A pretend update: a newer version is found, downloads and installs.
    UpdateStatus update() override {
        UpdateStatus st;
        st.current = update_done_ ? "0.3.0" : ZS_VERSION;
        long ms = update_clock_.ms();
        switch (update_step_) {
            case 0: break;
            case 1:
                st.state = ms < 1500 ? UpdateStatus::Checking
                                     : update_done_ ? UpdateStatus::UpToDate : UpdateStatus::Available;
                break;
            case 2:
                if (ms < 5000) {
                    st.state = UpdateStatus::Downloading;
                    st.percent = int(ms / 50);
                } else if (ms < 7000) {
                    st.state = UpdateStatus::Installing;
                } else if (ms < 9000) {
                    st.state = UpdateStatus::Restarting;
                } else {
                    st.state = UpdateStatus::Done;
                    st.message = "Now on 0.3.0";
                    update_done_ = true;
                    st.current = "0.3.0";
                }
                break;
        }
        if (st.state != UpdateStatus::Idle && st.state != UpdateStatus::Checking) st.latest = "0.3.0";
        return st;
    }
    void update_check() override {
        update_step_ = 1;
        update_clock_.start();
    }
    void update_install() override {
        update_step_ = 2;
        update_clock_.start();
    }

    CameraStatus camera() override { return {true, true, true}; }
    bool camera_open() override { return false; }
    bool camera_active() override { return false; }
    void camera_close() override {}

    void power_off() override { std::printf("[mock] power off\n"); }
    void reboot() override { std::printf("[mock] reboot\n"); }
    void beep(int, int) override {}

private:
    std::vector<BtDevice> bt_ = {
        {"F4:4E:FD:12:9A:0C", "Car Multimedia", true, false},
        {"AC:12:2F:5B:E0:41", "Headphones", true, false},
    };
    bool stick_on_ = false;
    int update_step_ = 0;
    bool update_done_ = false;
    PlayClock update_clock_;
    PlayClock stick_clock_;
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
    static constexpr int kCamW = 640, kCamH = 480;
    bool cam_on_ = false;
    PlayClock cam_clock_;
    long cam_drawn_ = 0;
    uint32_t cam_seq_ = 0;
    int cam_scene_ = 0;
    std::string cam_text_;
    std::vector<uint8_t> cam_;
    std::vector<TetherPhone> tether_phones_, tether_found_;
    std::string tether_job_, tether_mac_;
    PlayClock tether_clock_;
};

}  // namespace

void mock_set_hdmi(bool connected) { g_hdmi = connected; }
void mock_set_pose(int pose) {
    g_pose = pose;
    g_pose_ticks = 3;
}

std::unique_ptr<Hal> make_mock_hal() { return std::make_unique<MockHal>(); }

}  // namespace zs
