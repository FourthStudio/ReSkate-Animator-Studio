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
    // Each body's axes as the game's are (the recordings, 2026-11-02): its bone along x; the torso's y forwards and z to his
    // left; an arm's z to his right and a leg's z to his left, so elbows and knees bend +z.
    const auto axes = [&](Part part, V3 x, V3 side) {
        x = math::normalized(x);
        const V3 z = math::normalized(side - x * math::dot(side, x));
        in.body[part].r = {x, math::cross(z, x), z};
    };
    for (const Part p : {pelvis, spine, back1, chest2, chest3, neck, head}) in.body[p].r = {V3{0, 1, 0}, V3{0, 0, -1}, V3{-1, 0, 0}};
    const auto along = [&](Part from, Part to) { return in.body[to].p - in.body[from].p; };
    for (const bool right : {false, true}) {
        const float side = right ? 1.0f : -1.0f;
        const Part collar = right ? r_collar : l_collar, arm = right ? r_arm : l_arm, forearm = right ? r_forearm : l_forearm, hand = right ? r_hand : l_hand;
        const Part thigh = right ? r_thigh : l_thigh, shin = right ? r_shin : l_shin, foot = right ? r_foot : l_foot, toe = right ? r_toe : l_toe;
        axes(collar, along(collar, arm), V3{0, 0, -1});
        axes(arm, along(arm, forearm), V3{1, 0, 0});
        axes(forearm, along(forearm, hand), V3{1, 0, 0});
        axes(hand, along(forearm, hand), V3{1, 0, 0});
        axes(thigh, along(thigh, shin), V3{-1, 0, 0});
        axes(shin, along(shin, foot), V3{-1, 0, 0});
        axes(foot, along(foot, toe), V3{-1, 0, 0});
        axes(toe, along(foot, toe), V3{-1, 0, 0});
        (void)side;
    }
    return in;
}
V3 bone_of(const Input& in, Part a, Part b) { return math::normalized(in.body[b].p - in.body[a].p); }
// The same skater tipped `forward` radians about his feet (+ his head towards -z, forwards; - backwards): a fall begun.
Input leaning(float forward) {
    Input in = standing();
    const float c = std::cos(-forward), s = std::sin(-forward);
    for (auto& b : in.body) {
        if (!b.ok) continue;
        const float y = b.p[1], z = b.p[2];
        b.p[1] = y * c - z * s;
        b.p[2] = y * s + z * c;
        for (auto& axis : b.r) {
            const float ay = axis[1], az = axis[2];
            axis[1] = ay * c - az * s;
            axis[2] = ay * s + az * c;
        }
    }
    return in;
}
}

