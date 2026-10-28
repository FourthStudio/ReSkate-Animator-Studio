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
// Which manual the player holds, written through the int setter: 1 manual (right stick down), 2 nose manual (up), 0
// none. Measured 2026-10-27 rolling on flat ground: it changes on the frame the stick does; the physics state stays
// 100 (riding) all through a manual.
inline constexpr char manual_intent_name[] = "Animation/Dingo/Enum.Intent.Manual";
// The grind or slide he is on, written through the int setter as he locks on (Enum.SkateGrindType's values below; 0
// Invalid). Measured 2026-10-27: a BS crook 32, a BS noseslide 21, a BS boardslide 19, matching the HUD's names; one lock
// wrote 23 for a single write before 32.
inline constexpr char skate_grind_type_name[] = "Animation/Dingo/Enum.GS.SkateGrindType";
// Enum.SkateGrindType's value names in order, from the game's own enumeration asset (animation/dingo/enum.skategrindtype).
inline constexpr const char* skate_grind_type_names[] = {
    "Invalid", "FS_Lip", "FS_Board", "FS_50_50", "FS_NoseSlide", "FS_TailSlide", "FS_NoseGrind", "FS_5_0", "FS_Salad", "FS_Smith",
    "FS_Feeble", "FS_Willy", "FS_OverWilly", "FS_NoseBlunt", "FS_Blunt", "FS_Crook", "FS_OverCrook", "FS_DarkSlide", "BS_Lip", "BS_Board",
    "BS_50_50", "BS_NoseSlide", "BS_TailSlide", "BS_NoseGrind", "BS_5_0", "BS_Salad", "BS_Smith", "BS_Feeble", "BS_Willy", "BS_OverWilly",
    "BS_NoseBlunt", "BS_Blunt", "BS_Crook", "BS_OverCrook", "BS_DarkSlide", "BF_FS_Lip", "BF_FS_Board", "BF_FS_50_50", "BF_FS_NoseSlide",
    "BF_FS_TailSlide", "BF_FS_NoseGrind", "BF_FS_5_0", "BF_FS_Salad", "BF_FS_Smith", "BF_FS_Feeble", "BF_FS_Willy", "BF_FS_OverWilly",
    "BF_FS_NoseBlunt", "BF_FS_Blunt", "BF_FS_Crook", "BF_FS_OverCrook", "BF_FS_DarkSlide", "BF_BS_Lip", "BF_BS_Board", "BF_BS_50_50",
    "BF_BS_NoseSlide", "BF_BS_TailSlide", "BF_BS_NoseGrind", "BF_BS_5_0", "BF_BS_Salad", "BF_BS_Smith", "BF_BS_Feeble", "BF_BS_Willy",
    "BF_BS_OverWilly", "BF_BS_NoseBlunt", "BF_BS_Blunt", "BF_BS_Crook", "BF_BS_OverCrook", "BF_BS_DarkSlide", "FS_Suski", "BF_FS_Suski",
    "BS_Suski", "BF_BS_Suski", "BS_Darklip", "BF_BS_Darklip", "FS_Darklip", "BF_FS_Darklip", "Exit"};
} // namespace dingosdk::game::build::v20260929::game_state_values
