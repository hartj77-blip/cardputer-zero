// Hardware abstraction for ZeroShell.
//
// Every hardware feature the UI touches goes through this interface, so the
// same UI runs on the device (LinuxHal: sysfs, iio, nmcli, ir-ctl...) and on a
// PC (MockHal: simulated values). In Phase 1 the Linux side moves behind the
// zerod system service; this interface stays the same.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hal/jellyfin.h"

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
    std::string tether;         // "usb" or "bluetooth" while online through a phone
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

// GPS (Cap LoRa-1262's GPS, or a GPS unit on the Grove port), through gpsd.
struct GpsFix {
    bool device = false;      // gpsd sees a receiver
    int mode = 0;             // 0 unknown, 1 no fix, 2 2D, 3 3D
    double lat = 0, lon = 0;  // degrees
    double alt_m = 0;         // above sea level
    double speed_mps = 0, track_deg = 0;
    double error_m = 0;       // horizontal estimate (eph), 0 unknown
    int sats_used = 0, sats_seen = 0;
    std::string time;         // UTC, ISO 8601
};

// LoRa mesh: meshtasticd with the Cap LoRa-1262 (docs/EXPANSION.md).
struct MeshStatus {
    bool installed = false;
    bool running = false;
    bool no_radio = false;    // it fell back to a simulated radio: no module, or wrong pins
    std::string web_url;      // Meshtastic web client, e.g. https://192.168.1.20:9443
    std::string api;          // for the phone apps: "192.168.1.20" (TCP 4403)
    bool connected = false;   // ZeroShell is talking to the daemon (chat works)
    std::string my_name;      // this node's short name
    int nodes = 0;            // other nodes heard of
};

struct MeshMessage {
    std::string from;         // sender's short name (or !hex id); "" for ours
    std::string text;
    bool mine = false;
    bool direct = false;      // sent to us only, not the whole channel
    long time = 0;            // unix time
};

// Pictures from the camera for the QR reader: 640x480
// planar YUV (I420): w*h luma bytes, then two quarter-size colour planes.
enum class CamState { Off, Starting, Running, NoCamera };

struct CameraFrame {
    int w = 0, h = 0;
    const uint8_t* y = nullptr;   // valid until the next camera call
    const uint8_t* u = nullptr;
    const uint8_t* v = nullptr;
    uint32_t seq = 0;             // goes up with every new frame
};

// Tethering: the internet through a phone, over USB or Bluetooth
// (docs/TETHER.md). A phone's Wi-Fi hotspot is just a Wi-Fi network.
struct TetherPhone {
    std::string mac, name;
    bool link = false;      // Bluetooth is connected
    bool online = false;    // and we have a network through it
};

struct TetherStatus {
    bool bt_on = false;
    // A phone on the USB port sharing its connection.
    bool usb = false;
    std::string usb_name, usb_ip;
    std::vector<TetherPhone> phones;   // paired over Bluetooth
    std::vector<TetherPhone> found;    // from the last search, not yet paired
    // What is going on now: "", "searching", "pairing", "connecting".
    std::string busy, busy_mac;
    std::string passkey;               // while pairing: the phone shows the same number
    std::string error;                 // why the last action failed
};

struct IrCode {
    std::string name;
    std::string protocol;   // nec, rc5, sony12, raw...
    unsigned scancode = 0;
};

// A Bluetooth speaker, headset or car stereo.
struct BtDevice {
    std::string address;      // AA:BB:CC:DD:EE:FF
    std::string name;
    bool paired = false;
    bool connected = false;
};

// USB stick mode: the device port offers a copy of ~/Music to a car stereo.
struct UsbStickStatus {
    enum State { Off, Building, Ready, Attached, Error } state = Off;
    int percent = 0;          // Building: how far the music copy has got
    std::string error;        // Error: a short reason
};

// Play/skip buttons on a car stereo or headset (Bluetooth), or a USB keyboard.
enum class MediaKey { None, PlayPause, Play, Pause, Stop, Next, Previous };

struct CameraStatus {
    bool installed = false;   // the camera app is on this image
    bool present = false;     // a camera sensor answered (never on the Lite)
    bool simulated = false;   // the simulator: nothing real to open
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

    // Expansion port 5 V (off by default; powers a Cap module or Grove unit).
    virtual bool ext_power() = 0;
    virtual void set_ext_power(bool on) = 0;

    // GPS. gps() starts watching gpsd (which starts on demand) and stays
    // watching while it keeps being called; gps_stop() lets it go now.
    virtual GpsFix gps() = 0;
    virtual void gps_stop() = 0;

