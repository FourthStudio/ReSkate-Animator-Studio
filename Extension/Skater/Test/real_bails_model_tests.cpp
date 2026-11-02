// Real Bails' muscles (Extension/Skater/real_bails_model.h) on a made-up ragdoll. The bail is the ragdoll's: what the
// muscles must never do is snap the body on the first step, push or spin the body as a whole (but for the board's push
// back on a kicking foot), use more torque than a person has, drive the legs or back towards any pose while he falls,
// rolls or slides, lift the legs lying down (but the hugged knee and the racked curl), hold the head against a hard slam,
// react to pain while he tumbles or slides fast, hold a flinch, or keep moving him once he lies still. What they must
// do: bring the arms out the way his momentum takes him (forwards, to the side, back and down) whatever the fall, let
// them give once down, pull the chin in as hard as Head protection says, flinch briefly from a hit, react to what hurt
// most as soon as he is down and slow (the other hand to an elbow, the hands to a knee or the head, both hands to the
// groin and curled up), kick the board away once when the feet are on it, and stay finite whatever the bodies do.
#include "Extension/Skater/real_bails_body.h"
#include "Extension/Skater/real_bails_model.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <vector>

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
constexpr float pi = 3.14159265f;
// A skater standing up straight at the origin, facing -z with +x on his right, knees a little bent and toes forward, as
// the game's skater stands on the board. Each body's axes as the game's are (the recordings, 2026-11-02): its bone along
// x; the torso's y forwards and z to his left; an arm's z to his right and a leg's z to his left, so elbows and knees
// bend +z.
Input standing() {
    Input in;
    in.dt = 1.0f / 60;
    const auto put = [&](Part p, V3 at) {
        auto& b = in.body[p];
        b.ok = true;
        b.p = at;
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
        put(right ? r_forearm : l_forearm, {side * 0.22f, 1.2f, -0.02f});
        put(right ? r_hand : l_hand, {side * 0.24f, 0.93f, -0.08f});
        put(right ? r_thigh : l_thigh, {side * 0.09f, 0.95f, 0});
        put(right ? r_shin : l_shin, {side * 0.1f, 0.53f, -0.08f});
        put(right ? r_foot : l_foot, {side * 0.1f, 0.1f, 0});
        put(right ? r_toe : l_toe, {side * 0.1f, 0.03f, -0.12f});
    }
    const auto axes = [&](Part part, V3 x, V3 side) {
        x = math::normalized(x);
        const V3 z = math::normalized(side - x * math::dot(side, x));
        in.body[part].r = {x, math::cross(z, x), z};
    };
    for (const Part p : {pelvis, spine, back1, chest2, chest3, neck, head}) in.body[p].r = {V3{0, 1, 0}, V3{0, 0, -1}, V3{-1, 0, 0}};
    const auto along = [&](Part from, Part to) { return in.body[to].p - in.body[from].p; };
    for (const bool right : {false, true}) {
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
    }
    return in;
}
// Turn `parts` by `angle` about `axis` (unit) through `about`: where they are, their axes and their motion.
void turn(Input& in, std::initializer_list<Part> parts, V3 about, V3 axis, float angle) {
    const M3 r = math::rotation(axis, angle);
    for (const Part p : parts) {
        auto& b = in.body[p];
        b.p = about + math::turn_by(r, b.p - about);
        b.r = math::multiply(r, b.r);
        b.v = math::turn_by(r, b.v);
        b.w = math::turn_by(r, b.w);
        b.point = about + math::turn_by(r, b.point - about);
    }
}
void turn_all(Input& in, V3 about, V3 axis, float angle) {
    for (std::size_t i = 1; i < part_count; ++i) turn(in, {static_cast<Part>(i)}, about, axis, angle);
}
// The same skater tipped `forward` radians about his feet (+ his head towards -z, forwards; - backwards).
Input leaning(float forward) {
    Input in = standing();
    turn_all(in, {0, 0, 0}, {1, 0, 0}, -forward);
    return in;
}
// Tipped to his right (+x) by `angle` about his feet.
Input leaning_right(float angle) {
    Input in = standing();
    turn_all(in, {0, 0, 0}, {0, 0, 1}, -angle);
    return in;
}
void move_all(Input& in, V3 v) {
    for (auto& b : in.body) b.v = v;
}
// These bodies on the ground under them.
void on_ground(Input& in, std::initializer_list<Part> parts, float impact = 0) {
    for (const Part p : parts) {
        auto& b = in.body[p];
        b.touching = true;
        b.impact = impact;
        b.has_point = true;
        b.point = {b.p[0], 0.0f, b.p[2]};
    }
}
// Flat on his back on the ground (y = 0), facing the sky, the whole of him touching it.
Input on_his_back() {
    Input in = leaning(-pi / 2);
    for (std::size_t i = 1; i < part_count; ++i) {
        auto& b = in.body[i];
        b.p[1] += 0.1f;
        b.touching = true;
        b.has_point = true;
        b.point = {b.p[0], 0.0f, b.p[2]};
    }
    return in;
}
bool is_leg(Part p) { return p == l_thigh || p == l_shin || p == l_foot || p == l_toe || p == r_thigh || p == r_shin || p == r_foot || p == r_toe; }
bool is_arm(Part p) {
    return p == l_collar || p == l_arm || p == l_forearm || p == l_hand || p == r_collar || p == r_arm || p == r_forearm || p == r_hand;
}
bool is_trunk(Part p) { return p == pelvis || p == spine || p == back1 || p == chest2 || p == chest3 || p == neck || p == head; }
// What the step does to the momentum (linear, and angular about the whole body's centre) of the bodies `which` picks.
template <typename F> void change_of(const Input& in, const Output& out, F which, V3& linear, V3& angular) {
    const body::Mass m = body::measure(in.body);
    std::array<V3, part_count> dw{}, dv{};
    for (std::size_t i = 1; i < part_count; ++i)
        if (out.write[i] && which(static_cast<Part>(i))) {
            dw[i] = out.w[i] - in.body[i].w;
            dv[i] = out.v[i] - in.body[i].v;
        }
    body::momentum_of(m, dw, dv, linear, angular);
}
void change_of(const Input& in, const Output& out, V3& linear, V3& angular) {
    change_of(in, out, [](Part) { return true; }, linear, angular);
}
V3 spin_of(const Input& in, const Output& out, Part p) { return out.write[p] ? out.w[p] : in.body[p].w; }
V3 velocity_of(const Input& in, const Output& out, Part p) { return out.write[p] ? out.v[p] : in.body[p].v; }
float relative_speed(const Input& in, const Output& out, Part c) {
    return math::length(spin_of(in, out, c) - spin_of(in, out, parent_of[c]));
}
bool torques_in_limits(const Output& out, float strength) {
    for (std::size_t i = 1; i < part_count; ++i)
        if (out.torque[i] > torque_limit(static_cast<Part>(i), strength) * 1.0001f + 1e-4f) return false;
    return true;
}
bool all_finite(const Output& out) {
    for (std::size_t i = 1; i < part_count; ++i)
        if (!math::finite(out.w[i]) || !math::finite(out.v[i]) || !std::isfinite(out.torque[i])) return false;
    return std::isfinite(out.tone) && std::isfinite(out.work) && std::isfinite(out.time_to_hit);
}
// Runs `steps` steps of the same input; `each` sees every output.
template <typename F> Output run(Muscles& m, const Input& in, int steps, F each) {
    Output out;
    for (int i = 0; i < steps; ++i) {
        out = m.step(in);
        each(out);
    }
    return out;
}
Output run(Muscles& m, const Input& in, int steps) {
    return run(m, in, steps, [](const Output&) {});
}
// Falls with his feet on the ground: tipped 0.3 rad, his centre going over the feet.
Input forward_fall() {
    Input in = leaning(0.3f);
    move_all(in, {0, -0.4f, -1.6f});
    on_ground(in, {l_foot, r_foot, l_toe, r_toe});
    return in;
}
Input backward_fall() {
    Input in = leaning(-0.3f);
    move_all(in, {0, -0.4f, 1.6f});
    on_ground(in, {l_foot, r_foot, l_toe, r_toe});
    return in;
}
Input right_fall() {
    Input in = leaning_right(0.3f);
    move_all(in, {1.6f, -0.4f, 0});
    on_ground(in, {l_foot, r_foot, l_toe, r_toe});
    return in;
}
// Astride a rail, racked: the pelvis comes down on it from above between his legs (they hang either side, the feet well
// under him, off the ground), the rail under the middle of the pelvis.
Input racked() {
    Input in = standing();
    auto& p = in.body[pelvis];
    p.touching = true;
    p.impact = 3.5f;
    p.has_point = true;
    p.point = p.p - p.r[0] * 0.13f; // under its middle, towards the legs
    p.has_normal = true;
    p.normal = {0, 1, 0};
    return in;
}
// The skater with his feet on the board (the game's contact says so), going over backwards.
Input on_board_backwards() {
    Input in = backward_fall();
    for (const Part p : {l_foot, r_foot, l_toe, r_toe}) in.body[p].on_board = true;
    return in;
}
// The distance from a hand's mark to a point.
float mark_to(const Output& o, int hand, const V3& at) { return math::length(o.hand_mark[static_cast<std::size_t>(hand)] - at); }
// Lying still on his back after a hit of `speed` m/s on `part` (on the first step); then `steps` steps lying there.
Output hurt_on_back(Muscles& m, Part part, float speed, int steps, Input* lying = nullptr) {
    Input in = on_his_back();
    m.begin(in, 9);
    Input hit = in;
    hit.body[part].impact = speed;
    (void)m.step(hit);
    if (lying) *lying = in;
    return run(m, in, steps);
}
// Rolling over along the ground: on his back, the torso touching, the whole of him turning about his length and moving.
Input rolling() {
    Input in = on_his_back();
    for (std::size_t i = 1; i < part_count; ++i) {
        auto& b = in.body[i];
        b.touching = is_trunk(static_cast<Part>(i));
        b.w = {0, 0, 6.0f};
        b.v = {2.0f, 0.5f, 0};
    }
    return in;
}
}

