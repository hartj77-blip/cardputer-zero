// App model for ZeroShell.
//
// An app owns a full-width content area (320x150 under the status bar).
// The shell calls on_key() before LVGL sees a key, so apps can grab keys;
// returning false passes the key on to the focused widget (lists, text areas).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lvgl.h"

namespace zs {

class App {
public:
    virtual ~App() = default;
    virtual void build(lv_obj_t* root) = 0;
    virtual void tick() {}                         // ~10 Hz
    virtual bool on_key(uint32_t key) { return false; }
    // Escape: return true if handled inside the app (e.g. leave a sub-page).
    virtual bool on_back() { return false; }

    lv_group_t* group = nullptr;   // focus group, set by the shell
};

struct AppInfo {
    std::string id;
    std::string name;
    const char* symbol;       // LV_SYMBOL_* or ZS_ICON_*
    uint32_t color;           // icon background, 0xRRGGBB
    std::function<std::unique_ptr<App>()> make;
};

// Registry, populated by apps/registry.cpp.
const std::vector<AppInfo>& app_registry();

// Shell services available to apps.
void shell_toast(const std::string& msg);
void shell_open_app(const std::string& id);
void shell_go_home();
void shell_add_focus(lv_obj_t* obj);   // add widget to the current app's focus group
void shell_screen_changed();            // power manager: screen dimmed, off or on
// Open the terminal on the built-in screen ("" = shell, or a command such as
// "ssh desktop"). False in the simulator, which has no real console.
bool shell_open_terminal(const std::string& command);
// Hand the screen to the camera app until it exits. False if it can't start.
bool shell_open_camera();

}  // namespace zs
