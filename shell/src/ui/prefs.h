// User preferences, kept in ~/.config/zeroos/settings.conf as "key=value"
// lines. Read once at start; every set() writes the file straight away.
#pragma once

#include <string>

namespace zs::prefs {

int get_int(const std::string& key, int def);
void set_int(const std::string& key, int value);
std::string get_str(const std::string& key, const std::string& def);
void set_str(const std::string& key, const std::string& value);

}  // namespace zs::prefs
