// Nightmare Lizalfos - decision logic.
//
// This header has NO game dependencies on purpose: it only knows about plain numbers, so
// it can be unit-tested offline (see tests/brain_test.cpp). mod.cpp translates game state
// into the small structs below and applies the decisions back to the Lizalfos actor.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace nl {

// ============================================================================================
//  TUNING - every number that shapes the fight lives in this block.
//  Distances are game units, speeds are units per game frame, times are game frames (30/s).
// ============================================================================================
namespace cfg {

// --- Defense quality ---
// Chance that a single attack instance (one sword swing, one jump attack, one arrow release,
// ...) gets through the Lizalfos' defense. 0.005 = it blocks/dodges 99.5% of attacks.
#ifdef NL_TEST_LEAK_CHANCE
inline constexpr float LEAK_CHANCE = NL_TEST_LEAK_CHANCE;  // offline simulation only
#else
inline constexpr float LEAK_CHANCE = 0.005f;
#endif

// --- What it reacts to ---
inline constexpr float REACT_RANGE = 650.0f;   // your melee swing is noticed inside this distance
inline constexpr float AIM_RANGE = 2600.0f;    // bow / hookshot / slingshot aim is noticed inside this
inline constexpr int AIM_CONE = 0x3000;        // ...if the target is within this angle of your aim
inline constexpr int AT_MARGIN = 4;            // protect this many frames after your sword's hit window
inline constexpr int CUT_END_GRACE = 3;        // ...and this many frames after your swing ends
inline constexpr int ARROW_TAIL = 36;          // shield stays up this long after an arrow is released
inline constexpr int TURTLE_FRAMES = 45;       // you held your own shield this long -> it uses the tail sweep

// --- Awareness ---
inline constexpr float AGGRO_RANGE = 2200.0f;  // idle Lizalfos within this distance snap straight to attack

// --- Chasing / fighting ---
inline constexpr float PURSUE_TAKEOVER = 1000.0f;  // inside this, the mod's close-combat AI drives
inline constexpr float PURSUE_RELEASE = 1200.0f;   // outside this, the original run AI drives again
inline constexpr float RUN_SPEED = 50.0f;          // original dash speed is 40
inline constexpr float FAR_RUN_BONUS = 10.0f;      // extra distance per frame while the original AI runs
inline constexpr float ACCEL_UP = 14.0f;
inline constexpr float ACCEL_DOWN = 18.0f;
inline constexpr float SPACING_OUTER = 330.0f;     // outside: full speed
inline constexpr float SPACING_INNER = 200.0f;     // between inner and outer: hold lunge distance
inline constexpr float ATTACK_RANGE = 390.0f;
inline constexpr int TURN_STEP = 0x1300;           // max turn per frame while chasing from a distance
inline constexpr int CLOSE_TURN_STEP = 0x0800;     // ...and when it is on top of you (circle it to get behind)
inline constexpr float CLOSE_RANGE = 350.0f;
inline constexpr int GUARD_TURN_STEP = 0x0800;     // max turn per frame while shielding / dodging
inline constexpr float LEAD_MAX_FRAMES = 7.0f;     // how far ahead of you it aims its run
inline constexpr int ATTACK_CD_MIN = 6;           // frames between attacks (random in this range)
inline constexpr int ATTACK_CD_MAX = 14;
inline constexpr float RIPOSTE_CHANCE = 0.70f;     // counter-attack right after a block
inline constexpr float DODGE_RIPOSTE_CHANCE = 0.65f;
inline constexpr float ATTACK_SPEED_MULT = 1.25f;   // its attack animations play this much faster (1.0 = original)
inline constexpr int WEAVE_AMPL = 0x1500;          // zig-zag while you aim a bow at it
inline constexpr float WEAVE_RATE = 0.5f;
inline constexpr int STUCK_FRAMES = 18;            // blocked by a wall this long -> hop over it

// --- Gap closer leap (uses the original jump) ---
inline constexpr float LEAP_MIN = 560.0f;
inline constexpr float LEAP_MAX = 1000.0f;
inline constexpr float LEAP_CHANCE = 0.09f;        // per frame while you stay in leap range
inline constexpr int LEAP_COOLDOWN = 55;

// --- Shield ---
inline constexpr float AIM_SHIELD_RANGE = 1300.0f; // raise the shield pose when you aim at it from inside this
inline constexpr float ADVANCE_SPEED = 34.0f;      // shield-up advance while you aim at it
inline constexpr float ADVANCE_STOP = 220.0f;
inline constexpr int MIN_GUARD_HOLD = 3;
inline constexpr int GUARD_MAX_HOLD = 150;

// --- Openings: how you earn free hits without a parry ---
// These make the fight a rhythm instead of a wall. While any of them is active the body is NOT
// protected and it does not react to your swings.
//  * Attacking: once an attack's strike frame is reached it is committed - no guard, no cancel.
//    (During the wind-up it still guards, so you cannot simply hit it out of its attack.)
//  * Recovery: after an attack, a dodge, or a leap landing, it is open for a short moment.
//  * Behind: your swings are not blocked if you are behind it (it turns slowly up close).
#ifdef NL_TEST_NO_OPENINGS
inline constexpr bool OPENINGS = false;  // offline tests of the always-protected defense only
#else
inline constexpr bool OPENINGS = true;
#endif
inline constexpr float COMMIT_FRAME_ATTACK_0 = 20.0f;  // animation frames, a little before each strike lands
inline constexpr float COMMIT_FRAME_ATTACK = 11.0f;
inline constexpr float COMMIT_FRAME_TAIL = 31.0f;
inline constexpr int ATTACK_RECOVERY = 12;
inline constexpr int DODGE_RECOVERY = 10;
inline constexpr int LEAP_RECOVERY = 15;
inline constexpr int BEHIND_ANGLE = 0x3800;        // ~79 degrees: more than this off its facing = behind it

// --- Parry window (like the Darknut) ---
// If its attack hits your shield, or you shield-bash it while it attacks, it recoils and is
// fully hittable for this many frames. No protection, no guard, nothing - go punish it.
inline constexpr int PARRY_FRAMES = 20;
inline constexpr int PARRY_COOLDOWN = 45;          // can't be parry-locked: immune this long afterwards
inline constexpr float PARRY_BASH_RANGE = 350.0f;  // your shield bash counts as a parry inside this distance
inline constexpr float PARRY_RECOIL_SPEED = -3.0f;   // small step back: it must stay inside your sword reach

// --- Guard break (hidden pressure counter) ---
// Every attack of yours that it successfully blocks or dodges adds one. If BREAK_COUNT of them land
// inside BREAK_WINDOW frames, its guard cracks: same open window as a parry, and the swing that
// cracked it connects. Back off for a moment and the older ones expire - no free bar to farm.
inline constexpr int BREAK_COUNT = 4;              // a full sword combo plus one
inline constexpr int BREAK_WINDOW = 150;           // 5 seconds
inline constexpr int BREAK_FRAMES = 30;            // how long it stays open once broken
inline constexpr int BREAK_COOLDOWN = 75;          // after the open window, before it can be broken again
inline constexpr float PRESSURE_RANGE = 450.0f;    // swings only count when you are this close

// --- Dodge ---
inline constexpr int DODGE_FRAMES = 7;
inline constexpr float DODGE_SPEED = 38.0f;
inline constexpr float DODGE_HOP = 15.0f;

// Hit bits the original AI "learns" (guards against). Bombs (0x10000000) are left out on
// purpose: the original code then learns them only after a bomb actually hits it.
inline constexpr uint32_t LEARN_MASK = 0xC0001FFFu;

// Log the first N decisions to the game log so playtest reports are easy to read.
inline constexpr int DEBUG_LOG_LIMIT = 300;

}  // namespace cfg

