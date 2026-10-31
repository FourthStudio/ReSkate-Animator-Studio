#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

// Where the game's own clips stand on the studio's floor (the editor and its previews). A clip is put down with its
// Ground joint at its first frame on the floor, which is right for a whole trick (its board on the ground before the pop
// and after the landing). It is not for the rest: a dark catch lands on its upside-down board 9.5 cm up, a fingerflip's
// way in dives 46 cm into the floor, a handplant hangs 2 m under it (checked on the whole trick library). This works out,
// per frame, how far to lift a clip so that:
// - where its board rests (still for a moment, near the floor, lying flat or upside down) at its start or its end, it
//   rests on the floor; between a resting start and a resting end the lift goes smoothly from one to the other;
// - a clip with no resting board (a piece in the air) is lifted once, as little as keeps its board off the floor,
//   and keeps the game's own height otherwise.
namespace dingosdk::anim_studio {
inline constexpr float floor_rest_moves = 0.015f; // the board rests while its lowest point stays this near (m)
inline constexpr float floor_rest_near = 0.12f;   // and only this near the floor: higher up it is in the air
inline constexpr float floor_rest_level = 0.82f;  // and lies flat or upside down (its up within 35 degrees of up or down)
inline constexpr float floor_rest_time = 0.1f;    // for this long (s), or half the clip when it is shorter
inline constexpr float floor_dip_allowed = 0.03f; // a resting clip that goes this far under elsewhere is lifted whole

namespace editor_floor_detail {
struct Rest {
    std::size_t last{}; // the last frame of the rest, counted from its end of the clip
    float low{};        // the board's lowest point there
};
// The rest at the start (`from_end` false) or the end of the clip, if its board rests there.
inline bool rest_at(std::span<const float> times, std::span<const float> low, std::span<const float> level, bool from_end, Rest& out) {
    const std::size_t n = low.size();
    if (!n) return false;
    const std::size_t first = from_end ? n - 1 : 0;
    const float ref = low[first];
    if (!std::isfinite(ref) || std::abs(ref) > floor_rest_near || !(level[first] >= floor_rest_level)) return false;
    std::size_t last = first;
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t i = from_end ? n - 1 - k : k;
        if (!(std::abs(low[i] - ref) <= floor_rest_moves) || !(level[i] >= floor_rest_level)) break;
        last = i;
    }
    const float held = std::abs(times[last] - times[first]);
    const float wanted = std::min(floor_rest_time, std::abs(times[n - 1] - times[0]) * 0.5f);
    if (held + 1e-4f < wanted) return false;
    out = {last, ref};
    return true;
}
} // namespace editor_floor_detail

// The lift (m) for each frame. `times` the frames' times, `low` the board's lowest point over the floor each frame as
// the clip is put down (its first frame's Ground joint on the floor), `level` how upright the board lies (|its up . up|).
// Empty when the three do not match.
inline std::vector<float> floor_lifts(std::span<const float> times, std::span<const float> low, std::span<const float> level) {
    using namespace editor_floor_detail;
    const std::size_t n = low.size();
    std::vector<float> out;
    if (!n || times.size() != n || level.size() != n) return out;
    float lowest = low[0];
    for (const float v : low)
        if (std::isfinite(v)) lowest = std::min(lowest, v);
    if (!std::isfinite(lowest)) return out;
    out.assign(n, 0.0f);
    Rest start, end;
    const bool rests_first = rest_at(times, low, level, false, start), rests_last = rest_at(times, low, level, true, end);
    if (rests_first && rests_last && start.last < end.last) {
        const float a = times[start.last], b = times[end.last];
        for (std::size_t i = 0; i < n; ++i) {
            float u = b > a ? std::clamp((times[i] - a) / (b - a), 0.0f, 1.0f) : 0.0f;
            u = u * u * (3 - 2 * u);
            out[i] = -start.low + (start.low - end.low) * u;
        }
        return out;
    }
    float lift;
    if (rests_first || rests_last) {
        // Resting the whole way, or at one end only: that rest on the floor, unless the rest of it would go well under.
        const float rest = rests_first ? start.low : end.low;
        lift = rest - lowest > floor_dip_allowed ? -lowest : -rest;
    } else lift = std::max(0.0f, -lowest); // in the air: off the floor, at the game's own height
    std::fill(out.begin(), out.end(), lift);
    return out;
}
// The lift at `seconds`, between the frames around it.
inline float floor_lift_at(std::span<const float> times, std::span<const float> lifts, float seconds) {
    if (lifts.empty() || times.size() != lifts.size()) return 0.0f;
    if (!(seconds > times.front())) return lifts.front();
    if (!(seconds < times.back())) return lifts.back();
    const auto after = std::upper_bound(times.begin(), times.end(), seconds);
    const std::size_t i = static_cast<std::size_t>(after - times.begin());
    const float t0 = times[i - 1], t1 = times[i];
    const float u = t1 > t0 ? (seconds - t0) / (t1 - t0) : 0.0f;
    return lifts[i - 1] + (lifts[i] - lifts[i - 1]) * u;
}
} // namespace dingosdk::anim_studio
