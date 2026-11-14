// Simulated hardware for running ZeroShell on a PC.
#include <cmath>
#include <cstdio>
#include <ctime>

#include "hal/hal.h"

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
        s.cpu_temp_c = 47;
        s.cpu_load_percent = 12;
        s.mem_used_mb = 118;
        s.mem_total_mb = 438;
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
