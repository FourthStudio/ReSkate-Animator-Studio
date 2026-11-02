#pragma once
#include "real_bails_model.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Real Bails' body as rigid bodies (real_bails_model.cpp): each body's mass, centre and inertia, the whole body's centre,
// momentum and spin, and the linked bodies' response to a torque at any joint (Tree).
//
// A muscle is inside the body: it turns one side of its joint one way and the other side the other way, and the whole
// body's momentum stays as it was. How much each side turns is the linked bodies' own business: a light hand turns a lot
// against a heavy forearm, a hip turns the leg and, a little, the whole of him the other way.
namespace dingosdk::real_bails::body {
using math::M3;
using math::V3;
using model::BodyIn;
using model::Part;
using model::part_count;
constexpr V3 world_up{0, 1, 0};
constexpr float gravity = 9.81f;

struct Shape {
    float mass{};   // kg
    float along{};  // centre of mass from the body's joint, along its bone (x), m
    float ahead{};  // and along its y (the head's sits forward of the neck), m
    V3 moments{};   // principal moments about the centre, in the body's own axes (x along the bone), kg m^2
    float radius{}; // its surface from its centre, for where it meets the ground, m
    float torque{}; // the most its joint's muscles turn it with, Nm: a fit adult's isometric strength
};
// About an 80 kg skater. The centres were fitted from the recorded bails (each joint's two velocities match at them). The
// moments are those of the collision capsules fitted to the ragdoll (the offline test bench's rig, from the recorded
// bails), in each body's axes (x along its bone). The torques are the joints' strongest (neck 30, shoulder 70, elbow 60,
// wrist 12, back 80 per joint of four, hip 200, knee 220, ankle 120 Nm), so whatever the tuning a muscle is never
// stronger than a person's.
inline constexpr std::array<Shape, part_count> shapes{{
    {},                                                               // board: not a body of his
    {5.0f, .064f, .025f, {.0226f, .0233f, .0233f}, .10f, 12},         // head (on its neck: nodding, turning)
    {1.5f, .068f, 0, {.0019f, .0034f, .0034f}, .05f, 30},             // neck
    {0.5f, .068f, 0, {.0003f, .0010f, .0010f}, .04f, 12},             // left hand (the wrist)
    {1.3f, .15f, 0, {.0011f, .0080f, .0080f}, .045f, 60},             // left forearm (the elbow)
    {2.0f, .16f, 0, {.0025f, .0153f, .0153f}, .05f, 70},              // left upper arm (the shoulder)
    {1.5f, .11f, 0, {.0019f, .0041f, .0041f}, .05f, 40},              // left collarbone
    {0.5f, .068f, 0, {.0003f, .0010f, .0010f}, .04f, 12},             // right hand
    {1.3f, .15f, 0, {.0011f, .0080f, .0080f}, .045f, 60},             // right forearm
    {2.0f, .16f, 0, {.0025f, .0153f, .0153f}, .05f, 70},              // right upper arm
    {1.5f, .11f, 0, {.0019f, .0041f, .0041f}, .05f, 40},              // right collarbone
    {8.0f, .078f, 0, {.0883f, .0883f, .0484f}, .12f, 80},             // upper chest
    {6.0f, .097f, 0, {.0602f, .0602f, .0363f}, .12f, 80},             // chest
    {5.0f, .047f, 0, {.0453f, .0453f, .0276f}, .12f, 80},             // lower back
    {5.0f, .038f, 0, {.0429f, .0429f, .0250f}, .12f, 80},             // spine
    {0.3f, .025f, 0, {.0001f, .0002f, .0002f}, .025f, 15},            // left toes
    {1.0f, .055f, 0, {.0016f, .0027f, .0033f}, .045f, 120},           // left foot (the ankle)
    {4.0f, .238f, 0, {.0060f, .0632f, .0632f}, .055f, 220},           // left shin (the knee)
    {9.0f, .22f, 0, {.0253f, .1297f, .1297f}, .08f, 200},             // left thigh (the hip)
    {0.3f, .025f, 0, {.0001f, .0002f, .0002f}, .025f, 15},            // right toes
    {1.0f, .055f, 0, {.0017f, .0025f, .0034f}, .045f, 120},           // right foot
    {4.0f, .238f, 0, {.0060f, .0632f, .0632f}, .055f, 220},           // right shin
    {9.0f, .22f, 0, {.0253f, .1297f, .1297f}, .08f, 200},             // right thigh
    {11.0f, 0, 0, {.0952f, .0952f, .0665f}, .13f, 0},                 // pelvis (the root: no joint of its own)
}};

// The bodies from the pelvis outwards: every body comes after its parent.
inline constexpr std::array<Part, part_count - 1> chain{model::pelvis, model::spine, model::back1, model::chest2, model::chest3,
    model::neck, model::head, model::l_collar, model::l_arm, model::l_forearm, model::l_hand, model::r_collar, model::r_arm,
    model::r_forearm, model::r_hand, model::l_thigh, model::l_shin, model::l_foot, model::l_toe, model::r_thigh, model::r_shin,
    model::r_foot, model::r_toe};

// The whole body at one step. Positions are kept relative to its centre of mass: the world's coordinates are around a
// thousand metres, where a float has only a tenth of a millimetre, and inertias built from them would be noise.
struct Mass {
    float total{};
    V3 com{};      // world
    V3 velocity{}; // of the centre
    V3 angular{};  // angular momentum about the centre
    M3 locked{};   // the inertia about the centre, as if he were rigid
    V3 spin{};     // locked^-1 angular: how fast the body as a whole turns
    float energy{}; // kinetic energy, J
    std::array<V3, part_count> at{};      // each body's centre of mass, from com
    std::array<V3, part_count> joint{};   // each body's joint with its parent (its origin), from com
    std::array<M3, part_count> inertia{}; // each body's own, about its centre, world axes
};
// The game's ragdoll bodies turn as if about three times as hard to turn as their shapes are, with none under 0.005 kg
// m^2 (as physics engines often make a ragdoll's light bodies, so they do not spin wild): the offline test bench fitted
// this to the recorded muscles-off bails (its hands and toes spin as the game's only so). The muscles' sums use the same,
// so the momentum they keep is the game's.
constexpr float inertia_scale = 3.0f, inertia_floor = 0.005f;
inline M3 inertia_of(const M3& r, const V3& moments) noexcept {
    const auto moment = [&](std::size_t k) { return std::max(moments[k] * inertia_scale, inertia_floor); };
    return math::add(math::add(math::scaled(math::outer(r[0], r[0]), moment(0)), math::scaled(math::outer(r[1], r[1]), moment(1))),
                     math::scaled(math::outer(r[2], r[2]), moment(2)));
}
inline Mass measure(const std::array<BodyIn, part_count>& b) noexcept {
    using namespace math;
    Mass m;
    const V3 ref = b[model::pelvis].p;
    V3 first{}, momentum{};
    for (std::size_t i = 1; i < part_count; ++i) {
        const auto& s = shapes[i];
        m.joint[i] = b[i].p - ref;
        m.at[i] = m.joint[i] + b[i].r[0] * s.along + b[i].r[1] * s.ahead;
        m.inertia[i] = inertia_of(b[i].r, s.moments);
        first = first + m.at[i] * s.mass;
        momentum = momentum + b[i].v * s.mass;
        m.total += s.mass;
        m.energy += 0.5f * s.mass * dot(b[i].v, b[i].v) + 0.5f * dot(b[i].w, turn_by(m.inertia[i], b[i].w));
    }
    const V3 offset = first * (1.0f / m.total);
    m.com = ref + offset;
    m.velocity = momentum * (1.0f / m.total);
    for (std::size_t i = 1; i < part_count; ++i) {
        m.at[i] = m.at[i] - offset;
        m.joint[i] = m.joint[i] - offset;
        const float kg = shapes[i].mass;
        m.angular = m.angular + turn_by(m.inertia[i], b[i].w) + cross(m.at[i], b[i].v - m.velocity) * kg;
        m.locked = add(m.locked, add(m.inertia[i], point_inertia(kg, m.at[i])));
    }
    m.spin = turn_by(inverse(m.locked), m.angular);
    return m;
}

// How each joint turns. Elbows and knees are hinges about the upper bone's z (the game holds their other two axes). A
// collarbone is held to the upper chest by the shoulder blade's muscles and the ribs under it: in a person it moves only
// a little and only with the arm, so for the muscles it is part of the upper chest (welded), and what little it moves is
// the game's own joint's (its range is small). Light as it is, left free it is what a shoulder's torque would spin, round
// its own length, instead of the arm. Every other joint turns any way.
enum class Kind : std::uint8_t { ball, hinge, weld };
constexpr Kind kind_of(Part c) noexcept {
    return c == model::l_forearm || c == model::r_forearm || c == model::l_shin || c == model::r_shin ? Kind::hinge
           : c == model::l_collar || c == model::r_collar                                          ? Kind::weld
                                                                                                    : Kind::ball;
}
constexpr bool hinge(Part c) noexcept { return kind_of(c) == Kind::hinge; }
constexpr bool weld(Part c) noexcept { return kind_of(c) == Kind::weld; }
// Whether body `i` is `c` or beyond it (in its subtree).
inline bool beyond(Part c, std::size_t i) noexcept {
    for (std::size_t k = i; k != 0; k = model::parent_of[k]) {
        if (k == c) return true;
        if (k == model::pelvis) break;
    }
    return false;
}

// A spatial vector (angular, linear part) and a spatial inertia [[a, b], [b^T, c]], about com in world axes.
struct V6 {
    V3 a{}, l{};
};
struct I6 {
    M3 a{}, b{}, c{};
};
inline V6 times(const I6& m, const V6& v) noexcept {
    using namespace math;
    return {turn_by(m.a, v.a) + turn_by(m.b, v.l), turn_by(transposed(m.b), v.a) + turn_by(m.c, v.l)};
}

// The body as the tree of linked rigid bodies it is: Featherstone's articulated-body method, for impulses. Given an
// angular impulse at each joint (a muscle's torque over the step, or a stop's), it gives every body's change of spin and
// velocity as the linked bodies move. Only impulses between his own parts go in, so the body's momentum, linear and
// angular, stays exactly as it was (nothing pushes or spins him as a whole), and every joint stays together (the game's
// solver has nothing to undo).
//
// No joint of a person is free: each has its muscles' tone, which resists how fast it turns. Each joint's `armature` is
// that tone's stiffness and damping over the step as an inertia of the joint's own (damping x dt + stiffness x dt^2),
// so the tone's torque from the motion within the step is part of the solve: the stable PD, for the whole body at once.
class Tree {
public:
    // What does not depend on the tone: each joint's axis, far side and weight. Leaves the joints free.
    void build(const Mass& m, const std::array<BodyIn, part_count>& b) noexcept;
    // The linked bodies with each joint's armature (by child; a hinge's in [0][0]).
    void articulate(const std::array<M3, part_count>& armature) noexcept;
    // Each joint's angular impulse (by child; a hinge's along its axis) to each body's change of spin and velocity.
    void solve(const std::array<V3, part_count>& impulse, std::array<V3, part_count>& dw, std::array<V3, part_count>& dv) const noexcept;
    // Each joint's inertia as it presents it now: the impulse per unit change of its own turn (child against parent) from
    // an impulse there and nowhere else (a hinge's in [0][0]; a weld's nothing).
    std::array<M3, part_count> respond() const noexcept;

