// Nightmare Lizalfos
//
// Turns the Lizalfos into a relentless, near-perfect defender. See nl_brain.hpp for every
// tunable number and tests/brain_test.cpp for the offline checks of the decision logic.
//
// How it works (short version):
//   * The Lizalfos AI lives in one per-frame function, `action()`, in d_a_e_dn.cpp. We hook it.
//   * BEFORE it runs we read what you are doing (sword swing type, bow aimed, ...) and steer
//     the Lizalfos: raise the shield, dodge, chase, attack. New behaviours are implemented as
//     extra "action" ids (100+) that the original code ignores, so the original still provides
//     physics, collisions and damage while we provide the brain.
//   * Blocking works by making the body un-hittable for the length of an attack
//     (`invulnerability_timer`) while the shield pose + shield hitbox are up. Each attack rolls
//     once against cfg::LEAK_CHANCE; a failed roll leaves the body hittable for that attack.
//   * AFTER it runs we cancel flinches/knockdowns (damage still lands) and apply movement nudges.

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"

// Game headers. dolzel_rel.h first: it is the shared header set every actor .cpp starts with.
#include "d/dolzel_rel.h"
#include "d/actor/d_a_e_dn.h"
#include "d/actor/d_a_player.h"
#include "d/actor/d_a_alink.h"
#include "f_op/f_op_actor_mng.h"

#include "nl_brain.hpp"

DEFINE_MOD();

IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);

// The Lizalfos per-frame AI. (File-local static; the hook test proved this resolves.)
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_e_dn.cpp#action", void(e_dn_class*), LizAction);
// Optional: the original ledge check. If it cannot be resolved we simply skip ledge avoidance.
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_e_dn.cpp#move_gake_check", BOOL(e_dn_class*, f32, s8), LizGake);

