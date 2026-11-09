# Nightmare Lizalfos

A Dusklight mod that turns the Lizalfos into a relentless, near-perfect defender in the style of
the Darknut: instant shield, ~99.5% block/dodge rate, pre-emptive arrow guard, faster and smarter
chasing, no flinching. Built from the TwilitRealm mod template, pinned to the Lazy Tweaks fork.

## Build

    cmake -S . -B build -DDUSKLIGHT_DIR=<path to your kamilink7/dusklight checkout>
    cmake --build build

The mod is `build/mods/nightmare_lizalfos.dusk`. Copy it into your Dusklight mods folder and enable it
in the in-game mod manager. A locally built `.dusk` only works on the platform that built it; push
this project to a GitHub repo made from the template to get all platforms from the included
GitHub Action.

If the build cannot find `res/Object/...` headers, make sure your checkout has `assets/GZ2E01`, and
if it cannot find `dolphin/types.h`, run `git submodule update --init extern/aurora`.

## Tuning

Every number is in `src/nl_brain.hpp`, block `namespace cfg`:

| Constant | Meaning |
|---|---|
| `LEAK_CHANCE` | chance one attack gets through (0.005 = 99.5% blocked). 0 = perfect, 0.05 = 95% |
| `REACT_RANGE`, `AIM_RANGE`, `AIM_SHIELD_RANGE` | how far away it reacts to swings / aiming |
| `RUN_SPEED`, `FAR_RUN_BONUS`, `ACCEL_*` | chase speed (original dash speed is 40) |
| `ATTACK_CD_MIN/MAX`, `ATTACK_SPEED_MULT`, `RIPOSTE_CHANCE` | how often and how fast it attacks |
| `DODGE_*`, `LEAP_*`, `ADVANCE_*` | dodge hop, gap-closing leap, shield-up advance |
| `LEARN_MASK` | which of your attack types it always guards (bombs are left out on purpose) |

## Debugging

The first `DEBUG_LOG_LIMIT` decisions are written to the game log, e.g.
`NL: attack cut=0x0A dist=312 -> DODGE (protected)` and `NL: state -> GUARD`.
A `(LEAK: gets through)` line means that attack rolled the 0.5% and will hit.

## Offline tests (no game needed)

    g++ -std=c++20 -O1 -Isrc tests/brain_test.cpp -o brain_test && ./brain_test
    g++ -std=c++20 -O1 -DNL_TEST_LEAK_CHANCE=0.0f -Itests/mock -Isrc tests/sim_test.cpp -o sim && ./sim

`sim_test.cpp` compiles the real `src/mod.cpp` against mock game headers and a simplified emulation of
the original Lizalfos AI. It checks logic (no stuck states, protection windows, riposte, dodges, leak
handling); it cannot check how anything looks or feels in the real game.
