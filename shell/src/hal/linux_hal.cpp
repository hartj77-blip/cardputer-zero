// Real hardware on the Cardputer Zero, through standard Linux interfaces.
//
// Sources (see docs/HARDWARE.md):
//   battery    /sys/class/power_supply/bq27220-0/       (bq27xxx driver)
//   IMU        /sys/bus/iio/devices/iio:deviceN/        (bmi270, bmm150)
//   backlight  /sys/class/backlight/*/                  (pwm_bl_m5stack)
//   key LEDs   /sys/class/leds/*                        (names matched by substring)
//   Wi-Fi      nmcli (NetworkManager)
//   Bluetooth  bluetoothctl / rfkill
//   audio      wpctl (PipeWire), falls back to amixer
//   IR         ir-ctl / ir-keytable (rc-core, gpio-ir-tx / gpio-ir-recv)
//   camera     rpicam-vid (preview frames on a pipe), rpicam-still (photos)
//   tethering  /usr/bin/zeroos-tether (bluetoothctl + nmcli), run in the background
//   media      sox: `play` for mp3/flac/ogg/wav, `rec` for voice notes, `soxi` for
//              durations. One small process per stream; pause = SIGSTOP/SIGCONT.
//
// Everything degrades gracefully: a missing file means "unknown", never a crash.
// None of this is verified on hardware yet (Phase 1 bring-up).
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

#include "hal/hal.h"
#include "hal/meshtastic.h"
#include "hal/play_clock.h"
#include "hal/proc_stats.h"

namespace zs {
namespace {

std::string read_str(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    return s;
}

long read_long(const std::string& path, long dflt = 0) {
    std::string s = read_str(path);
    if (s.empty()) return dflt;
    return std::strtol(s.c_str(), nullptr, 10);
}

double read_double(const std::string& path, double dflt = 0) {
    std::string s = read_str(path);
    if (s.empty()) return dflt;
    return std::strtod(s.c_str(), nullptr);
}

bool write_str(const std::string& path, const std::string& v) {
    std::ofstream f(path);
    if (!f) return false;
    f << v;
    return bool(f);
}

std::string run(const std::string& cmd) {
    std::string out;
    FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    return out;
}

std::string shell_quote(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    return r + "'";
}

std::vector<std::string> list_dir(const std::string& path) {
    std::vector<std::string> v;
    if (DIR* d = opendir(path.c_str())) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] != '.') v.push_back(e->d_name);
        }
        closedir(d);
    }
    return v;
}

// Find an iio device directory whose "name" contains needle.
std::string find_iio(const std::string& needle) {
    const std::string base = "/sys/bus/iio/devices/";
    for (auto& n : list_dir(base)) {
        std::string dir = base + n + "/";
        if (read_str(dir + "name").find(needle) != std::string::npos) return dir;
    }
    return "";
}

// Start argv as a child in its own process group; returns pid or -1.
pid_t spawn(const std::vector<std::string>& argv) {
#ifdef __EMSCRIPTEN__
    return -1;
#else
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
        std::vector<char*> a;
        for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    return pid;
#endif
}

// The same, with the child's output on a pipe that we read without blocking.
pid_t spawn_pipe(const std::vector<std::string>& argv, int& fd) {
    fd = -1;
#ifdef __EMSCRIPTEN__
    return -1;
#else
    int p[2];
    if (pipe(p) < 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 2); }
        dup2(p[1], 1);
        close(p[0]);
        close(p[1]);
        std::vector<char*> a;
        for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) {
        close(p[0]);
        return -1;
    }
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fd = p[0];
    return pid;
#endif
}

// True if the child has exited (and reaps it).
bool exited(pid_t pid) {
#ifdef __EMSCRIPTEN__
    return true;
#else
    if (pid <= 0) return true;
    int st;
    return waitpid(pid, &st, WNOHANG) == pid;
#endif
}

void kill_child(pid_t pid, int sig) {
#ifndef __EMSCRIPTEN__
    if (pid > 0) {
        kill(pid, SIGCONT);  // a stopped process can't act on SIGTERM
        kill(pid, sig);
        int st;
        waitpid(pid, &st, 0);
    }
#endif
}

bool has_ext(const std::string& n, std::initializer_list<const char*> exts) {
    std::string lower;
    for (char c : n) lower += char(std::tolower((unsigned char)c));
    for (auto e : exts) {
        std::string x = e;
        if (lower.size() > x.size() && lower.compare(lower.size() - x.size(), x.size(), x) == 0) return true;
    }
    return false;
}

