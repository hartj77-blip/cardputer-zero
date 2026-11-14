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
//   media      sox: `play` for mp3/flac/ogg/wav, `rec` for voice notes, `soxi` for
//              durations. One small process per stream; pause = SIGSTOP/SIGCONT.
//
// Everything degrades gracefully: a missing file means "unknown", never a crash.
// None of this is verified on hardware yet (Phase 1 bring-up).
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "hal/hal.h"
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
    }

    void poll() override {
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
        if (!path.empty()) cmd << ' ' << std::quoted(path);
        run(cmd.str());
        return true;
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
        n.usb_gadget = read_str("/sys/class/net/usb0/operstate") == "up";
        return n;
    }

    bool dock_cache_ = false;
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
