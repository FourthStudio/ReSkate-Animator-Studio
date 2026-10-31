#pragma once
#include <array>
#include <cmath>
#include <string_view>

// The studio's grind rail: one of the game's own straight build-kit rails, put in the studio under a grind while it is
// open, so the board sits on it as in game (anim_studio.cpp, prop_tick). Only grinds: a flip trick never shows it.
//
// How they are made (their physics parts, from each item's EcsPhysicsAsset volume moments): the item's origin is on the
// floor at the middle of the rail, and the bar runs along its own Z, with feet and posts under it. The bar's top is about
// 0.50 m up (0.75 m on the tall ones); the studio measures it once the rail stands there and uses these only until then.
namespace dingosdk::anim_studio {
struct StudioRail {
    std::string_view key;  // the build-kit item
    std::string_view name; // as the studio's messages name it
    float top;             // the bar's top above the floor at full size, metres
    float length;          // metres
};
// The one rail the studio puts up: the square 4 m rail at 60%, 30 cm high and 2.4 m long. The game's are big for a skater
// (a 50 cm rail with a bar near 10 cm thick); the build kit scales an object whole, lower, thinner and shorter.
inline constexpr std::array<StudioRail, 1> studio_rails{{
    {"own_bkrails_generic_flatrailsquare_00001", "square rail, 4 m", 0.5f, 4},
}};
inline constexpr std::string_view studio_rail_item = "own_bkrails_generic_flatrailsquare_00001";
inline constexpr float studio_rail_scale = 0.6f;
inline const StudioRail* studio_rail(std::string_view key) noexcept {
    for (const auto& rail : studio_rails)
        if (rail.key == key) return &rail;
    return nullptr;
}
// A clip of the game's grinds and slides on a rail (animation/dingo/c_proto_onb_grind_..., the off-board feet grinds):
// the ones whose Ground joint is where the board meets the rail. Flip tricks, manuals and the rest are not.
inline bool grind_clip(std::string_view asset) noexcept { return asset.find("_grind_") != std::string_view::npos; }

// Where the rail goes under a grind clip, from the clip's first frame as the studio draws it: its Ground joint is where
// the board meets the rail (7.9 cm under the deck between the trucks on a 50-50, under the back truck on a 5-0, under
// the deck on a boardslide, at the tail on a tailslide), and the rail runs along the clip's own Z, not along the way he
// faces (the clips start him turned every way, -90 to 270 degrees about up, by the trick). The grind clips stand still
// (their placement never moves), so that line holds on every frame. `placement` the placement's turn (x y z w) and place
// as the studio draws it, `clip_placement` the placement's turn in the clip's first frame, `ground` the Ground joint's
// place in the placement's frame, `floor` the floor's height. The rail's origin (on the floor) and its turn about up (its
// Z along the rail).
struct RailSpot {
    std::array<float, 3> at{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    bool valid{};
};
inline RailSpot rail_spot(const std::array<float, 4>& placement_rotation, const std::array<float, 3>& placement_at,
                          const std::array<float, 4>& clip_placement, const std::array<float, 3>& ground, float floor) noexcept {
    const auto rotate = [](const std::array<float, 4>& q, const std::array<float, 3>& v) {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const std::array<float, 3> t{2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0])};
        return std::array<float, 3>{v[0] + w * t[0] + (y * t[2] - z * t[1]), v[1] + w * t[1] + (z * t[0] - x * t[2]),
                                    v[2] + w * t[2] + (x * t[1] - y * t[0])};
    };
    RailSpot spot;
    const auto contact = rotate(placement_rotation, ground);
    // The clip's Z in the placement's frame, then as the studio turns the placement.
    const std::array<float, 4> back{-clip_placement[0], -clip_placement[1], -clip_placement[2], clip_placement[3]};
    const auto along = rotate(placement_rotation, rotate(back, {0, 0, 1}));
    const float n = std::sqrt(along[0] * along[0] + along[2] * along[2]);
    if (!(n > 1e-3f) || !std::isfinite(floor)) return spot;
    const float yaw = std::atan2(along[0] / n, along[2] / n); // the turn about up that takes Z along the way he travels
    spot.at = {placement_at[0] + contact[0], floor, placement_at[2] + contact[2]};
    spot.rotation = {0, std::sin(yaw * 0.5f), 0, std::cos(yaw * 0.5f)};
    spot.valid = std::isfinite(spot.at[0]) && std::isfinite(spot.at[2]);
    return spot;
}
// Close enough to be the same rail where it is: within 2 cm and a degree (a rail is a line: turned half round it is the
// same rail).
inline bool same_rail_spot(const std::array<float, 3>& a, const std::array<float, 4>& qa, const std::array<float, 3>& b,
                           const std::array<float, 4>& qb) noexcept {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    if (dx * dx + dy * dy + dz * dz > 0.02f * 0.02f) return false;
    const float dot = std::abs(qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3]);
    const float turned = 2 * std::acos(std::fmin(dot, 1.0f));
    constexpr float degree = 0.01745329f, half_turn = 3.14159265f;
    return turned < degree || std::abs(turned - half_turn) < degree;
}
} // namespace dingosdk::anim_studio
