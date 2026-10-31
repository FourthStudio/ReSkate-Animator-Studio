#pragma once
#include <cstddef>
#include <span>
#include <utility>

namespace dingosdk::anim_studio {
struct HeldLoopRanks {
    int best{-1};
    bool keep{};
};
// A valid fallback must give way when a better style becomes available. Equal-ranked loops keep their running clock.
template <class Kind, class Rank>
HeldLoopRanks held_loop_ranks(std::span<const std::pair<std::size_t, Kind>> loops, Kind kind, int playing, Rank&& rank) {
    HeldLoopRanks out;
    int current = -1;
    for (const auto& [index, k] : loops) {
        if (k != kind) continue;
        const int value = rank(index);
        if (playing >= 0 && index == static_cast<std::size_t>(playing)) current = value;
        if (value >= 0 && (out.best < 0 || value < out.best)) out.best = value;
    }
    out.keep = current >= 0 && current == out.best;
    return out;
}
// How long a better loop must stay better before it takes over. Each change starts a loop from its start, so a value that
// flickers for a frame must not cut the loop that plays.
inline constexpr float held_loop_better_for = 0.1f;
enum class HeldLoopStep { play_on, let_go, pick_best };
// What the running loop does now. A loop the game does not name (a one-foot grab's) is found by its pose, so its rank says
// nothing and it plays on. A named loop is let go of when the game has left it, and gives way to a better one.
inline HeldLoopStep held_loop_step(int playing, bool playing_named, int playing_rank, const HeldLoopRanks& ranks) noexcept {
    if (playing < 0) return HeldLoopStep::pick_best;
    if (!playing_named) return HeldLoopStep::play_on;
    if (ranks.best < 0 || playing_rank < 0) return HeldLoopStep::let_go;
    return ranks.keep ? HeldLoopStep::play_on : HeldLoopStep::pick_best;
}
}
