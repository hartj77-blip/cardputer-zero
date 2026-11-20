// The built-in apps, in launcher order.
#include "ui/app.h"
#include "ui/theme.h"

namespace zs {

std::unique_ptr<App> make_settings_app();
std::unique_ptr<App> make_wifi_app();
std::unique_ptr<App> make_media_app();
std::unique_ptr<App> make_files_app();
std::unique_ptr<App> make_notes_app();
std::unique_ptr<App> make_ir_app();
std::unique_ptr<App> make_compass_app();
std::unique_ptr<App> make_battery_app();
std::unique_ptr<App> make_clock_app();
std::unique_ptr<App> make_sysmon_app();
std::unique_ptr<App> make_dock_app();
std::unique_ptr<App> make_terminal_app();
std::unique_ptr<App> make_remote_app();
std::unique_ptr<App> make_sync_app();
std::unique_ptr<App> make_gps_app();
std::unique_ptr<App> make_mesh_app();
std::unique_ptr<App> make_camera_app();

const std::vector<AppInfo>& app_registry() {
    static const std::vector<AppInfo> apps = {
        {"terminal", "Terminal", ZS_ICON_TERMINAL, 0x3a3a3a, make_terminal_app},
        {"remote", "Remote", ZS_ICON_NETWORK, 0x0e8a9e, make_remote_app},
        {"sync", "Sync", ZS_ICON_SYNC, 0x7764d8, make_sync_app},
        {"files", "Files", LV_SYMBOL_DIRECTORY, 0xe95420, make_files_app},
        {"notes", "Notes", ZS_ICON_NOTE, 0xe0a100, make_notes_app},
        {"media", "Media", LV_SYMBOL_AUDIO, 0xd6336c, make_media_app},
        {"camera", "Camera", ZS_ICON_CAMERA, 0x2f6f8f, make_camera_app},
        {"wifi", "Wi-Fi", LV_SYMBOL_WIFI, 0x0073e5, make_wifi_app},
        {"ir", "IR Remote", LV_SYMBOL_POWER, 0xc7162b, make_ir_app},
        {"gps", "GPS", ZS_ICON_PIN, 0x0e8420, make_gps_app},
        {"mesh", "Mesh", ZS_ICON_CHAT, 0x12a182, make_mesh_app},
        {"compass", "Compass", ZS_ICON_COMPASS, 0xb04a8f, make_compass_app},
        {"battery", "Battery", LV_SYMBOL_BATTERY_3, 0x3aa655, make_battery_app},
        {"clock", "Clock", ZS_ICON_CLOCK, 0x4a5bd6, make_clock_app},
        {"dock", "Dock", ZS_ICON_DESKTOP, 0x1c71d8, make_dock_app},
        {"sysmon", "System", ZS_ICON_GAUGE, 0x5e6c7b, make_sysmon_app},
        {"settings", "Settings", LV_SYMBOL_SETTINGS, 0x77767b, make_settings_app},
    };
    return apps;
}

}  // namespace zs
