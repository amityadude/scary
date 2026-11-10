// Mock of the Dusklight mod SDK surface used by src/mod.cpp (offline simulation only).
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <utility>

#define MOD_EXPORT
#define DEFINE_MOD() static_assert(true)
#define IMPORT_SERVICE(a, b) static_assert(true)

enum ModResult { MOD_OK = 0, MOD_UNAVAILABLE = 1, MOD_ERROR = 2 };
struct ModError { const char* msg = nullptr; };
struct ModContext {};
enum HookAction { HOOK_CONTINUE = 0, HOOK_SKIP_ORIGINAL = 1 };

namespace mods {
inline ModResult set_error(ModError* e, ModResult r, const char* m) { if (e) e->msg = m; return r; }
template <class T> T arg(void* args, int i) { return *reinterpret_cast<T*>(static_cast<void**>(args) + i); }
}  // namespace mods
