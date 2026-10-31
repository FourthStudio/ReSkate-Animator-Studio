#pragma once
#include "trick_modifier.h"
#include <algorithm>
#include <limits>
#include <span>
#include <string_view>
#include <vector>
namespace dingosdk::anim_studio {
struct NativeLoopCandidate {
    std::size_t piece{};
    std::string_view asset;
    bool modifier{}, mirrored{};
    int rank{-1}; // native family/style/fakie compatibility; negative is ineligible
    float score{1};
};
// Route preference only applies after native eligibility and exact-stance rules. Recognise the best asset;
// equivalent saved variants of that asset can then obey Fixed / In turns / Shuffle without pose ambiguity.
inline std::vector<std::size_t> native_loop_candidates(std::span<const NativeLoopCandidate> candidates, bool held, bool front_foot) {
    return choose_trick_route(held, [&](bool modified) {
        int rank = std::numeric_limits<int>::max();
        float score = std::numeric_limits<float>::max();
        std::string_view asset;
        bool found = false;
        for (const auto& c : candidates) {
            if (c.modifier != modified || c.rank < 0 || (!front_foot && c.mirrored)) continue;
            if (!found || c.rank < rank || (c.rank == rank && c.score < score)) {
                found = true; rank = c.rank; score = c.score; asset = c.asset;
            }
        }
        std::vector<std::size_t> out;
        const bool exact = std::any_of(candidates.begin(), candidates.end(), [&](const auto& c) {
            return found && c.modifier == modified && c.rank == rank && c.asset == asset && !c.mirrored;
        });
        for (const auto& c : candidates)
            if (found && c.modifier == modified && c.rank == rank && c.asset == asset && ((!exact && front_foot) || !c.mirrored)) out.push_back(c.piece);
        return out;
    });
}
}
