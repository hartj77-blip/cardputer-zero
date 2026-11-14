// ZeroShell entry point.
//
//   zeroshell                 run (SDL window on PC, framebuffer on device)
//   zeroshell --mock          device build, but with simulated hardware
//   zeroshell --zoom N        simulator window scale (default 3)
//   zeroshell --script S      headless build: run a key script and save screenshots
//
// Script syntax (headless): comma-separated steps
//   right left up down enter esc bksp   navigation keys
//   type:<text>                          type characters
//   key:<char>                           one character key
//   open:<app-id>  home                  jump straight to an app / home
//   wait:<ms>                            let timers and animations run
//   shot:<file.ppm>                      save the screen
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>

#include "hal/hal.h"
#include "lvgl.h"
#include "platform/platform.h"
#include "ui/shell.h"
#include "ui/theme.h"

using namespace zs;

int main(int argc, char** argv) {
    bool mock = std::strcmp(ZS_BACKEND_NAME, "device") != 0;
    int zoom = 3;
    std::string script;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--mock") mock = true;
        else if (a == "--real-hal") mock = false;
        else if (a == "--zoom" && i + 1 < argc) zoom = std::atoi(argv[++i]);
        else if (a == "--script" && i + 1 < argc) script = argv[++i];
        else if (a == "--version") {
            std::printf("zeroshell %s (%s)\n", ZS_VERSION, ZS_BACKEND_NAME);
            return 0;
        } else {
            std::fprintf(stderr, "usage: %s [--mock|--real-hal] [--zoom N] [--script STEPS] [--version]\n", argv[0]);
            return 2;
        }
    }

    set_hal(mock ? make_mock_hal() : make_linux_hal());
    lv_init();

    lv_indev_t* keypad = platform_init(theme::kScreenW, theme::kScreenH, zoom);
    Shell::get().start(keypad);

    if (!script.empty()) return platform_run_script(script);
    return platform_run();
}
