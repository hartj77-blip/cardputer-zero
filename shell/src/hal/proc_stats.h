// Memory and CPU readings from /proc and /sys, shared by the Linux backend
// (everything) and the mock backend (the shell's own memory, when available).
#pragma once

#include "hal/hal.h"

namespace zs {

// Fills the memory, zram, swap and CPU fields of `s`. Missing files leave
// fields at zero. `cpu_prev` holds the previous /proc/stat sample (busy, total).
void read_mem_cpu(SystemInfo& s, long long cpu_prev[2]);

// The shell's own resident and peak memory, in kB. False if unavailable.
bool read_self_mem(int& rss_kb, int& peak_kb);

// The n processes using the most resident memory.
std::vector<ProcMem> top_processes(size_t n);

}  // namespace zs
