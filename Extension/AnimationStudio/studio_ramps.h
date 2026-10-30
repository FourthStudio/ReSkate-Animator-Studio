#pragma once
#include "studio_rails.h"
#include <array>
#include <cmath>
#include <string_view>

// The studio's quarter pipe: one of the game's own build-kit quarter pipes, put in the studio under a handplant (or any
// of the game's inverts) while it is open, the planted hand on its coping as in game (anim_studio.cpp, prop_tick). Only
// the inverts: nothing else shows it.
//
// How they are made (their meshes, read from the game 2026-10-30; every colour of a size has the same one): the item's
// origin is on the floor at the middle of its footprint, the ramp faces its own +Z (the transition there, the deck at -Z),
// and its coping runs along X at the deck's front edge. `coping` is that edge's Z, `top` the coping's top above the floor.
// The studio always puts up the M, 2.25 m high (the user's pick in game, 2026-10-30: "just the 2.25m is correct"), its
// last 25 cm straight up. Played on it (all 36 clips, against its mesh) a plant's board goes 8 cm into it at most (a
// wheel at the coping as he comes up), a wind-up's (_antic) 14 cm into its transition. (The L, 3.25 m: 7 cm at most;
// the S, 1.74 m: 73 cm.)
namespace dingosdk::anim_studio {
struct StudioQuarterPipe {
    std::string_view key;  // the build-kit item
    std::string_view name; // as the studio's messages name it
    float top;             // the coping's top above the floor, metres
    float coping;          // the coping's front edge along the item's Z (the transition faces +Z), metres
    float width;           // along the coping, metres
};
inline constexpr std::array<StudioQuarterPipe, 1> studio_quarter_pipes{{
    {"own_bkvert_generic_quarterpipemedium_00001", "quarter pipe M, 2.25 m high", 2.25f, -0.75f, 2.5f},
}};
inline constexpr std::string_view studio_quarter_pipe_item = "own_bkvert_generic_quarterpipemedium_00001";
// The coping is a pipe this thick (its radius, from the meshes): the hand rests on its top, this far behind its front.
inline constexpr float studio_coping_radius = 0.025f;
inline const StudioQuarterPipe* studio_quarter_pipe(std::string_view key) noexcept {
    for (const auto& ramp : studio_quarter_pipes)
        if (ramp.key == key) return &ramp;
    return nullptr;
}
// A clip of the game's inverts (animation/dingo/c_proto_onb_invert_..., the handplants and the plants done on a lip, and
// their wind-ups, _antic). In every one the Ground joint stays on the coping's top until he drops back in, and a hand
// plants on it (within 5 cm, the wrist over the palm), checked on all 36 (2026-10-30).
inline bool invert_clip(std::string_view asset) noexcept { return asset.find("_invert_") != std::string_view::npos; }

// Where the quarter pipe stands under an invert: `coping` the coping's top in the world (the clip's first frame's Ground
// joint as the studio draws it), `open` which way the transition faces (the placement's up at the first frame: the skater
// rides up the wall with his up pointing out of it), `floor` the floor's height. The item's origin (on the floor) and its
// turn about up (its +Z toward `open`), so the coping's top is at `coping` (along it: its middle).
inline RailSpot quarter_pipe_spot(const StudioQuarterPipe& ramp, const std::array<float, 3>& coping, const std::array<float, 3>& open,
                                  float floor) noexcept {
    RailSpot spot;
    const float n = std::sqrt(open[0] * open[0] + open[2] * open[2]);
    if (!(n > 1e-3f) || !std::isfinite(floor)) return spot;
    const float ox = open[0] / n, oz = open[2] / n;
    const float yaw = std::atan2(ox, oz); // the turn about up that takes the item's +Z to `open`
    // The coping's top is at (0, top, coping - radius) on the item: its origin that far back along `open`.
    const float back = ramp.coping - studio_coping_radius;
    spot.at = {coping[0] - back * ox, floor, coping[2] - back * oz};
    spot.rotation = {0, std::sin(yaw * 0.5f), 0, std::cos(yaw * 0.5f)};
    spot.valid = std::isfinite(spot.at[0]) && std::isfinite(spot.at[2]);
    return spot;
}
// Close enough to be the same quarter pipe where it is: within 2 cm and a degree (unlike a rail, turned half round it is
// not: it faces the other way).
inline bool same_ramp_spot(const std::array<float, 3>& a, const std::array<float, 4>& qa, const std::array<float, 3>& b,
                           const std::array<float, 4>& qb) noexcept {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    if (dx * dx + dy * dy + dz * dz > 0.02f * 0.02f) return false;
    const float dot = std::abs(qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3]);
    return 2 * std::acos(std::fmin(dot, 1.0f)) < 0.01745329f;
}
} // namespace dingosdk::anim_studio