int main() {
    using math::cross;
    using math::dot;
    using math::length;

    // Nothing is snapped on the first step: the game hands him over with his joints turning (in the recordings up to 29
    // rad/s at a knee); the muscles' tone only damps that a little (taking energy out, never putting it in), and no joint
    // has a goal until the arms' reflex starts.
    {
        Input in = forward_fall();
        // Joints turning as the game hands the ragdoll over: an elbow and a knee bending, a whole arm and a leg swinging,
        // the head turning.
        in.body[l_forearm].w = in.body[l_hand].w = {5, 0, 0};
        for (const Part p : {r_arm, r_forearm, r_hand}) in.body[p].w = {0, 0, 4};
        in.body[l_shin].w = in.body[l_foot].w = in.body[l_toe].w = {-6, 0, 0};
        for (const Part p : {r_thigh, r_shin, r_foot, r_toe}) in.body[p].w = {3, 0, 0};
        in.body[head].w = {0, 2, 0};
        Muscles m;
        m.begin(in, 7);
        const Output first = m.step(in);
        check(first.phase == Phase::brace, "A fall starts braced");
        bool slowed = true;
        for (const Part p : {l_forearm, r_arm, l_shin, r_thigh, head}) {
            const float before = length(in.body[p].w - in.body[parent_of[p]].w), after = relative_speed(in, first, p);
            slowed = slowed && after <= before + 1e-3f && after > 0.8f * before;
        }
        bool no_goal = true, light = true;
        for (std::size_t i = 1; i < part_count; ++i) {
            no_goal = no_goal && !first.driven[i];
            light = light && first.torque[i] <= 0.2f * torque_limit(static_cast<Part>(i), 1) + 1e-3f;
        }
        check(slowed, "First step: a turning joint is slowed a little, never driven on");
        check(no_goal, "First step: no joint has a goal");
        check(light, "First step: no joint has more than its riding tone");
        check(first.work <= 1e-3f, "First step: the muscles take energy out, never put it in");
        // Until the arms' reflex starts (0.1 s), no arm has a goal.
        Muscles m2;
        m2.begin(forward_fall(), 7);
        bool early = true;
        run(m2, forward_fall(), 5, [&](const Output& o) { early = early && !o.driven[l_arm] && !o.driven[r_arm]; });
        check(early, "Before the reflex delay, the arms only have their tone");
    }

    // The muscles move one part against another, never the body as a whole: its momentum, linear and angular, is the same
    // after they act, in the air and rolling on the ground. (A hand or foot planted on the ground is the ground's and is
    // not written, so its share is the ground's: those cases are left out here.)
    {
        bool kept = true;
        for (Input in : {forward_fall(), backward_fall(), right_fall(), rolling()}) {
            for (std::size_t i = 1; i < part_count; ++i)
                if (in.body[i].touching && (i == l_hand || i == r_hand || i == l_foot || i == r_foot || i == l_toe || i == r_toe))
                    in.body[i].touching = false;
            // An elbow and a knee bending, as the game hands him over.
            in.body[l_forearm].w = in.body[l_forearm].w + in.body[l_arm].r[2] * 5.0f;
            in.body[l_hand].w = in.body[l_forearm].w;
            in.body[r_shin].w = in.body[r_shin].w + in.body[r_thigh].r[2] * 4.0f;
            Muscles m;
            m.begin(in, 3);
            run(m, in, 40, [&](const Output& o) {
                V3 linear{}, angular{};
                change_of(in, o, linear, angular);
                kept = kept && length(linear) < 0.02f && length(angular) < 0.02f;
            });
        }
        check(kept, "The muscles never change the body's momentum, linear or angular");
    }

    // In the game, whose joint solve takes most of each push back, the muscles push harder by about what is lost (a
    // shoulder about four times): the arms come out as hard as a person's would. Still one part against another.
    {
        // Feet on the ground (where the ground is, so the fall is read and the arms reach).
        const auto arm_turn = [&](bool in_game) {
            Input in = forward_fall();
            in.in_game = in_game;
            Muscles m;
            m.begin(in, 3);
            float most = 0;
            run(m, in, 15, [&](const Output& o) {
                for (const Part p : {l_arm, r_arm})
                    most = std::max(most, length((spin_of(in, o, p) - spin_of(in, o, chest3)) - (in.body[p].w - in.body[chest3].w)));
            });
            return most;
        };
        const float person = arm_turn(false), game = arm_turn(true);
        check(person > 0.05f && game > 2.5f * person, "In the game the arms are pushed harder, making up for what its joints take back");
        // Nothing planted (so every body is written): the momentum is the same after they act.
        bool kept = true;
        for (Input in : {forward_fall(), rolling()}) {
            for (const Part p : {l_foot, r_foot, l_toe, r_toe}) in.body[p].touching = false;
            in.body[l_forearm].w = in.body[l_forearm].w + in.body[l_arm].r[2] * 5.0f;
            in.body[l_hand].w = in.body[l_forearm].w;
            in.in_game = true;
            Muscles m;
            m.begin(in, 3);
            run(m, in, 40, [&](const Output& o) {
                V3 linear{}, angular{};
                change_of(in, o, linear, angular);
                kept = kept && length(linear) < 0.02f && length(angular) < 0.02f;
            });
        }
        check(kept, "In the game too, the muscles never change the body's momentum");
    }

    // No joint ever gets more torque than a person's joint has, at any strength, in any fall.
    {
        bool within = true;
        for (const float strength : {0.25f, 1.0f, 2.0f}) {
            for (Input in : {forward_fall(), backward_fall(), right_fall(), on_his_back(), rolling()}) {
                in.strength = strength;
                in.tune.reaction = 2.0f;
                in.tune.head = 1.5f;
                in.tune.tuck = 1.5f;
                Muscles m;
                m.begin(in, 11);
                run(m, in, 60, [&](const Output& o) { within = within && torques_in_limits(o, strength); });
                Input hit = in;
                on_ground(hit, {back1, chest3, head}, 6.0f);
                run(m, hit, 5, [&](const Output& o) { within = within && torques_in_limits(o, strength); });
                move_all(hit, {});
                for (auto& b : hit.body) {
                    b.impact = 0;
                    b.w = {};
                }
                run(m, hit, 300, [&](const Output& o) { within = within && torques_in_limits(o, strength); });
            }
        }
        check(within, "No joint's torque is ever over a person's joint strength (times Strength)");
    }

    // Falling, rolling or sliding, nothing drives the legs or the back towards any pose: their muscles only resist being
    // turned fast. Rolling, the arms are left alone too.
    {
        bool passive = true, arms_loose = true;
        for (const Input& in : {forward_fall(), backward_fall(), right_fall(), rolling()}) {
            Muscles m;
            m.begin(in, 5);
            run(m, in, 120, [&](const Output& o) {
                for (std::size_t i = 1; i < part_count; ++i)
                    if (!is_arm(static_cast<Part>(i)) && i != neck && i != head) passive = passive && !o.driven[i];
            });
        }
        Input roll = rolling();
        roll.body[head].impact = 5.0f; // hurt, but still rolling
        Muscles m;
        m.begin(roll, 5);
        (void)m.step(roll);
        roll.body[head].impact = 0;
        run(m, roll, 240, [&](const Output& o) {
            if (o.phase == Phase::brace || o.phase == Phase::impact) return; // its first moments, before he is rolling
            for (std::size_t i = 1; i < part_count; ++i) {
                // The neck may have its goal: the chin tucked while he tumbles (Head protection).
                if (i != neck && i != head) passive = passive && !o.driven[i];
                if (is_arm(static_cast<Part>(i))) arms_loose = arms_loose && o.activation[i] < 0.2f;
            }
            passive = passive && o.phase != Phase::settle && o.phase != Phase::rest && !o.pain;
        });
        check(passive, "Falling, rolling or sliding, no leg or back joint is driven towards a pose");
        check(arms_loose, "Rolling, the arms are as loose as the rest of him");
    }

    // Lying on his back the legs are never lifted: falling, the muscles may slow a leg as it comes down, never stop it
    // short of the ground by lifting, never add to a swing that carries it up; and a leg up in the air (the candle he once
    // held for seconds) is not held there.
    {
        const auto legs_up = [] {
            Input in = standing();
            for (const auto& [thigh, shin, foot, toe] : {std::array{l_thigh, l_shin, l_foot, l_toe}, std::array{r_thigh, r_shin, r_foot, r_toe}})
                turn(in, {thigh, shin, foot, toe}, in.body[thigh].p, {1, 0, 0}, pi / 2 * 0.95f);
            turn_all(in, {0, 0, 0}, {1, 0, 0}, pi / 2);
            for (std::size_t i = 1; i < part_count; ++i) in.body[i].p[1] += 0.1f;
            on_ground(in, {pelvis, spine, back1, chest2, chest3, neck, head, l_collar, r_collar, l_arm, r_arm, l_forearm, r_forearm, l_hand, r_hand}, 0);
            return in;
        };
        bool never_up = true, never_holds = true;
        for (const float going : {-0.6f, 0.0f, 0.6f}) {
            Input in = legs_up();
            in.tune.hold_hurt = 6.0f;
            Muscles m;
            m.begin(in, 5);
            Input hit = in;
            hit.body[head].impact = 5.0f; // hurt: he will react to it once still
            (void)m.step(hit);
            for (std::size_t i = 1; i < part_count; ++i)
                if (is_leg(static_cast<Part>(i))) in.body[i].v = {0, going, 0};
            run(m, in, 300, [&](const Output& o) {
                float before = 0, after = 0, kg = 0;
                for (std::size_t i = 1; i < part_count; ++i) {
                    if (!is_leg(static_cast<Part>(i))) continue;
                    before += in.body[i].v[1] * body::shapes[i].mass;
                    after += velocity_of(in, o, static_cast<Part>(i))[1] * body::shapes[i].mass;
                    kg += body::shapes[i].mass;
                }
                before /= kg;
                after /= kg;
                never_up = never_up && after <= std::max(before, 0.0f) + 0.01f;
                // Its feet are off the ground: the pain reaction leaves these legs as they lie.
                never_holds = never_holds && !o.driven[l_thigh] && !o.driven[r_thigh] && !o.driven[l_shin] && !o.driven[r_shin];
            });
        }
        check(never_up, "Lying down, the muscles never lift a leg");
        check(never_holds, "Lying down, a leg up in the air is never driven");
    }

    // The arms go out towards where he will land: ahead of him falling forwards, both to the side he falls to, behind and
    // out falling backwards.
    {
        const auto aims = [](Input in, int steps) {
            Muscles m;
            m.begin(in, 1);
            return run(m, in, steps);
        };
        const Output fwd = aims(forward_fall(), 24);
        check(fwd.fall == Fall::forward, "Tipping forwards over his feet is read as a forward fall");
        check(fwd.time_to_hit > 0.15f && fwd.time_to_hit < 1.2f, "A forward fall from standing is read to land in a fraction of a second");
        const Input f_in = forward_fall();
        for (const Part shoulder : {l_arm, r_arm}) {
            const std::size_t s = shoulder == l_arm ? 0 : 1;
            check(fwd.driven[shoulder] && fwd.torque[shoulder] > 1.0f, "Falling forwards, the shoulders drive the arms out");
            check(dot(fwd.hand_mark[s] - f_in.body[shoulder].p, V3{0, 0, -1}) > 0.2f, "Falling forwards, each arm aims out in front of him");
            const Part elbow = shoulder == l_arm ? l_forearm : r_forearm;
            const V3 heads = cross(spin_of(f_in, fwd, shoulder) - spin_of(f_in, fwd, chest3), f_in.body[elbow].p - f_in.body[shoulder].p);
            check(dot(heads, V3{0, 0, -1}) > 0, "Falling forwards, the arms swing forwards");
        }
        const Output back = aims(backward_fall(), 24);
        check(back.fall == Fall::backward, "Tipping backwards is read as a backward fall");
        const Input b_in = backward_fall();
        for (const Part shoulder : {l_arm, r_arm}) {
            const std::size_t s = shoulder == l_arm ? 0 : 1;
            const V3 off = back.hand_mark[s] - b_in.body[shoulder].p;
            check(dot(off, V3{0, 0, 1}) > 0.05f, "Falling backwards, each arm aims behind him");
            check(dot(off, V3{shoulder == l_arm ? -1.0f : 1.0f, 0, 0}) > 0.1f, "Falling backwards, each arm aims out to its side");
        }
        const Output side = aims(right_fall(), 24);
        check(side.fall == Fall::right, "Tipping to his right is read as a fall to the right");
        const Input s_in = right_fall();
        check(dot(side.hand_mark[1] - s_in.body[r_arm].p, V3{1, 0, 0}) > 0.15f, "Falling to his right, the right hand reaches out to the right");
        check(dot(side.hand_mark[0] - s_in.body[l_arm].p, V3{1, 0, 0}) > 0.0f, "Falling to his right, the left hand comes towards the right too");
        Input off = forward_fall();
        off.tune.catch_arms = false;
        const Output still = aims(off, 24);
        check(!still.driven[l_arm] && !still.driven[r_arm], "Catch with the arms off, the arms do not reach");
    }

    // A hand landing is the catch: the fall goes on, the hand is the ground's, and the shoulder has no goal any more: for
    // a moment the arm resists folding, the elbow giving to about 100 degrees against high damping, then it is loose.
    {
        Input in = forward_fall();
        Muscles m;
        m.begin(in, 2);
        run(m, in, 18);
        on_ground(in, {l_hand, r_hand}, 3.0f);
        const Output caught = m.step(in);
        check(caught.phase == Phase::brace, "The hands landing is the catch, still going down");
        check(!caught.write[l_hand] && !caught.write[r_hand], "A hand on the ground is the ground's: not written");
        for (auto& b : in.body) b.impact = 0;
        const Output giving = run(m, in, 6);
        check(!giving.driven[l_arm], "Once down, the shoulder has no goal");
        check(giving.driven[l_forearm] && dot(spin_of(in, giving, l_forearm) - spin_of(in, giving, l_arm), in.body[l_arm].r[2]) > 0,
              "Once down, the elbow gives (it bends)");
        check(giving.activation[l_forearm] > 0.3f, "Once down, the arm resists folding for a moment");
        const Output loose = run(m, in, 30);
        check(loose.activation[l_forearm] < 0.15f, "Then it is loose");
    }

    // Head protection: falling backwards the chin tucks in; falling forwards it does not (that would put the face in
    // first): the head turns instead, the cheek to the ground. Head protection off lets the head go.
    {
        Muscles m;
        const Input in = backward_fall();
        m.begin(in, 2);
        const Output back = run(m, in, 15);
        check(back.driven[neck] && dot(spin_of(in, back, neck) - spin_of(in, back, chest3), in.body[chest3].r[2]) > 0,
              "Falling backwards, the chin tucks");
        check(back.torque[neck] < 0.75f * torque_limit(neck, 1), "Falling backwards, the chin's pull is a neck's, not more");
        Muscles f;
        const Input fin = forward_fall();
        f.begin(fin, 2);
        const Output fwd = run(f, fin, 15);
        const V3 nod = spin_of(fin, fwd, neck) - spin_of(fin, fwd, chest3);
        check(fwd.driven[neck] && dot(nod, fin.body[chest3].r[2]) <= 1e-3f, "Falling forwards, the chin is not tucked");
        check(std::abs(dot(nod, fin.body[chest3].r[0])) > 0.02f, "Falling forwards, the head turns (the cheek to the ground)");
        // Head protection off lets the head go: the chin does not tuck and the neck's tone is next to nothing.
        Input loose = backward_fall();
        loose.tune.head = 0;
        Muscles l;
        l.begin(loose, 2);
        const Output limp = run(l, loose, 15);
        check(!limp.driven[neck] && limp.activation[neck] < 0.03f, "Head protection off lets the head go");
    }

    // A hard backward slam: the back stops on the ground, the head keeps going. The neck is not strong enough to stop it:
    // the head still slaps down (the user wants that).
    {
        Input in = leaning(-1.2f);
        move_all(in, {});
        on_ground(in, {pelvis, spine, back1, chest2}, 6.0f);
        for (const Part p : {neck, head}) in.body[p].v = {0, -3.5f, 1.0f};
        in.body[neck].w = in.body[head].w = {-12.0f, 0, 0};
        in.tune.head = 1.5f;
        in.strength = 2.0f;
        Muscles m;
        m.begin(backward_fall(), 4);
        for (int i = 0; i < 30; ++i) (void)m.step(backward_fall()); // the fall before it, so every reflex is awake
        bool keeps = true, bounded = true;
        for (int i = 0; i < 6; ++i) {
            const Output o = m.step(in);
            keeps = keeps && velocity_of(in, o, head)[1] < 0.75f * in.body[head].v[1];
            bounded = bounded && o.torque[neck] <= torque_limit(neck, 2.0f) + 1e-3f && o.torque[head] <= torque_limit(head, 2.0f) + 1e-3f;
        }
        check(keeps, "A hard backward slam: the neck cannot stop the head (it keeps 3/4 of its speed each step)");
        check(bounded, "A hard backward slam: the neck's torque stays a neck's");
    }

    // The pain reaction: never while he moves; once he lies still a hand goes to what hurts, lightly, and with Tuck he
    // curls a little (feet on the ground); none of it moves the body as a whole; moving again stops it at once.
    {
        Input in = on_his_back();
        in.tune.hold_hurt = 3.0f;
        in.tune.tuck = 1.0f;
        Muscles m;
        m.begin(in, 9);
        Input hit = in;
        hit.body[head].impact = 5.0f;
        (void)m.step(hit);
        // Still sliding along fast (over 2.6 m/s): nothing.
        Input sliding = in;
        move_all(sliding, {3.0f, 0, 0});
        bool none = true;
        run(m, sliding, 90, [&](const Output& o) { none = none && !o.pain; });
        check(none, "No pain reaction while he slides fast");
        // Lying still: after a moment, the reaction.
        bool started = false, light = true, kept = true, trunk_kept = true, arms_led = true;
        Output o = run(m, in, 90, [&](const Output& r) {
            if (!r.pain) return;
            started = true;
            for (std::size_t i = 1; i < part_count; ++i) light = light && r.torque[i] <= 0.5f * torque_limit(static_cast<Part>(i), 1) + 1e-3f;
            V3 linear{}, angular{};
            change_of(in, r, linear, angular);
            kept = kept && length(linear) < 0.02f && length(angular) < 0.02f;
            change_of(in, r, [](Part p) { return is_trunk(p); }, linear, angular);
            // Lifting his arms presses his trunk into the ground (their weight); along the ground or round it, next to nothing.
            trunk_kept = trunk_kept && std::hypot(linear[0], linear[2]) < 0.3f && length(angular) < 0.5f;
            arms_led = arms_led && (r.driven[l_arm] || r.driven[r_arm]);
        });
        check(started && o.phase == Phase::settle, "Lying still and hurt, he reacts to it");
        check(arms_led, "A hand goes to what hurts");
        check(light, "The pain reaction is moderate (at most half a joint's strength: an arm lifted to the spot, pressed there)");
        check(kept, "The pain reaction never moves the body as a whole");
        check(trunk_kept, "The pain reaction never drags or rolls his trunk");
        check(o.driven[l_shin] && o.driven[r_shin], "Hurt and lying still with his feet down, the knees draw up a little");
        // Moving again stops it at once.
        const Output moved = m.step(sliding);
        bool any_goal = false;
        for (std::size_t i = 1; i < part_count; ++i) any_goal = any_goal || moved.driven[i];
        check(!moved.pain && !any_goal, "Moving again stops the pain reaction at once");
        // Tuck off: the arms only.
        Input no_tuck = on_his_back();
        no_tuck.tune.tuck = 0;
        Muscles n;
        n.begin(no_tuck, 9);
        Input nhit = no_tuck;
        nhit.body[head].impact = 5.0f;
        (void)n.step(nhit);
        bool arms_only = true;
        run(n, no_tuck, 120, [&](const Output& r) {
            if (!r.pain) return;
            for (std::size_t i = 1; i < part_count; ++i)
                if (!is_arm(static_cast<Part>(i))) arms_only = arms_only && !r.driven[i];
        });
        check(arms_only, "Tuck off, the pain reaction moves the arms only");
        // Then he lets go and rests.
        o = run(m, in, 60 * 5);
        check(o.phase == Phase::rest && !o.pain, "Then he lets go and rests");
    }

    // The hand goes to what hurts smoothly: its goal moves along a minimum-jerk path, so the arm's torque builds from
    // nothing with no jolt, and letting go it eases off over most of a second instead of dropping the arm.
    {
        Input in = on_his_back();
        in.tune.hold_hurt = 2.0f;
        in.tune.tuck = 0.0f;
        Muscles m;
        m.begin(in, 9);
        Input hit = in;
        hit.body[head].impact = 5.0f;
        (void)m.step(hit);
        std::vector<float> torque, act;
        bool was_pain = false;
        int let_go_steps = 0;
        float held_act = 0, after_act_min = 1;
        run(m, in, 60 * 5, [&](const Output& r) {
            const float t = std::max(r.torque[l_arm], r.torque[r_arm]), a = std::max(r.activation[l_arm], r.activation[r_arm]);
            if (r.pain) {
                torque.push_back(t);
                held_act = a;
            } else if (was_pain && let_go_steps < 12) {
                ++let_go_steps; // the first fifth of a second of letting go
                after_act_min = std::min(after_act_min, a);
            }
            was_pain = was_pain || r.pain;
        });
        float peak = 0, jolt = 0;
        for (std::size_t i = 0; i < torque.size(); ++i) {
            peak = std::max(peak, torque[i]);
            if (i) jolt = std::max(jolt, std::abs(torque[i] - torque[i - 1]));
        }
        check(torque.size() > 30 && peak > 0.5f, "The hand sets off for what hurts");
        check(torque.front() < 0.1f * peak, "The reach starts from nothing (no jolt as it sets off)");
        check(jolt < 0.15f * peak, "The reach builds smoothly, step by step");
        check(let_go_steps > 0 && after_act_min > 0.5f * held_act, "Letting go, the arm eases down instead of dropping");
    }

    // Lying still: next to nothing. The tone of lying there (a few percent) and no motion of its own.
    {
        Input in = on_his_back();
        in.tune.grab_hurt = false;
        Muscles m;
        m.begin(in, 9);
        on_ground(in, {back1}, 3.0f);
        (void)m.step(in);
        for (auto& b : in.body) b.impact = 0;
        const Output out = run(m, in, 60 * 5);
        check(out.phase == Phase::rest, "Lying still he comes to rest");
        bool quiet = true;
        for (std::size_t i = 1; i < part_count; ++i) {
            if (!out.write[i]) continue;
            quiet = quiet && length(out.w[i] - in.body[i].w) < 0.02f && length(out.v[i] - in.body[i].v) < 0.01f;
        }
        check(quiet, "Lying still, the muscles move nothing");
        check(out.tone < 0.05f, "Lying still, the muscles' activation is a few percent");
    }

    // The phases: a hit on the back, a slide, lying still, then rest.
    {
        Input in = standing();
        in.tune.grab_hurt = false;
        move_all(in, {0, 0, -3.0f});
        Muscles m;
        m.begin(in);
        (void)m.step(in);
        on_ground(in, {back1}, 3.0f);
        check(m.step(in).phase == Phase::impact, "A hard hit on the back is the impact");
        in.body[back1].impact = 0;
        Output out = run(m, in, 40);
        check(out.phase == Phase::slide, "Moving along the ground after the impact is a slide");
        move_all(in, {});
        out = run(m, in, 30);
        check(out.phase == Phase::settle, "Lying still the body settles");
        const float settling = out.tone;
        out = run(m, in, 150);
        check(out.phase == Phase::rest && out.tone < settling, "Then it rests, with less tone");
    }

    // There is no catch step any more: whatever the fall, no leg steps.
    {
        Input stumble = leaning(0.1f);
        move_all(stumble, {0, 0, -1.3f});
        on_ground(stumble, {l_foot, r_foot, l_toe, r_toe});
        Muscles m;
        m.begin(stumble, 2);
        bool none = true;
        run(m, stumble, 40, [&](const Output& o) { none = none && o.step < 0 && !o.driven[l_thigh] && !o.driven[r_thigh]; });
        check(none, "A small stumble takes no step: the legs are left to the fall");
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

    // Strength scales the muscles.
    {
        const auto reach = [](float strength) {
            Input in = forward_fall();
            in.strength = strength;
            Muscles m;
            m.begin(in, 1);
            return run(m, in, 15).torque[l_arm];
        };
        check(reach(2.0f) > reach(0.5f), "Strength scales the muscles");
    }

    // The muscles add no more energy than a person's could: in a fall, a few joules a step at most (this input is the
    // same every step, so it is the most they ever add in one).
    {
        bool modest = true;
        for (const Input& in : {forward_fall(), backward_fall(), right_fall()}) {
            Input strong = in;
            strong.strength = 2.0f;
            strong.tune.reaction = 2.0f;
            Muscles m;
            m.begin(strong, 1);
            // At most a few hundred watts at twice a person's strength (a shoulder's peak is 200 to 400 W).
            run(m, strong, 60, [&](const Output& o) { modest = modest && o.work < 8.0f; });
        }
        check(modest, "The muscles add only what a person's could");
    }

    // Whatever the bodies do, the output stays finite and bounded.
    {
        std::uint32_t h = 12345;
        const auto noise = [&h] {
            h ^= h << 13; h ^= h >> 17; h ^= h << 5;
            return static_cast<float>(h & 0xffff) / 32767.5f - 1.0f;
        };
        bool ok = true;
        for (int trial = 0; trial < 20; ++trial) {
            Input in = standing();
            Muscles m;
            m.begin(in, static_cast<std::uint32_t>(trial));
            for (int i = 0; i < 120; ++i) {
                for (std::size_t k = 1; k < part_count; ++k) {
                    auto& b = in.body[k];
                    b.v = V3{noise(), noise(), noise()} * 8.0f;
                    b.w = V3{noise(), noise(), noise()} * 15.0f;
                    b.r = math::multiply(math::rotation(V3{noise(), noise(), noise()} * 0.2f), b.r);
                    b.touching = noise() > 0.3f;
                    b.impact = b.touching ? 6.0f * (noise() + 1) : 0;
                    b.has_point = b.touching;
                    b.point = b.p - V3{0, 0.1f, 0};
                }
                const Output o = m.step(in);
                ok = ok && all_finite(o);
                for (std::size_t k = 1; k < part_count; ++k)
                    ok = ok && (!o.write[k] || (length(o.w[k]) <= std::max(25.0f, length(in.body[k].w)) + 1e-3f &&
                                                length(o.v[k]) <= std::max(40.0f, length(in.body[k].v)) + 1e-3f));
                ok = ok && torques_in_limits(o, 1.0f);
            }
        }
        check(ok, "Whatever the bodies do, every spin, velocity and torque stays finite and bounded");
    }

    // The arms reach the way his momentum takes him from the start of every fall, whatever its kind: with nothing to go
    // on (no lean, no momentum: forwards, the way he faces), off the ground with nothing in reach below (still out), and
    // when the board shoots out ahead of him (his feet go on, he is left behind it: back and down).
    {
        const auto marks = [](Input in, int steps) {
            Muscles m;
            m.begin(in, 1);
            return run(m, in, steps);
        };
        Input slump = standing();
        on_ground(slump, {l_foot, r_foot, l_toe, r_toe});
        const Output still = marks(slump, 24);
        check(still.driven[l_arm] && still.driven[r_arm], "With nothing to go on, the arms still reach");
        check(dot(still.hand_mark[0] - slump.body[l_arm].p, V3{0, 0, -1}) > 0.1f && dot(still.hand_mark[1] - slump.body[r_arm].p, V3{0, 0, -1}) > 0.1f,
              "With nothing to go on, the arms reach forwards (the way he faces)");
        Input up = standing();
        move_all(up, {0, 8.0f, 0}); // thrown up: nothing below him within the look ahead
        const Output high = marks(up, 24);
        check(high.driven[l_arm] && high.driven[r_arm], "Flying with no landing in sight, the arms still reach");
        Input shot = standing();
        on_ground(shot, {l_foot, r_foot, l_toe, r_toe});
        for (const Part p : {l_foot, r_foot, l_toe, r_toe}) shot.body[p].v = {0, 0, -3.0f}; // the feet go on forwards
        const Output back = marks(shot, 24);
        check(back.fall == Fall::backward, "The board shooting out ahead of him is a fall backwards");
        check(dot(back.hand_mark[0] - shot.body[l_arm].p, V3{0, 0, 1}) > 0.0f && dot(back.hand_mark[1] - shot.body[r_arm].p, V3{0, 0, 1}) > 0.0f,
              "The board shooting out ahead, the arms go back");
        // Short of time (onto the face in a fraction of a second) they still go, part of the way.
        Input fast = leaning(0.9f);
        move_all(fast, {0, -2.5f, -2.5f});
        on_ground(fast, {l_foot, r_foot, l_toe, r_toe});
        const Output quick = marks(fast, 12);
        check(quick.driven[l_arm] || quick.driven[r_arm], "Short of time, the arms still reach");
    }

    // A hit flinches the limb hit: for a moment it tenses (25 ms on), then pulls back from it (60 to 90 ms on); a fifth of
    // a second or so later it is over, never a pose held, and the body's momentum is kept throughout. A hit to the chest
    // brings the chin in, briefly.
    {
        Input in = forward_fall();
        for (const Part p : {l_foot, r_foot, l_toe, r_toe}) in.body[p].touching = false;
        Muscles m;
        m.begin(in, 4);
        run(m, in, 10);
        Input hit = in;
        auto& fa = hit.body[l_arm]; // the upper arm (a forearm coming down going down is the catch)
        fa.touching = true;
        fa.impact = 5.0f;
        fa.has_point = true;
        fa.point = fa.p + fa.r[0] * 0.15f - V3{0, 0.05f, 0};
        fa.has_normal = true;
        fa.normal = {0, 1, 0};
        (void)m.step(hit);
        bool tensed_soon = false, pulled = false, never_posed = true, kept = true;
        float last_reflex = 0;
        for (int k = 1; k <= 45; ++k) {
            const Output o = m.step(in);
            const float t = static_cast<float>(k) / 60;
            if (o.reflex[l_arm] && t <= 0.05f) tensed_soon = true;
            if (t > 0.08f && t < 0.25f && dot(spin_of(in, o, l_forearm) - spin_of(in, o, l_arm), in.body[l_arm].r[2]) > 0.05f) pulled = true;
            if (o.reflex[l_forearm] || o.reflex[l_arm]) last_reflex = t;
            if (t < 0.55f) never_posed = never_posed && !o.driven[l_forearm] && !o.driven[l_arm]; // (after 0.6 s it may reach again)
            V3 linear{}, angular{};
            change_of(in, o, linear, angular);
            kept = kept && length(linear) < 0.02f && length(angular) < 0.02f;
        }
        check(tensed_soon, "A hit limb tenses within 50 ms");
        check(pulled, "Then it pulls back from the hit (the elbow bends)");
        check(last_reflex > 0.1f && last_reflex < 0.45f, "The flinch is brief (over in under half a second)");
        check(never_posed, "A flinch is never a pose");
        check(kept, "A flinch keeps the body's momentum");
        // The chest hit: the chin comes in, 30 to 60 ms on, and it is over by half a second.
        Input lying = on_his_back();
        move_all(lying, {1.0f, 0, 0});
        Muscles c;
        c.begin(lying, 4);
        run(c, lying, 30);
        Input chest_hit = lying;
        chest_hit.body[chest2].impact = 4.0f;
        (void)c.step(chest_hit);
        float first = -1, last = -1;
        for (int k = 1; k <= 45; ++k) {
            const Output o = c.step(lying);
            if (o.reflex[neck]) {
                if (first < 0) first = static_cast<float>(k) / 60;
                last = static_cast<float>(k) / 60;
            }
        }
        check(first > 0.02f && first < 0.07f, "A chest hit brings the chin in 30 to 60 ms after it");
        check(last > 0 && last < 0.5f, "The chin's flinch is over within half a second");
    }

    // What hurts, as soon as he is down: not while he tumbles, and within half a second of being down and slow.
    {
        Input roll = rolling();
        Muscles m;
        m.begin(roll, 6);
        Input hit = roll;
        hit.body[l_shin].touching = true;
        hit.body[l_shin].impact = 5.0f;
        (void)m.step(hit);
        bool none = true;
        run(m, roll, 90, [&](const Output& o) { none = none && !o.pain; });
        check(none, "No reaction to what hurts while he tumbles");
        Input still = on_his_back();
        int steps = 0;
        bool started = false;
        run(m, still, 60, [&](const Output& o) {
            if (!started) ++steps;
            started = started || o.pain;
        });
        check(started && steps <= 30, "Once he is down and slow he reacts to what hurts within half a second");
        // From a hit as he lands: it starts within half a second.
        Muscles l;
        int to_pain = 0;
        Input on_back = on_his_back();
        l.begin(on_back, 6);
        Input knock = on_back;
        knock.body[l_shin].impact = 5.0f;
        (void)l.step(knock);
        bool on = false;
        run(l, on_back, 60, [&](const Output& o) {
            if (!on) ++to_pain;
            on = on || o.pain;
        });
        check(on && to_pain <= 30, "A knee hit on landing: the reaction starts within half a second");
    }

    // An elbow that hurts: the other hand goes to it, the hurt arm is held in.
    {
        Muscles m;
        Input in;
        const Output o = hurt_on_back(m, l_forearm, 5.0f, 60, &in);
        check(o.pain && o.hurt == Hurt::left_arm, "A hit on the left forearm hurts the left arm");
        check(o.driven[r_arm] && mark_to(o, 1, in.body[l_forearm].p) < 0.12f, "The right hand goes to the left elbow");
        check(o.driven[l_arm] && o.driven[l_forearm], "The hurt arm is held in");
        check(!o.leg_lifted[0] && !o.leg_lifted[1], "An elbow lifts no leg");
    }
    // A knee that hurts: the hands go to it and it comes up to the chest (that leg only).
    {
        Muscles m;
        Input in;
        const Output o = hurt_on_back(m, l_shin, 5.0f, 60, &in);
        check(o.pain && o.hurt == Hurt::left_leg, "A hit on the left shin hurts the left leg");
        check(mark_to(o, 0, in.body[l_shin].p) < 0.15f && mark_to(o, 1, in.body[l_shin].p) < 0.15f, "Both hands go to the hurt knee");
        check(o.leg_lifted[0] && !o.leg_lifted[1], "Only the hurt leg comes up off the ground");
        check(dot(spin_of(in, o, l_thigh) - spin_of(in, o, pelvis), in.body[pelvis].r[2]) < 0, "The hurt knee is drawn towards the chest");
    }
    // The head: both hands to it.
    {
        Muscles m;
        Input in;
        const Output o = hurt_on_back(m, head, 5.0f, 90, &in);
        check(o.pain && o.hurt == Hurt::head, "A hit on the head hurts the head");
        const V3 at = in.body[head].p + in.body[head].r[0] * 0.064f;
        check(mark_to(o, 0, at) < 0.2f && mark_to(o, 1, at) < 0.2f, "Both hands go to the head");
        check(!o.leg_lifted[0] && !o.leg_lifted[1], "A head hit lifts no leg off the ground");
    }
    // Racked on a rail: both hands to the groin at once, the knees drawn to the chest, the back rounded, held longest.
    {
        Input in = racked();
        Muscles m;
        m.begin(in, 3);
        (void)m.step(in);
        Input sat = in;
        sat.body[pelvis].impact = 0;
        int to_pain = 0;
        bool on = false;
        const Output o = run(m, sat, 90, [&](const Output& r) {
            if (!on) ++to_pain;
            on = on || r.pain;
        });
        const V3 groin = sat.body[pelvis].p - sat.body[pelvis].r[0] * 0.12f + sat.body[pelvis].r[1] * 0.07f;
        check(o.hurt == Hurt::groin && on && to_pain <= 12, "Racked: the reaction starts at once (within 0.2 s)");
        check(mark_to(o, 0, groin) < 0.06f && mark_to(o, 1, groin) < 0.06f && o.driven[l_arm] && o.driven[r_arm], "Racked: both hands go to the groin");
        check(o.leg_lifted[0] && o.leg_lifted[1], "Racked: both knees come up");
        check(dot(spin_of(sat, o, l_thigh) - spin_of(sat, o, pelvis), sat.body[pelvis].r[2]) < 0 &&
                  dot(spin_of(sat, o, r_thigh) - spin_of(sat, o, pelvis), sat.body[pelvis].r[2]) < 0,
              "Racked: both knees are drawn towards the chest");
        check(o.driven[spine] && o.driven[chest3], "Racked: the back rounds");
        V3 linear{}, angular{};
        change_of(sat, o, linear, angular);
        check(length(linear) < 0.02f && length(angular) < 0.02f, "Racked: the curl keeps the body's momentum");
        // Held longest: longer than a knee hit as hard.
        int racked_for = 0, knee_for = 0;
        run(m, sat, 60 * 16, [&](const Output& r) { racked_for += r.pain ? 1 : 0; });
        Muscles k;
        Input lying;
        (void)hurt_on_back(k, l_shin, 3.5f, 0, &lying);
        run(k, lying, 60 * 16, [&](const Output& r) { knee_for += r.pain ? 1 : 0; });
        check(racked_for > knee_for + 120, "Racked is held longest");
        // Sitting down hard on flat ground is not racked: the legs do not hang.
        Input seat = on_his_back();
        seat.body[pelvis].impact = 3.5f;
        Muscles f;
        f.begin(seat, 3);
        check(f.step(seat).hurt != Hurt::groin, "Sitting down hard on the ground is not racked");
    }

    // Head protection pulls the chin in as hard as its slider says.
    {
        const auto chin = [](float head_k) {
            Input in = backward_fall();
            in.tune.head = head_k;
            Muscles m;
            m.begin(in, 2);
            return run(m, in, 15).torque[neck];
        };
        check(chin(1.5f) > 1.5f * chin(0.5f), "Head protection scales how hard the chin is pulled in");
    }

    // The board: with his feet on it as the bail starts he kicks it away once, out in front going over backwards, at 2 to
    // 4 m/s; not with his feet on the ground, nor with Kick the board away off. When the board gets the kick, the leg gets
    // it back: the body's momentum changes by the board's, that once.
    {
        const auto kicks = [](Input in, int& count, V3& dv, int& leg_steps, float& at) {
            Muscles m;
            m.begin(in, 5);
            count = 0;
            leg_steps = 0;
            at = -1;
            for (int k = 1; k <= 60; ++k) {
                const Output o = m.step(in);
                if (o.board_kick) {
                    ++count;
                    dv = o.board_dv;
                    at = static_cast<float>(k) / 60;
                }
                leg_steps += o.kick_leg != 0 ? 1 : 0;
            }
        };
        int count{}, leg_steps{};
        V3 dv{};
        float at{};
        kicks(on_board_backwards(), count, dv, leg_steps, at);
        check(count == 1, "Feet on the board: he kicks it away, once");
        check(length(dv) >= 2.0f && length(dv) <= 4.0f && dot(dv, V3{0, 0, -1}) > 0.9f * length(dv), "Going over backwards the board goes out in front, 2 to 4 m/s");
        check(leg_steps >= 6 && leg_steps <= 12, "The kick lasts 100 to 200 ms");
        check(at > 0.17f && at < 0.4f, "The kick comes 170 to 300 ms into the bail");
        kicks(backward_fall(), count, dv, leg_steps, at);
        check(count == 0 && leg_steps == 0, "Feet on the ground: no kick");
        Input off = on_board_backwards();
        off.tune.kick_board = false;
        kicks(off, count, dv, leg_steps, at);
        check(count == 0 && leg_steps == 0, "Kick the board away off: no kick");
        // Written to the board: the body gets the push back, exactly; other steps keep its momentum.
        Input write = on_board_backwards();
        write.board.writable = true;
        Muscles m;
        m.begin(write, 5);
        bool honest = true, kept = true;
        for (int k = 1; k <= 40; ++k) {
            const Output o = m.step(write);
            V3 linear{}, angular{};
            change_of(write, o, linear, angular);
            if (o.board_kick) honest = length(linear + o.board_dv * 2.5f) < 0.05f;
            else kept = kept && length(linear) < 0.02f && length(angular) < 0.02f;
        }
        check(honest, "Kicked, the body gets back what the board gets (2.5 kg x its change)");
        check(kept, "Before and after the kick the momentum is kept");
    }

    // What hurts is remembered by place: a hit counts once (lying or sliding on the spot adds nothing), knocks on one place
    // add up (two on an elbow outweigh one harder on the hip), and a hurt arm is weaker for the rest of the bail.
    {
        Input lying = on_his_back();
        Muscles m;
        m.begin(lying, 2);
        Input pressed = lying;
        pressed.body[l_forearm].impact = 3.5f;
        (void)m.step(pressed);
        const float once = m.step(pressed).hurt_amount;
        float most = once;
        run(m, pressed, 60, [&](const Output& o) { most = std::max(most, o.hurt_amount); });
        check(once > 1.0f && most <= once + 1e-4f, "A hit counts once: lying on the spot adds nothing");
        // Two knocks on the left elbow, then one harder on the right hip: he holds the elbow.
        Muscles k;
        Input base = on_his_back();
        for (auto& b : base.body) b.touching = false;
        k.begin(base, 2);
        Input knock = base;
        knock.body[l_forearm].touching = true;
        knock.body[l_forearm].impact = 3.5f;
        (void)k.step(knock);
        run(k, base, 9); // off the ground a moment (a contact that only flickers is the same hit)
        (void)k.step(knock);
        run(k, base, 9);
        Input hip = base;
        hip.body[pelvis].touching = true;
        hip.body[pelvis].impact = 4.5f;
        hip.body[pelvis].has_point = true;
        hip.body[pelvis].point = hip.body[pelvis].p + V3{0.13f, 0, 0}; // his right side
        check(k.step(hip).hurt == Hurt::left_arm, "Two knocks on one elbow outweigh one harder knock on the hip");
        // The hurt arm is weaker: lying there (no reaction to it, so both arms have only their tone), a hard knock on the
        // right hand; a second later both arms are flung the same: the right one resists less.
        Input still = on_his_back();
        still.tune.grab_hurt = false;
        Muscles w;
        w.begin(still, 1);
        run(w, still, 60);
        Input knocked = still;
        knocked.body[r_hand].impact = 6.0f;
        (void)w.step(knocked);
        run(w, still, 60);
        Input pushed = still;
        for (const Part p : {l_arm, r_arm, l_forearm, r_forearm, l_hand, r_hand}) pushed.body[p].w = pushed.body[p].w + V3{0, 30.0f, 0};
        float l = 0, r = 0;
        run(w, pushed, 3, [&](const Output& o) {
            l = std::max(l, o.torque[l_arm]);
            r = std::max(r, o.torque[r_arm]);
        });
        check(r < 0.8f * l, "A hurt arm is weaker than the other");
    }

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real bails model: all checks passed\n";
    return 0;
}
