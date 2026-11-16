// Sync: when Syncthing runs (docs/SYNC.md).
//
// sync.mode = charging (default) | always | manual  (Settings > Sync)
//   charging: while on USB power with a network, plus the cases below
//   always:   from boot
//   manual:   only the two cases below
// In every mode it also runs while the Sync app is open, and after
// "Sync now" until everything is up to date (or 10 minutes).
#pragma once

#include <cstdint>
#include <string>

namespace zs {

void sync_tick(uint32_t now_ms);   // from the shell's timer
std::string sync_mode();
void set_sync_mode(const std::string& m);

void sync_now();                   // run until up to date, then stop
bool sync_now_active();
bool sync_running_cached();        // for the status bar; no fork

}  // namespace zs
