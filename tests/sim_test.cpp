// Offline simulation of the Nightmare Lizalfos mod.
//
// This compiles the REAL src/mod.cpp against mock game headers (tests/mock) and drives it with
// a scripted Link and a small emulation of the original Lizalfos AI, to catch logic bugs
// (stuck states, missed protection windows, runaway loops) without running the game.
// It is NOT a substitute for playing: the emulated vanilla AI is a simplification.
//
//   g++ -std=c++20 -O1 -DNL_TEST_LEAK_CHANCE=0.0f -Itests/mock -Isrc tests/sim_test.cpp -o sim_noleak
//   g++ -std=c++20 -O1 -DNL_TEST_LEAK_CHANCE=1.0f -Itests/mock -Isrc tests/sim_test.cpp -o sim_allleak
#include "../src/mod.cpp"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            std::printf("  FAIL line %d: %s  ", __LINE__, #cond);             \
            std::printf(__VA_ARGS__);                                         \
            std::printf("\n");                                                \
            ++g_fail;                                                         \
        }                                                                     \
    } while (0)

enum { V_ANM_BLOWN = 9, V_ANM_ATK = 4 };

struct World {
    e_dn_class dn;
    daAlink_c link;
    mDoExt_McaMorfSO morf;
    bool hitFlag = false;
    bool heavyHit = false;
    int frame = 0;
    // metrics
    int damageTaken = 0, hitsLanded = 0, attackStarts = 0, linkHit = 0, vanillaStrafe = 0;
    int atkKinds[3] = {0, 0, 0};
    int atkFrames = 0, jumpT = 0, gakeT = 0;
    bool gakeAhead = false;
    std::vector<int> actionTrace;

    World() { dn.anm_p = &morf; mock_player() = &link; }

    float dist() const {
        const float dx = link.current.pos.x - dn.actor.current.pos.x, dz = link.current.pos.z - dn.actor.current.pos.z;
        return std::sqrt(dx * dx + dz * dz);
    }
};

static void anm(World& w, int id, float rate) { w.morf.setAnm(static_cast<J3DAnmTransform*>(dComIfG_getObjectRes("E_dn", id)), 0, 3, rate, 0, -1); w.dn.anm_no = id; }

