#pragma once
#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Held moves the game says it is doing, and which saved piece of the game's animation plays for one. A manual is not
// one clip in the game: it is into, loop and exit nodes with a blend space of low, mid and high poses and turns driven by
// the stick, so the pose never sits on one saved loop long enough to be found by it (held_pose_fits measured 0.19 from
// the tail manual loop all through a held manual, where 0.015 plays one; 2026-10-27). The game's own value says which.
namespace dingosdk::anim_studio {
enum class ManualSide { none, tail, nose };
// The parts of a manual as the game's clips are cut: its way in, its loop, its way out, a held brake, a pose or a turn.
enum class ManualPart { into, loop, exit, brake, pose };

// Animation/Dingo/Enum.Intent.Manual as the graph writes it (Enum.Manual: None, Tail_Manual, Nose_Manual, Tail_Brake,
// Nose_Brake). Measured 2026-10-27: right stick down 1, up 2, released 0; it changes on the frame the stick does, about
// 80 ms before the HUD names the manual.
struct ManualIntent {
    ManualSide side{ManualSide::none};
    bool brake{};
};
inline ManualIntent manual_intent_of(int intent) noexcept {
    switch (intent) {
    case 1: return {ManualSide::tail, false};
    case 2: return {ManualSide::nose, false};
    case 3: return {ManualSide::tail, true};
    case 4: return {ManualSide::nose, true};
    default: return {};
    }
}

struct ManualPiece {
    ManualSide side{ManualSide::none};
    bool fakie{};
    ManualPart part{ManualPart::pose};
    std::string after; // the move it follows (Animation::after), "" any
};
// A saved piece's trick key ("native:animation/dingo/c_proto_onb_manual_t_fakie_loop_med") as a manual piece, or none.
// The landing layer (manual_<t|n>_land_addi) is an additive the game lays over the loop, not a part of its own.
inline std::optional<ManualPiece> manual_piece_of(std::string_view trick) noexcept {
    constexpr std::string_view marker = "_manual_";
    const auto at = trick.find(marker);
    if (at == std::string_view::npos || trick.find("ofb") != std::string_view::npos || trick.find("land_addi") != std::string_view::npos)
        return std::nullopt;
    const auto rest = trick.substr(at + marker.size());
    ManualPiece piece;
    if (rest.starts_with("t_")) piece.side = ManualSide::tail;
    else if (rest.starts_with("n_")) piece.side = ManualSide::nose;
    else return std::nullopt;
    const auto after_side = rest.substr(2);
    piece.fakie = after_side.starts_with("fakie_");
    const auto has = [&](std::string_view word) { return after_side.find(word) != std::string_view::npos; };
    piece.part = has("brake") ? ManualPart::brake : has("loop") ? ManualPart::loop : has("into") ? ManualPart::into
               : has("exit") ? ManualPart::exit : ManualPart::pose;
    return piece;
}
// The piece to play for `part` of the side the game says, rolling fakie or not, the one made for that way of rolling
// first. A loop or a brake falls back to that side's loop, then to any piece of that side, so one saved piece is enough
// to see a manual change; a way in or out plays only when one is saved for it. `previous`: the move he came out of
// straight into it ("kickflip"), "" none; a piece made to follow a move plays only after that move, and before the
// others. -1 when none is.
inline int choose_manual_piece(std::span<const ManualPiece> pieces, ManualSide side, bool fakie, ManualPart part = ManualPart::loop,
                               std::string_view previous = {}) noexcept {
    if (side == ManualSide::none) return -1;
    const bool held = part == ManualPart::loop || part == ManualPart::brake;
    int best = -1, best_rank = 0;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const auto& p = pieces[i];
        if (p.side != side || (!p.after.empty() && p.after != previous)) continue;
        const int fit = p.part == part ? 4 : held && p.part == ManualPart::loop ? 2 : held ? 0 : -1;
        if (fit < 0) continue;
        const int rank = fit * 4 + (p.after.empty() ? 0 : 2) + (p.fakie == fakie ? 1 : 0) + 1;
        if (rank > best_rank) best = static_cast<int>(i), best_rank = rank;
    }
    return best;
}

