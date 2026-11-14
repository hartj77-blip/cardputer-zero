// The built-in apps, in launcher order.
#include "ui/app.h"

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

const std::vector<AppInfo>& app_registry() {
    static const std::vector<AppInfo> apps = {
        {"files", "Files", LV_SYMBOL_DIRECTORY, 0x58a6ff, make_files_app},
        {"notes", "Notes", LV_SYMBOL_EDIT, 0xe3b341, make_notes_app},
        {"media", "Media", LV_SYMBOL_AUDIO, 0xff7eb6, make_media_app},
        {"wifi", "Wi-Fi", LV_SYMBOL_WIFI, 0x3fb950, make_wifi_app},
        {"ir", "IR Remote", LV_SYMBOL_POWER, 0xf85149, make_ir_app},
        {"compass", "Compass", LV_SYMBOL_GPS, 0xff8a1f, make_compass_app},
        {"battery", "Battery", LV_SYMBOL_BATTERY_3, 0x3fb950, make_battery_app},
        {"clock", "Clock", LV_SYMBOL_BELL, 0xbc8cff, make_clock_app},
        {"dock", "Dock", LV_SYMBOL_IMAGE, 0x58a6ff, make_dock_app},
        {"sysmon", "System", LV_SYMBOL_LIST, 0x39c5cf, make_sysmon_app},
        {"settings", "Settings", LV_SYMBOL_SETTINGS, 0x8b97a5, make_settings_app},
    };
    return apps;
}

}  // namespace zs
