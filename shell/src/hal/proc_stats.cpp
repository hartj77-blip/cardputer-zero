#include "hal/proc_stats.h"

#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace zs {
namespace {

std::string slurp_line(const char* path) {
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    return s;
}

long long read_ll(const char* path) {
    std::string s = slurp_line(path);
    return s.empty() ? 0 : std::strtoll(s.c_str(), nullptr, 10);
}

// Reads "Key: value kB" lines from a /proc status-style file.
long long field_kb(const std::string& text, const char* key) {
    auto p = text.find(key);
    if (p == std::string::npos) return 0;
    return std::strtoll(text.c_str() + p + std::strlen(key), nullptr, 10);
}

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

void read_mem_cpu(SystemInfo& s, long long cpu_prev[2]) {
    std::string mi = slurp("/proc/meminfo");
    if (!mi.empty()) {
        long long total = field_kb(mi, "MemTotal:"), avail = field_kb(mi, "MemAvailable:");
        s.mem_total_mb = int(total / 1024);
        s.mem_avail_mb = int(avail / 1024);
        s.mem_used_mb = int((total - avail) / 1024);
        s.mem_cache_mb = int(field_kb(mi, "\nCached:") / 1024);
    }

    // zram0: mm_stat = orig_data_size compr_data_size mem_used_total ...
    std::string mm = slurp_line("/sys/block/zram0/mm_stat");
    if (!mm.empty()) {
        long long orig = 0, compr = 0, used = 0;
        std::sscanf(mm.c_str(), "%lld %lld %lld", &orig, &compr, &used);
        s.zram_stored_mb = float(orig / 1048576.0);
        s.zram_ram_mb = float(used / 1048576.0);
        s.zram_capacity_mb = int(read_ll("/sys/block/zram0/disksize") / 1048576);
    }

    // /proc/swaps: Filename Type Size Used Priority (kB); skip zram devices.
    std::ifstream sw("/proc/swaps");
    std::string line;
    std::getline(sw, line);  // header
    s.disk_swap_total_mb = s.disk_swap_used_mb = 0;
    while (std::getline(sw, line)) {
        char name[256];
        long long size = 0, used = 0;
        if (std::sscanf(line.c_str(), "%255s %*s %lld %lld", name, &size, &used) == 3 &&
            std::strstr(name, "zram") == nullptr) {
            s.disk_swap_total_mb += int(size / 1024);
            s.disk_swap_used_mb += int(used / 1024);
        }
    }

    // CPU busy % since the previous sample.
    std::string st = slurp_line("/proc/stat");
    if (st.rfind("cpu ", 0) == 0) {
        std::istringstream in(st.substr(4));
        long long v, total = 0, idle = 0;
        for (int i = 0; in >> v; ++i) {
            total += v;
            if (i == 3 || i == 4) idle += v;  // idle + iowait
        }
        long long busy = total - idle;
        if (cpu_prev[1] > 0 && total > cpu_prev[1])
            s.cpu_load_percent = int(100 * (busy - cpu_prev[0]) / (total - cpu_prev[1]));
        cpu_prev[0] = busy;
        cpu_prev[1] = total;
    }
    s.cpu_mhz = int(read_ll("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq") / 1000);
    s.cpu_max_mhz = int(read_ll("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq") / 1000);
    s.cpu_governor = slurp_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");

    read_self_mem(s.self_rss_kb, s.self_peak_kb);
}

bool read_self_mem(int& rss_kb, int& peak_kb) {
    std::string st = slurp("/proc/self/status");
    if (st.empty()) return false;
    rss_kb = int(field_kb(st, "VmRSS:"));
    peak_kb = int(field_kb(st, "VmHWM:"));
    return rss_kb > 0;
}

std::vector<ProcMem> top_processes(size_t n) {
    std::vector<ProcMem> v;
    if (DIR* d = opendir("/proc")) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            std::string st = slurp(std::string("/proc/") + e->d_name + "/status");
            long long rss = field_kb(st, "VmRSS:");
            if (rss <= 0) continue;  // kernel threads
            auto p = st.find("Name:");
            std::string name;
            if (p != std::string::npos) {
                p += 5;
                while (p < st.size() && (st[p] == ' ' || st[p] == '\t')) ++p;
                name = st.substr(p, st.find('\n', p) - p);
            }
            // Merge processes with the same name (e.g. several bash shells).
            auto it = std::find_if(v.begin(), v.end(), [&](const ProcMem& m) { return m.name == name; });
            if (it != v.end()) it->rss_kb += int(rss);
            else v.push_back({name, int(rss)});
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end(), [](const ProcMem& a, const ProcMem& b) { return a.rss_kb > b.rss_kb; });
    if (v.size() > n) v.resize(n);
    return v;
}

}  // namespace zs