// ============================================================================================
//  Small utilities
// ============================================================================================
struct Rng {
    uint32_t s = 0x9E3779B9u;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1)); }
};

// Game angles: 0x10000 = full circle, 0 = +Z, positive turns toward +X (same as cM_atan2s(x, z)).
inline int16_t angle_of(float dx, float dz) {
    const float a = std::atan2(dx, dz) * (32768.0f / 3.14159265358979f);
    return static_cast<int16_t>(static_cast<int32_t>(std::lround(a)));
}
inline int16_t angle_diff(int16_t a, int16_t b) { return static_cast<int16_t>(a - b); }
inline int abs_angle(int16_t a) { return std::abs(static_cast<int>(a)); }

// Move `cur` toward `target` by a fraction of the gap, capped at maxStep (like cLib_addCalcAngleS2).
inline int16_t chase_angle(int16_t cur, int16_t target, int divisor, int maxStep) {
    const int d = static_cast<int16_t>(target - cur);
    int step = d / divisor;
    if (step > maxStep) step = maxStep;
    if (step < -maxStep) step = -maxStep;
    if (step == 0 && d != 0) step = d > 0 ? 1 : -1;
    return static_cast<int16_t>(cur + step);
}

inline float approach(float cur, float target, float up, float down) {
    if (cur < target) return std::min(cur + up, target);
    return std::max(cur - down, target);
}

