#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Which grind the game is doing, by its own name for it: Enum.SkateGrindType (animation/dingo/enum.skategrindtype, read
// from the game's data 2026-10-29), the value Enum.GS.Anim.CurrentSkateGrindType holds while he grinds. The game's grind
// clips are shared (one body is a FS boardslide one way and a BS lipslide the other, a 50-50 both ways), and a saved grind
// found only by the pose he holds played on any grind that looked like it - a FS blunt on a 50-50. A saved grind now plays
// only while the game says it is doing one of the grinds its clip is for (grind_types_of). "BF" is the fakie way into and
// along the rail (as TRICKS reads a clip's "bf_"); "fakie_" on a pop-out is the same.
namespace dingosdk::anim_studio {
inline constexpr std::array<std::string_view, 78> skate_grind_types{
    "Invalid",         "FS_Lip",          "FS_Board",        "FS_50_50",        "FS_NoseSlide",    "FS_TailSlide",    "FS_NoseGrind",
    "FS_5_0",          "FS_Salad",        "FS_Smith",        "FS_Feeble",       "FS_Willy",        "FS_OverWilly",    "FS_NoseBlunt",
    "FS_Blunt",        "FS_Crook",        "FS_OverCrook",    "FS_DarkSlide",    "BS_Lip",          "BS_Board",        "BS_50_50",
    "BS_NoseSlide",    "BS_TailSlide",    "BS_NoseGrind",    "BS_5_0",          "BS_Salad",        "BS_Smith",        "BS_Feeble",
    "BS_Willy",        "BS_OverWilly",    "BS_NoseBlunt",    "BS_Blunt",        "BS_Crook",        "BS_OverCrook",    "BS_DarkSlide",
    "BF_FS_Lip",       "BF_FS_Board",     "BF_FS_50_50",     "BF_FS_NoseSlide", "BF_FS_TailSlide", "BF_FS_NoseGrind", "BF_FS_5_0",
    "BF_FS_Salad",     "BF_FS_Smith",     "BF_FS_Feeble",    "BF_FS_Willy",     "BF_FS_OverWilly", "BF_FS_NoseBlunt", "BF_FS_Blunt",
    "BF_FS_Crook",     "BF_FS_OverCrook", "BF_FS_DarkSlide", "BF_BS_Lip",       "BF_BS_Board",     "BF_BS_50_50",     "BF_BS_NoseSlide",
    "BF_BS_TailSlide", "BF_BS_NoseGrind", "BF_BS_5_0",       "BF_BS_Salad",     "BF_BS_Smith",     "BF_BS_Feeble",    "BF_BS_Willy",
    "BF_BS_OverWilly", "BF_BS_NoseBlunt", "BF_BS_Blunt",     "BF_BS_Crook",     "BF_BS_OverCrook", "BF_BS_DarkSlide", "FS_Suski",
    "BF_FS_Suski",     "BS_Suski",        "BF_BS_Suski",     "BS_Darklip",      "BF_BS_Darklip",   "FS_Darklip",      "BF_FS_Darklip",
    "Exit",
};
// A grind he is doing (not Invalid, not Exit).
inline bool real_grind_type(std::uint32_t type) noexcept { return type > 0 && type < 77; }
inline std::uint32_t skate_grind_type(std::string_view name) noexcept {
    for (std::uint32_t i = 0; i < skate_grind_types.size(); ++i)
        if (skate_grind_types[i] == name) return i;
    return 0;
}
// As the game says it on screen: "FS Bluntslide", "Fakie BS Noseslide", "BS 50-50".
inline std::string grind_type_label(std::uint32_t type) {
    if (!real_grind_type(type)) return {};
    std::string_view name = skate_grind_types[type];
    std::string out;
    if (name.starts_with("BF_")) {
        out = "Fakie ";
        name.remove_prefix(3);
    }
    out += name.substr(0, 2);
    name.remove_prefix(3);
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 19> words{{
        {"50_50", "50-50"}, {"5_0", "5-0"}, {"NoseGrind", "Nosegrind"}, {"Crook", "Crooked grind"}, {"OverCrook", "Overcrook"},
        {"Smith", "Smith grind"}, {"Feeble", "Feeble grind"}, {"Willy", "Willy grind"}, {"OverWilly", "Overwilly"},
        {"Salad", "Salad grind"}, {"Suski", "Suski"}, {"Board", "Boardslide"}, {"Lip", "Lipslide"}, {"NoseSlide", "Noseslide"},
        {"TailSlide", "Tailslide"}, {"Blunt", "Bluntslide"}, {"NoseBlunt", "Nosebluntslide"}, {"DarkSlide", "Darkslide"},
        {"Darklip", "Darklip"},
    }};
    for (const auto& [game, said] : words)
        if (name == game) return out + " " + std::string(said);
    return out + " " + std::string(name);
}
// The grinds one of the game's grind clips is for, by its name: c_proto_onb_grind_[bf_|fakie_]<tricks>_rail_..._[dark],
// <tricks> as "fs_blunt", "fs_boardslide_bs_lipslide", "fs_bs_5_0" (both sides) or "5050" (no side: both); the leaning
// pieces adjust_<left|right|nose|tail>_<tricks>_<tucked|extended>_pose; slappy_[fakie_]<fs|bs>_<trick>_into. Empty for
// anything else, or a name it cannot read (such a clip is never taken for a grind).
inline std::vector<std::uint32_t> grind_types_of(std::string_view asset) {
    const auto slash = asset.rfind('/');
    if (slash != std::string_view::npos) asset.remove_prefix(slash + 1);
    if (asset.find("ofb") != std::string_view::npos) return {}; // on foot
    std::vector<std::string_view> w;
    for (std::size_t at = 0; at <= asset.size();) {
        const auto end = asset.find('_', at);
        const auto word = asset.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        if (word == "0" && !w.empty() && w.back() == "5") w.back() = std::string_view(w.back().data(), 3); // "5_0"
        else w.push_back(word);
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    std::size_t from = 0;
    while (from < w.size() && w[from] != "grind" && w[from] != "adjust" && w[from] != "slappy") ++from;
    if (from >= w.size()) return {};
    const auto kind = w[from];
    bool fakie = false, dark = false;
    std::size_t first = from + 1, last = first;
    if (kind == "grind") {
        if (first < w.size() && (w[first] == "bf" || w[first] == "fakie")) fakie = true, ++first;
        last = first;
        while (last < w.size() && w[last] != "rail") ++last;
        if (last >= w.size()) return {};
        for (std::size_t j = last; j < w.size(); ++j) dark = dark || w[j] == "dark";
    } else if (kind == "adjust") {
        ++first; // left, right, nose, tail
        last = first;
        while (last < w.size() && w[last] != "tucked" && w[last] != "extended") ++last;
        if (last >= w.size()) return {};
        dark = w.back() == "dark";
    } else {
        if (first < w.size() && w[first] == "fakie") fakie = true, ++first;
        last = w.empty() || w.back() != "into" ? first : w.size() - 1;
    }
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 17> tricks{{
        {"5050", "50_50"}, {"5_0", "5_0"}, {"nosegrind", "NoseGrind"}, {"crook", "Crook"}, {"overcrook", "OverCrook"},
        {"smith", "Smith"}, {"feeble", "Feeble"}, {"willy", "Willy"}, {"overwilly", "OverWilly"}, {"salad", "Salad"},
        {"oversalad", "Suski"}, {"boardslide", "Board"}, {"lipslide", "Lip"}, {"noseslide", "NoseSlide"}, {"tailslide", "TailSlide"},
        {"blunt", "Blunt"}, {"noseblunt", "NoseBlunt"},
    }};
    std::vector<std::uint32_t> out;
    std::vector<std::string_view> sides;
    for (std::size_t i = first; i < last; ++i) {
        if (w[i] == "fs" || w[i] == "bs") {
            sides.push_back(w[i] == "fs" ? "FS" : "BS");
            continue;
        }
        std::string_view trick;
        for (const auto& [word, game] : tricks)
            if (w[i] == word) trick = game;
        if (trick.empty()) return {};
        if (dark && trick == "Board") trick = "DarkSlide";
        else if (dark && trick == "Lip") trick = "Darklip";
        if (sides.empty()) sides = {"FS", "BS"};
        for (const auto side : sides) {
            const auto type = skate_grind_type(std::string(fakie ? "BF_" : "") + std::string(side) + "_" + std::string(trick));
            if (!real_grind_type(type)) return {};
            out.push_back(type);
        }
        sides.clear();
    }
    if (!sides.empty()) return {};
    return out;
}
// "FS Bluntslide", "FS Boardslide or BS Lipslide": what a saved grind plays on.
inline std::string grind_types_words(const std::vector<std::uint32_t>& types) {
    std::string out;
    for (std::size_t i = 0; i < types.size(); ++i) out += (i == 0 ? "" : i + 1 == types.size() ? " or " : ", ") + grind_type_label(types[i]);
    return out;
}
} // namespace dingosdk::anim_studio
