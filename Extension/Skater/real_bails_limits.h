#pragma once
// Real Bails' joint ranges (real_bails_model.cpp): every joint's stops, where a joint is in its range, the speed into a
// stop cut so the joint comes to rest on it, and a target moved into the range. Every number was measured from the game's
// ragdoll (bails recorded with the muscles off, 2026-11-02) or is a human range inside it, so the muscles never take a
// joint past where a skater's goes, and the stops hold whatever the muscles or the slam do.
//
// The rig, as the recordings show it: every body's bone runs along its own x axis. The torso bodies (pelvis, spine,
// back1, chest2, chest3, neck, head) have y forward and z to his left, so a +z turn bends forward. Elbows and knees are
// hinges about the upper bone's z (the game holds the other two axes to within a few degrees) and bend +z on both sides;
// the game stops the elbow at about -4 and 140-150 degrees and the knee at 10 and 150-160.
#include "real_bails_model.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace dingosdk::real_bails::limits {
using namespace math;
using model::Part;
constexpr float deg = 3.14159265f / 180.0f;

enum class Kind : std::uint8_t { hinge, cone, box };
struct Range { float lo, hi; };
struct Limit {
    Part child{};
    Part frame{};      // the body the range is measured in: the parent, or chest3 for a shoulder
    Kind kind{};
    M3 zero{};         // the middle of the range, in `frame`'s axes (columns). Unused for a hinge.
    bool zero_is_rest{}; // box: the middle is the joint's pose at the bail's start (rest_), clamped
    Range r[3]{};      // hinge: r[2] = bend about z. box: r[k] about zero's axis k. cone: r[0] = twist about the bone,
                       // r[1] = swing half-angle about zero's z, r[2] = about zero's y (an ellipse)
};
inline M3 identity() { return {V3{1, 0, 0}, V3{0, 1, 0}, V3{0, 0, 1}}; }
// Left to right: the same joint frame seen in the mirror (z flipped).
inline M3 mirrored(const M3& m) {
    M3 o = m;
    for (auto& c : o) c[2] = -c[2];
    o[2] = -o[2];
    return o;
}
inline M3 columns(V3 x, V3 y, V3 z) { return {x, y, z}; }

