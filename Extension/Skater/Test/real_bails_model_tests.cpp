// Real Bails' muscles (Extension/Skater/real_bails_model.h) on a made-up ragdoll. The bail is the ragdoll's: what the
// muscles must never do is snap the body on the first step, push or spin the body as a whole, use more torque than a
// person has, drive the legs or back towards any pose while he falls, rolls or slides, lift the legs, hold the head
// against a hard slam, react to pain while he still moves, or keep moving him once he lies still. What they must do:
// bring the arms out towards where he will land (forwards, to the side, back and down), let them give once down, and
// stay finite whatever the bodies do.
#include "Extension/Skater/real_bails_body.h"
#include "Extension/Skater/real_bails_model.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
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
                passive = passive && !o.driven[i];
                if (is_arm(static_cast<Part>(i))) arms_loose = arms_loose && o.activation[i] < 0.2f;
            }
            passive = passive && o.phase != Phase::settle && o.phase != Phase::rest && !o.pain;
        });
        check(passive, "Falling, rolling or sliding, no leg, back or neck joint is driven towards a pose");
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
        const Output fwd = aims(forward_fall(), 15);
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
        const Output back = aims(backward_fall(), 15);
        check(back.fall == Fall::backward, "Tipping backwards is read as a backward fall");
        const Input b_in = backward_fall();
        for (const Part shoulder : {l_arm, r_arm}) {
            const std::size_t s = shoulder == l_arm ? 0 : 1;
            const V3 off = back.hand_mark[s] - b_in.body[shoulder].p;
            check(dot(off, V3{0, 0, 1}) > 0.05f, "Falling backwards, each arm aims behind him");
            check(dot(off, V3{shoulder == l_arm ? -1.0f : 1.0f, 0, 0}) > 0.1f, "Falling backwards, each arm aims out to its side");
        }
        const Output side = aims(right_fall(), 15);
        check(side.fall == Fall::right, "Tipping to his right is read as a fall to the right");
        const Input s_in = right_fall();
        check(dot(side.hand_mark[1] - s_in.body[r_arm].p, V3{1, 0, 0}) > 0.15f, "Falling to his right, the right hand reaches out to the right");
        check(dot(side.hand_mark[0] - s_in.body[l_arm].p, V3{1, 0, 0}) > 0.0f, "Falling to his right, the left hand comes towards the right too");
        Input off = forward_fall();
        off.tune.catch_arms = false;
        const Output still = aims(off, 15);
        check(!still.driven[l_arm] && !still.driven[r_arm], "Catch with the arms off, the arms do not reach");
    }

    // A hand landing is the catch: the fall goes on, the hand is the ground's, and the arm has no goal any more: for a
    // moment it resists folding as hard as it can, then it is loose.
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
        check(!giving.driven[l_arm] && !giving.driven[l_forearm], "Once down, the arm has no goal");
        check(giving.activation[l_forearm] > 0.3f, "Once down, the arm resists folding for a moment");
        const Output loose = run(m, in, 20);
        check(loose.activation[l_forearm] < 0.15f, "Then it is loose");
    }

    // The neck: tone only, but falling backwards the chin tucks in (lightly); falling forwards the head is left alone.
    {
        Muscles m;
        const Input in = backward_fall();
        m.begin(in, 2);
        const Output back = run(m, in, 15);
        check(back.driven[neck] && dot(spin_of(in, back, neck) - spin_of(in, back, chest3), in.body[chest3].r[2]) > 0,
              "Falling backwards, the chin tucks");
        check(back.torque[neck] < 0.35f * torque_limit(neck, 1), "Falling backwards, the chin's pull is a light one");
        Muscles f;
        const Input fin = forward_fall();
        f.begin(fin, 2);
        check(!run(f, fin, 15).driven[neck], "Falling forwards, the head is left to the fall");
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
        // Still sliding along: nothing.
        Input sliding = in;
        move_all(sliding, {2.5f, 0, 0});
        bool none = true;
        run(m, sliding, 90, [&](const Output& o) { none = none && !o.pain; });
        check(none, "No pain reaction while he still moves");
        // Lying still: after a moment, the reaction.
        bool started = false, light = true, kept = true, trunk_kept = true, arms_led = true;
        Output o = run(m, in, 90, [&](const Output& r) {
            if (!r.pain) return;
            started = true;
            for (std::size_t i = 1; i < part_count; ++i) light = light && r.torque[i] <= 0.16f * torque_limit(static_cast<Part>(i), 1) + 1e-3f;
            V3 linear{}, angular{};
            change_of(in, r, linear, angular);
            kept = kept && length(linear) < 0.02f && length(angular) < 0.02f;
            change_of(in, r, [](Part p) { return is_trunk(p); }, linear, angular);
            trunk_kept = trunk_kept && length(linear) < 0.3f && length(angular) < 0.5f;
            arms_led = arms_led && (r.driven[l_arm] || r.driven[r_arm]);
        });
        check(started && o.phase == Phase::settle, "Lying still and hurt, he reacts to it");
        check(arms_led, "A hand goes to what hurts");
        check(light, "The pain reaction is light (a few Nm)");
        check(kept, "The pain reaction never moves the body as a whole");
        check(trunk_kept, "The pain reaction hardly moves his trunk");
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

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real bails model: all checks passed\n";
    return 0;
}
