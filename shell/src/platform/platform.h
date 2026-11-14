#pragma once

#include <string>

#include "lvgl.h"

namespace zs {

// Create the display and the keyboard input device for this backend.
lv_indev_t* platform_init(int w, int h, int zoom);

// Main loop. Returns the process exit code.
int platform_run();

// Screen off: the device backend stops polling the keyboard on a timer and
// sleeps until a key arrives. No-op elsewhere.
void platform_set_low_power(bool on);

// Headless only: run a scripted session (see main.cpp). Returns exit code.
int platform_run_script(const std::string& script);

}  // namespace zs