inline std::array<Limit, 22> table() {
    using namespace model;
    const Range none{-0.0f, 0.0f};
    // Shoulder: in chest3's axes, the middle of the arm's reach (75 deg up from hanging, 47 deg in front of the side), a
    // 95 deg cone round it, the twist +70 inwards / -90 outwards. Hip: in the pelvis's axes, 47 deg forward and 8 out,
    // 75 deg forward/back and 45 deg in/out round that, twist +40 in / -50 out.
    const M3 shoulder_l = columns({-0.259f, 0.706f, 0.659f}, {0.706f, 0.604f, -0.370f}, {-0.659f, 0.370f, -0.655f});
    const M3 hip_l = columns({-0.675f, 0.724f, 0.139f}, {-0.724f, -0.687f, 0.060f}, {0.139f, -0.060f, 0.988f});
    const auto box = [](Part c, Part f, bool rest, Range twist, Range side, Range flex) {
        return Limit{c, f, Kind::box, identity(), rest, {twist, side, flex}};
    };
    const auto d = [](float lo, float hi) { return Range{lo * deg, hi * deg}; };
    return {{
        box(spine, pelvis, true, d(-18, 18), d(-18, 18), d(-20, 28)),   // each back joint: round its riding pose
        box(back1, spine, true, d(-18, 18), d(-18, 18), d(-20, 28)),
        box(chest2, back1, true, d(-18, 18), d(-18, 18), d(-20, 28)),
        box(chest3, chest2, true, d(-18, 18), d(-18, 18), d(-20, 28)),
        box(neck, chest3, false, d(-30, 30), d(-25, 25), d(-25, 40)),
        box(head, neck, false, d(-45, 45), d(-25, 25), d(-45, 20)),
        box(l_collar, chest3, true, d(-10, 10), d(-30, 30), d(-28, 28)),
        box(r_collar, chest3, true, d(-10, 10), d(-30, 30), d(-28, 28)),
        Limit{l_arm, chest3, Kind::cone, shoulder_l, false, {d(-90, 70), d(-95, 95), d(-95, 95)}},
        Limit{r_arm, chest3, Kind::cone, mirrored(shoulder_l), false, {d(-70, 90), d(-95, 95), d(-95, 95)}},
        Limit{l_forearm, l_arm, Kind::hinge, identity(), false, {none, none, d(0, 145)}},
        Limit{r_forearm, r_arm, Kind::hinge, identity(), false, {none, none, d(0, 145)}},
        box(l_hand, l_forearm, true, d(-45, 45), d(-70, 70), d(-30, 30)), // y bends to the palm (z) side
        box(r_hand, r_forearm, true, d(-45, 45), d(-70, 70), d(-30, 30)),
        Limit{l_thigh, pelvis, Kind::cone, hip_l, false, {d(-50, 40), d(-75, 75), d(-45, 45)}},
        Limit{r_thigh, pelvis, Kind::cone, mirrored(hip_l), false, {d(-40, 50), d(-75, 75), d(-45, 45)}},
        Limit{l_shin, l_thigh, Kind::hinge, identity(), false, {none, none, d(10, 150)}},
        Limit{r_shin, r_thigh, Kind::hinge, identity(), false, {none, none, d(10, 150)}},
        box(l_foot, l_shin, true, d(-28, 28), d(-35, 35), d(-30, 50)),   // z: - lifts the toes (dorsiflexion)
        box(r_foot, r_shin, true, d(-28, 28), d(-35, 35), d(-30, 50)),
        box(l_toe, l_foot, true, d(-15, 15), d(-15, 15), d(-30, 22)),
        box(r_toe, r_foot, true, d(-15, 15), d(-15, 15), d(-30, 22)),
    }};
}

