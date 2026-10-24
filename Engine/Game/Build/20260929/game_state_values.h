#pragma once
#include "Engine/Game/Build/fingerprint.h"
#include <cstddef>
#include <cstdint>

// The skater graph's named game-state values ("Animation/Dingo/EnumGS.PopHeight"...). The graph writes them through
// these setters, on job worker threads. Found 2026-10-10 with a hardware write watch (ReSkate research notes, "Tricks
// by command: the gesture graph's intents").
namespace dingosdk::game::build::v20260929::game_state_values {
// The setter for the graph's int and enum values: (setter, instance, value), where `setter` is the value's descriptor
// + descriptor_to_setter and `instance` the graph instance that owns the storage.
inline constexpr Fingerprint int_setter{0x1137350, {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0xf7,0x41,0x20,0xfb,0xff,0xff,0xff,0x48,0x8b,
                                                    0xc2,0x48,0x8d,0x51,0xa0,0x41,0x8b,0xd8,0x74,0x0d,0x41,0xb0,0x01,0x48,0x8b,0xc8}};
inline constexpr std::uintptr_t descriptor_to_setter = 0x60;
// The descriptor's name: a char*.
inline constexpr std::size_t descriptor_name = 0x18;
// The game's own grade of a pop, written once as the skater pops: 1 low, 2 medium, 3 high. Measured 2026-10-20,
// standing, quick flick: a crouch of 3-5 game ticks gave 1, 7-11 gave 2, 13 and up 3 (kickflip and nollie inward
// heelflip alike).
inline constexpr char pop_height_name[] = "Animation/Dingo/EnumGS.PopHeight";
// The setter for the graph's float values, the same way round as int_setter. Read 2026-10-24 from the running game.
inline constexpr Fingerprint float_setter{0x11DD7D0, {0x48,0x83,0xec,0x38,0x48,0x8b,0xc2,0x41,0xb0,0x01,0x48,0x8d,0x51,0xa0,0x48,0x8b,
                                                      0xc8,0xc5,0xf8,0x29,0x74,0x24,0x20,0xc5,0xf8,0x28,0xf2,0xe8,0x00,0xf8,0xff,0xff}};
// How far the skater is into the trick clip the game plays: 0 at its first frame, 1 at its last, trick_phase_none
// when it plays none. Written every game tick while it changes. Found 2026-10-24 by a census of every float the graph
// writes through a kickflip: the live skeleton matches the library clip at this phase times the clip's length to
// 0.3-3 degrees for the whole trick, the game's slowing of the clip in the air included (ReSkate research notes, "The
// playing trick clip's own time"). Its storage moves between launches; the name does not.
inline constexpr char trick_phase_name[] = "Animation/Dingo/Float.Anim.TrickPhase";
inline constexpr float trick_phase_none = 5.0f;
} // namespace dingosdk::game::build::v20260929::game_state_values
