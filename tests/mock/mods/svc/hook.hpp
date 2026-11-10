#pragma once
#include "mods/service.hpp"
#include "d/dolzel_rel.h"

namespace mockhook {
using PreFn = HookAction (*)(ModContext*, void*, void*, void*);
using PostFn = void (*)(ModContext*, void*, void*, void*);
template <class Sig> struct Entry;
template <class R, class... A> struct Entry<R(A...)> {
    static inline R (*g_orig)(A...) = nullptr;
    static inline PreFn pre = nullptr;
    static inline PostFn post = nullptr;
    static inline bool available = true;
};
}  // namespace mockhook

#define DEFINE_HOOK_SYMBOL(name, sig, Alias) struct Alias : mockhook::Entry<sig> {}

namespace mods::hook {
template <class E> ModResult add_pre(mockhook::PreFn f) { if (!E::available) return MOD_UNAVAILABLE; E::pre = f; return MOD_OK; }
template <class E> ModResult add_post(mockhook::PostFn f) { if (!E::available) return MOD_UNAVAILABLE; E::post = f; return MOD_OK; }
template <class E> ModResult install() { return E::available ? MOD_OK : MOD_UNAVAILABLE; }
}  // namespace mods::hook
