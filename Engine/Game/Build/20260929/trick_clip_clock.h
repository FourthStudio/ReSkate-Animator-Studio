#pragma once
#include <cstdint>

// Which skater graph writes the game's trick phase (game_state_values::trick_phase_name): the graph instance of the
// skater's own animation. Found 2026-10-24: the rig points at it, and its writes of the trick phase are that skater's.
namespace dingosdk::game::build::v20260929::trick_clip_clock {
// From the skater's pose holder (its animation component + 0xa0): the rig (as native_pose_layout.h reads it), then
// the graph instance the game's value setters are given for this skater.
inline constexpr std::uintptr_t holder_to_rig = 0x78;
inline constexpr std::uintptr_t rig_to_graph_instance = 0x10;
} // namespace dingosdk::game::build::v20260929::trick_clip_clock