// Where a joint is in its range: q[k] per axis (radians) and the world axis a relative spin turns it about.
struct Coord {
    V3 q{};
    std::array<V3, 3> axis{};
    std::array<bool, 3> used{};
    float cone_max{}; // a cone: how far the bone may swing in the direction it has swung (radians)
};
inline M3 zero_of(const Limit& l, const std::array<M3, model::part_count>& rest) {
    return l.zero_is_rest ? rest[l.child] : l.zero;
}
// Swing then twist of `d` (a child's axes in its zero's axes): the shortest turn of the bone from x, then the turn about it.
inline void swing_twist(const M3& d, V3& swing, float& twist) {
    swing = swing_between({1, 0, 0}, normalized(d[0], {1, 0, 0}));
    const float s = length(swing);
    const M3 sm = s > 1e-6f ? rotation(swing * (1.0f / s), s) : identity();
    const M3 t = multiply(transposed(sm), d);
    twist = std::atan2(t[1][2], t[1][1]);
}
inline Coord measure(const Limit& l, const std::array<model::BodyIn, model::part_count>& b, const std::array<M3, model::part_count>& rest) {
    Coord c;
    const M3& frame = b[l.frame].r;
    const M3 rel = multiply(transposed(frame), b[l.child].r);
    const M3 zero = zero_of(l, rest);
    const M3 zero_world = multiply(frame, zero);
    switch (l.kind) {
    case Kind::hinge: {
        const V3 xp = frame[0], xc = b[l.child].r[0], z = frame[2];
        c.q[2] = std::atan2(dot(cross(xp, xc), z), dot(xp, xc));
        c.axis[2] = z;
        c.used[2] = true;
        break;
    }
    case Kind::box: {
        c.q = rotation_vector(multiply(transposed(zero), rel)); // per axis; good to well past these ranges
        for (int k = 0; k < 3; ++k) { c.axis[k] = zero_world[k]; c.used[k] = true; }
        break;
    }
    case Kind::cone: {
        V3 swing{};
        float twist{};
        swing_twist(multiply(transposed(zero), rel), swing, twist);
        // The swing as one angle out from the middle, against the ellipse's edge in that direction. Turning about the
        // radial axis moves the bone out from the middle.
        const float s = length(swing);
        const V3 u = s > 1e-6f ? swing * (1.0f / s) : V3{0, 0, 1};
        const float ez = u[2] / l.r[1].hi, ey = u[1] / l.r[2].hi;
        c.cone_max = 1.0f / std::max(std::sqrt(ez * ez + ey * ey), 1e-3f);
        c.q = {twist, s, 0};
        c.axis[0] = b[l.child].r[0];
        c.axis[1] = normalized(cross(zero_world[0], b[l.child].r[0]), zero_world[2]);
        c.used = {true, true, false};
        break;
    }
    }
    return c;
}
// The stops a coordinate has, in its own units (the cone's swing is 0..1 of its ellipse).
inline Range stops(const Limit& l, const Coord& c, int k) {
    if (l.kind == Kind::cone && k == 1) return {-1e9f, c.cone_max};
    return l.r[k];
}
// A relative spin towards a stop is cut so the joint comes to rest on it: within `zone` the allowed speed falls with the
// room left (reaching the stop in `brake` seconds), past it the joint is turned back out at up to `back_out`. Returns the
// relative spin to add (world).
inline V3 stop_fix(const Limit& l, const Coord& c, const V3& relative, float zone = 15 * deg, float brake = 0.08f, float back_out = 2.0f) {
    V3 fix{};
    for (int k = 0; k < 3; ++k) {
        if (!c.used[k]) continue;
        const Range s = stops(l, c, k);
        const float rate = dot(relative, c.axis[k]);
        const float room_hi = s.hi - c.q[k], room_lo = c.q[k] - s.lo;
        const float zone_k = std::min(zone, 0.3f * std::min(s.hi - s.lo, 2 * s.hi)); // small ranges get a small zone
        float most = 1e9f, least = -1e9f;
        if (room_hi < zone_k) most = room_hi > 0 ? room_hi / brake : std::max(room_hi / brake, -back_out);
        if (room_lo < zone_k) least = room_lo > 0 ? -room_lo / brake : std::min(-room_lo / brake, back_out);
        const float allowed = std::clamp(rate, least, most);
        if (allowed != rate) fix = fix + c.axis[k] * (allowed - rate);
    }
    return fix;
}
// A target pose (the child's axes in the world) moved into the range, `margin` inside the stops.
inline M3 clamp_target(const Limit& l, const M3& frame, const M3& want, const std::array<M3, model::part_count>& rest, float margin = 8 * deg) {
    const M3 zero = zero_of(l, rest);
    const M3 d = multiply(transposed(zero), multiply(transposed(frame), want));
    M3 fixed = d;
    if (l.kind == Kind::box) {
        V3 q = rotation_vector(d);
        for (int k = 0; k < 3; ++k) q[k] = std::clamp(q[k], l.r[k].lo + margin, std::max(l.r[k].hi - margin, l.r[k].lo + margin));
        const float a = length(q);
        fixed = a > 1e-6f ? rotation(q * (1.0f / a), a) : identity();
    } else if (l.kind == Kind::cone) {
        V3 swing{};
        float twist{};
        swing_twist(d, swing, twist);
        const float sz = swing[2] / (l.r[1].hi - margin), sy = swing[1] / (l.r[2].hi - margin);
        const float out = std::sqrt(sz * sz + sy * sy);
        if (out > 1) swing = swing * (1.0f / out);
        twist = std::clamp(twist, l.r[0].lo + margin, l.r[0].hi - margin);
        const float s = length(swing);
        fixed = multiply(s > 1e-6f ? rotation(swing * (1.0f / s), s) : identity(), rotation({1, 0, 0}, twist));
    }
    return multiply(frame, multiply(zero, fixed));
}
}
