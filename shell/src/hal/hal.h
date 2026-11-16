// Hardware abstraction for ZeroShell.
//
// Every hardware feature the UI touches goes through this interface, so the
// same UI runs on the device (LinuxHal: sysfs, iio, nmcli, ir-ctl...) and on a
// PC (MockHal: simulated values). In Phase 1 the Linux side moves behind the
// zerod system service; this interface stays the same.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace zs {

struct BatteryInfo {
    int percent = -1;          // 0..100, -1 unknown
    bool charging = false;
    bool external_power = false;
    int voltage_mv = 0;
    int current_ma = 0;        // negative while discharging
    int temp_c10 = 0;          // tenths of a degree C
    int minutes_left = -1;     // to empty (or to full while charging)
    int health_percent = -1;
    int cycle_count = -1;
    int power_mw = 0;          // instantaneous draw (V x I), positive
    int charge_now_mah = -1;   // remaining charge, from the gauge
    int charge_full_mah = -1;  // what a full charge holds today
};

// CPU speed policy. The shell picks one from the user's power profile and
// switches to ScreenOff while the screen is off.
enum class CpuMode {
    Saver,         // on-demand, capped at ~70% of top speed
    Balanced,      // on-demand up to top speed
    Performance,   // always at top speed
    ScreenOff,     // lowest speed
};

struct ImuSample {
    float ax = 0, ay = 0, az = 0;   // m/s^2
    float gx = 0, gy = 0, gz = 0;   // deg/s
    float heading_deg = 0;          // magnetic heading, 0 = north
    float pitch_deg = 0, roll_deg = 0;
    bool available = false;         // false on CardputerZero Lite
};

struct WifiNetwork {
    std::string ssid;
    int signal = 0;      // 0..100
    bool secure = true;
    bool connected = false;
};

struct NetStatus {
    bool wifi_enabled = true;
    bool wifi_connected = false;
    std::string wifi_ssid;
    int wifi_signal = 0;
    std::string ip;
    bool bt_enabled = false;
    int bt_connected = 0;
    bool ethernet = false;
    bool usb_gadget = false;
    std::string tailscale_ip;   // set while Tailscale is connected
};

// Tailscale (see docs/REMOTE.md).
struct TsPeer {
    std::string name;    // machine name, e.g. "desktop"
    std::string ip;      // 100.x.y.z
    std::string os;      // linux, windows, macOS, iOS, android...
    bool online = false;
};

struct TsStatus {
    bool installed = false;
    bool running = false;        // tailscaled is up
    std::string state;           // Stopped, NeedsLogin, Starting, Running, NeedsMachineAuth
    std::string auth_url;        // sign-in link while state == NeedsLogin
    std::string self_name, self_ip, tailnet;
    std::vector<TsPeer> peers;   // online first
};

// Sync: Syncthing keeps Notes, Documents and Recordings the same on your
// other computers (see docs/SYNC.md).
struct SyncFolder {
    std::string id, label, path;
    std::string state;      // idle, scanning, syncing, error, ...
    int percent = 100;      // how much of the latest version we have
    int need_files = 0;
    std::string error;
};

struct SyncDevice {
    std::string id, name;
    bool connected = false;
    int percent = 100;      // how up to date that device is with us
};

// Someone asked to connect, or offered us a folder.
struct SyncRequest {
    bool is_folder = false;
    std::string device_id, device_name;
    std::string folder_id, folder_label;   // folder offers only
};

struct SyncStatus {
    bool installed = false;
    bool running = false;   // the service is up
    bool ready = false;     // and answering (set up, folders known)
    std::string device_id;  // ours: give this to your other devices
    std::vector<SyncFolder> folders;
    std::vector<SyncDevice> devices;
    std::vector<SyncRequest> requests;
};

struct IrCode {
    std::string name;
    std::string protocol;   // nec, rc5, sony12, raw...
    unsigned scancode = 0;
};

struct Track {
    std::string path;
    std::string title;
    std::string artist;       // folder name for music, date for voice notes
    int duration_s = -1;      // -1 unknown until played
};

struct PlaybackState {
    std::string path;         // empty when stopped
    bool playing = false;     // false while paused or stopped
    bool paused = false;
    bool finished = false;    // last track reached its end (cleared by next play)
    int pos_ms = 0;
    int dur_ms = -1;
};

struct RecordState {
    bool recording = false;
    std::string path;
    int elapsed_ms = 0;
};

struct ProcMem {
    std::string name;
    int rss_kb = 0;
};

struct SystemInfo {
    std::string hostname;
    std::string model;          // "CardputerZero" or "CardputerZero Lite"
    std::string kernel;
    std::string os_version;
    int cpu_temp_c = 0;
    int cpu_load_percent = 0;   // all cores, since the previous call
    int cpu_mhz = 0, cpu_max_mhz = 0;
    std::string cpu_governor;
    // Memory, from /proc/meminfo. "Used" = total - available (cache doesn't count).
    int mem_used_mb = 0, mem_total_mb = 0;
    int mem_avail_mb = 0, mem_cache_mb = 0;
    // zram: data stored vs the RAM it takes (ratio = stored / taken)
    float zram_stored_mb = 0, zram_ram_mb = 0;
    int zram_capacity_mb = 0;
    // Swap that isn't zram (the SD card swap file / writeback)
    int disk_swap_used_mb = 0, disk_swap_total_mb = 0;
    // The shell's own memory
    int self_rss_kb = 0, self_peak_kb = 0;
    std::vector<ProcMem> top_procs;   // biggest resident first, up to 5
    int disk_used_gb10 = 0, disk_total_gb10 = 0;  // tenths of GB
    long uptime_s = 0;
    bool simulated = false;     // mock backend: numbers are illustrative
};