    std::array<V3, part_count> axis{};         // a hinge's axis (world)
    std::array<float, part_count> mass_out{};
    std::array<V3, part_count> velocity_out{}; // the far side's centre's velocity now
    std::array<V3, part_count> hold{};         // the torque that holds the far side up against its weight

private:
    const Mass* mass_{};
    std::array<I6, part_count> articulated_{};
    std::array<M3, part_count> ua_{}, ul_{}, d_inv_{}; // U = I^A S (its two 3x3 halves) and (S^T U + armature)^-1; a hinge's in column 0
    std::array<float, 36> root_inv_{};
};

inline void Tree::articulate(const std::array<M3, part_count>& armature) noexcept {
    using namespace math;
    const Mass& m = *mass_;
    for (std::size_t i = 1; i < part_count; ++i) {
        const float kg = shapes[i].mass;
        articulated_[i] = {add(m.inertia[i], point_inertia(kg, m.at[i])), scaled(skew(m.at[i]), kg), scaled(identity3(), kg)};
    }
    // Inwards: each joint's subtree as an articulated body, folded into its parent. A ball joint's motion is S = [E; [x]]
    // (a turn about its point x), a hinge's S = [n; x x n], a weld's nothing (its subtree moves with the parent).
    for (std::size_t k = chain.size(); k-- > 1;) {
        const Part c = chain[k], p = model::parent_of[c];
        const I6& ia = articulated_[c];
        const V3 x = m.joint[c];
        M3 ua{}, ul{}, d_inv{};
        I6 fold = ia;
        if (weld(c)) {
            // Nothing to fold out: the whole of it rides on the parent.
        } else if (hinge(c)) {
            const V3 n = axis[c];
            const V6 un = times(ia, {n, cross(x, n)});
            const float d = dot(n, un.a - cross(x, un.l)) + std::max(armature[c][0][0], 0.0f);
            const float inv = d > 1e-9f ? 1.0f / d : 0.0f;
            ua[0] = un.a;
            ul[0] = un.l;
            d_inv[0] = {inv, 0, 0};
            fold.a = subtract(fold.a, scaled(outer(un.a, un.a), inv));
            fold.b = subtract(fold.b, scaled(outer(un.a, un.l), inv));
            fold.c = subtract(fold.c, scaled(outer(un.l, un.l), inv));
        } else {
            const M3 sx = skew(x);
            ua = add(ia.a, multiply(ia.b, sx));
            ul = add(transposed(ia.b), multiply(ia.c, sx));
            d_inv = inverse(add(subtract(ua, multiply(sx, ul)), armature[c]));
            fold.a = subtract(fold.a, multiply(ua, multiply(d_inv, transposed(ua))));
            fold.b = subtract(fold.b, multiply(ua, multiply(d_inv, transposed(ul))));
            fold.c = subtract(fold.c, multiply(ul, multiply(d_inv, transposed(ul))));
        }
        ua_[c] = ua;
        ul_[c] = ul;
        d_inv_[c] = d_inv;
        auto& into = articulated_[p];
        into = {add(into.a, fold.a), add(into.b, fold.b), add(into.c, fold.c)};
    }
    // The pelvis floats: the whole articulated body's 6x6 inertia, inverted (Gauss-Jordan with pivoting).
    {
        const I6& r = articulated_[model::pelvis];
        float a[6][12]{};
        for (int col = 0; col < 3; ++col)
            for (int row = 0; row < 3; ++row) {
                const auto ci = static_cast<std::size_t>(col), ri = static_cast<std::size_t>(row);
                a[row][col] = r.a[ci][ri];
                a[row][col + 3] = r.b[ci][ri];
                a[row + 3][col] = r.b[ri][ci]; // b^T
                a[row + 3][col + 3] = r.c[ci][ri];
            }
        for (int i = 0; i < 6; ++i) a[i][6 + i] = 1;
        bool ok = true;
        for (int col = 0; col < 6 && ok; ++col) {
            int best = col;
            for (int row = col + 1; row < 6; ++row)
                if (std::abs(a[row][col]) > std::abs(a[best][col])) best = row;
            if (!(std::abs(a[best][col]) > 1e-9f)) {
                ok = false;
                break;
            }
            if (best != col)
                for (int k = 0; k < 12; ++k) std::swap(a[col][k], a[best][k]);
            const float inv = 1.0f / a[col][col];
            for (int k = 0; k < 12; ++k) a[col][k] *= inv;
            for (int row = 0; row < 6; ++row) {
                if (row == col) continue;
                const float f = a[row][col];
                if (f != 0)
                    for (int k = 0; k < 12; ++k) a[row][k] -= f * a[col][k];
            }
        }
        for (int row = 0; row < 6; ++row)
            for (int col = 0; col < 6; ++col) root_inv_[static_cast<std::size_t>(row * 6 + col)] = ok ? a[row][6 + col] : 0.0f;
    }
}
inline void Tree::build(const Mass& m, const std::array<BodyIn, part_count>& b) noexcept {
    using namespace math;
    mass_ = &m;
    for (std::size_t k = 1; k < chain.size(); ++k) {
        const Part c = chain[k];
        if (hinge(c)) axis[c] = normalized(b[model::parent_of[c]].r[2], {0, 0, 1});
    }
    // Each joint's far side as one: its mass, velocity, and the torque that holds up its weight.
    std::array<float, part_count> sub_mass{};
    std::array<V3, part_count> sub_first{}, sub_momentum{};
    for (std::size_t i = 1; i < part_count; ++i) {
        sub_mass[i] = shapes[i].mass;
        sub_first[i] = m.at[i] * shapes[i].mass;
        sub_momentum[i] = b[i].v * shapes[i].mass;
    }
    for (std::size_t k = chain.size(); k-- > 1;) {
        const Part c = chain[k], p = model::parent_of[c];
        sub_mass[p] += sub_mass[c];
        sub_first[p] = sub_first[p] + sub_first[c];
        sub_momentum[p] = sub_momentum[p] + sub_momentum[c];
    }
    for (std::size_t k = 1; k < chain.size(); ++k) {
        const Part c = chain[k];
        mass_out[c] = sub_mass[c];
        velocity_out[c] = sub_momentum[c] * (1.0f / sub_mass[c]);
        hold[c] = cross(sub_first[c] * (1.0f / sub_mass[c]) - m.joint[c], world_up * (gravity * sub_mass[c]));
    }
    articulate(std::array<M3, part_count>{}); // free joints until told otherwise
}
inline std::array<M3, part_count> Tree::respond() const noexcept {
    using namespace math;
    std::array<M3, part_count> out{};
    for (std::size_t k = 1; k < chain.size(); ++k) {
        const Part c = chain[k], p = model::parent_of[c];
        if (weld(c)) continue;
        const int axes = hinge(c) ? 1 : 3;
        M3 mobility{};
        for (int e = 0; e < axes; ++e) {
            std::array<V3, part_count> impulse{}, dw{}, dv{};
            V3 unit{};
            unit[static_cast<std::size_t>(e)] = 1;
            impulse[c] = hinge(c) ? axis[c] : unit;
            solve(impulse, dw, dv);
            mobility[static_cast<std::size_t>(e)] = dw[c] - dw[p];
        }
        if (hinge(c)) {
            const float along = dot(axis[c], mobility[0]);
            out[c] = {V3{along > 1e-9f ? 1.0f / along : 0.0f, 0, 0}, V3{}, V3{}};
        } else {
            // Symmetric in theory; kept so against rounding before it is inverted.
            out[c] = inverse(scaled(add(mobility, transposed(mobility)), 0.5f));
        }
    }
    return out;
}
inline void Tree::solve(const std::array<V3, part_count>& impulse, std::array<V3, part_count>& dw, std::array<V3, part_count>& dv) const noexcept {
    using namespace math;
    const Mass& m = *mass_;
    std::array<V6, part_count> bias{}, motion{};
    std::array<V3, part_count> u{};
    // Inwards: what each subtree passes on to its parent.
    for (std::size_t k = chain.size(); k-- > 1;) {
        const Part c = chain[k], p = model::parent_of[c];
        const V3 x = m.joint[c];
        const V6& f = bias[c];
        V6 pass = f;
        if (weld(c)) {
            // It all goes on to the parent, the impulse at it too (welded, it has nowhere else to go).
        } else if (hinge(c)) {
            const V3 moment = f.a - cross(x, f.l); // S^T f: the moment about the joint
            u[c] = {dot(axis[c], impulse[c]) - dot(axis[c], moment), 0, 0};
            const float s = d_inv_[c][0][0] * u[c][0];
            pass.a = pass.a + ua_[c][0] * s;
            pass.l = pass.l + ul_[c][0] * s;
        } else {
            const V3 moment = f.a - cross(x, f.l);
            u[c] = impulse[c] - moment;
            const V3 s = turn_by(d_inv_[c], u[c]);
            pass.a = pass.a + turn_by(ua_[c], s);
            pass.l = pass.l + turn_by(ul_[c], s);
        }
        bias[p].a = bias[p].a + pass.a;
        bias[p].l = bias[p].l + pass.l;
    }
    // The pelvis, then outwards.
    {
        const V6& f = bias[model::pelvis];
        const float in[6]{f.a[0], f.a[1], f.a[2], f.l[0], f.l[1], f.l[2]};
        float o[6]{};
        for (std::size_t row = 0; row < 6; ++row)
            for (std::size_t col = 0; col < 6; ++col) o[row] -= root_inv_[row * 6 + col] * in[col];
        motion[model::pelvis] = {V3{o[0], o[1], o[2]}, V3{o[3], o[4], o[5]}};
    }
    for (std::size_t k = 1; k < chain.size(); ++k) {
        const Part c = chain[k], p = model::parent_of[c];
        const V3 x = m.joint[c];
        const V6& vp = motion[p];
        V3 turn{};
        if (weld(c)) turn = {};
        else if (hinge(c)) turn = axis[c] * (d_inv_[c][0][0] * (u[c][0] - dot(ua_[c][0], vp.a) - dot(ul_[c][0], vp.l)));
        else turn = turn_by(d_inv_[c], u[c] - turn_back(ua_[c], vp.a) - turn_back(ul_[c], vp.l));
        motion[c] = {vp.a + turn, vp.l + cross(x, turn)};
    }
    dw = {};
    dv = {};
    for (std::size_t i = 1; i < part_count; ++i) {
        dw[i] = motion[i].a;
        dv[i] = motion[i].l + cross(motion[i].a, m.at[i]); // the velocity at com's point, carried to the body's centre
    }
}

// The changes `dw`, `dv` (per body) as momentum: linear, and angular about com.
inline void momentum_of(const Mass& m, const std::array<V3, part_count>& dw, const std::array<V3, part_count>& dv, V3& linear,
                        V3& angular) noexcept {
    using namespace math;
    linear = {};
    angular = {};
    for (std::size_t i = 1; i < part_count; ++i) {
        linear = linear + dv[i] * shapes[i].mass;
        angular = angular + turn_by(m.inertia[i], dw[i]) + cross(m.at[i], dv[i]) * shapes[i].mass;
    }
}
// The kinetic energy the changes add (negative: they take it out), J.
inline float energy_of(const Mass& m, const std::array<BodyIn, part_count>& b, const std::array<V3, part_count>& dw,
                       const std::array<V3, part_count>& dv, const std::array<bool, part_count>& applied) noexcept {
    using namespace math;
    float e = 0;
    for (std::size_t i = 1; i < part_count; ++i) {
        if (!applied[i]) continue;
        const V3 v = b[i].v + dv[i], w = b[i].w + dw[i];
        e += 0.5f * shapes[i].mass * (dot(v, v) - dot(b[i].v, b[i].v)) +
             0.5f * (dot(w, turn_by(m.inertia[i], w)) - dot(b[i].w, turn_by(m.inertia[i], b[i].w)));
    }
    return e;
}
}
