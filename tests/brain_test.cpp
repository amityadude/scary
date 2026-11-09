// Offline checks for the Nightmare Lizalfos decision logic (no game needed).
//   g++ -std=c++20 -O1 -I../src brain_test.cpp -o brain_test && ./brain_test
#include "nl_brain.hpp"

#include <cstdio>
#include <vector>

using namespace nl;

static int g_fail = 0;
#define CHECK(cond, ...)                                          \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                             \
            std::printf("\n");                                    \
            ++g_fail;                                             \
        }                                                         \
    } while (0)

struct Frame { uint8_t cut; bool at; bool aim; bool shoot; bool guard; };

static std::vector<Decision> run(Threat& th, Rng& rng, const std::vector<Frame>& fr, float dist, bool cone = true) {
    std::vector<Decision> out;
    for (const Frame& f : fr) {
        LinkView L; L.cut = f.cut; L.cutAt = f.at; L.aiming = f.aim; L.shooting = f.shoot; L.guarding = f.guard;
        Obs o; o.dist = dist; o.inAimCone = cone;
        out.push_back(observe(th, L, o, rng));
    }
    return out;
}

static std::vector<Frame> seq(uint8_t cutType, int frames, int atFrom, int atTo) {
    std::vector<Frame> v;
    for (int i = 0; i < frames; ++i) v.push_back({cutType, i >= atFrom && i < atTo, false, false, false});
    return v;
}
static void idle(std::vector<Frame>& v, int n) { for (int i = 0; i < n; ++i) v.push_back({0, false, false, false, false}); }

