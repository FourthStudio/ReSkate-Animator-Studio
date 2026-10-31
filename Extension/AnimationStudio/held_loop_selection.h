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
}