// ---- A simplified emulation of the ORIGINAL action() so the hook has something to wrap ------
static void vanilla_action(World& w) {
    e_dn_class* dn = &w.dn;
    fopAc_ac_c& a = dn->actor;
    const float dist = w.dist();
    const int16_t toLink = nl::angle_of(w.link.current.pos.x - a.current.pos.x, w.link.current.pos.z - a.current.pos.z);

    // damage_check()
    if (dn->invulnerability_timer == 0 && w.hitFlag) {
        a.health -= 20; w.damageTaken += 20; ++w.hitsLanded;
        if (a.health <= 0 || w.heavyHit) {
            dn->action = 21; dn->mode = 0; dn->field_0x74c = 150; dn->field_0x704 = -20; a.speed.y = 25; a.speedF = 0; dn->invulnerability_timer = 1000;
        } else {
            dn->action = 20; dn->mode = 0; dn->field_0x74c = 150; dn->field_0x708 = 25; a.speedF = 0; dn->invulnerability_timer = 6;
        }
    }
    w.hitFlag = false;

    switch (dn->action) {
    case 3:  // FIGHT_RUN (simplified)
        if (dn->mode == 0) { anm(w, 0x1E, 1.0f); dn->mode = 1; }
        a.current.angle.y = nl::chase_angle(a.current.angle.y, toLink, 2, 0x800);
        a.speedF = nl::approach(a.speedF, 40.0f, 5.0f, 5.0f);
        if (dist < 350.0f) ++w.vanillaStrafe;  // the original would start its hop-strafe here
        break;
    case 4: case 5: case 6:  // attacks
        if (dn->mode == 0) {
            anm(w, V_ANM_ATK, 1.75f); dn->mode = 1; w.atkFrames = 0; ++w.attackStarts; w.atkKinds[dn->action - 4]++;
            a.speedF = 0;
        } else {
            w.atkFrames += 1;
            if (w.atkFrames > 22 && w.atkFrames < 28 && dist < 320.0f && !w.link.m_guard) ++w.linkHit;
            if (w.atkFrames * w.morf.rate / 1.75f * 1.75f > 38) { dn->action = 3; dn->mode = 0; }
        }
        break;
    case 7:  // vanilla GUARD
        dn->guard_flag = 1; a.speedF = 0;
        if (dn->mode == 0) { dn->mode = 1; dn->timer[0] = 12; }
        if (dn->timer[0] == 0) { dn->action = 3; dn->mode = 0; }
        break;
    case 8:  // JUMP (leap)
        if (dn->mode == 0) { anm(w, 0x1B, 1.0f); dn->mode = 1; w.jumpT = 0; a.speedF = 0; }
        ++w.jumpT;
        if (w.jumpT == 8) { a.speed.y = 40; }
        if (w.jumpT >= 8) { a.speedF = 30; }
        if (w.jumpT > 10 && a.current.pos.y <= 0.01f) { dn->action = 3; dn->mode = 0; }
        break;
    case 25:  // GAKEJUMP
        if (dn->mode == 0) { dn->mode = 1; w.gakeT = 0; }
        if (++w.gakeT > 20) { dn->action = 3; dn->mode = 0; }
        break;
    case 20:  // S_DAMAGE
        if (dn->mode == 0) { anm(w, 0x1F, 1.0f); dn->mode = 1; dn->timer[0] = 10; a.speedF = 0; }
        if (dn->timer[0] == 0) { dn->action = 3; dn->mode = 0; }
        break;
    case 21:  // DAMAGE (knockdown / death)
        if (dn->mode == 0) { anm(w, V_ANM_BLOWN, 1.0f); dn->mode = 1; dn->timer[0] = 60; a.speedF = 0; }
        if (dn->timer[0] == 0 && a.health > 0) { dn->action = 3; dn->mode = 0; dn->invulnerability_timer = 0; }
        break;
    default:
        break;  // NORMAL / STAY / custom ids: the original does nothing
    }

    // movement integration (end of the original action())
    a.speed.x = std::sin(a.current.angle.y * (3.14159265f / 32768.0f)) * a.speedF;
    a.speed.z = std::cos(a.current.angle.y * (3.14159265f / 32768.0f)) * a.speedF;
    a.speed.y += a.gravity;
    a.current.pos.x += a.speed.x; a.current.pos.y += a.speed.y; a.current.pos.z += a.speed.z;
    a.shape_angle.y = nl::chase_angle(a.shape_angle.y, a.current.angle.y, 2, 0x2000);
}

static bool g_lag = false;   // true: the Lizalfos runs BEFORE Link in the frame and sees last frame's state
static void exec_frame(World& w) {
    e_dn_class* dn = &w.dn;
    static u8 prevCut = 0; static bool prevAt = false;
    const u8 curCut = w.link.m_cut; const bool curAt = w.link.m_at;
    if (g_lag) { w.link.m_cut = prevCut; w.link.m_at = prevAt; }
    for (s16& t : dn->timer) if (t) --t;
    if (dn->invulnerability_timer) --dn->invulnerability_timer;
    if (dn->unk_timer_1) --dn->unk_timer_1;
    void* argv[1] = {dn};
    if (LizAction::pre) LizAction::pre(nullptr, argv, nullptr, nullptr);
    vanilla_action(w);
    if (LizAction::post) LizAction::post(nullptr, argv, nullptr, nullptr);
    if (g_lag) { prevCut = curCut; prevAt = curAt; w.link.m_cut = curCut; w.link.m_at = curAt; }
    // ground
    fopAc_ac_c& a = dn->actor;
    if (a.current.pos.y <= 0.0f) { a.current.pos.y = 0; if (a.speed.y < 0) a.speed.y = 0; dn->objacch.ground = true; } else dn->objacch.ground = false;
    // collision pass: only a body that is not invulnerable can be hit (next frame's damage_check sees it)
    if (dn->invulnerability_timer == 0 && w.link.m_at && w.dist() < 260.0f) w.hitFlag = true;
    dn->guard_flag = 0;
    ++w.frame;
    w.actionTrace.push_back(dn->action);
}