// ============================================================================================
//  Link's attack types (mirror of daPy_py_c::CUT_TYPE_*; mod.cpp static_asserts the ones we use)
// ============================================================================================
namespace cut {
enum : uint8_t {
    NONE = 0x00,
    NM_VERTICAL = 0x01,
    NM_STAB = 0x02,
    NM_RIGHT = 0x03,
    NM_LEFT = 0x04,
    HEAD_JUMP = 0x05,  // Helm Splitter
    FINISH_LEFT = 0x06,
    FINISH_VERTICAL = 0x07,
    TURN_RIGHT = 0x08,
    JUMP = 0x0A,
    DASH_LEFT_SIDESTEP = 0x10,  // roll / sidestep attacks
    DASH_RIGHT_SIDESTEP = 0x11,
    LARGE_JUMP_INIT = 0x12,  // Jump Strike
    LARGE_JUMP = 0x13,
    LARGE_JUMP_FINISH = 0x14,
    FINISH_RIGHT = 0x15,
    TURN_LEFT = 0x16,
    LARGE_TURN_LEFT = 0x17,  // Great Spin
    LARGE_TURN_RIGHT = 0x18,
    MORTAL_DRAW_A = 0x1A,  // Mortal Draw
    TWIRL = 0x1E,          // Back Slice
    MORTAL_DRAW_B = 0x1F,
    FINISH_STAB = 0x20,
    COMBO_STAB = 0x21,
    DASH_LEFT = 0x25,
    DASH_RIGHT = 0x26,
    DOWN = 0x27,  // Ending Blow
    UNK_28 = 0x28,
    GUARD_ATTACK = 0x29,  // shield bash
    WOLF_JUMP_S = 0x31,
    WOLF_JUMP_S_FINISH = 0x32,
    DASH_CHARGE = 0x38,
    WOLF_JUMP = 0x39,
};
}  // namespace cut

enum class React : uint8_t {
    None,
    Block,  // raise the shield
    Dodge,  // hop out of the way (then shield for the rest of the attack)
    Aim,    // you are aiming/shooting a ranged weapon
};

// Skills and evasive attacks get dodged; everything else gets blocked.
inline React classify(uint8_t c) {
    switch (c) {
    case cut::NONE:
        return React::None;
    case cut::HEAD_JUMP:
    case cut::JUMP:
    case cut::LARGE_JUMP_INIT:
    case cut::LARGE_JUMP:
    case cut::LARGE_JUMP_FINISH:
    case cut::DASH_LEFT_SIDESTEP:
    case cut::DASH_RIGHT_SIDESTEP:
    case cut::DASH_LEFT:
    case cut::DASH_RIGHT:
    case cut::DASH_CHARGE:
    case cut::TWIRL:
    case cut::MORTAL_DRAW_A:
    case cut::MORTAL_DRAW_B:
    case cut::LARGE_TURN_LEFT:
    case cut::LARGE_TURN_RIGHT:
    case cut::WOLF_JUMP:
    case cut::WOLF_JUMP_S:
    case cut::WOLF_JUMP_S_FINISH:
        return React::Dodge;
    default:
        return React::Block;
    }
}

