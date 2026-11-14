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
};

struct IrCode {
    std::string name;
    std::string protocol;   // nec, rc5, sony12, raw...
    unsigned scancode = 0;
};

struct SystemInfo {
    std::string hostname;
    std::string model;          // "CardputerZero" or "CardputerZero Lite"
    std::string kernel;
    std::string os_version;
    int cpu_temp_c = 0;
    int cpu_load_percent = 0;
    int mem_used_mb = 0, mem_total_mb = 0;
    int disk_used_gb10 = 0, disk_total_gb10 = 0;  // tenths of GB
    long uptime_s = 0;
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
    virtual int volume() = 0;                // 0..100
    virtual void set_volume(int pct) = 0;
    virtual bool muted() = 0;
    virtual void set_muted(bool m) = 0;

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

// Global accessor used by the UI.
Hal& hal();
void set_hal(std::unique_ptr<Hal> h);

}  // namespace zs