int main() {
    // 1. A normal slash is blocked from the first frame until just after it ends, then released.
    {
        Threat th; Rng rng; rng.s = 12345;
        auto f = seq(cut::NM_VERTICAL, 12, 6, 10);
        idle(f, 20);
        auto d = run(th, rng, f, 250.0f);
        CHECK(d[0].want == React::Block, "slash should be reacted to on its first frame");
        CHECK(d[9].want == React::Block, "still blocking during hit window");
        CHECK(d[11 + cfg::CUT_END_GRACE].want == React::Block || d[11 + cfg::CUT_END_GRACE].want == React::None, "grace edge");
        CHECK(d.back().want == React::None, "must release after the attack ends");
        CHECK(th.instanceId == 1, "one swing = one instance, got %d", th.instanceId);
    }
    // 2. Skills are dodged, once per instance.
    {
        const uint8_t skills[] = {cut::JUMP, cut::HEAD_JUMP, cut::LARGE_JUMP, cut::DASH_LEFT_SIDESTEP, cut::DASH_RIGHT_SIDESTEP,
                                  cut::TWIRL, cut::MORTAL_DRAW_A, cut::LARGE_TURN_LEFT, cut::WOLF_JUMP};
        for (uint8_t c : skills) {
            Threat th; Rng rng; rng.s = 777;
            auto f = seq(c, 20, 10, 14); idle(f, 60);
            auto d = run(th, rng, f, 300.0f);
            CHECK(d[0].want == React::Dodge, "cut 0x%02X should be dodged", c);
            CHECK(th.instanceId == 1, "cut 0x%02X instances=%d", c, th.instanceId);
        }
    }
    // 3. A stale/lingering cut type with no hit window cannot lock the shield up forever.
    {
        Threat th; Rng rng; rng.s = 99;
        auto f = seq(cut::NM_VERTICAL, 400, 999, 999);
        auto d = run(th, rng, f, 250.0f);
        CHECK(d[0].want == React::Block, "starts blocking");
        CHECK(d[200].want == React::None, "must not turtle on a stale cut type");
    }
    // 4. Far away: no melee reaction.
    {
        Threat th; Rng rng; rng.s = 5;
        auto f = seq(cut::NM_VERTICAL, 12, 6, 10);
        auto d = run(th, rng, f, cfg::REACT_RANGE + 100.0f);
        for (auto& x : d) CHECK(x.want == React::None, "should ignore swings from far away");
    }
    // 5. Bow aimed from far away: shield is up the whole time, protection only after release.
    {
        Threat th; Rng rng; rng.s = 31337;
        std::vector<Frame> f;
        for (int i = 0; i < 40; ++i) f.push_back({0, false, true, false, false});   // aiming
        for (int i = 0; i < 10; ++i) f.push_back({0, false, false, true, false});   // release
        idle(f, 80);
        auto d = run(th, rng, f, 900.0f);
        CHECK(d[5].want == React::Aim, "shield up while aiming");
        CHECK(!d[5].protect, "no invulnerability needed before the arrow exists");
        CHECK(d[42].want == React::Aim && d[42].protect, "protected after release");
        CHECK(d.back().want == React::None, "released after the arrow tail");
    }
    // 6. Bow aimed point-blank: it keeps fighting and only shields when the arrow is released.
    {
        Threat th; Rng rng; rng.s = 8;
        std::vector<Frame> f;
        for (int i = 0; i < 30; ++i) f.push_back({0, false, true, false, false});
        for (int i = 0; i < 6; ++i) f.push_back({0, false, false, true, false});
        idle(f, 50);
        auto d = run(th, rng, f, 250.0f);
        CHECK(d[10].want == React::None, "close up it should not turtle while you aim");
        CHECK(d[32].want == React::Aim, "but it shields when the arrow is released");
    }
    // 7. Aiming at something else (outside the cone) is ignored.
    {
        Threat th; Rng rng; rng.s = 4;
        std::vector<Frame> f;
        for (int i = 0; i < 30; ++i) f.push_back({0, false, true, false, false});
        auto d = run(th, rng, f, 900.0f, /*cone=*/false);
        for (auto& x : d) CHECK(x.want == React::None, "outside cone");
    }
    // 8. Leak rate is ~0.5% per instance.
    {
        Threat th; Rng rng; rng.s = 2024;
        const int N = 200000; int leaks = 0;
        for (int i = 0; i < N; ++i) {
            auto f = seq(cut::NM_LEFT, 3, 1, 2); idle(f, 8);
            for (auto& fr : f) {
                LinkView L; L.cut = fr.cut; L.cutAt = fr.at;
                Obs o; o.dist = 200.0f;
                observe(th, L, o, rng);
                if (th.newInstance && th.leaked) ++leaks;
            }
        }
        const double rate = double(leaks) / N;
        std::printf("leak rate over %d instances: %.4f%% (target %.2f%%)\n", N, rate * 100.0, cfg::LEAK_CHANCE * 100.0);
        CHECK(rate > 0.0035 && rate < 0.0065, "leak rate %.5f", rate);
    }
    // 9. Combo: three different swings back to back = three instances, protection never drops between them.
    {
        Threat th; Rng rng; rng.s = 61;
        std::vector<Frame> f;
        auto a = seq(cut::NM_VERTICAL, 8, 4, 7); auto b = seq(cut::NM_RIGHT, 8, 4, 7); auto c = seq(cut::FINISH_VERTICAL, 10, 5, 8);
        f.insert(f.end(), a.begin(), a.end()); f.insert(f.end(), b.begin(), b.end()); f.insert(f.end(), c.begin(), c.end());
        idle(f, 12);
        auto d = run(th, rng, f, 220.0f);
        CHECK(th.instanceId == 3, "combo instances=%d", th.instanceId);
        for (int i = 0; i < 26; ++i) CHECK(d[i].want == React::Block, "frame %d dropped guard mid-combo", i);
    }
    // 10. Same attack type repeated (hit window re-opens) counts as a new instance.
    {
        Threat th; Rng rng; rng.s = 71;
        std::vector<Frame> f;
        for (int i = 0; i < 30; ++i) f.push_back({cut::TURN_LEFT, (i % 10) >= 5 && (i % 10) < 8, false, false, false});
        run(th, rng, f, 200.0f);
        CHECK(th.instanceId >= 3, "spin pulses instances=%d", th.instanceId);
    }
    // 11. Turtle counter.
    {
        Threat th; Rng rng; rng.s = 3;
        std::vector<Frame> f;
        for (int i = 0; i < 60; ++i) f.push_back({0, false, false, false, true});
        run(th, rng, f, 200.0f);
        CHECK(th.linkGuardFrames > cfg::TURTLE_FRAMES, "turtle counter %d", th.linkGuardFrames);
        idle(f, 1); Threat t2; run(t2, rng, f, 200.0f); CHECK(t2.linkGuardFrames == 0, "resets when you drop the shield");
    }
    // 12. Math helpers.
    {
        CHECK(angle_of(0, 1) == 0, "angle +Z");
        CHECK(angle_of(1, 0) == 0x4000, "angle +X = %d", angle_of(1, 0));
        CHECK(angle_of(-1, 0) == -0x4000, "angle -X");
        const int16_t back = angle_of(0, -1);
        CHECK(back == 0x8000 || back == -0x8000, "angle -Z = %d", back);
        int16_t a = 0; for (int i = 0; i < 40; ++i) a = chase_angle(a, 0x4000, 2, 0x1500);
        CHECK(abs_angle(angle_diff(a, 0x4000)) < 8, "chase converges (%d)", a);
        int16_t b = 0x7000; for (int i = 0; i < 40; ++i) b = chase_angle(b, -0x7000, 2, 0x1500);   // across the wrap
        CHECK(abs_angle(angle_diff(b, -0x7000)) < 8, "chase wraps the short way (%d)", b);
        CHECK(desired_speed(900, 0) == cfg::RUN_SPEED, "full speed far away");
        CHECK(desired_speed(100, 0) == 0.0f, "stops inside lunge range");
        CHECK(desired_speed(900, 0x5000) < 10.0f, "turns before sprinting");
        Rng r; r.s = 55; int cnt[3] = {0,0,0}; for (int i = 0; i < 3000; ++i) cnt[pick_attack(250, false, r)]++;
        CHECK(cnt[0] > 900 && cnt[1] > 800 && cnt[2] > 500, "attack mix %d/%d/%d", cnt[0], cnt[1], cnt[2]);
        CHECK(pick_attack(250, true, r) == 2, "turtle -> tail sweep");
    }

    if (g_fail == 0) std::printf("ALL BRAIN TESTS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