    // QR scanning: ZeroShell's own look through the camera (the camera app
    // above must be closed; one program can hold the sensor). scan_start()
    // opens it (about a second); scan_frame() gives the newest picture and
    // keeps the camera on while it keeps being called.
    virtual void scan_start() = 0;
    virtual void scan_stop() = 0;
    virtual CamState scan_state() = 0;
    virtual bool scan_frame(CameraFrame& out) = 0;

    // Tethering. tether() is cheap and refreshes in the background while it
    // keeps being called. The actions return at once; watch busy and error.
    virtual TetherStatus tether() = 0;
    virtual void tether_search() = 0;                        // nearby phones, ~10 s
    virtual void tether_pair(const std::string& mac) = 0;    // pair, then connect
    virtual void tether_connect(const std::string& mac) = 0;
    virtual void tether_disconnect(const std::string& mac) = 0;
    virtual void tether_forget(const std::string& mac) = 0;

    // LoRa mesh (Meshtastic).
    virtual MeshStatus mesh() = 0;
    virtual void mesh_start() = 0;
    virtual void mesh_stop() = 0;
    // Chat on the primary channel. seq counts received messages, so the shell
    // can notice new ones cheaply.
    virtual std::vector<MeshMessage> mesh_messages() = 0;
    virtual bool mesh_send(const std::string& text) = 0;
    virtual int mesh_seq() = 0;

    // Terminal on the built-in screen: hands the screen and keyboard to a
    // Linux console running tmux (sessions survive going back). command ""
    // opens a shell; otherwise it runs in a new tmux window. False if this
    // backend has no real terminal (the simulator).
    virtual bool terminal_open(const std::string& command) = 0;
    virtual bool terminal_active() = 0;

    // Camera: hands the screen to M5Stack's camera app (viewfinder, photos,
    // gallery) until it exits. camera_open is false if it can't start, as
    // in the simulator.
    virtual CameraStatus camera() = 0;
    virtual bool camera_open() = 0;
    virtual bool camera_active() = 0;
    virtual void camera_close() = 0;
    virtual int volume() = 0;                // 0..100
    virtual void set_volume(int pct) = 0;
    virtual bool muted() = 0;
    virtual void set_muted(bool m) = 0;

    // Audio playback through the speaker / headphone jack (ES8389 + AW8737A).
    virtual std::vector<Track> music_library() = 0;     // ~/Music
    virtual bool play(const std::string& path) = 0;      // stops anything playing
    // Play audio from an http(s) address (a Jellyfin track). auth is sent as
    // the Authorization header; duration_s is what the server said, or -1.
    virtual bool play_stream(const std::string& url, const std::string& auth, int duration_s) = 0;
    virtual void set_paused(bool paused) = 0;
    virtual void stop() = 0;
    virtual PlaybackState playback() = 0;

    // Music from a Jellyfin server (hal/jellyfin.h). These wait on the
    // network, up to about 12 seconds.
    virtual bool jellyfin_login(const std::string& server, const std::string& user, const std::string& password,
                                const std::string& device_id, JfSession& out, std::string& error) = 0;
    virtual bool jellyfin_list(const JfSession& s, JfView view, const std::string& arg, std::vector<JfItem>& out,
                               std::string& error) = 0;

    // Bluetooth audio out. bt_scan and bt_connect wait: about 8 and up to
    // 40 seconds. Connecting pairs first if needed; sound then follows.
    virtual std::vector<BtDevice> bt_devices() = 0;       // paired, and seen by the last scan
    virtual void bt_scan() = 0;
    virtual bool bt_connect(const std::string& address, std::string& error) = 0;
    virtual void bt_disconnect(const std::string& address) = 0;
    virtual void bt_forget(const std::string& address) = 0;
    // Let a car find and pair with us from its own screen, for seconds.
    virtual void bt_set_visible(int seconds) = 0;
    // The next play/skip button press, or None. Call until None.
    virtual MediaKey media_key() = 0;

    virtual UsbStickStatus usb_stick() = 0;
    virtual void usb_stick_start() = 0;
    virtual void usb_stick_stop() = 0;

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
// Simulator only: 0 flat face up, 1 face down, 2 in hand, 3 upright in a pocket.
void mock_set_pose(int pose);

// Global accessor used by the UI.
Hal& hal();
void set_hal(std::unique_ptr<Hal> h);

}  // namespace zs