int main() {
    using math::cross;
    using math::dot;
    // Nothing on the first frames: a reflex takes a moment, and the body goes where the slam sends it.
    {
        Input in = leaning(0.4f);
        for (auto& b : in.body) b.v = {0, -1.0f, -2.5f};
        Muscles m;
        m.begin(in);
        const auto out = m.step(in);
        check(out.phase == Phase::brace, "A fall starts braced");
        float most = 0;
        for (std::size_t i = 1; i < part_count; ++i)
            if (out.write[i]) most = std::max(most, math::length(out.w[i] - in.body[i].w));
        check(most < 0.5f, "Nothing is driven on the first frame (the joints' stops only)");
    }
    // Falling forwards: the arms swing their hands out in front.
    {
        Input in = leaning(0.4f);
        for (auto& b : in.body) b.v = {0, -1.0f, -2.5f};
        Muscles m;
        m.begin(in);
        Output out;
        for (int i = 0; i < 18; ++i) out = m.step(in);
        check(out.phase == Phase::brace, "A fall stays braced until it hits");
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
        Input in = leaning(-0.4f);
        for (auto& b : in.body) b.v = {0, -1.0f, 2.5f};
        Muscles m;
        m.begin(in);
        Output out;
        for (int i = 0; i < 12; ++i) out = m.step(in);
        const V3 heads = cross(out.w[l_arm], bone_of(in, l_arm, l_forearm));
        check(dot(heads, V3{0, 0, 1}) > 0, "Falling backwards, the hands go back");
        const V3 nods = cross(out.w[head] - out.w[neck], V3{0, 1, 0});
        check(dot(nods, V3{0, 0, -1}) > 0, "Falling backwards, the chin tucks");
    }
    // The phases: a hit on the back, a slide, lying still, then rest.
    {
        Input in = standing();
        in.tune.grab_hurt = false;
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
    // A hard hit on the back held for as long as Hold what hurts for says, then rest.
    {
        Input in = standing();
        in.tune.hold_hurt = 4.0f;
        in.tune.lie_still = 1.0f;
        for (auto& b : in.body) b.v = {0, 0, -3.0f};
        Muscles m;
        m.begin(in);
        (void)m.step(in);
        in.body[back1].touching = true;
        in.body[back1].impact = 5.0f;
        (void)m.step(in);
        in.body[back1].impact = 0;
        for (auto& b : in.body) b.v = {};
        Output out;
        for (int i = 0; i < 60 * 3; ++i) out = m.step(in);
        check(out.phase == Phase::settle, "Holding what hurts he stays down past Lie still for");
        for (int i = 0; i < 60 * 3; ++i) out = m.step(in);
        check(out.phase == Phase::rest, "Then he lets go and rests");
    }
    // On his back after a hard hit to the head, holding it: the knees may come up but the feet stay down. They were aimed
    // at a pose taken from his body alone, which held both feet up in the air for as long as he held his head.
    {
        Input in = leaning(-1.5708f); // flat on his back, facing the sky
        for (auto& b : in.body) b.v = {0, 0, -3.0f};
        Muscles m;
        m.begin(in);
        (void)m.step(in);
        in.body[head].touching = in.body[back1].touching = true;
        in.body[head].has_point = in.body[back1].has_point = true;
        in.body[head].point = in.body[head].p - V3{0, 0.1f, 0};
        in.body[back1].point = in.body[back1].p - V3{0, 0.12f, 0};
        in.body[head].impact = 5.0f;
        (void)m.step(in);
        in.body[head].impact = 0;
        for (auto& b : in.body) b.v = {};
        Output out;
        for (int i = 0; i < 60 * 2; ++i) out = m.step(in);
        check(out.phase == Phase::settle, "Holding his head he lies there");
        for (int s = 0; s < 2; ++s) {
            const Part hip = s == 0 ? l_thigh : r_thigh;
            check(out.foot_mark[static_cast<std::size_t>(s)][1] <= in.body[hip].p[1] + 0.001f, "Lying on his back, no foot is held up above its hip");
        }
    }
    // A hand landing in the catch does not end the brace: the bail goes on from the catch.
    {
        Input in = standing();
        for (auto& b : in.body) b.v = {0, -2.0f, -2.5f};
        Muscles m;
        m.begin(in);
        for (int i = 0; i < 10; ++i) (void)m.step(in);
        in.body[l_hand].touching = in.body[r_hand].touching = true;
        in.body[l_hand].impact = in.body[r_hand].impact = 5.0f;
        check(m.step(in).phase == Phase::brace, "The hands landing hard is the catch, still braced");
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
    // The muscles move one part against another, never the whole body: off the ground the body's momentum is the same
    // after they act as before (it was launched forwards at up to 4 m/s before this was kept).
    {
        static constexpr std::array<float, part_count> kg{0, 5, 1.5f, .5f, 1.3f, 2, 1.5f, .5f, 1.3f, 2, 1.5f, 8, 6, 5, 5, .3f, 1, 4, 9, .3f, 1, 4, 9, 11};
        for (const V3 going : {V3{0, -1, -3}, V3{0, -1, 3}, V3{3, -2, 0}}) {
            Input in = standing();
            for (auto& b : in.body) b.v = going;
            Muscles m;
            m.begin(in);
            Output out;
            for (int i = 0; i < 12; ++i) out = m.step(in);
            V3 before{}, after{};
            for (std::size_t i = 1; i < part_count; ++i) {
                before = before + in.body[i].v * kg[i];
                after = after + (out.write[i] ? out.v[i] : in.body[i].v) * kg[i];
            }
            check(math::length(after - before) < 0.05f * 75, "The muscles never push the body as a whole");
        }
    }
    // Head protection off: the neck lets go (it is driven far more weakly than with protection on).
    {
        Input in = standing();
        for (auto& b : in.body) b.v = {0, -1.0f, 2.5f};
        in.body[head].r = math::multiply(math::rotation({1, 0, 0}, 0.6f), in.body[head].r); // the head tipped well away from where it was
        const auto neck_drive = [&](float guard) {
            Input copy = in;
            copy.tune.head = guard;
            Muscles m;
            m.begin(standing());
            Output out;
            for (int i = 0; i < 10; ++i) out = m.step(copy);
            return math::length(out.w[head] - copy.body[head].w);
        };
        check(neck_drive(0.0f) < 0.3f * neck_drive(1.0f), "Head protection off lets the head go");
    }
    // Nothing is aimed through the body: on his back with the hands told to grab a hurt chest, no hand is driven into him.
    {
        Input in = standing();
        Muscles m;
        m.begin(in);
        for (int i = 0; i < 40; ++i) (void)m.step(in);
        const auto out = m.step(in);
        bool finite_all = true;
        for (std::size_t i = 1; i < part_count; ++i) finite_all = finite_all && math::finite(out.w[i]) && math::finite(out.v[i]);
        check(finite_all, "Lying still the drives stay finite");
    }
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real bails model: all checks passed\n";
    return 0;
}
