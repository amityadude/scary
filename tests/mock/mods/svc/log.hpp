#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
namespace fmt { template <class... A> using format_string = const char*; }
namespace mods::log {
inline std::vector<std::string>& lines() { static std::vector<std::string> v; return v; }
inline bool& echo() { static bool e = false; return e; }
template <class T> void put(std::ostringstream& o, const T& v) { o << v; }
template <class... A> void info(const char* f, A&&... a) {
    std::vector<std::string> args;
    ((args.push_back([&] { std::ostringstream o; put(o, a); return o.str(); }())), ...);
    std::string out, fs(f); size_t ai = 0;
    for (size_t i = 0; i < fs.size(); ++i) {
        if (fs[i] == '{') { size_t j = fs.find('}', i); out += ai < args.size() ? args[ai++] : "?"; i = j; }
        else out += fs[i];
    }
    lines().push_back(out);
    if (echo()) std::printf("  [log] %s\n", out.c_str());
}
}  // namespace mods::log
