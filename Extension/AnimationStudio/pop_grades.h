#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// The game's own grade of each pop (EnumGS.PopHeight): low, medium or high, which the player's Minimum Pop Height
// setting moves. Playback chooses the clip for the grade the game gave the player's pop.
//
// Every skater's graph is heard, each grade with the graph that wrote it; the local skater's are told apart by his graph
// (PopGradeLog), so the grade is used online and beside AI skaters too.
namespace dingosdk::anim_studio::pop_grades {
// 1 low, 2 medium, 3 high (0 none).
using PopHeight = std::int32_t;

// Hooks the game's setter for the graph's int values, once. False with `why` when this game build's setter is not the
// one this was written for. Client thread.
bool install(std::uintptr_t base, std::string& why);

// The grade of the local skater's pop at `takeoff_ms` (GetTickCount64), asked at `now_ms` (0 none: not graded, or not
// known yet to be his). `solo`: no other skater in the game. `attempt`: the air it is for. Any thread.
[[nodiscard]] PopHeight local_pop_height(std::uint64_t takeoff_ms, std::uint64_t now_ms, bool solo, std::uint64_t attempt) noexcept;
// A value the game wrote for the local skater, and when (GetTickCount64): the game keeps the last one written.
struct LocalValue {
    std::int32_t value{};
    std::uint64_t ms{};
};
// The local skater's Animation/Dingo/Enum.Intent.Manual (held_moves.h: manual_intent_of), 0 while none is heard.
// `solo`: no other skater in the game, so any graph's value is his until his first pop says which graph is his.
[[nodiscard]] LocalValue local_manual_intent(bool solo) noexcept;
// The local skater's Animation/Dingo/Enum.GS.SkateGrindType (game_state_values.h names its values), the same way.
[[nodiscard]] LocalValue local_skate_grind_type(bool solo) noexcept;
} // namespace dingosdk::anim_studio::pop_grades
