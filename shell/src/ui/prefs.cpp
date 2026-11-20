#include "ui/prefs.h"

#include <sys/stat.h>

#include <cstdlib>
#include <fstream>
#include <map>

namespace zs::prefs {
namespace {

std::string dir() {
    const char* h = std::getenv("HOME");
    return std::string(h ? h : "/tmp") + "/.config/zeroos";
}

std::map<std::string, std::string>& store() {
    static std::map<std::string, std::string> m;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream in(dir() + "/settings.conf");
        std::string line;
        while (std::getline(in, line)) {
            auto eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
            m[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    return m;
}

void save() {
    std::string d = dir();
    std::string parent = d.substr(0, d.rfind('/'));
    mkdir(parent.c_str(), 0755);
    mkdir(d.c_str(), 0755);
    std::string tmp = d + "/settings.conf.tmp";
    {
        std::ofstream out(tmp);
        out << "# ZeroOS settings (written by ZeroShell)\n";
        for (auto& [k, v] : store()) out << k << '=' << v << '\n';
    }
    chmod(tmp.c_str(), 0600);   // it can hold a Jellyfin sign-in token
    std::rename(tmp.c_str(), (d + "/settings.conf").c_str());
}

}  // namespace

std::string get_str(const std::string& key, const std::string& def) {
    auto& m = store();
    auto it = m.find(key);
    return it == m.end() ? def : it->second;
}

void set_str(const std::string& key, const std::string& value) {
    auto& m = store();
    if (m.count(key) && m[key] == value) return;
    m[key] = value;
    save();
}

int get_int(const std::string& key, int def) {
    std::string v = get_str(key, "");
    if (v.empty()) return def;
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    return end && *end == '\0' ? int(n) : def;
}

void set_int(const std::string& key, int value) { set_str(key, std::to_string(value)); }

}  // namespace zs::prefs
