// Which /dev/fbN is the Cardputer's own LCD.
//
// The LCD (ST7789V on SPI, M5's panel-mipi-dbi driver) and HDMI (vc4) are both
// DRM devices with an emulated framebuffer, and their numbers depend on which
// driver loads first. On M5's image vc4 is usually card0/fb0, so the LCD is
// fb1. M5's udev rule adds /dev/fb_lcd for it; we use that, then look in sysfs,
// and only then fall back to fb0. ZS_FRAMEBUFFER still overrides everything.
#pragma once

#include <dirent.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <fstream>
#include <string>

namespace zs {

inline std::string lcd_fb_read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

// Device path, e.g. "/dev/fb1".
inline std::string lcd_framebuffer() {
    if (const char* e = std::getenv("ZS_FRAMEBUFFER"); e && e[0]) return e;
    if (access("/dev/fb_lcd", F_OK) == 0) return "/dev/fb_lcd";
    std::string fallback;
    if (DIR* d = opendir("/sys/class/graphics")) {
        std::string found;
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.rfind("fb", 0) != 0 || n.size() < 3) continue;
            std::string base = "/sys/class/graphics/" + n + "/";
            // of_node/compatible is NUL-separated; a plain find works on it.
            std::string compat = lcd_fb_read(base + "device/of_node/compatible");
            std::string name = lcd_fb_read(base + "name");
            if (compat.find("st7789") != std::string::npos ||
                name.find("mipi-dbi") != std::string::npos) {
                found = "/dev/" + n;
                break;
            }
            if (fallback.empty() && name.find("vc4") == std::string::npos) fallback = "/dev/" + n;
        }
        closedir(d);
        if (!found.empty()) return found;
    }
    return fallback.empty() ? "/dev/fb0" : fallback;
}

// sysfs name of that framebuffer ("fb1"), following the /dev/fb_lcd symlink.
inline std::string lcd_fb_sysfs_name() {
    std::string dev = lcd_framebuffer();
    char buf[PATH_MAX];
    if (realpath(dev.c_str(), buf)) dev = buf;
    return dev.substr(dev.rfind('/') + 1);
}

}  // namespace zs