namespace {

using nl::React;

// ---- Constants mirrored from d_a_e_dn.cpp (read from the source, not guessed) ----------------
enum : s16 {
    A_NORMAL = 0,
    A_FIGHT_RUN = 3,
    A_ATTACK_0 = 4,
    A_ATTACK = 5,
    A_TAIL_ATTACK = 6,
    A_GUARD = 7,
    A_JUMP = 8,
    A_STAY = 9,
    A_OTOREAC = 10,
    A_BOMB_ACTION = 11,
    A_DRAWBACK = 12,
    A_WOLFBITE = 13,
    A_S_DAMAGE = 20,
    A_DAMAGE = 21,
    A_BACKDROP = 23,
    A_WATER = 24,
    A_GAKEJUMP = 25,
    A_REG = 60,
};
// Our own action ids. The original switch has no case for them, so the original AI does nothing
// while we are in one - but it still runs damage checks, physics and collision every frame.
enum : s16 { NA_GUARD = 100, NA_DODGE = 101, NA_PURSUE = 102, NA_PARRIED = 103 };

enum : int { ANM_GUARD = 0x16, ANM_JUMP_B = 0x1C, ANM_RUN = 0x1E, ANM_WAIT_01 = 0x1F };

// Keep our mirror of Link's attack ids honest: a mismatch is a compile error, not a silent bug.
#define NL_CUT_CHECK(name) \
    static_assert(static_cast<int>(nl::cut::name) == static_cast<int>(daPy_py_c::CUT_TYPE_##name), #name)
NL_CUT_CHECK(NM_VERTICAL);
NL_CUT_CHECK(NM_STAB);
NL_CUT_CHECK(NM_RIGHT);
NL_CUT_CHECK(NM_LEFT);
NL_CUT_CHECK(HEAD_JUMP);
NL_CUT_CHECK(FINISH_LEFT);
NL_CUT_CHECK(FINISH_VERTICAL);
NL_CUT_CHECK(TURN_RIGHT);
NL_CUT_CHECK(JUMP);
NL_CUT_CHECK(DASH_LEFT_SIDESTEP);
NL_CUT_CHECK(DASH_RIGHT_SIDESTEP);
NL_CUT_CHECK(LARGE_JUMP_INIT);
NL_CUT_CHECK(LARGE_JUMP);
NL_CUT_CHECK(LARGE_JUMP_FINISH);
NL_CUT_CHECK(FINISH_RIGHT);
NL_CUT_CHECK(TURN_LEFT);
NL_CUT_CHECK(LARGE_TURN_LEFT);
NL_CUT_CHECK(LARGE_TURN_RIGHT);
NL_CUT_CHECK(MORTAL_DRAW_A);
NL_CUT_CHECK(TWIRL);
NL_CUT_CHECK(MORTAL_DRAW_B);
NL_CUT_CHECK(FINISH_STAB);
NL_CUT_CHECK(COMBO_STAB);
NL_CUT_CHECK(DASH_LEFT);
NL_CUT_CHECK(DASH_RIGHT);
NL_CUT_CHECK(DOWN);
NL_CUT_CHECK(UNK_28);
NL_CUT_CHECK(GUARD_ATTACK);
NL_CUT_CHECK(WOLF_JUMP_S);
NL_CUT_CHECK(WOLF_JUMP_S_FINISH);
NL_CUT_CHECK(DASH_CHARGE);
NL_CUT_CHECK(WOLF_JUMP);
#undef NL_CUT_CHECK

// ---- Per-Lizalfos extra state -------------------------------------------------------------
struct Ext {
    bool used = false;
    e_dn_class* who = nullptr;
    unsigned lastSeen = 0;
    nl::Threat th;

    // state machine
    int stateT = 0;
    React guardReason = React::Block;
    int instDodged = -1;  // attack instance we already dodged
    float dodgeVX = 0.0f;
    float dodgeVZ = 0.0f;
    float nudgeX = 0.0f;  // extra movement applied after the original AI runs
    float nudgeZ = 0.0f;
    int leapCd = 0;
    int stuck = 0;
    int parryT = 0;   // frames left in the parry window
    int parryCd = 0;  // frames until it can be parried again

    // snapshot for the post-hook
    s16 preAction = -1;
    bool managed = false;
    s16 lastLoggedAction = -1;
};

constexpr int kMaxExt = 16;
Ext g_ext[kMaxExt];
unsigned g_tick = 0;
nl::Rng g_rng;
int g_logCount = 0;
bool g_haveGake = false;

template <class... A>
void dbg(fmt::format_string<A...> f, A&&... a) {
    if (g_logCount < nl::cfg::DEBUG_LOG_LIMIT) {
        ++g_logCount;
        mods::log::info(f, std::forward<A>(a)...);
    }
}

Ext& ext_for(e_dn_class* dn) {
    Ext* freeSlot = nullptr;
    Ext* oldest = &g_ext[0];
    for (Ext& x : g_ext) {
        if (x.used && x.who == dn && g_tick - x.lastSeen < 600) {
            x.lastSeen = g_tick;
            return x;
        }
        if (!x.used && freeSlot == nullptr) {
            freeSlot = &x;
        }
        if (x.lastSeen < oldest->lastSeen) {
            oldest = &x;
        }
    }
    Ext* slot = freeSlot != nullptr ? freeSlot : oldest;
    *slot = Ext{};
    slot->used = true;
    slot->who = dn;
    slot->lastSeen = g_tick;
    return *slot;
}

// ---- Reading the player ---------------------------------------------------------------------
struct LinkInfo {
    nl::LinkView v;
    float x = 0.0f;
    float z = 0.0f;
    float vx = 0.0f;
    float vz = 0.0f;
    int16_t facing = 0;
};

bool read_link(LinkInfo& out) {
    // Same as daPy_getPlayerActorClass(), spelled out: that helper calls dComIfGp_getPlayer(), an inline
    // wrapper that the game's link stubs do not export (Windows/macOS/Android builds fail to link).
    daPy_py_c* py = static_cast<daPy_py_c*>(g_dComIfG_gameInfo.play.getPlayer(0));
    if (py == nullptr) {
        return false;
    }
    daAlink_c* link = static_cast<daAlink_c*>(py);

    out.v.cut = py->getCutType();
    out.v.cutAt = py->getCutAtFlg() != 0;
    out.v.guarding = py->checkPlayerGuard() != 0;
    out.v.aiming = false;
    out.v.shooting = false;
    // The bow / hookshot / boomerang animation checks compare animation ids that only mean
    // those things in human form, so skip them while Link is a wolf.
    if (!py->checkWolf()) {
        out.v.aiming = link->checkBowWaitAnime() || link->checkBowChargeWaitAnime() ||
                       link->checkBowReloadAnime() || link->checkHookshotReadyAnime() ||
                       link->checkBoomerangReadyAnime() || py->checkArrowChargeEnd();
        out.v.shooting = link->checkBowShootAnime() || link->checkHookshotShootAnime();
    }

    out.x = py->current.pos.x;
    out.z = py->current.pos.z;
    out.vx = std::clamp(static_cast<float>(py->speed.x), -90.0f, 90.0f);
    out.vz = std::clamp(static_cast<float>(py->speed.z), -90.0f, 90.0f);
    out.facing = static_cast<int16_t>(py->shape_angle.y);
    return true;
}

// ---- Small actor helpers ------------------------------------------------------------------
void play_anim(e_dn_class* dn, int id, int attr, f32 morf, f32 rate, f32 start = 0.0f) {
    // Same as dComIfG_getObjectRes("E_dn", id), spelled out (that wrapper is not in the link stubs either).
    dn->anm_p->setAnm(static_cast<J3DAnmTransform*>(g_dComIfG_gameInfo.mResControl.getObjectRes("E_dn", id)),
                      attr, morf, rate, start, -1.0f);
    dn->anm_no = id;
}

float fsin(int16_t a) { return std::sin(static_cast<float>(a) * (3.14159265358979f / 32768.0f)); }
float fcos(int16_t a) { return std::cos(static_cast<float>(a) * (3.14159265358979f / 32768.0f)); }

bool is_attack(s16 a) { return a == A_ATTACK_0 || a == A_ATTACK || a == A_TAIL_ATTACK; }

// States the original code owns completely: we do not even look at them.
bool frozen(s16 a) {
    switch (a) {
    case A_DAMAGE:
    case A_WATER:
    case A_BACKDROP:
    case A_REG:
    case A_WOLFBITE:
        return true;
    default:
        return false;
    }
}

// Committed movements (leaps, bomb dodge, hookshot pull): we leave the movement to the original
// but still protect the body, so a leap cannot be punished with a free hit.
bool committed(s16 a) {
    switch (a) {
    case A_DRAWBACK:
    case A_GAKEJUMP:
    case A_JUMP:
    case A_BOMB_ACTION:
        return true;
    default:
        return false;
    }
}

struct Ctx {
    e_dn_class* dn;
    Ext* e;
    const LinkInfo* L;
    float dist;
    int16_t toLink;
    nl::Decision d;
};

void go_pursue(const Ctx& c) {
    c.dn->action = c.dist < nl::cfg::PURSUE_RELEASE ? static_cast<s16>(NA_PURSUE) : static_cast<s16>(A_FIGHT_RUN);
    c.dn->mode = 0;
    c.e->stateT = 0;
}

void start_attack(const Ctx& c, bool turtle) {
    const int kind = nl::pick_attack(c.dist, turtle, g_rng);
    c.dn->action = kind == 0 ? A_ATTACK_0 : (kind == 1 ? A_ATTACK : A_TAIL_ATTACK);
    c.dn->mode = 0;
    c.dn->timer[2] = static_cast<s16>(g_rng.range(nl::cfg::ATTACK_CD_MIN, nl::cfg::ATTACK_CD_MAX));
    c.dn->actor.current.angle.y = c.toLink;
}

void enter_guard(const Ctx& c, React reason) {
    c.dn->action = NA_GUARD;
    c.dn->mode = 0;
    c.e->stateT = 0;
    c.e->guardReason = reason;
}

// Leaving a guard/dodge: counter-attack if it makes sense, otherwise go back to the chase.
void finish_defense(const Ctx& c, float riposteChance) {
    if (c.dist < nl::cfg::ATTACK_RANGE + 100.0f && g_rng.unit() < riposteChance) {
        start_attack(c, false);
    } else {
        go_pursue(c);
    }
}

// ---- State controllers (run in the pre-hook; the original does physics afterwards) -----------
void run_guard(const Ctx& c) {
    e_dn_class* dn = c.dn;
    Ext& e = *c.e;
    if (dn->mode == 0) {
        dn->mode = 1;
        e.stateT = 0;
    }
    ++e.stateT;
    dn->guard_flag = 1;  // shield hitbox on

    dn->actor.current.angle.y =
        nl::chase_angle(static_cast<int16_t>(dn->actor.current.angle.y), c.toLink, 1, nl::cfg::GUARD_TURN_STEP);

    // While you aim at it from a distance it keeps closing in. It RUNS to do that (legs animate,
    // shield hitbox on); the held-shield pose is used whenever it is standing still, and the moment
    // an arrow is released. Never slide in the shield pose: the pose has no leg animation.
    bool advance = e.guardReason == React::Aim && c.dist > nl::cfg::ADVANCE_STOP && !c.d.arrow;
    if (advance && g_haveGake && LizGake::g_orig(dn, 120.0f, 0) != 0) {
        advance = false;  // ledge ahead: hold position
    }
    if (advance) {
        dn->actor.speedF = nl::approach(dn->actor.speedF, nl::cfg::ADVANCE_SPEED, 8.0f, 14.0f);
        if (dn->anm_no != ANM_RUN) {
            play_anim(dn, ANM_RUN, J3DFrameCtrl::EMode_LOOP, 4.0f, 1.0f);
        }
        dn->anm_p->setPlaySpeed(std::clamp(static_cast<float>(dn->actor.speedF) / 40.0f, 0.5f, 1.4f));
    } else {
        dn->actor.speedF = 0.0f;
        if (dn->anm_no != ANM_GUARD) {
            // Snap straight to the held-shield pose: no blend, no hop, no wind-up.
            play_anim(dn, ANM_GUARD, J3DFrameCtrl::EMode_NONE, 0.0f, 0.0f, 8.0f);
        }
    }

    if (c.d.want == React::Block || c.d.want == React::Dodge) {
        e.guardReason = React::Block;
    }
    if ((c.d.want == React::None && e.stateT >= nl::cfg::MIN_GUARD_HOLD) ||
        e.stateT > nl::cfg::GUARD_MAX_HOLD) {
        finish_defense(c, e.guardReason == React::Block ? nl::cfg::RIPOSTE_CHANCE : 0.0f);
    }
}

// Parried: it recoils with the original's hit-wobble and is completely open for a short window.
void run_parried(const Ctx& c) {
    e_dn_class* dn = c.dn;
    Ext& e = *c.e;
    if (dn->mode == 0) {
        play_anim(dn, ANM_WAIT_01, J3DFrameCtrl::EMode_LOOP, 2.0f, 0.8f);
        dn->field_0x74c = 150.0f;  // the original's flinch wobble, as a visible stagger
        dn->actor.speedF = nl::cfg::PARRY_RECOIL_SPEED;
        dn->mode = 1;
        e.parryT = nl::cfg::PARRY_FRAMES;
    }
    --e.parryT;
    dn->actor.current.angle.y =
        nl::chase_angle(static_cast<int16_t>(dn->actor.current.angle.y), c.toLink, 2, 0x1000);
    dn->actor.speedF = nl::approach(dn->actor.speedF, 0.0f, 3.0f, 3.0f);
    if (e.parryT <= 0) {
        e.parryCd = nl::cfg::PARRY_COOLDOWN;
        dn->timer[2] = 8;
        go_pursue(c);
    }
}

void run_dodge(const Ctx& c) {
    e_dn_class* dn = c.dn;
    Ext& e = *c.e;
    if (dn->mode == 0) {
        const uint8_t ic = e.th.instCut;
        const bool jumpish = ic == nl::cut::JUMP || ic == nl::cut::HEAD_JUMP ||
                             ic == nl::cut::LARGE_JUMP_INIT || ic == nl::cut::LARGE_JUMP ||
                             ic == nl::cut::LARGE_JUMP_FINISH || ic == nl::cut::MORTAL_DRAW_A ||
                             ic == nl::cut::MORTAL_DRAW_B;
        int16_t dir;
        bool back = jumpish || g_rng.unit() < 0.5f;
        if (back && g_haveGake && LizGake::g_orig(dn, 150.0f, 1) != 0) {
            back = false;  // ledge behind us: sidestep instead of hopping off it
        }
        if (back) {
            dir = static_cast<int16_t>(c.toLink + 0x8000);  // straight back
        } else {
            dir = static_cast<int16_t>(c.toLink + ((g_rng.next() & 1u) ? 0x4000 : -0x4000));  // sidestep
        }
        e.dodgeVX = fsin(dir) * nl::cfg::DODGE_SPEED;
        e.dodgeVZ = fcos(dir) * nl::cfg::DODGE_SPEED;
        dn->actor.speed.y = nl::cfg::DODGE_HOP;
        play_anim(dn, ANM_JUMP_B, J3DFrameCtrl::EMode_NONE, 1.0f, 1.0f);
        dn->mode = 1;
        e.stateT = 0;
    }
    ++e.stateT;
    dn->actor.current.angle.y =
        nl::chase_angle(static_cast<int16_t>(dn->actor.current.angle.y), c.toLink, 1, nl::cfg::GUARD_TURN_STEP);
    dn->actor.speedF = 0.0f;  // the hop itself is a position nudge applied after the original runs

    if (e.stateT <= nl::cfg::DODGE_FRAMES) {
        const float k = 1.0f - 0.5f * (static_cast<float>(e.stateT) / static_cast<float>(nl::cfg::DODGE_FRAMES));
        e.nudgeX = e.dodgeVX * k;
        e.nudgeZ = e.dodgeVZ * k;
    }

    const bool landed = dn->objacch.ChkGroundHit() != 0;
    if (e.stateT >= nl::cfg::DODGE_FRAMES && (landed || e.stateT >= nl::cfg::DODGE_FRAMES + 10)) {
        if (c.d.want != React::None) {
            enter_guard(c, React::Block);  // the attack is still going: shield for the rest of it
        } else {
            finish_defense(c, nl::cfg::DODGE_RIPOSTE_CHANCE);
        }
    }
}

void run_pursue(const Ctx& c) {
    e_dn_class* dn = c.dn;
    Ext& e = *c.e;
    const LinkInfo& L = *c.L;
    const float mx = dn->actor.current.pos.x;
    const float mz = dn->actor.current.pos.z;

    if (dn->mode == 0) {
        if (dn->anm_no != ANM_RUN) {
            play_anim(dn, ANM_RUN, J3DFrameCtrl::EMode_LOOP, 5.0f, 1.0f);
        }
        dn->mode = 1;
        e.stateT = 0;
    }
    ++e.stateT;
    if (e.leapCd > 0) {
        --e.leapCd;
    }

    // Aim where you WILL be, not where you are.
    const float lead = std::min(c.dist / nl::cfg::RUN_SPEED, nl::cfg::LEAD_MAX_FRAMES);
    int16_t aim = nl::angle_of(L.x + L.vx * lead - mx, L.z + L.vz * lead - mz);
    if (L.v.aiming && c.dist > 450.0f) {
        // Zig-zag so a bow aimed at it is hard to line up.
        aim = static_cast<int16_t>(aim + static_cast<int>(std::sin(static_cast<float>(e.stateT) * nl::cfg::WEAVE_RATE) * nl::cfg::WEAVE_AMPL));
    }

    dn->actor.current.angle.y =
        nl::chase_angle(static_cast<int16_t>(dn->actor.current.angle.y), aim, 2, nl::cfg::TURN_STEP);
    const int16_t err = nl::angle_diff(aim, static_cast<int16_t>(dn->actor.current.angle.y));
    dn->actor.speedF = nl::approach(dn->actor.speedF, nl::desired_speed(c.dist, err), nl::cfg::ACCEL_UP, nl::cfg::ACCEL_DOWN);

    // Run while closing, plain stance while holding lunge distance.
    const bool wantRun = dn->actor.speedF > 5.0f || c.dist > nl::cfg::SPACING_INNER + 60.0f;
    if (wantRun && dn->anm_no != ANM_RUN) {
        play_anim(dn, ANM_RUN, J3DFrameCtrl::EMode_LOOP, 3.0f, 1.0f);
    } else if (!wantRun && dn->anm_no != ANM_WAIT_01) {
        play_anim(dn, ANM_WAIT_01, J3DFrameCtrl::EMode_LOOP, 4.0f, 1.6f);
    }
    if (dn->anm_no == ANM_RUN) {
        dn->anm_p->setPlaySpeed(std::clamp(static_cast<float>(dn->actor.speedF) / 40.0f, 0.5f, 1.7f));
    }
    dn->unk_timer_1 = 40;  // never give up the chase

    // Ledge ahead: let the original code leap the gap, exactly as it does itself.
    if (g_haveGake && LizGake::g_orig(dn, 50.0f, 0) != 0) {
        dn->action = A_GAKEJUMP;
        dn->mode = 0;
        dn->invulnerability_timer = 100;
        dn->actor.speedF = 0.0f;
        return;
    }

    // Stuck behind a wall: hop over it (original jump).
    if (dn->objacch.ChkWallHit() != 0 && c.dist > 220.0f) {
        ++e.stuck;
    } else if (e.stuck > 0) {
        --e.stuck;
    }
    if (e.stuck > nl::cfg::STUCK_FRAMES) {
        e.stuck = 0;
        dn->actor.current.angle.y = c.toLink;
        dn->action = A_JUMP;
        dn->mode = 0;
        return;
    }

    // Gap closer: you back off, it leaps (original jump, aimed at you).
    if (c.dist > nl::cfg::LEAP_MIN && c.dist < nl::cfg::LEAP_MAX && e.leapCd == 0 &&
        dn->objacch.ChkGroundHit() != 0 && g_rng.unit() < nl::cfg::LEAP_CHANCE) {
        e.leapCd = nl::cfg::LEAP_COOLDOWN;
        dn->actor.current.angle.y = c.toLink;
        dn->actor.speedF = 0.0f;
        dn->action = A_JUMP;
        dn->mode = 0;
        dbg("NL: gap-closer leap at dist {:.0f}", c.dist);
        return;
    }

    // Attack as soon as it is in range, facing you, and off cooldown. Holding your own shield
    // up for a long time earns you the tail sweep.
    const bool facing = nl::abs_angle(nl::angle_diff(c.toLink, static_cast<int16_t>(dn->actor.shape_angle.y))) < 0x3800;
    if (c.dist < nl::cfg::ATTACK_RANGE && facing && dn->timer[2] == 0) {
        start_attack(c, e.th.linkGuardFrames > nl::cfg::TURTLE_FRAMES);
    }
}

// ---- Hooks ----------------------------------------------------------------------------------
HookAction on_action_pre(ModContext*, void* args, void*, void*) {
    e_dn_class* dn = mods::arg<e_dn_class*>(args, 0);
    if (dn == nullptr) {
        return HOOK_CONTINUE;
    }
    ++g_tick;
    Ext& e = ext_for(dn);
    e.managed = false;
    e.preAction = dn->action;

    // Scripted Lizalfos, dying Lizalfos, and states the original owns completely.
    if (dn->status != 0 || dn->actor.health <= 0 || frozen(dn->action)) {
        return HOOK_CONTINUE;
    }
    const bool steer = !committed(dn->action);
    LinkInfo L;
    if (!read_link(L)) {
        return HOOK_CONTINUE;
    }
    e.managed = true;

    const float mx = dn->actor.current.pos.x;
    const float mz = dn->actor.current.pos.z;
    const float dx = L.x - mx;
    const float dz = L.z - mz;
    const float dist = std::sqrt(dx * dx + dz * dz);
    const int16_t toLink = nl::angle_of(dx, dz);

    // Always "learned": it already knows how to guard every one of your attack types, and
    // it never has the post-hit window where it could not guard.
    dn->learn |= static_cast<int>(nl::cfg::LEARN_MASK);
    if (dn->action != NA_PARRIED) {
        dn->field_0x74c = 0.0f;
    }

    // Full rage: an idle Lizalfos that can see this far goes straight for you, no roar.
    if (steer && (dn->action == A_NORMAL || dn->action == A_STAY || dn->action == A_OTOREAC)) {
        if (dist < std::min(nl::cfg::AGGRO_RANGE, static_cast<float>(dn->pl_range))) {
            dn->action = A_FIGHT_RUN;
            dn->mode = 0;
            dn->timer[0] = 60;
        }
    }

    // What are you doing to it right now?
    nl::Obs obs;
    obs.dist = dist;
    obs.inAimCone = nl::abs_angle(nl::angle_diff(nl::angle_of(mx - L.x, mz - L.z), L.facing)) < nl::cfg::AIM_CONE;
    nl::Decision d = nl::observe(e.th, L.v, obs, g_rng);
    if (e.th.newInstance) {
        dbg("NL: attack cut=0x{:02X} dist={:.0f} -> {} {}", static_cast<unsigned>(e.th.instCut), dist,
            e.th.meleeReact == React::Dodge ? "DODGE" : "BLOCK", e.th.leaked ? "(LEAK: gets through)" : "(protected)");
    }

    // Parry: its attack hit your shield, or you shield-bashed it mid-attack. It recoils and is
    // wide open for PARRY_FRAMES. (The collision flags are last frame's, as the original reads them.)
    if (steer) {
        if (e.parryCd > 0) {
            --e.parryCd;
        }
        if (e.parryCd == 0 && is_attack(dn->action)) {
            const bool hitShield = L.v.guarding && dn->at_sph.ChkAtHit() != 0 && dn->at_sph.ChkAtShieldHit() != 0;
            const bool bashed = L.v.cut == nl::cut::GUARD_ATTACK && dist < nl::cfg::PARRY_BASH_RANGE;
            if (hitShield || bashed) {
                dn->action = NA_PARRIED;
                dn->mode = 0;
                e.stateT = 0;
                dbg("NL: PARRIED ({}) - open for {} frames", hitShield ? "its attack hit your shield" : "your shield bash",
                    static_cast<int>(nl::cfg::PARRY_FRAMES));
            }
        }
        if (dn->action == NA_PARRIED) {
            d.want = React::None;     // no guard, no dodge, no protection while it is open
            d.protect = false;
        }
    }

    Ctx c{dn, &e, &L, dist, toLink, d};

    if (steer) {
        // Original guard becomes the instant guard.
        if (dn->action == A_GUARD) {
            enter_guard(c, React::Block);
        }
        // Close combat is ours; long-range running stays with the original (it knows the terrain).
        if (dn->action == A_FIGHT_RUN && dist < nl::cfg::PURSUE_TAKEOVER) {
            dn->action = NA_PURSUE;
            dn->mode = 0;
            e.stateT = 0;
        } else if (dn->action == NA_PURSUE && dist > nl::cfg::PURSUE_RELEASE) {
            dn->action = A_FIGHT_RUN;
            dn->mode = 0;
        }
        if (dn->action == A_FIGHT_RUN) {
            dn->unk_timer_1 = 40;  // never give up the chase
        }

        // React to the attack (interrupts attacks and the chase, never a leap or a knockback).
        if (d.want == React::Dodge) {
            if (dn->action != NA_DODGE) {
                if (e.instDodged != e.th.instanceId) {
                    e.instDodged = e.th.instanceId;
                    dn->action = NA_DODGE;
                    dn->mode = 0;
                    e.stateT = 0;
                } else if (dn->action != NA_GUARD) {
                    enter_guard(c, React::Block);
                }
            }
        } else if (d.want == React::Block) {
            if (dn->action != NA_GUARD && dn->action != NA_DODGE) {
                enter_guard(c, React::Block);
            }
        } else if (d.want == React::Aim && dist < nl::cfg::AIM_SHIELD_RANGE) {
            if (dn->action == NA_PURSUE || dn->action == A_FIGHT_RUN) {
                enter_guard(c, React::Aim);
            }
        }

        // Our controllers.
        switch (dn->action) {
        case NA_GUARD:
            run_guard(c);
            break;
        case NA_DODGE:
            run_dodge(c);
            break;
        case NA_PURSUE:
            run_pursue(c);
            break;
        case NA_PARRIED:
            run_parried(c);
            break;
        default:
            break;
        }
    }  // steer

    // Protection: while an attack is incoming and its roll said "defended", the body cannot be
    // hit. (Hits are detected one frame late, and a hit is only processed while this is 0, so
    // holding it through the attack cancels the damage cleanly.)
    if (d.protect) {
        if (dn->invulnerability_timer < 2) {
            dn->invulnerability_timer = 2;
        }
        dn->guard_flag = 1;
    }

    if (dn->action != e.lastLoggedAction) {
        if (dn->action >= NA_GUARD) {
            dbg("NL: state -> {}", dn->action == NA_GUARD ? "GUARD" : (dn->action == NA_DODGE ? "DODGE" : (dn->action == NA_PARRIED ? "PARRIED" : "PURSUE")));
        }
        e.lastLoggedAction = dn->action;
    }
    return HOOK_CONTINUE;
}

void revert_stagger(e_dn_class* dn, Ext& e) {
    // A hit landed (health already went down) but we refuse the flinch / knockdown / hookshot pull.
    const bool parried = e.parryT > 0;
    dn->invulnerability_timer = 6;  // the normal short invulnerability after a hit
    dn->field_0x708 = 0.0f;
    dn->field_0x704 = 0.0f;
    dn->field_0x70e = 0;
    dn->field_0x710 = 0;
    dn->field_0x7e8 = 0.0f;
    if (dn->actor.speed.y > 12.0f) {
        dn->actor.speed.y = 0.0f;
    }
    if (parried) {
        // Still inside the parry window: stay recoiled so you can keep hitting it.
        dn->action = NA_PARRIED;
        dn->mode = 1;
        dn->field_0x74c = 150.0f;
        play_anim(dn, ANM_WAIT_01, J3DFrameCtrl::EMode_LOOP, 2.0f, 0.8f);
    } else {
        dn->action = A_FIGHT_RUN;
        dn->mode = 0;
        dn->field_0x74c = 0.0f;
        // Replace the knocked-back pose the original just started (it has not been drawn yet).
        play_anim(dn, ANM_RUN, J3DFrameCtrl::EMode_LOOP, 2.0f, 1.0f);
    }
    e.stateT = 0;
    dbg("NL: hit landed, flinch cancelled (health {})", static_cast<int>(dn->actor.health));
}

void on_action_post(ModContext*, void* args, void*, void*) {
    e_dn_class* dn = mods::arg<e_dn_class*>(args, 0);
    if (dn == nullptr) {
        return;
    }
    Ext& e = ext_for(dn);
    if (!e.managed) {
        return;
    }
    e.managed = false;
    const s16 a = dn->action;

    // Unstoppable: damage lands, the stagger does not. Never cancel a killing blow.
    const bool wasStagger = e.preAction == A_S_DAMAGE || e.preAction == A_DAMAGE || e.preAction == A_DRAWBACK;
    if (!wasStagger && (a == A_S_DAMAGE || a == A_DAMAGE || a == A_DRAWBACK) && dn->actor.health > 0) {
        revert_stagger(dn, e);
    }

    // Movement nudges (dodge hops). Collision is resolved by the game right after this.
    if (e.nudgeX != 0.0f || e.nudgeZ != 0.0f) {
        dn->actor.current.pos.x += e.nudgeX;
        dn->actor.current.pos.z += e.nudgeZ;
        e.nudgeX = 0.0f;
        e.nudgeZ = 0.0f;
    }

    // The original long-range run is a bit faster.
    if (dn->action == A_FIGHT_RUN && dn->mode == 1 && dn->actor.speedF > 20.0f) {
        const int16_t ang = static_cast<int16_t>(dn->actor.current.angle.y);
        dn->actor.current.pos.x += fsin(ang) * nl::cfg::FAR_RUN_BONUS;
        dn->actor.current.pos.z += fcos(ang) * nl::cfg::FAR_RUN_BONUS;
    }

    // Faster attack animations (applied once, when the attack starts).
    if (is_attack(a) && e.preAction != a) {
        dn->anm_p->setPlaySpeed(dn->anm_p->getPlaySpeed() * nl::cfg::ATTACK_SPEED_MULT);
    }
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    ModResult result = mods::hook::add_pre<LizAction>(on_action_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result,
                               "Nightmare Lizalfos: could not hook the Lizalfos action() function");
    }
    result = mods::hook::add_post<LizAction>(on_action_post);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "Nightmare Lizalfos: could not add the post hook");
    }
    g_haveGake = mods::hook::install<LizGake>() == MOD_OK;
    g_rng.s ^= 0xA5A5F00Du;
    mods::log::info("Nightmare Lizalfos: active (ledge check {})",
                    g_haveGake ? "hooked" : "unavailable - ledge avoidance off");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    // Put any Lizalfos that was in one of our custom states back on the original AI, so
    // disabling the mod never leaves one frozen. Only entries seen in the last few frames
    // are touched, so a freed actor is never written to.
    for (Ext& x : g_ext) {
        if (x.used && x.who != nullptr && g_tick - x.lastSeen < 8 && x.who->action >= NA_GUARD) {
            x.who->action = A_FIGHT_RUN;
            x.who->mode = 0;
        }
    }
    return MOD_OK;
}
}
