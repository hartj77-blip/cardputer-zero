#pragma once

#include <string>

#include "lvgl.h"

namespace zs {

// Create the display and the keyboard input device for this backend.
lv_indev_t* platform_init(int w, int h, int zoom);

// Main loop. Returns the process exit code.
int platform_run();

// Headless only: run a scripted session (see main.cpp). Returns exit code.
int platform_run_script(const std::string& script);

}  // namespace zs