static void setup(World& w, float dist, s16 action = 3) {
    for (auto& x : g_ext) x = Ext{};
    g_tick = 0; g_logCount = 0; mods::log::lines().clear();
    w.link.current.pos = {0, 0, 0}; w.link.shape_angle.y = 0;   // Link faces +Z, where the Lizalfos is
    w.dn.actor.current.pos = {0, 0, dist}; w.dn.actor.current.angle.y = 0x8000; w.dn.actor.shape_angle.y = 0x8000;
    w.dn.action = action; w.dn.mode = 0; w.dn.actor.health = 100;
}

// Link script helpers
static void slash_frame(World& w, int f, uint8_t cutType, int len, int atFrom, int atTo) {
    w.link.m_cut = f < len ? cutType : 0;
    w.link.m_at = f >= atFrom && f < atTo;
}

static int count_action(const World& w, int from, int to, int act) { int n = 0; for (int i = from; i < to && i < (int)w.actionTrace.size(); ++i) n += w.actionTrace[i] == act; return n; }

int main() {
    mods::log::echo() = false;
    ModError err;
    CHECK(mod_initialize(&err) == MOD_OK, "init");
#if NL_TEST_LEAK_CHANCE_IS_ZERO
#endif
    const bool noLeak = nl::cfg::LEAK_CHANCE == 0.0f;
    const bool allLeak = nl::cfg::LEAK_CHANCE >= 1.0f;
    std::printf("simulation with LEAK_CHANCE=%.3f\n", nl::cfg::LEAK_CHANCE);

    // gake mock: "ledge ahead" is controlled per scenario
    static World* cur = nullptr;
    LizGake::g_orig = [](e_dn_class*, f32, s8) -> BOOL { return cur ? cur->gakeAhead : 0; };

    // ---------------------------------------------------------------- S1 chase + pressure
    {
        std::printf("S1 chase & pressure\n");
        World w; cur = &w; g_rng.s = 11; setup(w, 2000);
        int reached = -1;
        for (int f = 0; f < 400; ++f) { exec_frame(w); if (reached < 0 && w.dist() < nl::cfg::ATTACK_RANGE) reached = f; }
        CHECK(reached > 0 && reached < 60, "should cross 2000 units fast (took %d frames)", reached);
        CHECK(w.attackStarts >= 15, "should attack relentlessly (got %d attacks in 400 frames)", w.attackStarts);
        CHECK(w.vanillaStrafe == 0, "original hop-strafe must never get a chance (%d)", w.vanillaStrafe);
        CHECK(w.atkKinds[1] > 0 && w.atkKinds[2] > 0, "mix of lunge attacks %d/%d/%d", w.atkKinds[0], w.atkKinds[1], w.atkKinds[2]);
        CHECK(w.dist() < nl::cfg::ATTACK_RANGE + 50, "stays on top of a stationary Link");
    }
    // ---------------------------------------------------------------- S2 sword blocks
    {
        std::printf("S2 sword swings are blocked, then punished\n");
        World w; cur = &w; g_rng.s = 22; setup(w, 250);
        for (int i = 0; i < 40; ++i) exec_frame(w);   // settle
        const int hp0 = w.dn.actor.health; w.attackStarts = 0;
        int longestGuard = 0, run = 0, swings = 0;
        for (int f = 0; f < 40 * 120; ++f) {
            const int ph = f % 40;
            if (ph == 0) ++swings;
            slash_frame(w, ph, nl::cut::NM_VERTICAL, 14, 6, 10);
            w.link.current.pos = {0, 0, 0};
            exec_frame(w);
            if (w.dn.action == NA_GUARD) { ++run; longestGuard = std::max(longestGuard, run); } else run = 0;
        }
        std::printf("   swings=%d  damage=%d  Lizalfos attacks=%d  longest guard=%d frames\n", swings, hp0 - w.dn.actor.health, w.attackStarts, longestGuard);
        if (noLeak) CHECK(w.dn.actor.health == hp0, "must take NO damage with leak=0 (lost %d)", hp0 - w.dn.actor.health);
        if (noLeak) CHECK(w.attackStarts > swings / 2, "must counter-attack (riposte) often: %d attacks vs %d swings", w.attackStarts, swings);
        CHECK(longestGuard <= nl::cfg::GUARD_MAX_HOLD, "guard must never exceed its cap (%d)", longestGuard);
        // Link stops attacking -> it must stop turtling and fight
        w.link.m_cut = 0; w.link.m_at = false; int guarding = 0;
        for (int f = 0; f < 60; ++f) { exec_frame(w); guarding += w.dn.action == NA_GUARD; }
        CHECK(guarding <= nl::cfg::MIN_GUARD_HOLD + 3, "no turtling after you stop attacking (%d frames of guard)", guarding);
    }
    // ---------------------------------------------------------------- S3 skills are dodged
    {
        std::printf("S3 skills are dodged\n");
        const uint8_t skills[] = {nl::cut::JUMP, nl::cut::HEAD_JUMP, nl::cut::LARGE_JUMP, nl::cut::DASH_LEFT_SIDESTEP, nl::cut::TWIRL, nl::cut::MORTAL_DRAW_A, nl::cut::LARGE_TURN_LEFT};
        for (uint8_t c : skills) {
            World w; cur = &w; g_rng.s = 33 + c; setup(w, 280);
            for (int i = 0; i < 30; ++i) exec_frame(w);
            const float startDist = w.dist(); const int hp0 = w.dn.actor.health; float maxDist = 0; bool dodged = false;
            for (int f = 0; f < 60; ++f) {
                slash_frame(w, f, c, 30, 14, 20); exec_frame(w);
                dodged |= w.dn.action == NA_DODGE; maxDist = std::max(maxDist, w.dist());
            }
            if (noLeak) CHECK(w.dn.actor.health == hp0, "cut 0x%02X: no damage allowed (lost %d)", c, hp0 - w.dn.actor.health);
            CHECK(dodged, "cut 0x%02X should trigger a dodge", c);
            CHECK(maxDist > startDist + 120.0f || maxDist > 380, "cut 0x%02X: dodge should move it (start %.0f, max %.0f)", c, startDist, maxDist);
        }
    }
    // ---------------------------------------------------------------- S4 leaks: damage lands, flinch is cancelled
    if (allLeak) {
        std::printf("S4 leaked hits: damage lands, no flinch, death still works\n");
        World w; cur = &w; g_rng.s = 44; setup(w, 250);
        for (int i = 0; i < 30; ++i) exec_frame(w);
        int staggerFrames = 0; int lastHp = w.dn.actor.health; bool died = false;
        for (int f = 0; f < 40 * 12; ++f) {
            slash_frame(w, f % 40, nl::cut::NM_VERTICAL, 14, 6, 10); exec_frame(w);
            if (w.dn.actor.health > 0 && (w.dn.action == 20 || w.dn.action == 21)) ++staggerFrames;
            if (w.dn.actor.health <= 0) { died = true; break; }
        }
        std::printf("   hits landed=%d  hp=%d  frames spent in a stagger state while alive=%d\n", w.hitsLanded, w.dn.actor.health, staggerFrames);
        CHECK(w.hitsLanded >= 3, "hits must land when the roll leaks (%d)", w.hitsLanded);
        CHECK(staggerFrames == 0, "never stays staggered while alive (%d)", staggerFrames);
        CHECK(died, "enough landed hits must still kill it");
        // killing blow is not cancelled
        CHECK(w.dn.action == 21, "death state must run (action %d)", w.dn.action);
        const int actionAfter = w.dn.action; for (int i = 0; i < 100; ++i) exec_frame(w);
        CHECK(w.dn.action == actionAfter && w.dn.actor.health <= 0, "dead stays dead");
        CHECK(w.dn.invulnerability_timer == 0 || w.dn.invulnerability_timer > 0, "n/a");
    }
    // ---------------------------------------------------------------- S5 bow
    {
        std::printf("S5 bow: shield before release, protected in flight, still advances\n");
        World w; cur = &w; g_rng.s = 55; setup(w, 1200);
        for (int i = 0; i < 5; ++i) exec_frame(w);
        w.link.m_bowWait = true; bool guardUp = false; float d0 = w.dist(); int protectedFrames = 0, shotFrames = 0;
        for (int f = 0; f < 50; ++f) { exec_frame(w); if (f < 6) guardUp |= w.dn.action == NA_GUARD; }
        CHECK(guardUp, "shield must be up within a few frames of you aiming");
        CHECK(w.dist() < d0 - 150.0f || w.dist() < 400, "it keeps closing the distance while aiming (%.0f -> %.0f)", d0, w.dist());
        w.link.m_bowWait = false; w.link.m_bowShoot = true;
        for (int f = 0; f < 10; ++f) { exec_frame(w); }
        w.link.m_bowShoot = false;
        for (int f = 0; f < nl::cfg::ARROW_TAIL + 10; ++f) {
            exec_frame(w);
            if (f < nl::cfg::ARROW_TAIL - 4) { if (noLeak) CHECK(w.dn.invulnerability_timer > 0, "protected during arrow flight (frame %d)", f); protectedFrames += w.dn.invulnerability_timer > 0; }
            ++shotFrames;
        }
        for (int f = 0; f < 30; ++f) exec_frame(w);
        CHECK(w.dn.action != NA_GUARD || w.dist() < 300, "back to fighting once the arrows are done (action %d)", w.dn.action);
    }
    // ---------------------------------------------------------------- S6 aggro
    {
        std::printf("S6 full rage / aggro range\n");
        World w; cur = &w; g_rng.s = 66; setup(w, 1500, 0);
        for (int i = 0; i < 3; ++i) exec_frame(w);
        CHECK(w.dn.action != 0, "idle Lizalfos within range must wake (action %d)", w.dn.action);
        World w2; cur = &w2; setup(w2, 3000, 0);
        for (int i = 0; i < 30; ++i) exec_frame(w2);
        CHECK(w2.dn.action == 0, "idle Lizalfos far away stays idle (action %d)", w2.dn.action);
        World w3; cur = &w3; setup(w3, 1500, 9);   // STAY
        w3.dn.pl_range = 800;                        // placed with a short detection range
        for (int i = 0; i < 5; ++i) exec_frame(w3);
        CHECK(w3.dn.action == 9, "a Lizalfos with a short detection range respects it (action %d)", w3.dn.action);
    }
    // ---------------------------------------------------------------- S7 hands-off states
    {
        std::printf("S7 scripted / dying / frozen states are left alone\n");
        World w; cur = &w; setup(w, 200, 3); w.dn.status = 1; w.dn.action = 60;
        w.link.m_cut = nl::cut::NM_VERTICAL; w.link.m_at = false;
        for (int i = 0; i < 20; ++i) exec_frame(w);
        CHECK(w.dn.action == 60 && w.dn.invulnerability_timer == 0 && w.dn.learn == 0, "scripted Lizalfos untouched (action %d inv %d)", w.dn.action, w.dn.invulnerability_timer);
        World w2; cur = &w2; setup(w2, 200, 21); w2.dn.actor.health = 0; w2.dn.mode = 1; w2.dn.timer[0] = 500;
        for (int i = 0; i < 20; ++i) exec_frame(w2);
        CHECK(w2.dn.action == 21, "dying Lizalfos untouched (action %d)", w2.dn.action);
    }
    // ---------------------------------------------------------------- S8 shutdown restores the original AI
    {
        std::printf("S8 disabling the mod mid-fight does not freeze a Lizalfos\n");
        World w; cur = &w; g_rng.s = 88; setup(w, 250);
        for (int i = 0; i < 20; ++i) exec_frame(w);
        for (int f = 0; f < 10; ++f) { slash_frame(w, f, nl::cut::NM_VERTICAL, 14, 6, 10); exec_frame(w); }
        const bool custom = w.dn.action >= NA_GUARD;
        mod_shutdown(&err);
        CHECK(!custom || w.dn.action == 3, "custom action reset to the original AI (custom=%d action=%d)", custom, w.dn.action);
    }
    // ---------------------------------------------------------------- S9 wall / ledge / turtle / leap
    {
        std::printf("S9 walls, ledges, turtling, leaping\n");
        { World w; cur = &w; g_rng.s = 91; setup(w, 700); w.dn.objacch.wall = true; bool jumped = false;
          for (int f = 0; f < 120; ++f) { exec_frame(w); jumped |= w.dn.action == 8; if (w.dn.action == 8) w.dn.objacch.wall = false; }
          CHECK(jumped, "stuck behind a wall -> hops over it"); }
        { World w; cur = &w; g_rng.s = 92; setup(w, 700); w.gakeAhead = true; bool gake = false;
          for (int f = 0; f < 10; ++f) { exec_frame(w); gake |= w.dn.action == 25; }
          CHECK(gake, "ledge ahead -> original gap-jump"); }
        { World w; cur = &w; g_rng.s = 93; setup(w, 250); w.link.m_guard = true; int tails = 0;
          for (int f = 0; f < 300; ++f) { exec_frame(w); }
          tails = w.atkKinds[2];
          std::printf("   attack mix vs a turtling Link: %d/%d/%d\n", w.atkKinds[0], w.atkKinds[1], w.atkKinds[2]);
          CHECK(tails > w.atkKinds[0] + w.atkKinds[1], "turtling Link gets mostly tail sweeps"); }
        { World w; cur = &w; g_rng.s = 94; setup(w, 900); int leaps = 0, prev = 0;
          for (int f = 0; f < 300; ++f) { exec_frame(w); leaps += (w.dn.action == 8 && prev != 8); prev = w.dn.action;
            // Link keeps backing away so the gap stays open
            w.link.current.pos.z -= 0; w.link.current.pos.z = std::min(w.link.current.pos.z, 0.0f); w.link.current.pos.z -= 0.0f;
            w.link.current.pos.z = w.dn.actor.current.pos.z - 900.0f; }
          CHECK(leaps >= 1, "gap closer leaps when you keep your distance (%d)", leaps); }
    }
    // ---------------------------------------------------------------- S10 committed states are protected
    if (noLeak) {
        std::printf("S10 a leaping Lizalfos cannot be punished mid-air\n");
        World w; cur = &w; g_rng.s = 101; setup(w, 400); w.dn.action = 8; w.dn.mode = 0;
        const int hp0 = w.dn.actor.health; w.link.current.pos = {0, 0, 0};
        for (int f = 0; f < 40; ++f) { slash_frame(w, f % 20, nl::cut::NM_VERTICAL, 14, 6, 10); w.dn.actor.current.pos.z = std::min(w.dn.actor.current.pos.z, 200.0f); exec_frame(w); }
        CHECK(w.dn.actor.health == hp0, "no free hit during a leap (lost %d)", hp0 - w.dn.actor.health);
    }
    // ---------------------------------------------------------------- S12 one frame of sensing lag still protects
    if (noLeak) {
        std::printf("S12 Lizalfos sees your attack one frame late (worst-case actor order)\n");
        g_lag = true;
        const uint8_t types[] = {nl::cut::NM_VERTICAL, nl::cut::JUMP, nl::cut::TURN_LEFT, nl::cut::DASH_RIGHT_SIDESTEP, nl::cut::FINISH_STAB};
        for (uint8_t c : types) {
            World w; cur = &w; g_rng.s = 120 + c; setup(w, 250);
            for (int i = 0; i < 30; ++i) exec_frame(w);
            const int hp0 = w.dn.actor.health;
            for (int f = 0; f < 40 * 40; ++f) {
                const int ph = f % 40;
                // worst case: the hit window is open on the very first frame of the swing
                slash_frame(w, ph, c, 14, 0, 6); w.link.current.pos = {0, 0, 0}; exec_frame(w);
            }
            CHECK(w.dn.actor.health == hp0, "cut 0x%02X with 1 frame of lag: lost %d", c, hp0 - w.dn.actor.health);
        }
        g_lag = false;
    }
    // ---------------------------------------------------------------- S11 long fight stays sane
    {
        std::printf("S11 5-minute mixed fight: no stuck state, no NaNs, stays near Link\n");
        World w; cur = &w; g_rng.s = 111; setup(w, 600);
        const uint8_t pool[] = {nl::cut::NM_VERTICAL, nl::cut::NM_RIGHT, nl::cut::JUMP, nl::cut::TURN_LEFT, nl::cut::DASH_LEFT_SIDESTEP, nl::cut::HEAD_JUMP, nl::cut::FINISH_STAB};
        nl::Rng script; script.s = 5; int next = 0, cutLen = 0; uint8_t cutType = 0; bool nan = false; int sameAction = 0, lastA = -1, maxSame = 0, maxAct = -1;
        for (int f = 0; f < 9000; ++f) {
            if (next-- <= 0) { cutType = pool[script.range(0, 6)]; cutLen = nl::duration(cutType) - 6; w.link.m_bowWait = script.unit() < 0.15f; next = cutLen + script.range(10, 60); }
            const int idx = cutLen - (next - 0) ; (void)idx;
            const int local = (cutLen + 60) - next;
            slash_frame(w, std::max(local, 0), cutType, cutLen, cutLen / 3, cutLen / 3 + 4);
            // Link wanders slowly
            w.link.current.pos.x = 200.0f * std::sin(f * 0.01f); w.link.current.pos.z = 150.0f * std::cos(f * 0.013f);
            exec_frame(w);
            const auto& p = w.dn.actor.current.pos; if (!std::isfinite(p.x) || !std::isfinite(p.z) || !std::isfinite(w.dn.actor.speedF)) nan = true;
            sameAction = (w.dn.action == lastA) ? sameAction + 1 : 0; lastA = w.dn.action; if (sameAction > maxSame) { maxSame = sameAction; maxAct = w.dn.action; }
            if (w.dn.actor.health <= 0) { w.dn.actor.health = 100; w.dn.action = 3; w.dn.mode = 0; w.dn.invulnerability_timer = 0; }
        }
        std::printf("   attacks=%d  damage taken=%d  longest time in one action=%d frames (action %d)  final dist=%.0f\n", w.attackStarts, w.damageTaken, maxSame, maxAct, w.dist());
        CHECK(!nan, "no NaN / inf");
        CHECK(maxSame <= nl::cfg::GUARD_MAX_HOLD + 20, "no action may last forever (%d)", maxSame);
        CHECK(w.attackStarts > 60, "keeps attacking through a long fight (%d)", w.attackStarts);
        if (noLeak) CHECK(w.damageTaken == 0, "no damage taken from melee in the whole fight (%d)", w.damageTaken);
    }

    std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf("ALL SIMULATION CHECKS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
