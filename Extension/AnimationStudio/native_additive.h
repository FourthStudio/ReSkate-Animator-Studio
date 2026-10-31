#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The game's additive clips: the ones whose ClipControllerAsset has DofMode 1 (their EclipseAnimationAsset BaseFlags 1).
// Their tracks are not a pose but a change laid over another clip as the game plays - a manual's loop over the manual's
// pose, a riding lean over the riding pose, a climb's lean over the climb idle. Read as they are, every joint the clip
// has a track for starts from nothing, and the skater lies flat on the floor in the studio. The library builder lays each
// one over the pose it goes on (add_onto), found by additive_bases.
//
// How they go on, measured on the game's own clips: each tracked joint's turn after the base's own (base * clip) and its
// position added to the base's; joints with no track are the base's. So laid, a manual loop keeps both feet within about
// a centimetre of where they stand on the deck all the way through (the other order moves them 50 cm), and an
// `X_pose_addi` riding clip over the neutral riding pose is the game's own `X_pose` to 0.3 degrees.
namespace dingosdk::anim_studio::native_library {

// Which clip an additive clip goes on, best first, as "animation/dingo/..." names; the builder takes the first that is
// a whole pose (not additive itself) and decoded.
//  - A manual's loop or landing (c_proto_onb_manual_<t|n>_[fakie_]...): that manual's medium pose. The game blends
//    the low, medium and high poses by the balance; the medium one is where it sits balanced.
//  - An `..._pose_addi`: the neutral riding pose (fakie's when it is a fakie one). These are a riding pose less the
//    neutral one, so laid over the pose they are named after they would lean twice as far.
//  - Any other: the clip it is named after - its name shortened a word at a time from the end, as it is or as a pose
//    (a slide loop's lean goes on that slide loop, a setup loop on the setup's pose).
//  - Then by kind: climbing on the climb idle, off the board on the standing idle, else the riding pose.
inline std::vector<std::string> additive_bases(std::string_view asset) {
    const auto slash = asset.rfind('/');
    const std::string folder(asset.substr(0, slash == std::string_view::npos ? 0 : slash + 1));
    const std::string name(asset.substr(slash == std::string_view::npos ? 0 : slash + 1));
    const auto has = [&](std::string_view part) { return name.find(part) != std::string::npos; };
    std::vector<std::string> out;
    if (const auto at = name.find("_manual_"); at != std::string::npos && at + 10 <= name.size() && (name[at + 8] == 't' || name[at + 8] == 'n') &&
                                               name[at + 9] == '_') {
        std::string stem = name.substr(0, at + 10);
        if (name.compare(at + 10, 6, "fakie_") == 0) stem += "fakie_";
        out.push_back(folder + stem + "med_pose");
    }
    constexpr std::string_view pose_addi = "_pose_addi";
    const bool less_neutral = name.size() >= pose_addi.size() && name.compare(name.size() - pose_addi.size(), pose_addi.size(), pose_addi) == 0;
    if (!less_neutral)
        for (std::string s = name;;) {
            for (const auto& candidate : {s, s + "_pose"})
                if (candidate != name) out.push_back(folder + candidate);
            const auto cut = s.rfind('_');
            if (cut == std::string::npos || cut == 0) break;
            s.resize(cut);
        }
    if (has("climb")) out.push_back(folder + "c_proto_climb_idle_center_01");
    else if (has("_ofb") || has("cas_")) out.push_back(folder + "c_proto_ofbnb_idle_lfoot");
    else if (has("fakie") && !has("to_fakie")) out.push_back(folder + "c_proto_onb_idle_rolling_fakie_neutral_pose");
    out.push_back(folder + "c_proto_onb_idle_rolling_neutral_pose");
    return out;
}

// What a clip has a track for, joint by joint (Decoded::animated).
inline constexpr std::uint8_t tracks_rotation = 1, tracks_position = 2;

// An additive clip laid over its base pose: `frames` (frame by frame, `joints` each, scale / quaternion xyzw / position)
// as decoded, `base` one pose of `joints`, `animated` per joint what the clip has tracks for. A tracked rotation becomes
// base * clip, a tracked position base + clip; everything else is the base's (scale too: no additive clip has scale
// tracks).
inline void add_onto(std::vector<std::array<float, 10>>& frames, std::size_t joints, std::span<const std::array<float, 10>> base,
                     std::span<const std::uint8_t> animated) {
    if (joints == 0 || base.size() != joints || animated.size() != joints || frames.size() % joints != 0)
        throw std::runtime_error("An additive clip and its base pose do not match");
    for (std::size_t at = 0; at < frames.size(); ++at) {
        const std::size_t j = at % joints;
        const auto& under = base[j];
        auto& joint = frames[at];
        std::array<float, 10> out = under;
        if (animated[j] & tracks_rotation) {
            const double ax = under[3], ay = under[4], az = under[5], aw = under[6];
            const double bx = joint[3], by = joint[4], bz = joint[5], bw = joint[6];
            const double q[4]{aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw,
                              aw * bw - ax * bx - ay * by - az * bz};
            const double length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (!(length > 1e-9)) throw std::runtime_error("An additive clip holds a broken rotation");
            for (std::size_t c = 0; c < 4; ++c) out[3 + c] = static_cast<float>(q[c] / length);
        }
        if (animated[j] & tracks_position)
            for (std::size_t c = 0; c < 3; ++c) out[7 + c] = under[7 + c] + joint[7 + c];
        joint = out;
    }
}
} // namespace dingosdk::anim_studio::native_library