// ---- grinds and slides ----
// A grind as the game names it in Enum.SkateGrindType ("BS_Crook", "BF_FS_50_50"): its side, whether it is the bigflip
// family's, and the grind, spelt as the game's clips spell it ("crook", "5050", "50" for the 5-0, "board", "lip").
struct SkateGrind {
    bool frontside{};
    bool bigflip{};
    std::string grind;
};
inline std::string grind_word(std::string_view word) {
    std::string out;
    for (const char c : word)
        if (c != '_') out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (out == "lipslide") return "lip";
    if (out == "boardslide") return "board";
    return out;
}
inline std::optional<SkateGrind> skate_grind_of(std::string_view name) {
    SkateGrind g;
    if (name.starts_with("BF_")) g.bigflip = true, name.remove_prefix(3);
    if (name.starts_with("FS_")) g.frontside = true;
    else if (!name.starts_with("BS_")) return std::nullopt; // Invalid, Exit
    name.remove_prefix(3);
    g.grind = grind_word(name);
    return g.grind.empty() ? std::nullopt : std::optional<SkateGrind>(g);
}
// The grinds a saved grind or slide loop is the game's clip for: its asset is named for the grind on each side it is
// done from ("c_proto_onb_grind_fs_crook_bs_overcrook_rail_loop": a FS crook and a BS overcrook hold the same pose;
// "..._fs_bs_5_0_...": the 5-0 from both sides; "..._5050_...": the 50-50). Not a loop, or one of the dark lip and board
// clips (their grind is not in the name): none.
struct GrindLoop {
    bool bigflip{};
    std::string frontside, backside; // the grind it is from each side, "" none
};
inline std::optional<GrindLoop> grind_loop_of(std::string_view trick) {
    constexpr std::string_view marker = "_grind_";
    const auto at = trick.find(marker);
    if (at == std::string_view::npos || trick.find("ofb") != std::string_view::npos || !trick.ends_with("_loop")) return std::nullopt;
    auto rest = trick.substr(at + marker.size());
    const auto end = std::min(rest.find("_rail"), rest.find("_loop"));
    rest = rest.substr(0, end);
    GrindLoop loop;
    if (rest.starts_with("bf_")) loop.bigflip = true, rest.remove_prefix(3);
    if (rest.starts_with("fs_bs_")) loop.frontside = loop.backside = grind_word(rest.substr(6));
    else if (rest.starts_with("fs_")) {
        const auto split = rest.find("_bs_");
        loop.frontside = grind_word(rest.substr(3, split == std::string_view::npos ? std::string_view::npos : split - 3));
        if (split != std::string_view::npos) loop.backside = grind_word(rest.substr(split + 4));
    } else if (rest.starts_with("bs_")) {
        const auto split = rest.find("_fs_");
        loop.backside = grind_word(rest.substr(3, split == std::string_view::npos ? std::string_view::npos : split - 3));
        if (split != std::string_view::npos) loop.frontside = grind_word(rest.substr(split + 4));
    } else loop.frontside = loop.backside = grind_word(rest);
    if (loop.frontside.empty() && loop.backside.empty()) return std::nullopt;
    return loop;
}
// The loop to play for the grind the game says: the one made for that grind from that side, in its own family first
// (bigflip or not), then the other family's. -1 when none is for it.
inline int choose_grind_loop(std::span<const GrindLoop> loops, const SkateGrind& grind) noexcept {
    int best = -1, best_rank = 0;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        const auto& l = loops[i];
        if ((grind.frontside ? l.frontside : l.backside) != grind.grind) continue;
        const int rank = l.bigflip == grind.bigflip ? 2 : 1;
        if (rank > best_rank) best = static_cast<int>(i), best_rank = rank;
    }
    return best;
}
} // namespace dingosdk::anim_studio
