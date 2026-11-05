#pragma once
#include "real_bails_body.h"
#include <algorithm>
#include <array>
#include <cmath>

// Real Bails' read of a fall (real_bails_model.cpp): where the body will meet the ground, when, with what and how fast.
//
// The body is carried on as it moves now, as if it held its shape: its centre on its path under gravity and the whole of
// him turning as he turns now. With feet (or shins, or knees) on something under him he goes over them instead, as a
// pole falls: turning about where they touch, faster as gravity pulls him over, until his centre is low, and then on
// through the air. The first body to reach the ground that is not a hand, forearm, foot or toes (those are what he puts
// down on purpose) is the impact: its time, where, and its speed then. Feet are followed on their own, for a drop he
// lands on his feet.
namespace dingosdk::real_bails::read {
using math::M3;
using math::V3;
using model::Part;
using model::part_count;

struct Prediction {
    bool hit{};                 // something meets the ground within the look ahead
    float t{};                  // when, s (the look ahead when nothing does)
    Part first{model::board};   // what
    V3 at{};                    // where its centre is then (world)
    V3 velocity{};              // and how fast it is going (world)
    float feet_t{};             // when a foot or toes first meet the ground, s
    M3 turn{math::identity3()}; // how the body has turned by then (taken no further on than aim_ahead)
    V3 chest{};                 // where the upper chest is then (the same)
    bool pivot{};               // he goes over a support
};
constexpr float look_ahead = 1.5f;
constexpr float aim_ahead = 0.6f; // past this the guess of how he has turned is no use to aim with

inline bool aims_down(Part p) noexcept {
    using namespace model;
    return p == l_hand || p == r_hand || p == l_forearm || p == r_forearm || p == l_foot || p == r_foot || p == l_toe || p == r_toe;
}
inline bool foot_part(Part p) noexcept {
    using namespace model;
    return p == l_foot || p == r_foot || p == l_toe || p == r_toe;
}

// `skip`: bodies already on something (they are not the next impact). `ground`: the ground's height. `support`,
// `pivot`: he stands or kneels on something, there.
inline Prediction predict(const body::Mass& m, const std::array<bool, part_count>& skip, float ground, bool support, const V3& pivot) noexcept {
    using namespace math;
    Prediction out;
    out.t = look_ahead;
    out.feet_t = look_ahead;
    constexpr float step = 0.02f;
    const V3 g{0, -body::gravity, 0};
    // Every body is where the turn and the centre's path carry it: centre + turn (its offset from com now).
    const V3 offset = m.com - pivot;
    bool pivoting = support && offset[1] > 0.35f && length(offset) > 0.3f;
    out.pivot = pivoting;
    V3 spin = m.spin, centre = m.com, velocity = m.velocity;
    const M3 inertia_pivot = add(m.locked, point_inertia(m.total, offset));
    if (pivoting) {
        // Turning about the support: the spin that carries the centre as it moves across the support's line, and the
        // body's own spin along that line.
        const V3 along = normalized(offset);
        spin = cross(offset, velocity) * (1.0f / dot(offset, offset)) + along * dot(m.spin, along);
    }
    spin = clamp_length(spin, 20.0f);
    M3 turn = identity3();
    bool aimed = false;
    const auto aim = [&] {
        out.turn = turn;
        out.chest = centre + turn_by(turn, m.at[model::chest3]);
        aimed = true;
    };
    for (int n = 1; n * step <= look_ahead + 1e-4f; ++n) {
        const float t = static_cast<float>(n) * step;
        if (pivoting) {
            // Gravity turns him on over the support (his inertia about it turned with him).
            const V3 r = turn_by(turn, offset);
            const M3 inertia_now = multiply(turn, multiply(inertia_pivot, transposed(turn)));
            spin = clamp_length(spin + turn_by(inverse(inertia_now), cross(r, g * m.total)) * step, 20.0f);
            turn = multiply(rotation(spin * step), turn);
            const V3 r_next = turn_by(turn, offset);
            centre = pivot + r_next;
            velocity = cross(spin, r_next);
            // Low over the support, or gone past lying flat on it: from here on he falls through the air.
            if (r_next[1] < 0.3f * length(offset) || r_next[1] < 0.25f) pivoting = false;
        } else {
            velocity = velocity + g * step;
            centre = centre + velocity * step;
            turn = multiply(rotation(spin * step), turn);
        }
        if (!aimed && t >= aim_ahead - 1e-4f) aim();
        for (std::size_t i = 1; i < part_count; ++i) {
            const auto p = static_cast<Part>(i);
            if (skip[i]) continue;
            const bool foot = foot_part(p);
            if (!foot && aims_down(p)) continue;
            if (foot && out.feet_t < look_ahead) continue;
            const V3 now = centre + turn_by(turn, m.at[i]);
            if (now[1] - body::shapes[i].radius > ground) continue;
            if (foot) {
                out.feet_t = t;
                continue;
            }
            if (!out.hit) {
                out.hit = true;
                out.t = t;
                out.first = p;
                out.at = now;
                out.velocity = velocity + cross(spin, now - centre);
            }
        }
        if (out.hit) break;
    }
    if (!aimed) aim();
    return out;
}
}