// The HDMI port and Dock mode (a desktop on the monitor, see docs/DOCK.md).
struct DisplayStatus {
    bool hdmi_connected = false;
    std::string hdmi_mode;        // preferred mode, e.g. "1920x1080"
    bool dock_running = false;
};

class Hal {
public:
    virtual ~Hal() = default;

    // Called ~every 100 ms from the UI loop; refresh cached readings.
    virtual void poll() = 0;

    virtual BatteryInfo battery() = 0;
    virtual ImuSample imu() = 0;
    virtual NetStatus net() = 0;
    virtual SystemInfo system() = 0;

    virtual std::vector<WifiNetwork> wifi_scan() = 0;
    virtual bool wifi_connect(const std::string& ssid, const std::string& psk) = 0;
    virtual void set_wifi_enabled(bool on) = 0;
    virtual void set_bt_enabled(bool on) = 0;

    virtual int brightness() = 0;            // 0..100
    virtual void set_brightness(int pct) = 0;
    // Backlight fully off (and the panel blanked) or back on at brightness().
    virtual void set_screen_on(bool on) = 0;
    // False in the simulator: the shell then dims and blanks by drawing.
    virtual bool has_backlight() = 0;
    virtual void set_cpu_mode(CpuMode mode) = 0;

    // HDMI and Dock mode.
    virtual DisplayStatus display() = 0;
    virtual void dock_start() = 0;
    virtual void dock_stop() = 0;
    // Open something on the monitor: "terminal", "browser", "files", or
    // "video" with a file path. False if Dock mode isn't running.
    virtual bool dock_open(const std::string& what, const std::string& path = "") = 0;

    // Tailscale. start() starts the daemon and connects (asking for sign-in
    // the first time); stop() disconnects and stops the daemon.
    virtual TsStatus tailscale() = 0;
    virtual void tailscale_start() = 0;
    virtual void tailscale_stop() = 0;
    virtual void tailscale_logout() = 0;
    virtual bool taildrop_send(const std::string& path, const std::string& peer) = 0;

    // Sync (Syncthing). start/stop the service; accept or ignore a request
    // (accepting a device shares Notes, Documents and Recordings with it; an
    // offered folder goes to ~/Sync/<name>); add a device by its ID.
    virtual SyncStatus sync() = 0;          // slow on the device: a few calls a minute
    virtual bool sync_running() = 0;        // cheap: is the service up
    virtual void sync_start() = 0;
    virtual void sync_stop() = 0;
    virtual bool sync_answer(const SyncRequest& r, bool accept) = 0;
    virtual bool sync_add_device(const std::string& id, const std::string& name) = 0;
    virtual bool sync_remove_device(const std::string& id) = 0;
    virtual void sync_rescan() = 0;

    // Terminal on the built-in screen: hands the screen and keyboard to a
    // Linux console running tmux (sessions survive going back). command ""
    // opens a shell; otherwise it runs in a new tmux window. False if this
    // backend has no real terminal (the simulator).
    virtual bool terminal_open(const std::string& command) = 0;
    virtual bool terminal_active() = 0;
    virtual int volume() = 0;                // 0..100
    virtual void set_volume(int pct) = 0;
    virtual bool muted() = 0;
    virtual void set_muted(bool m) = 0;

    // Audio playback through the speaker / headphone jack (ES8389 + AW8737A).
    virtual std::vector<Track> music_library() = 0;     // ~/Music
    virtual bool play(const std::string& path) = 0;      // stops anything playing
    virtual void set_paused(bool paused) = 0;
    virtual void stop() = 0;
    virtual PlaybackState playback() = 0;

    // Voice notes from the MEMS mic, saved to ~/Recordings as 16 kHz mono WAV.
    virtual std::vector<Track> recordings() = 0;          // newest first
    virtual bool record_start() = 0;
    virtual void record_stop() = 0;
    virtual RecordState record_state() = 0;
    virtual bool delete_recording(const std::string& path) = 0;

    // Keyboard LEDs (Shift / Fn / Sym / 3 status LEDs)
    enum class Led { Shift, Fn, Sym, Status1, Status2, Status3 };
    virtual void set_led(Led led, bool on) = 0;

    // Infrared
    virtual bool ir_send(const IrCode& code) = 0;
    // Blocks up to timeout_ms waiting for one IR frame. Returns false on timeout.
    virtual bool ir_learn(IrCode& out, int timeout_ms) = 0;

    virtual void power_off() = 0;
    virtual void reboot() = 0;

    // Sound effects through the speaker (key click, notification).
    virtual void beep(int freq_hz, int ms) = 0;
};

// MockHal for the simulator and tests; LinuxHal for the device.
std::unique_ptr<Hal> make_mock_hal();
std::unique_ptr<Hal> make_linux_hal();

// Simulator only: plug or unplug a pretend HDMI monitor.
void mock_set_hdmi(bool connected);

// Global accessor used by the UI.
Hal& hal();
void set_hal(std::unique_ptr<Hal> h);

}  // namespace zs