// How long (frames) an attack of this type is considered dangerous after it starts.
// The sword's own hit window and your swing ending both shorten/extend this at runtime.
inline int duration(uint8_t c) {
    switch (c) {
    case cut::NM_VERTICAL:
    case cut::NM_RIGHT:
    case cut::NM_LEFT:
        return 14;
    case cut::NM_STAB:
    case cut::COMBO_STAB:
    case cut::FINISH_STAB:
        return 16;
    case cut::FINISH_LEFT:
    case cut::FINISH_RIGHT:
    case cut::FINISH_VERTICAL:
        return 20;
    case cut::JUMP:
        return 30;
    case cut::HEAD_JUMP:
        return 40;
    case cut::LARGE_JUMP_INIT:
    case cut::LARGE_JUMP:
    case cut::LARGE_JUMP_FINISH:
        return 44;
    case cut::DASH_LEFT_SIDESTEP:
    case cut::DASH_RIGHT_SIDESTEP:
        return 24;
    case cut::DASH_LEFT:
    case cut::DASH_RIGHT:
        return 26;
    case cut::DASH_CHARGE:
        return 30;
    case cut::TURN_LEFT:
    case cut::TURN_RIGHT:
        return 34;
    case cut::LARGE_TURN_LEFT:
    case cut::LARGE_TURN_RIGHT:
        return 46;
    case cut::TWIRL:
        return 26;
    case cut::MORTAL_DRAW_A:
    case cut::MORTAL_DRAW_B:
        return 34;
    case cut::GUARD_ATTACK:
    case cut::UNK_28:
        return 18;
    case cut::WOLF_JUMP:
    case cut::WOLF_JUMP_S:
    case cut::WOLF_JUMP_S_FINISH:
        return 26;
    default:
        return 20;
    }
}

// Spin attacks hit all around you, so they count as aimed at everything nearby.
inline bool is_spin(uint8_t c) {
    return c == cut::TURN_LEFT || c == cut::TURN_RIGHT || c == cut::LARGE_TURN_LEFT || c == cut::LARGE_TURN_RIGHT;
}

// ============================================================================================
//  Guard break: counts defended attacks inside a sliding window
// ============================================================================================
struct Pressure {
    int stamp[cfg::BREAK_COUNT] = {};
    int count = 0;
    int head = 0;
    int ignoreUntil = -1;  // no counting while it is open / recovering
};

// Record one successfully defended attack at time `now` (frames). Returns true when this attack
// cracks its guard (BREAK_COUNT defended attacks within BREAK_WINDOW frames).
inline bool add_pressure(Pressure& p, int now) {
    if (now <= p.ignoreUntil) {
        return false;
    }
    p.stamp[p.head] = now;
    p.head = (p.head + 1) % cfg::BREAK_COUNT;
    if (p.count < cfg::BREAK_COUNT) {
        ++p.count;
    }
    if (p.count == cfg::BREAK_COUNT) {
        const int oldest = p.stamp[p.head];  // after the insert, head points at the oldest entry
        if (now - oldest <= cfg::BREAK_WINDOW) {
            p.count = 0;
            p.head = 0;
            p.ignoreUntil = now + cfg::BREAK_FRAMES + cfg::BREAK_COOLDOWN;
            return true;
        }
    }
    return false;
}

// ============================================================================================
//  Threat tracking (one per Lizalfos)
// ============================================================================================
struct LinkView {
    uint8_t cut = cut::NONE;  // current sword attack type (0 = none)
    bool cutAt = false;       // sword hit window is active this frame
    bool aiming = false;      // bow / slingshot / hookshot / boomerang ready
    bool shooting = false;    // arrow / hook just released
    bool guarding = false;    // you are holding your shield up
};

struct Obs {
    float dist = 0.0f;      // distance from this Lizalfos to you
    bool inAimCone = false; // this Lizalfos is roughly where you are aiming
};

struct Threat {
    int t = 0;
    uint8_t lastCut = cut::NONE;
    bool lastAt = false;
    bool atSeen = false;  // a hit window already happened during the current instance
    int cutEndT = -1000;

    React meleeReact = React::None;
    bool meleeProtect = false;
    int meleeUntil = -1;
    int instanceId = 0;
    uint8_t instCut = cut::NONE;
    bool newInstance = false;  // true only on the frame a new attack instance was recognized
    bool leaked = false;       // the roll for the current instance said "let it through"

    bool aimOn = false;
    bool aimProtect = true;
    bool lastShoot = false;
    int aimUntil = -1;    // shield stays up until here
    int shootUntil = -1;  // body protection (arrow flight) until here

    int linkGuardFrames = 0;
};

struct Decision {
    React want = React::None;
    bool protect = false;  // grant invulnerability this frame (a "successful" block/dodge)
    bool arrow = false;    // an arrow / hook was just released and is still in flight
};

