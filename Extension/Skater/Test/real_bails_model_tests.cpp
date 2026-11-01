// Real Bails' muscles (Extension/Skater/real_bails_model.h) on a made-up ragdoll: the phases of a fall, hands that go
// out the way the body falls, nothing written for a body that cannot be read, and spins that stay finite and bounded.
#include "Extension/Skater/real_bails_model.h"
#include <cmath>
#include <iostream>

namespace {
using namespace dingosdk::real_bails;
using namespace dingosdk::real_bails::model;
using namespace dingosdk::real_bails::math; // its vector operators
using math::V3;
int failures{};
void check(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}
// A skater standing up straight at the origin, facing -z with +x on his right (so forward = up x right), knees a
// little bent and toes forward, as the game's skater stands on the board.
Input standing() {
    Input in;
    in.dt = 1.0f / 60;
    const auto put = [&](Part p, V3 at) {
        auto& b = in.body[p];
        b.ok = true;
        b.p = at;
        b.r = {V3{1, 0, 0}, V3{0, 1, 0}, V3{0, 0, 1}};
    };
    put(pelvis, {0, 1.0f, 0});
    put(spine, {0, 1.1f, 0});
    put(back1, {0, 1.2f, 0});
    put(chest2, {0, 1.3f, 0});
    put(chest3, {0, 1.43f, 0});
    put(neck, {0, 1.62f, 0});
    put(head, {0, 1.73f, 0});
    for (const float side : {-1.0f, 1.0f}) {
        const bool right = side > 0;
        put(right ? r_collar : l_collar, {side * 0.05f, 1.5f, 0});
        put(right ? r_arm : l_arm, {side * 0.2f, 1.5f, 0});
        put(right ? r_forearm : l_forearm, {side * 0.22f, 1.2f, 0});
        put(right ? r_hand : l_hand, {side * 0.24f, 0.92f, 0});
        put(right ? r_thigh : l_thigh, {side * 0.09f, 0.95f, 0});
        put(right ? r_shin : l_shin, {side * 0.1f, 0.53f, -0.08f});
        put(right ? r_foot : l_foot, {side * 0.1f, 0.1f, 0});
        put(right ? r_toe : l_toe, {side * 0.1f, 0.03f, -0.12f});
    }
    return in;
}
V3 bone_of(const Input& in, Part a, Part b) { return math::normalized(in.body[b].p - in.body[a].p); }
}

int main() {
    using math::cross;
    using math::dot;
    // Falling forwards: the arms swing their hands out in front.
    {
        Input in = standing();
        for (auto& b : in.body) b.v = {0, -1.0f, -2.5f};
        Muscles m;
        m.begin(in);
        const auto out = m.step(in);
        check(out.phase == Phase::brace, "A fall starts braced");
        bool all_finite = true, bounded = true;
        for (std::size_t i = 1; i < part_count; ++i) {
            all_finite = all_finite && math::finite(out.w[i]);
            bounded = bounded && math::length(out.w[i]) <= 25.001f;
        }
        check(all_finite && bounded, "Spins are finite and at most 25 rad/s");
        for (const auto [shoulder, elbow] : {std::pair{l_arm, l_forearm}, std::pair{r_arm, r_forearm}}) {
            check(out.write[shoulder], "The shoulders are driven");
            // Where the upper arm's far end heads: spin x bone.
            const V3 heads = cross(out.w[shoulder] - out.w[l_collar] * 0.0f, bone_of(in, shoulder, elbow));
            check(dot(heads, V3{0, 0, -1}) > 0, "Falling forwards, the arms reach forwards");
        }
    }
    // Falling backwards: the chin comes in (the head turns forward) and the hands go back.
    {
        Input in = standing();
        for (auto& b : in.body) b.v = {0, -1.0f, 2.5f};
        Muscles m;
        m.begin(in);
        Output out;
        for (int i = 0; i < 4; ++i) out = m.step(in);
        const V3 heads = cross(out.w[l_arm], bone_of(in, l_arm, l_forearm));
        check(dot(heads, V3{0, 0, 1}) > 0, "Falling backwards, the hands go back");
        const V3 nods = cross(out.w[head] - out.w[neck], V3{0, 1, 0});
        check(dot(nods, V3{0, 0, -1}) > 0, "Falling backwards, the chin tucks");
    }
    // The phases: a hit on the back, a slide, lying still, then rest.
    {
        Input in = standing();
        for (auto& b : in.body) b.v = {0, 0, -3.0f};
        Muscles m;
        m.begin(in);
        (void)m.step(in);
        in.body[back1].touching = true;
        in.body[back1].impact = 3.0f;
        check(m.step(in).phase == Phase::impact, "A hard hit on the back is the impact");
        in.body[back1].impact = 0;
        Output out;
        for (int i = 0; i < 12; ++i) out = m.step(in);
        check(out.phase == Phase::slide, "Moving along the ground after the impact is a slide");
        for (auto& b : in.body) b.v = {};
        for (int i = 0; i < 20; ++i) out = m.step(in);
        check(out.phase == Phase::settle, "Lying still the body settles");
        const float settling = out.tone;
        for (int i = 0; i < 120; ++i) out = m.step(in);
        check(out.phase == Phase::rest && out.tone < settling, "Then it rests, with less tone");
    }
    // A body that cannot be read: nothing is written.
    {
        Input in = standing();
        Muscles m;
        m.begin(in);
        in.body[l_shin].ok = false;
        const auto out = m.step(in);
        bool any = false;
        for (const bool w : out.write) any = any || w;
        check(!any, "Nothing is written while a body cannot be read");
    }
    // Strength scales the tone.
    {
        Input in = standing();
        in.strength = 2.0f;
        Muscles strong;
        strong.begin(in);
        const float tone_strong = strong.step(in).tone;
        in.strength = 0.5f;
        Muscles weak;
        weak.begin(in);
        check(weak.step(in).tone < tone_strong, "Strength scales the muscles");
    }
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real bails model: all checks passed\n";
    return 0;
}
