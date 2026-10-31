#pragma once
#include "anim_core.h"
#include "anim_presets.h"
#include "loop_lines.h"
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// COPY TO INS AND OUTS: a loop of the game's (a manual's, a grind's) with the clips that go into it and come out of it on
// its line (loop_lines.h: the same stance and way in). Its pose is put on the frame where each of them meets it - an in's
// last frame, an out's first - so an edited loop is gone into and come out of in its own pose, not through the game's.
//
// For that the meeting frames are not held to the game's own pose as the brackets' keys otherwise are (free_ends): a
// loop's first and last frame, an in's last and an out's first carry the pose across to the next clip.
namespace dingosdk::anim_studio {
// Copy into a personal save on the same physical foot; never overwrite another stance or an imported save.
inline bool loop_copy_target(const Preset& p, std::string_view asset, bool lead) {
    return p.trick == "native:" + std::string(asset) && !p.foreign && !p.locked && p.available && preset_leads_right(p) == lead;
}

enum class LineRole { none, in, loop, out };
struct LinePlace {
    const LoopLine* line{};
    LineRole role{LineRole::none};
};
// A clip's name after animation/dingo/ (or the whole of it when it has no folder).
inline std::string_view clip_short_name(std::string_view asset) noexcept {
    const auto slash = asset.rfind('/');
    return slash == std::string_view::npos ? asset : asset.substr(slash + 1);
}
// The names in one of a line's lists (split by |).
inline std::vector<std::string_view> line_names(std::string_view list) {
    std::vector<std::string_view> out;
    while (!list.empty()) {
        const auto bar = list.find('|');
        out.push_back(list.substr(0, bar));
        if (bar == std::string_view::npos) break;
        list.remove_prefix(bar + 1);
    }
    return out;
}
// Where a clip (its asset, animation/dingo/...) stands on a line: its loop, one that goes into it, or one that comes out.
inline LinePlace line_place(std::string_view asset) {
    const auto name = clip_short_name(asset);
    if (name.empty()) return {};
    for (const auto& line : loop_lines) {
        if (line.loop == name) return {&line, LineRole::loop};
        for (const auto in : line_names(line.ins))
            if (in == name) return {&line, LineRole::in};
        for (const auto out : line_names(line.outs))
            if (out == name) return {&line, LineRole::out};
    }
    return {};
}
// Which of a clip's ends carry the pose across to the next clip on its line (the rest stay at the game's own pose).
struct FreeEnds {
    bool start{}, end{};
};
inline FreeEnds free_ends(LineRole role) noexcept {
    return {role == LineRole::loop || role == LineRole::out, role == LineRole::loop || role == LineRole::in};
}
// A grind's loop or a manual's.
inline bool manual_line(const LoopLine& line) noexcept { return line.loop.find("_manual_") != std::string_view::npos; }

using LinePose = std::vector<std::pair<std::uint16_t, Offset>>;
// Whether an edit does anything: a turn of more than a tenth of a degree, or a move of more than a millimetre.
inline bool edit_shows(const Offset& edit) noexcept {
    const float w = std::fabs(normalised(edit.rotation)[3]);
    const float turn = 2 * std::acos(w > 1 ? 1.0f : w) * 57.2957795f;
    return turn > 0.1f || edit.offset[0] * edit.offset[0] + edit.offset[1] * edit.offset[1] + edit.offset[2] * edit.offset[2] > 1e-6f;
}
// The pose a loop goes in and out on, joint by joint: the key on its first frame where that is keyed, else the first one
// placed on the joint (a loop keyed in its middle holds that pose all round). The board is left out: its keys are in its
// own clip's frame, which is not the next clip's. Hidden joints are left out too.
inline LinePose loop_pose(const Animation& loop) {
    LinePose pose;
    for (const auto& track : loop.tracks) {
        if (track.hidden || track.joint == board_joint) continue;
        for (const auto& key : track.keys) {
            const Offset edit{key.rotation, key.offset};
            if (!edit_shows(edit)) continue;
            pose.emplace_back(track.joint, edit);
            break;
        }
    }
    return pose;
}
// Puts a pose on the frame `at` of a clip on the line: each joint's key there is set to it (its other keys stay as they
// are). A joint locked in that clip is left alone.
inline void put_pose(Animation& clip, const LinePose& pose, float at) {
    for (const auto& [joint, edit] : pose) {
        if (const auto* track = clip.track(joint); track && track->locked) continue;
        set_key(clip, joint, at, edit);
    }
}
// The loop closed on its own pose: its first and last frame both hold it, so it goes round without a jump, and its ins
// and outs meet it there. A joint keyed on either end already keeps that key.
inline void close_loop(Animation& loop, const LinePose& pose, float duration) {
    for (const auto& [joint, edit] : pose) {
        auto* track = loop.track(joint);
        if (track && track->locked) continue;
        for (const float at : {0.0f, duration}) {
            bool keyed = false;
            if (track)
                for (const auto& key : track->keys)
                    keyed |= std::fabs(key.time - at) <= 1.0f / 120 && edit_shows({key.rotation, key.offset});
            if (!keyed) set_key(loop, joint, at, edit);
            track = loop.track(joint);
        }
    }
}
// The pose at the frame an out starts from: the loop's own last frame, once closed.
inline LinePose loop_end_pose(const Animation& loop, float duration) {
    LinePose pose;
    for (const auto& track : loop.tracks) {
        if (track.hidden || track.joint == board_joint) continue;
        const auto edit = evaluate(track, duration);
        if (edit_shows(edit)) pose.emplace_back(track.joint, edit);
    }
    return pose;
}
inline LinePose loop_start_pose(const Animation& loop) {
    LinePose pose;
    for (const auto& track : loop.tracks) {
        if (track.hidden || track.joint == board_joint) continue;
        const auto edit = evaluate(track, 0);
        if (edit_shows(edit)) pose.emplace_back(track.joint, edit);
    }
    return pose;
}
} // namespace dingosdk::anim_studio