inline bool melee_active(const Threat& th, bool cutting) {
    return th.meleeReact != React::None && th.t <= th.meleeUntil &&
           (cutting || th.t - th.cutEndT <= cfg::CUT_END_GRACE);
}

inline Decision observe(Threat& th, const LinkView& L, const Obs& o, Rng& rng) {
    th.t++;
    th.newInstance = false;

    const bool cutting = L.cut != cut::NONE;
    const bool nearMe = o.dist < cfg::REACT_RANGE;
    if (!cutting && th.lastCut != cut::NONE) {
        th.cutEndT = th.t;
    }

    // A new attack instance starts when the attack type changes, or when a hit window re-opens
    // after one already happened (spin pulses, repeated identical slashes). The FIRST hit window
    // of a swing belongs to the instance that began when the swing started - it must not roll again.
    bool fresh = false;
    if (cutting && nearMe) {
        const bool changed = L.cut != th.lastCut;
        const bool reopened = L.cutAt && !th.lastAt && th.atSeen;
        fresh = changed || reopened;
    }
    th.lastCut = L.cut;
    th.lastAt = L.cutAt;

    if (fresh) {
        th.instanceId++;
        th.instCut = L.cut;
        th.meleeReact = classify(L.cut);
        th.leaked = rng.unit() < cfg::LEAK_CHANCE;
        th.meleeProtect = !th.leaked;
        th.meleeUntil = th.t + duration(L.cut);
        th.newInstance = true;
        th.atSeen = false;
    }
    if (L.cutAt) {
        th.atSeen = true;
    }
    if (cutting && L.cutAt && nearMe) {
        th.meleeUntil = std::max(th.meleeUntil, th.t + cfg::AT_MARGIN);
    }

    // Ranged aiming.
    const bool inRange = o.dist < cfg::AIM_RANGE && o.inAimCone;
    const bool aimNow = L.aiming && inRange;
    const bool shootNow = L.shooting && inRange;
    if ((aimNow || shootNow) && !th.aimOn) {
        th.aimProtect = rng.unit() >= cfg::LEAK_CHANCE;
    }
    if (shootNow && !th.lastShoot) {
        th.aimProtect = rng.unit() >= cfg::LEAK_CHANCE;  // fresh roll for every arrow
    }
    if (shootNow) {
        th.shootUntil = th.t + cfg::ARROW_TAIL;  // body protected until the arrow has had time to land
    }
    th.lastShoot = shootNow;
    th.aimOn = aimNow || shootNow;
    if (th.aimOn) {
        th.aimUntil = th.t + cfg::ARROW_TAIL;
    }

    th.linkGuardFrames = L.guarding ? th.linkGuardFrames + 1 : 0;

    Decision d;
    if (melee_active(th, cutting)) {
        d.want = th.meleeReact;
        d.protect = th.meleeProtect;
        return d;
    }
    const bool shootRecent = th.t <= th.shootUntil;
    const bool aimActive = th.aimOn || th.t <= th.aimUntil;
    // Far away: hold the shield up the whole time you aim. Close up: it fights, and only
    // shields when an arrow is actually released.
    if (aimActive && (o.dist > cfg::ATTACK_RANGE || shootRecent)) {
        d.want = React::Aim;
        d.protect = shootRecent && th.aimProtect;
        d.arrow = shootRecent;
    }
    return d;
}

// ============================================================================================
//  Close-combat helpers
// ============================================================================================
inline float desired_speed(float dist, int16_t facingErr) {
    float s;
    if (dist > cfg::SPACING_OUTER) {
        s = cfg::RUN_SPEED;
    } else if (dist > cfg::SPACING_INNER) {
        s = cfg::RUN_SPEED * 0.4f;
    } else {
        s = 0.0f;
    }
    const int ae = abs_angle(facingErr);
    if (ae > 0x3000) {
        s *= 0.15f;
    } else if (ae > 0x1800) {
        s *= 0.55f;
    }
    return s;
}

// 0 = short swing, 1 = two-hit combo, 2 = tail sweep
inline int pick_attack(float dist, bool turtle, Rng& rng) {
    if (turtle) {
        return 2;
    }
    const float r = rng.unit();
    if (dist < 300.0f) {
        return r < 0.40f ? 0 : (r < 0.75f ? 1 : 2);
    }
    return r < 0.55f ? 1 : 2;
}

}  // namespace nl