std::string stem(const std::string& name) {
    auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string find_power_supply() {
    const std::string base = "/sys/class/power_supply/";
    for (auto& n : list_dir(base)) {
        if (n.find("bq27") != std::string::npos) return base + n + "/";
    }
    return base + "bq27220-0/";
}

class LinuxHal : public Hal {
public:
    ~LinuxHal() override {
        camera_stop();
        tether_end();
        gps_stop();
        stop();
        record_stop();
    }

    LinuxHal() {
        bat_ = find_power_supply();
        accel_ = find_iio("bmi270");
        magn_ = find_iio("bmm150");
        for (auto& n : list_dir("/sys/class/backlight/")) {
            backlight_ = "/sys/class/backlight/" + n + "/";
            break;
        }
        // The mesh may still be running from before ZeroShell restarted.
        mesh_wanted_ = run("systemctl is-active meshtasticd.service 2>/dev/null").rfind("active", 0) == 0;
        // Expansion 5 V: LED-class supplies (ext_5v_out on M5's image; the
        // Grove port's, if the kernel exposes it the same way).
        for (auto& n : list_dir("/sys/class/leds/")) {
            std::string l = n;
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            if (l.find("5v") != std::string::npos) ext5v_.push_back("/sys/class/leds/" + n + "/");
        }
    }

    void poll() override {
        // Nobody has asked for the position for 5 s: let gpsd go idle.
        if (gps_fd_ >= 0 && time(nullptr) - gps_want_ > 5) gps_stop();
        mesh_pump();
        // Nobody has looked through the camera for 3 s: turn it off.
        if (cam_pid_ > 0 && time(nullptr) - cam_want_ > 3) camera_stop();
        tether_pump();
        // Battery and IMU are cheap sysfs reads; cache them per poll.
        bat_cache_ = read_battery();
        imu_cache_ = read_imu();
        if (++net_ticks_ % 20 == 0) net_cache_ = read_net();  // every ~2 s
    }

    BatteryInfo battery() override { return bat_cache_; }
    ImuSample imu() override { return imu_cache_; }
    NetStatus net() override {
        if (net_ticks_ < 20 && net_cache_.ip.empty()) net_cache_ = read_net();
        return net_cache_;
    }

    SystemInfo system() override {
        SystemInfo s;
        char host[64] = {0};
        gethostname(host, sizeof host - 1);
        s.hostname = host;
        utsname u{};
        if (uname(&u) == 0) s.kernel = u.release;
        s.model = accel_.empty() ? "CardputerZero Lite" : "CardputerZero";
        s.os_version = "ZeroOS " ZS_VERSION;
        s.cpu_temp_c = int(read_long("/sys/class/thermal/thermal_zone0/temp") / 1000);
#ifndef __EMSCRIPTEN__  // the web demo always uses MockHal
        struct sysinfo si{};
        if (sysinfo(&si) == 0) s.uptime_s = si.uptime;
#endif
        read_mem_cpu(s, cpu_prev_);
        // Scanning every process costs a little; refresh the list every few calls.
        if (top_.empty() || ++top_calls_ % 3 == 0) top_ = top_processes(5);
        s.top_procs = top_;
        struct statvfs vfs{};
        if (statvfs("/", &vfs) == 0) {
            double total = double(vfs.f_blocks) * vfs.f_frsize;
            double avail = double(vfs.f_bavail) * vfs.f_frsize;
            s.disk_total_gb10 = int(total / 1e8);
            s.disk_used_gb10 = int((total - avail) / 1e8);
        }
        return s;
    }

    std::vector<WifiNetwork> wifi_scan() override {
        std::vector<WifiNetwork> v;
        std::istringstream in(run("nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY dev wifi list --rescan auto"));
        std::string line;
        while (std::getline(in, line)) {
            // IN-USE:SSID:SIGNAL:SECURITY (SSID colons are escaped as \:)
            std::vector<std::string> f;
            std::string cur;
            for (size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '\\' && i + 1 < line.size()) { cur += line[++i]; continue; }
                if (line[i] == ':') { f.push_back(cur); cur.clear(); continue; }
                cur += line[i];
            }
            f.push_back(cur);
            if (f.size() < 4 || f[1].empty()) continue;
            WifiNetwork n;
            n.connected = f[0] == "*";
            n.ssid = f[1];
            n.signal = std::atoi(f[2].c_str());
            n.secure = !f[3].empty() && f[3] != "--";
            v.push_back(n);
        }
        return v;
    }

    bool wifi_connect(const std::string& ssid, const std::string& psk) override {
        std::string cmd = "nmcli dev wifi connect " + shell_quote(ssid);
        if (!psk.empty()) cmd += " password " + shell_quote(psk);
        return run(cmd + " && echo OK").find("OK") != std::string::npos;
    }
    void set_wifi_enabled(bool on) override { run(std::string("nmcli radio wifi ") + (on ? "on" : "off")); }
    void set_bt_enabled(bool on) override { run(std::string("rfkill ") + (on ? "unblock" : "block") + " bluetooth"); }

    int brightness() override {
        long max = read_long(backlight_ + "max_brightness", 0);
        if (max <= 0) return 100;
        return int(read_long(backlight_ + "brightness") * 100 / max);
    }
    void set_brightness(int pct) override {
        long max = read_long(backlight_ + "max_brightness", 0);
        if (max <= 0) return;
        if (pct < 5) pct = 5;  // never fully dark from the slider
        write_str(backlight_ + "brightness", std::to_string(max * pct / 100));
    }
    bool has_backlight() override { return !backlight_.empty(); }
    void set_screen_on(bool on) override {
        // bl_power: 0 = on, 4 = off. fb blank 4 = powerdown (panel sleep).
        if (!backlight_.empty()) write_str(backlight_ + "bl_power", on ? "0" : "4");
        const char* fb = std::getenv("ZS_FRAMEBUFFER");
        std::string name = fb ? fb : "/dev/fb0";
        name = name.substr(name.rfind('/') + 1);
        write_str("/sys/class/graphics/" + name + "/blank", on ? "0" : "4");
    }
    void set_cpu_mode(CpuMode mode) override {
        // All four cores share one policy. zeroshell.service makes these
        // files writable by the video group at start (see zeroos-power-perms).
        const std::string p = "/sys/devices/system/cpu/cpufreq/policy0/";
        std::string govs = read_str(p + "scaling_available_governors");
        auto has = [&](const char* g) { return (" " + govs + " ").find(std::string(" ") + g + " ") != std::string::npos; };
        const char* dynamic = has("schedutil") ? "schedutil" : has("ondemand") ? "ondemand" : "conservative";
        long lo = read_long(p + "cpuinfo_min_freq"), hi = read_long(p + "cpuinfo_max_freq");
        if (hi <= 0) return;
        std::string gov;
        long cap = hi;
        switch (mode) {
        case CpuMode::Saver: gov = dynamic; cap = pick_freq(p, lo + (hi - lo) * 7 / 10); break;
        case CpuMode::Balanced: gov = dynamic; break;
        case CpuMode::Performance: gov = "performance"; break;
        case CpuMode::ScreenOff: gov = has("powersave") ? "powersave" : dynamic; cap = lo; break;
        }
        // Lower the cap before switching to a faster governor, never above it.
        write_str(p + "scaling_max_freq", std::to_string(cap));
        write_str(p + "scaling_governor", gov);
    }

    int volume() override {
        std::string o = run("wpctl get-volume @DEFAULT_AUDIO_SINK@");  // "Volume: 0.60 [MUTED]"
        auto p = o.find("Volume:");
        if (p != std::string::npos) return int(std::strtod(o.c_str() + p + 7, nullptr) * 100 + 0.5);
        return 50;
    }
    void set_volume(int pct) override {
        run("wpctl set-volume @DEFAULT_AUDIO_SINK@ " + std::to_string(pct) + "%");
    }
    bool muted() override { return run("wpctl get-volume @DEFAULT_AUDIO_SINK@").find("MUTED") != std::string::npos; }
    void set_muted(bool m) override { run(std::string("wpctl set-mute @DEFAULT_AUDIO_SINK@ ") + (m ? "1" : "0")); }

    // --- media
    std::vector<Track> music_library() override {
        std::vector<Track> v;
        std::string root = home() + "/Music";
        auto add_dir = [&](const std::string& dir, const std::string& artist) {
            for (auto& n : list_dir(dir)) {
                std::string full = dir + "/" + n;
                if (has_ext(n, {".mp3", ".flac", ".ogg", ".wav", ".m4a", ".opus"}))
                    v.push_back({full, stem(n), artist, cached_duration(full)});
            }
        };
        add_dir(root, "Music");
        for (auto& n : list_dir(root)) {  // one level of album/artist folders
            struct stat st{};
            if (stat((root + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) add_dir(root + "/" + n, n);
        }
        std::sort(v.begin(), v.end(), [](const Track& a, const Track& b) {
            return a.artist == b.artist ? a.title < b.title : a.artist < b.artist;
        });
        return v;
    }

    bool play(const std::string& path) override {
        stop();
        play_pid_ = spawn({"play", "-q", path});
        if (play_pid_ <= 0) return false;
        pb_ = PlaybackState{};
        pb_.path = path;
        pb_.playing = true;
        int d = duration_of(path);
        pb_.dur_ms = d >= 0 ? d * 1000 : -1;
        clock_.start();
        return true;
    }
    bool play_stream(const std::string& url, const std::string& auth, int duration_s) override {
        stop();
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
        // mpv, not sox: it streams over HTTP and plays whatever the server sends.
        std::vector<std::string> argv = {"mpv", "--no-video", "--no-terminal", "--really-quiet", "--no-config",
                                         "--cache=yes", "--demuxer-max-bytes=16MiB"};
        if (!auth.empty()) argv.push_back("--http-header-fields=Authorization: " + auth);
        argv.push_back("--");
        argv.push_back(url);
        play_pid_ = spawn(argv);
        if (play_pid_ <= 0) return false;
        pb_ = PlaybackState{};
        pb_.path = url;
        pb_.playing = true;
        pb_.dur_ms = duration_s >= 0 ? duration_s * 1000 : -1;
        clock_.start();
        return true;
    }
    bool jellyfin_login(const std::string& server, const std::string& user, const std::string& password,
                        const std::string& device_id, JfSession& out, std::string& error) override {
        return jf::login(server, user, password, device_id, out, error);
    }
    bool jellyfin_list(const JfSession& s, JfView view, const std::string& arg, std::vector<JfItem>& out,
                       std::string& error) override {
        return jf::list(s, view, arg, out, error);
    }
    void set_paused(bool p) override {
        if (play_pid_ <= 0) return;
#ifndef __EMSCRIPTEN__
        kill(play_pid_, p ? SIGSTOP : SIGCONT);
#endif
        pb_.paused = p;
        pb_.playing = !p;
        if (p) clock_.pause(); else clock_.resume();
    }
    void stop() override {
        kill_child(play_pid_, SIGTERM);
        play_pid_ = -1;
        pb_ = PlaybackState{};
    }
    PlaybackState playback() override {
        if (play_pid_ > 0) {
            pb_.pos_ms = int(clock_.ms());
            if (!pb_.paused && exited(play_pid_)) {
                play_pid_ = -1;
                pb_ = PlaybackState{};
                pb_.finished = true;
            }
        }
        return pb_;
    }

    std::vector<Track> recordings() override {
        std::vector<Track> v;
        std::string dir = home() + "/Recordings";
        for (auto& n : list_dir(dir)) {
            if (!has_ext(n, {".wav"})) continue;
            std::string full = dir + "/" + n;
            struct stat st{};
            stat(full.c_str(), &st);
            char when[32] = "";
            tm lt{};
            localtime_r(&st.st_mtime, &lt);
            strftime(when, sizeof when, "%d %b %H:%M", &lt);
            // 16 kHz mono 16-bit: 32000 bytes per second after the 44-byte header.
            v.push_back({full, n, when, int(st.st_size > 44 ? (st.st_size - 44) / 32000 : 0)});
        }
        std::sort(v.begin(), v.end(), [](const Track& a, const Track& b) { return a.path > b.path; });
        return v;
    }
    bool record_start() override {
        if (rec_pid_ > 0) return false;
        stop();
        std::string dir = home() + "/Recordings";
        mkdir(dir.c_str(), 0755);
        char name[64];
        time_t now = time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        strftime(name, sizeof name, "/note-%Y%m%d-%H%M%S.wav", &lt);
        rec_ = RecordState{};
        rec_.path = dir + name;
        rec_pid_ = spawn({"rec", "-q", "-c", "1", "-r", "16000", "-b", "16", rec_.path});
        if (rec_pid_ <= 0) return false;
        rec_.recording = true;
        rec_clock_.start();
        return true;
    }
    void record_stop() override {
        kill_child(rec_pid_, SIGINT);  // SIGINT lets sox finish the WAV header
        rec_pid_ = -1;
        rec_ = RecordState{};
    }
    RecordState record_state() override {
        if (rec_pid_ > 0 && exited(rec_pid_)) {  // rec died (no mic?)
            rec_pid_ = -1;
            rec_ = RecordState{};
        }
        if (rec_.recording) rec_.elapsed_ms = int(rec_clock_.ms());
        return rec_;
    }
    bool delete_recording(const std::string& path) override {
        if (path.rfind(home() + "/Recordings/", 0) != 0) return false;  // only our own files
        if (pb_.path == path) stop();
        return std::remove(path.c_str()) == 0;
    }

    void set_led(Led led, bool on) override {
        static const char* keys[] = {"shift", "fn", "sym", "key_led1", "key_led2", "key_led3"};
        const char* key = keys[int(led)];
        for (auto& n : list_dir("/sys/class/leds/")) {
            std::string lower;
            for (char c : n) lower += char(std::tolower((unsigned char)c));
            if (lower.find(key) != std::string::npos) {
                write_str("/sys/class/leds/" + n + "/brightness", on ? "1" : "0");
            }
        }
    }

    bool ir_send(const IrCode& c) override {
        char sc[32];
        std::snprintf(sc, sizeof sc, "0x%x", c.scancode);
        std::string out = run("ir-ctl -S " + shell_quote(c.protocol) + ":" + sc + " && echo OK");
        return out.find("OK") != std::string::npos;
    }

    bool ir_learn(IrCode& out, int timeout_ms) override {
        // ir-keytable -t prints lines like:
        //   lirc protocol(nec): scancode = 0x40bf12
        int secs = timeout_ms / 1000 > 0 ? timeout_ms / 1000 : 1;
        std::string o = run("timeout " + std::to_string(secs) +
                            " ir-keytable -c -p all -t");
        auto p = o.find("protocol(");
        auto s = o.find("scancode = ");
        if (p == std::string::npos || s == std::string::npos) return false;
        auto e = o.find(')', p);
        out.protocol = o.substr(p + 9, e - p - 9);
        out.scancode = unsigned(std::strtoul(o.c_str() + s + 11, nullptr, 16));
        return true;
    }

    void power_off() override { run("systemctl poweroff"); }
    void reboot() override { run("systemctl reboot"); }

    void beep(int freq_hz, int ms) override {
        // Fire-and-forget; uses sox's play if present.
        std::string cmd = "play -nq synth " + std::to_string(ms / 1000.0) + " sine " +
                          std::to_string(freq_hz) + " vol 0.2 >/dev/null 2>&1 &";
        if (std::system(cmd.c_str())) {}
    }

private:
    static std::string home() {
        const char* h = std::getenv("HOME");
        return h ? h : "/home/pi";
    }

    int duration_of(const std::string& path) {
        int d = cached_duration(path);
        if (d >= 0) return d;
        std::string out = run("soxi -D " + shell_quote(path));
        if (out.empty()) return -1;
        d = int(std::strtod(out.c_str(), nullptr) + 0.5);
        durations_.push_back({path, d});
        return d;
    }
    int cached_duration(const std::string& path) {
        for (auto& p : durations_)
            if (p.first == path) return p.second;
        return -1;
    }

    long long cpu_prev_[2] = {0, 0};
    std::vector<ProcMem> top_;
    int top_calls_ = 0;
    pid_t play_pid_ = -1, rec_pid_ = -1;
    PlaybackState pb_;
    RecordState rec_;
    PlayClock clock_, rec_clock_;
    std::vector<std::pair<std::string, int>> durations_;

    DisplayStatus display() override {
        DisplayStatus d;
        // The vc4 card's HDMI connector, e.g. /sys/class/drm/card1-HDMI-A-1/.
        for (auto& n : list_dir("/sys/class/drm/")) {
            if (n.find("-HDMI-A-") == std::string::npos) continue;
            std::string base = "/sys/class/drm/" + n + "/";
            if (read_str(base + "status") == "connected") {
                d.hdmi_connected = true;
                std::istringstream modes(read_str(base + "modes"));
                std::getline(modes, d.hdmi_mode);
                break;
            }
        }
        // systemctl is a fork; ask every ~2 s at most.
        if (++dock_calls_ % 20 == 1) dock_cache_ = run("systemctl is-active zeroos-dock.service").rfind("active", 0) == 0;
        d.dock_running = dock_cache_;
        return d;
    }
    void dock_start() override { run("zeroos-dock start"); dock_cache_ = true; dock_calls_ = 0; }
    void dock_stop() override { run("zeroos-dock stop"); dock_cache_ = false; dock_calls_ = 0; }
    bool dock_open(const std::string& what, const std::string& path) override {
        if (!dock_cache_) return false;
        std::ostringstream cmd;
        cmd << "zeroos-dock open " << what;
        if (!path.empty()) cmd << ' ' << shell_quote(path);
        run(cmd.str());
        return true;
    }

    // --- Tailscale. Every call forks the tailscale CLI, so cache for 3 s.
    TsStatus tailscale() override {
        time_t now = time(nullptr);
        if (now - ts_at_ < 3 && ts_at_) return ts_cache_;
        ts_at_ = now;
        TsStatus t;
        t.installed = access("/usr/bin/tailscale", X_OK) == 0;
        if (!t.installed) return ts_cache_ = t;
        t.running = run("systemctl is-active tailscaled.service").rfind("active", 0) == 0;
        if (!t.running) {
            t.state = "Stopped";
            return ts_cache_ = t;
        }
        std::string js = run("tailscale status --json --peers=false 2>/dev/null");
        t.state = json_str(js, "BackendState");
        t.auth_url = json_str(js, "AuthURL");
        t.tailnet = json_str(js, "MagicDNSSuffix");
        if (t.state.empty()) t.state = "Starting";
        if (t.state == "Running") {
            // "100.x.y.z  name  user@  os  -|offline|active; direct ..." (first line is us)
            std::istringstream in(run("tailscale status 2>/dev/null"));
            bool first = true;
            for (std::string line; std::getline(in, line);) {
                std::istringstream f(line);
                std::string ip, name, user, os;
                if (!(f >> ip >> name >> user >> os) || ip.empty() || ip[0] == '#') continue;
                std::string rest;
                std::getline(f, rest);
                if (first) {
                    t.self_ip = ip;
                    t.self_name = name;
                    first = false;
                    continue;
                }
                t.peers.push_back({name, ip, os, rest.find("offline") == std::string::npos});
            }
            std::stable_sort(t.peers.begin(), t.peers.end(),
                             [](const TsPeer& a, const TsPeer& b) { return a.online > b.online; });
        }
        return ts_cache_ = t;
    }
    void tailscale_start() override {
        // tailscaled's ZeroOS drop-in makes pi the operator, so "up" needs no
        // root. It waits for sign-in; the link shows up in status (AuthURL).
        run("systemctl start tailscaled.service");
        run("mkdir -p \"$HOME/.cache/zeroos\"; setsid tailscale up --timeout=10m "
            ">\"$HOME/.cache/zeroos/tailscale-up.log\" 2>&1 </dev/null &");
        ts_at_ = 0;
    }
    void tailscale_stop() override {
        run("tailscale down; systemctl stop tailscaled.service");
        ts_at_ = 0;
    }
    void tailscale_logout() override {
        run("tailscale logout");
        ts_at_ = 0;
    }
    bool taildrop_send(const std::string& path, const std::string& peer) override {
        std::ostringstream cmd;
        cmd << "tailscale file cp " << shell_quote(path) << ' ' << shell_quote(peer + ":") << " && echo ok";
        return run(cmd.str()).find("ok") != std::string::npos;
    }

    // --- Sync: syncthing@pi.service, driven through /usr/bin/zeroos-sync
    // (Syncthing's REST API). The helper is Python, so cache for 4 s.
    SyncStatus sync() override {
        time_t now = time(nullptr);
        if (now - sync_at_ < 4 && sync_at_) return sync_cache_;
        sync_at_ = now;
        SyncStatus s;
        s.installed = access("/usr/bin/syncthing", X_OK) == 0;
        if (!s.installed) return sync_cache_ = s;
        s.running = sync_running();
        if (!s.running) return sync_cache_ = s;
        std::istringstream in(run("zeroos-sync status 2>/dev/null"));
        for (std::string line; std::getline(in, line);) {
            std::vector<std::string> f;
            std::istringstream ls(line);
            for (std::string x; std::getline(ls, x, '\t');) f.push_back(x);
            if (f.empty()) continue;
            auto at = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
            if (f[0] == "id") {
                s.device_id = at(1);
                s.ready = true;
            } else if (f[0] == "folder") {
                SyncFolder d;
                d.id = at(1), d.label = at(2), d.path = at(3), d.state = at(4);
                d.percent = std::atoi(at(5).c_str());
                d.need_files = std::atoi(at(6).c_str());
                d.error = at(7);
                s.folders.push_back(d);
            } else if (f[0] == "device") {
                s.devices.push_back({at(1), at(2), at(3) == "1", std::atoi(at(4).c_str())});
            } else if (f[0] == "request-device") {
                s.requests.push_back({false, at(1), at(2), "", ""});
            } else if (f[0] == "request-folder") {
                s.requests.push_back({true, at(3), at(4), at(1), at(2)});
            }
        }
        return sync_cache_ = s;
    }
    bool sync_running() override {
        time_t now = time(nullptr);
        if (now - sync_run_at_ >= 4 || !sync_run_at_) {
            sync_run_at_ = now;
            std::string st = run("systemctl is-active syncthing@${USER:-pi}.service");
            sync_running_ = st.rfind("active", 0) == 0 || st.rfind("activating", 0) == 0;
        }
        return sync_running_;
    }
    void sync_start() override {
        // The unit's ExecStartPost runs "zeroos-sync setup" (folders, options).
        run("systemctl --no-block start syncthing@${USER:-pi}.service");
        sync_at_ = sync_run_at_ = 0;
    }
    void sync_stop() override {
        run("systemctl --no-block stop syncthing@${USER:-pi}.service");
        sync_at_ = sync_run_at_ = 0;
    }
    bool sync_answer(const SyncRequest& r, bool accept) override {
        std::ostringstream cmd;
        if (r.is_folder)
            cmd << "zeroos-sync " << (accept ? "accept-folder " : "ignore-folder ") << shell_quote(r.folder_id) << ' '
                << shell_quote(r.folder_label) << ' ' << shell_quote(r.device_id);
        else
            cmd << "zeroos-sync " << (accept ? "add-device " : "ignore-device ") << shell_quote(r.device_id) << ' '
                << shell_quote(r.device_name);
        return sync_run(cmd.str());
    }
    bool sync_add_device(const std::string& id, const std::string& name) override {
        std::ostringstream cmd;
        cmd << "zeroos-sync add-device " << shell_quote(id) << ' ' << shell_quote(name);
        return sync_run(cmd.str());
    }
    bool sync_remove_device(const std::string& id) override {
        std::ostringstream cmd;
        cmd << "zeroos-sync remove-device " << shell_quote(id);
        return sync_run(cmd.str());
    }
    void sync_rescan() override { sync_run("zeroos-sync rescan"); }
    bool sync_run(const std::string& cmd) {
        sync_at_ = 0;
        return run(cmd + " 2>/dev/null").find("ok") != std::string::npos;
    }

    // --- Expansion 5 V
    bool ext_power() override { return !ext5v_.empty() && read_long(ext5v_[0] + "brightness") > 0; }
    void set_ext_power(bool on) override {
        for (auto& d : ext5v_) {
            long max = read_long(d + "max_brightness", 1);
            write_str(d + "brightness", on ? std::to_string(max > 0 ? max : 1) : "0");
        }
    }

    // --- GPS through gpsd (socket-activated: it starts when we connect and
    // stops reading the receiver when we stop watching).
    GpsFix gps() override {
        time_t now = time(nullptr);
        gps_want_ = now;
        if (gps_fd_ < 0 && now - gps_try_ >= 3) {
            gps_try_ = now;
            gps_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(2947);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (gps_fd_ >= 0 && connect(gps_fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 &&
                errno != EINPROGRESS) {
                close(gps_fd_);
                gps_fd_ = -1;
            }
            gps_sent_ = false;
            gps_buf_.clear();
        }
        if (gps_fd_ < 0) return gps_;
        if (!gps_sent_) {
            static const char kWatch[] = "?WATCH={\"enable\":true,\"json\":true};\n";
            if (send(gps_fd_, kWatch, sizeof kWatch - 1, MSG_NOSIGNAL) > 0) gps_sent_ = true;
        }
        char buf[4096];
        for (;;) {
            ssize_t n = recv(gps_fd_, buf, sizeof buf, MSG_DONTWAIT);
            if (n > 0) {
                gps_buf_.append(buf, size_t(n));
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOTCONN)) {
                close(gps_fd_);   // gpsd went away; try again in 3 s
                gps_fd_ = -1;
                gps_ = GpsFix{};
            }
            break;
        }
        size_t nl;
        while ((nl = gps_buf_.find('\n')) != std::string::npos) {
            std::string line = gps_buf_.substr(0, nl);
            gps_buf_.erase(0, nl + 1);
            gps_line(line);
        }
        if (gps_buf_.size() > 65536) gps_buf_.clear();
        return gps_;
    }
    void gps_stop() override {
        if (gps_fd_ < 0) return;
        static const char kOff[] = "?WATCH={\"enable\":false};\n";
        if (send(gps_fd_, kOff, sizeof kOff - 1, MSG_NOSIGNAL | MSG_DONTWAIT) < 0) {}
        close(gps_fd_);
        gps_fd_ = -1;
    }
    void gps_line(const std::string& js) {
        std::string cls = json_str(js, "class");
        if (cls == "DEVICES" || cls == "DEVICE") {
            gps_.device = js.find("\"path\"") != std::string::npos;
        } else if (cls == "TPV") {
            gps_.device = true;
            gps_.mode = int(json_num(js, "mode", 0));
            if (gps_.mode >= 2) {
                gps_.lat = json_num(js, "lat", gps_.lat);
                gps_.lon = json_num(js, "lon", gps_.lon);
                gps_.speed_mps = json_num(js, "speed", 0);
                gps_.track_deg = json_num(js, "track", gps_.track_deg);
                gps_.error_m = json_num(js, "eph", 0);
            }
            if (gps_.mode >= 3) gps_.alt_m = json_num(js, "altMSL", json_num(js, "alt", gps_.alt_m));
            std::string t = json_str(js, "time");
            if (!t.empty()) gps_.time = t;
        } else if (cls == "SKY") {
            int seen = 0, used = 0;
            for (size_t i = js.find("\"PRN\""); i != std::string::npos; i = js.find("\"PRN\"", i + 1)) {
                ++seen;
                size_t end = js.find('}', i);
                size_t u = js.find("\"used\":true", i);
                if (u != std::string::npos && u < end) ++used;
            }
            gps_.sats_seen = seen ? seen : int(json_num(js, "nSat", gps_.sats_seen));
            gps_.sats_used = seen ? used : int(json_num(js, "uSat", gps_.sats_used));
        }
    }
    static double json_num(const std::string& js, const std::string& key, double def) {
        auto k = js.find("\"" + key + "\":");
        if (k == std::string::npos) return def;
        const char* p = js.c_str() + k + key.size() + 3;
        char* end = nullptr;
        double v = std::strtod(p, &end);
        return end == p ? def : v;
    }

    // --- Camera. The preview is rpicam-vid writing raw 640x480 YUV frames to
    // a pipe, ten a second; we keep the newest whole one. (640 is a multiple
    // of 64, so the frames have no padding between rows.) To confirm on
    // hardware: which way up the picture is.
    static constexpr int kCamW = 640, kCamH = 480;
    static constexpr size_t kCamBytes = size_t(kCamW) * kCamH * 3 / 2;
    void camera_start() override {
        if (cam_pid_ > 0) return;
        cam_pid_ = spawn_pipe({"rpicam-vid", "--nopreview", "--timeout", "0", "--codec", "yuv420", "--width",
                               std::to_string(kCamW), "--height", std::to_string(kCamH), "--framerate", "10",
                               "--output", "-"},
                              cam_fd_);
        cam_state_ = cam_pid_ > 0 ? CamState::Starting : CamState::NoCamera;
        if (cam_pid_ <= 0) return;
#ifdef F_SETPIPE_SZ
        if (fcntl(cam_fd_, F_SETPIPE_SZ, 1 << 20) < 0) {}   // room for two frames
#endif
        cam_back_.resize(kCamBytes);
        cam_front_.resize(kCamBytes);
        cam_fill_ = 0;
        cam_seq_ = 0;
        cam_want_ = time(nullptr);
    }
    void camera_stop() override {
        kill_child(cam_pid_, SIGTERM);
        cam_pid_ = -1;
        if (cam_fd_ >= 0) close(cam_fd_);
        cam_fd_ = -1;
        cam_state_ = CamState::Off;
    }
    CamState camera_state() override { return cam_state_; }
    bool camera_frame(CameraFrame& out) override {
        cam_want_ = time(nullptr);
        if (cam_pid_ <= 0 || cam_fd_ < 0) return false;
        for (;;) {
            ssize_t n = read(cam_fd_, cam_back_.data() + cam_fill_, kCamBytes - cam_fill_);
            if (n > 0) {
                cam_fill_ += size_t(n);
                if (cam_fill_ == kCamBytes) {
                    cam_front_.swap(cam_back_);
                    cam_fill_ = 0;
                    ++cam_seq_;
                    cam_state_ = CamState::Running;
                }
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                // rpicam-vid gave up: no camera (the Lite model), or it's in use.
                camera_stop();
                cam_state_ = CamState::NoCamera;
                return false;
            }
            break;
        }
        if (!cam_seq_) return false;
        out.w = kCamW;
        out.h = kCamH;
        out.y = cam_front_.data();
        out.u = out.y + kCamW * kCamH;
        out.v = out.u + kCamW * kCamH / 4;
        out.seq = cam_seq_;
        return true;
    }
    std::string camera_photo() override {
        camera_stop();   // one program can hold the camera at a time
        std::string dir = home() + "/Pictures";
        mkdir(dir.c_str(), 0755);
        char name[64];
        time_t now = time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        strftime(name, sizeof name, "/photo-%Y%m%d-%H%M%S.jpg", &lt);
        std::string path = dir + name;
        // 1640x1232 is the sensor's binned mode: quick, and light on memory.
        // The second lets exposure and white balance settle.
        run("rpicam-still --nopreview --timeout 1000 --width 1640 --height 1232 --quality 90 --output " +
            shell_quote(path));
        struct stat st{};
        return stat(path.c_str(), &st) == 0 && st.st_size > 0 ? path : "";
    }

    // --- Tethering. zeroos-tether prints tab-separated lines and always ends
    // with the current state ("bt", "usb", "phone" lines, then "end"). It can
    // take many seconds (a search, pairing), so it runs in the background
    // and we read what it says from poll().
    TetherStatus tether() override {
        tether_want_ = time(nullptr);
        tether_pump();
        return tether_;
    }
    void tether_search() override { tether_run("searching", {"search"}, ""); }
    void tether_pair(const std::string& mac) override { tether_run("pairing", {"pair", mac}, mac); }
    void tether_connect(const std::string& mac) override { tether_run("connecting", {"connect", mac}, mac); }
    void tether_disconnect(const std::string& mac) override { tether_run("", {"disconnect", mac}, mac); }
    void tether_forget(const std::string& mac) override { tether_run("", {"forget", mac}, mac); }

    void tether_run(const std::string& busy, std::vector<std::string> args, const std::string& mac) {
        tether_end();
        args.insert(args.begin(), "zeroos-tether");
        tether_pid_ = spawn_pipe(args, tether_fd_);
        tether_status_job_ = args[1] == "status";
        if (tether_status_job_) return;
        tether_.busy = busy;
        tether_.busy_mac = mac;
        tether_.passkey.clear();
        tether_.error.clear();
        if (tether_pid_ <= 0) tether_.busy.clear();
    }
    void tether_end() {
        if (tether_pid_ > 0) kill_child(tether_pid_, SIGTERM);
        tether_pid_ = -1;
        if (tether_fd_ >= 0) close(tether_fd_);
        tether_fd_ = -1;
        tether_buf_.clear();
        tether_next_ = TetherStatus{};
        tether_found_seen_ = false;
    }
    void tether_pump() {
        time_t now = time(nullptr);
        if (tether_pid_ <= 0) {
            // Refresh every 3 s while someone is looking.
            if (now - tether_want_ <= 5 && now - tether_at_ >= 3) tether_run("", {"status"}, "");
            return;
        }
        char buf[1024];
        bool eof = false;
        for (;;) {
            ssize_t n = read(tether_fd_, buf, sizeof buf);
            if (n > 0) {
                tether_buf_.append(buf, size_t(n));
                continue;
            }
            eof = n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR);
            break;
        }
        size_t nl;
        while ((nl = tether_buf_.find('\n')) != std::string::npos) {
            std::vector<std::string> f;
            std::istringstream ls(tether_buf_.substr(0, nl));
            tether_buf_.erase(0, nl + 1);
            for (std::string x; std::getline(ls, x, '\t');) f.push_back(x);
            if (!f.empty()) tether_line(f);
        }
        if (eof) {
            bool was_status = tether_status_job_;
            tether_end();
            tether_at_ = now;
            if (!was_status) {
                tether_.busy.clear();
                tether_.busy_mac.clear();
                tether_.passkey.clear();
                net_ticks_ = 19;   // the status bar's network icons: look again now
            }
        }
    }
    void tether_line(const std::vector<std::string>& f) {
        auto at = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
        TetherStatus& n = tether_next_;
        if (f[0] == "bt") n.bt_on = at(1) == "on";
        else if (f[0] == "usb") {
            n.usb = true;
            n.usb_name = at(2);
            n.usb_ip = at(3);
        } else if (f[0] == "phone") n.phones.push_back({at(1), at(2), at(3) == "1", at(4) == "1"});
        else if (f[0] == "found") {
            n.found.push_back({at(1), at(2), false, false});
            tether_found_seen_ = true;
        } else if (f[0] == "searched") tether_found_seen_ = true;   // even if it found nothing
        else if (f[0] == "passkey") tether_.passkey = at(1);
        else if (f[0] == "stage") tether_.busy = at(1);
        else if (f[0] == "error") tether_.error = at(1);
        else if (f[0] == "end") {
            // Keep what belongs to the action in progress; take the rest.
            n.busy = tether_.busy;
            n.busy_mac = tether_.busy_mac;
            n.passkey = tether_.passkey;
            n.error = tether_.error;
            if (!tether_found_seen_) n.found = tether_.found;
            for (auto& p : n.phones)   // a phone we just paired is no longer "found"
                n.found.erase(std::remove_if(n.found.begin(), n.found.end(),
                                             [&](const TetherPhone& x) { return x.mac == p.mac; }),
                              n.found.end());
            tether_ = n;
            tether_next_ = TetherStatus{};
            tether_found_seen_ = false;
        }
    }

    // --- LoRa mesh: meshtasticd.service (config in /etc/meshtasticd/config.d).
    MeshStatus mesh() override {
        time_t now = time(nullptr);
        if (now - mesh_at_ < 3 && mesh_at_) return mesh_cache_;
        mesh_at_ = now;
        MeshStatus m;
        m.installed = access("/usr/bin/meshtasticd", X_OK) == 0 || access("/usr/sbin/meshtasticd", X_OK) == 0;
        std::string st = run("systemctl is-active meshtasticd.service");
        m.running = st.rfind("active", 0) == 0 || st.rfind("activating", 0) == 0;
        if (m.running) {
            // Without a working radio the daemon falls back to a simulated
            // one and says so in its log (pi is in the adm group). Look three
            // times, 15 s apart, after it starts.
            if (mesh_log_checks_ < 3 && now - mesh_log_at_ >= 15) {
                mesh_log_at_ = now;
                ++mesh_log_checks_;
                mesh_no_radio_ = !run("journalctl -u meshtasticd -b --no-pager -n 300 2>/dev/null | "
                                      "grep -m1 -i -E 'simradio|sim radio|radio init failed'").empty();
            }
            m.no_radio = mesh_no_radio_;
            NetStatus n = read_net();
            std::string ip = !n.tailscale_ip.empty() && n.ip.empty() ? n.tailscale_ip : n.ip;
            if (!ip.empty()) {
                m.web_url = "https://" + ip + ":9443";
                m.api = ip;
            }
        }
        m.connected = mesh_connected_;
        m.my_name = mesh_me_ ? node_name(mesh_me_) : "";
        m.nodes = int(mesh_names_.size()) - (mesh_names_.count(mesh_me_) ? 1 : 0);
        return mesh_cache_ = m;
    }
    void mesh_start() override {
        mesh_wanted_ = true;
        mesh_no_radio_ = false;
        mesh_log_at_ = time(nullptr);   // give it 15 s before reading its log
        mesh_log_checks_ = 0;
        set_ext_power(true);
        run("systemctl --no-block start meshtasticd.service");
        mesh_at_ = 0;
    }
    void mesh_stop() override {
        run("systemctl --no-block stop meshtasticd.service");
        mesh_at_ = 0;
        mesh_close();
    }

    // Chat: a client connection to meshtasticd's API while it runs.
    void mesh_pump() {
        time_t now = time(nullptr);
        if (!mesh_wanted_) return;   // never started this session: nothing to ask
        if (now - mesh_at_ >= 3) mesh();   // refresh "running" every few seconds
        if (!mesh_cache_.running) {
            mesh_close();
            return;
        }
        if (mesh_fd_ < 0) {
            if (now - mesh_try_ < 5) return;
            mesh_try_ = now;
            mesh_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(4403);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (mesh_fd_ >= 0 && connect(mesh_fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 &&
                errno != EINPROGRESS) {
                mesh_close();
                return;
            }
            mesh_hello_ = false;
            mesh_in_.clear();
        }
        if (!mesh_hello_ && mesh_write(mt::want_config(uint32_t(now)))) {
            mesh_hello_ = true;
            mesh_beat_ = now;
        }
        if (mesh_hello_ && now - mesh_beat_ > 240 && mesh_write(mt::heartbeat())) mesh_beat_ = now;
        char buf[2048];
        for (;;) {
            ssize_t n = recv(mesh_fd_, buf, sizeof buf, MSG_DONTWAIT);
            if (n > 0) {
                mesh_in_.feed(buf, size_t(n));
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOTCONN)) {
                mesh_close();
                return;
            }
            break;
        }
        std::string payload;
        while (mesh_in_.next(payload)) {
            mt::Event e = mt::decode(payload);
            switch (e.kind) {
            case mt::Event::MyNode: mesh_me_ = e.num; break;
            case mt::Event::Node:
                mesh_names_[e.num] = e.short_name.empty() ? e.long_name : e.short_name;
                break;
            case mt::Event::ConfigDone: mesh_connected_ = true; break;
            case mt::Event::Text: {
                if (e.from == mesh_me_) break;
                MeshMessage m;
                m.from = node_name(e.from);
                m.text = e.text;
                m.direct = e.to != mt::kBroadcast;
                m.time = e.rx_time ? long(e.rx_time) : long(now);
                mesh_log_.push_back(m);
                if (mesh_log_.size() > 200) mesh_log_.erase(mesh_log_.begin());
                ++mesh_seq_;
                break;
            }
            default: break;
            }
        }
    }
    std::string node_name(uint32_t num) {
        auto it = mesh_names_.find(num);
        if (it != mesh_names_.end() && !it->second.empty()) return it->second;
        char b[16];
        std::snprintf(b, sizeof b, "!%08x", num);
        return b;
    }
    bool mesh_write(const std::string& frame) {
        if (mesh_fd_ < 0) return false;
        return send(mesh_fd_, frame.data(), frame.size(), MSG_NOSIGNAL | MSG_DONTWAIT) == ssize_t(frame.size());
    }
    void mesh_close() {
        if (mesh_fd_ >= 0) close(mesh_fd_);
        mesh_fd_ = -1;
        mesh_hello_ = mesh_connected_ = false;
    }
    std::vector<MeshMessage> mesh_messages() override { return mesh_log_; }
    bool mesh_send(const std::string& text) override {
        if (!mesh_connected_ || text.empty()) return false;
        if (!mesh_write(mt::text_message(text.substr(0, 200), mt::kBroadcast, 0, ++mesh_packet_id_))) return false;
        MeshMessage m;
        m.text = text.substr(0, 200);
        m.mine = true;
        m.time = long(time(nullptr));
        mesh_log_.push_back(m);
        return true;
    }
    int mesh_seq() override { return mesh_seq_; }

    // --- Terminal: zeroos-terminal.service runs tmux on tty3 and switches to it.
    bool terminal_open(const std::string& command) override {
        std::string dir = std::string(std::getenv("HOME") ? std::getenv("HOME") : "/home/pi") + "/.cache/zeroos";
        run("mkdir -p '" + dir + "'");
        write_str(dir + "/terminal-cmd", command);
        run("systemctl start zeroos-terminal.service");
        term_at_ = 0;
        return true;
    }
    bool terminal_active() override {
        time_t now = time(nullptr);
        if (now != term_at_) {
            term_at_ = now;
            std::string st = run("systemctl is-active zeroos-terminal.service");
            term_cache_ = st.rfind("active", 0) == 0 || st.rfind("activating", 0) == 0;
        }
        return term_cache_;
    }

    static std::string json_str(const std::string& js, const std::string& key) {
        // Enough for tailscale's flat top-level string fields: "Key": "value"
        auto k = js.find("\"" + key + "\"");
        if (k == std::string::npos) return "";
        auto q1 = js.find('"', js.find(':', k) + 1);
        if (q1 == std::string::npos) return "";
        auto q2 = js.find('"', q1 + 1);
        if (q2 == std::string::npos || js.find('\n', k) < q1) return "";
        return js.substr(q1 + 1, q2 - q1 - 1);
    }

    // The highest listed frequency at or below target (or the lowest one).
    static long pick_freq(const std::string& policy, long target) {
        std::istringstream in(read_str(policy + "scaling_available_frequencies"));
        long f, best = 0, lowest = 0;
        while (in >> f) {
            if (!lowest || f < lowest) lowest = f;
            if (f <= target && f > best) best = f;
        }
        return best ? best : lowest ? lowest : target;
    }

    BatteryInfo read_battery() {
        BatteryInfo b;
        b.percent = int(read_long(bat_ + "capacity", -1));
        std::string st = read_str(bat_ + "status");
        b.charging = st == "Charging";
        b.external_power = st == "Charging" || st == "Full" || st == "Not charging";
        b.voltage_mv = int(read_long(bat_ + "voltage_now") / 1000);
        b.current_ma = int(read_long(bat_ + "current_now") / 1000);
        b.temp_c10 = int(read_long(bat_ + "temp"));
        long secs = read_long(bat_ + (b.charging ? "time_to_full_now" : "time_to_empty_now"), -60);
        b.minutes_left = secs >= 0 ? int(secs / 60) : -1;
        b.cycle_count = int(read_long(bat_ + "cycle_count", -1));
        long full = read_long(bat_ + "charge_full", 0), design = read_long(bat_ + "charge_full_design", 0);
        b.health_percent = design > 0 ? int(full * 100 / design) : -1;
        long uv = read_long(bat_ + "voltage_now"), ua = read_long(bat_ + "current_now");
        b.power_mw = int(std::llabs((long long)uv * ua / 1000000000LL));
        long now = read_long(bat_ + "charge_now", -1000);
        b.charge_now_mah = now >= 0 ? int(now / 1000) : -1;
        b.charge_full_mah = full > 0 ? int(full / 1000) : -1;
        return b;
    }

    ImuSample read_imu() {
        ImuSample s;
        if (accel_.empty()) return s;
        s.available = true;
        double as = read_double(accel_ + "in_accel_scale", 1.0);
        s.ax = float(read_long(accel_ + "in_accel_x_raw") * as);
        s.ay = float(read_long(accel_ + "in_accel_y_raw") * as);
        s.az = float(read_long(accel_ + "in_accel_z_raw") * as);
        double gs = read_double(accel_ + "in_anglvel_scale", 1.0) * 57.2958;
        s.gx = float(read_long(accel_ + "in_anglvel_x_raw") * gs);
        s.gy = float(read_long(accel_ + "in_anglvel_y_raw") * gs);
        s.gz = float(read_long(accel_ + "in_anglvel_z_raw") * gs);
        s.pitch_deg = float(std::atan2(-s.ax, std::sqrt(s.ay * s.ay + s.az * s.az)) * 57.2958);
        s.roll_deg = float(std::atan2(s.ay, s.az) * 57.2958);
        if (!magn_.empty()) {
            // Tilt-compensated heading. Axis orientation to be confirmed on hardware.
            double mx = read_long(magn_ + "in_magn_x_raw"), my = read_long(magn_ + "in_magn_y_raw"),
                   mz = read_long(magn_ + "in_magn_z_raw");
            double p = s.pitch_deg / 57.2958, r = s.roll_deg / 57.2958;
            double xh = mx * std::cos(p) + mz * std::sin(p);
            double yh = mx * std::sin(r) * std::sin(p) + my * std::cos(r) - mz * std::sin(r) * std::cos(p);
            double h = std::atan2(yh, xh) * 57.2958;
            s.heading_deg = float(h < 0 ? h + 360 : h);
        }
        return s;
    }

    NetStatus read_net() {
        NetStatus n;
        n.wifi_enabled = run("nmcli radio wifi").find("enabled") != std::string::npos;
        std::istringstream in(run("nmcli -t -f ACTIVE,SSID,SIGNAL dev wifi"));
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("yes:", 0) == 0) {
                auto last = line.rfind(':');
                n.wifi_connected = true;
                n.wifi_ssid = line.substr(4, last - 4);
                n.wifi_signal = std::atoi(line.c_str() + last + 1);
            }
        }
        std::string ip = run("hostname -I");
        n.ip = ip.substr(0, ip.find(' '));
        n.bt_enabled = run("rfkill list bluetooth").find("Soft blocked: no") != std::string::npos;
        if (n.bt_enabled) {
            std::istringstream bt(run("bluetoothctl devices Connected 2>/dev/null"));
            for (std::string l; std::getline(bt, l);)
                if (l.rfind("Device ", 0) == 0) ++n.bt_connected;
        }
        n.ethernet = read_str("/sys/class/net/eth0/operstate") == "up";
        // Online through a phone: a Bluetooth network link (bnep0), or a USB
        // network interface from a phone's driver (Android: rndis_host or
        // cdc_ncm; iPhone: ipheth).
        std::string usb_phone;
        for (auto& i : list_dir("/sys/class/net/")) {
            std::string base = "/sys/class/net/" + i;
            if (i.rfind("bnep", 0) == 0) {
                if (n.tether.empty()) n.tether = "bluetooth";
                continue;
            }
            char link[256];
            ssize_t len = readlink((base + "/device/driver").c_str(), link, sizeof link - 1);
            if (len <= 0) continue;
            std::string driver(link, size_t(len));
            driver = driver.substr(driver.rfind('/') + 1);
            if (driver != "rndis_host" && driver != "ipheth" && driver != "cdc_ncm") continue;
            usb_phone = i;
            if (read_str(base + "/carrier") == "1") n.tether = "usb";
        }
        n.usb_gadget = usb_phone != "usb0" && read_str("/sys/class/net/usb0/operstate") == "up";
        if (access("/sys/class/net/tailscale0", F_OK) == 0) {
            std::string a = run("ip -4 -o addr show dev tailscale0 2>/dev/null");   // "... inet 100.x.y.z/32 ..."
            auto i = a.find("inet ");
            if (i != std::string::npos) n.tailscale_ip = a.substr(i + 5, a.find('/', i) - i - 5);
        }
        return n;
    }

    pid_t cam_pid_ = -1;
    int cam_fd_ = -1;
    CamState cam_state_ = CamState::Off;
    std::vector<uint8_t> cam_front_, cam_back_;
    size_t cam_fill_ = 0;
    uint32_t cam_seq_ = 0;
    time_t cam_want_ = 0;
    pid_t tether_pid_ = -1;
    int tether_fd_ = -1;
    bool tether_status_job_ = false, tether_found_seen_ = false;
    std::string tether_buf_;
    TetherStatus tether_, tether_next_;
    time_t tether_want_ = 0, tether_at_ = 0;
    bool dock_cache_ = false;
    TsStatus ts_cache_;
    SyncStatus sync_cache_;
    std::vector<std::string> ext5v_;
    int gps_fd_ = -1;
    bool gps_sent_ = false;
    time_t gps_want_ = 0, gps_try_ = 0, mesh_at_ = 0;
    std::string gps_buf_;
    GpsFix gps_;
    MeshStatus mesh_cache_;
    int mesh_fd_ = -1, mesh_seq_ = 0;
    bool mesh_wanted_ = false, mesh_no_radio_ = false;
    time_t mesh_log_at_ = 0;
    int mesh_log_checks_ = 3;   // none until mesh_start
    bool mesh_hello_ = false, mesh_connected_ = false;
    time_t mesh_try_ = 0, mesh_beat_ = 0;
    uint32_t mesh_me_ = 0, mesh_packet_id_ = uint32_t(time(nullptr)) & 0xfffff;
    mt::Stream mesh_in_;
    std::map<uint32_t, std::string> mesh_names_;
    std::vector<MeshMessage> mesh_log_;
    time_t sync_at_ = 0, sync_run_at_ = 0;
    bool sync_running_ = false;
    time_t ts_at_ = 0, term_at_ = 0;
    bool term_cache_ = false;
    long dock_calls_ = 0;
    std::string bat_, accel_, magn_, backlight_;
    BatteryInfo bat_cache_;
    ImuSample imu_cache_;
    NetStatus net_cache_;
    long net_ticks_ = 0;
};

}  // namespace

std::unique_ptr<Hal> make_linux_hal() { return std::make_unique<LinuxHal>(); }

static std::unique_ptr<Hal> g_hal;
Hal& hal() { return *g_hal; }
void set_hal(std::unique_ptr<Hal> h) { g_hal = std::move(h); }

}  // namespace zs
