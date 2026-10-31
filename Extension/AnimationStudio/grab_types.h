#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

// The game's grabs as its grab loop table has them (animation/dingo/cdb_onboard_grab_loop, in the game's files): the table
// is keyed on EnumGS.GrabType, one loop family a value - 1 Frontside, 2 Backside, 3 Tail, 4 Seatbelt, 5 Crail, 6 Nose,
// 7 Stale, 8 Mute (the weddle loops), 9 Double, 10 Rocket - and the style within a family is Enum.GS.DetailedGrabType
// (BS_Melon, FS_TuckKnee, Tail_FS_Shifty, ...). A saved grab loop is filed the same way from its clip's name, so the one
// for the grab the game is doing can start the moment it starts (said_loop).
namespace dingosdk::anim_studio {
struct GrabLoop {
    int family{};      // EnumGS.GrabType 1 to 10; 0 not one of those loops (a one-foot grab, a grab on the ground)
    std::string style; // the words after the family in its name ("melon", "fs_shifty"); "" the plain one
    bool fakie{};
};
// "c_proto_onb_inair_bs_grab_melon_mid_loop": family 2, style "melon".
inline GrabLoop grab_loop_of(std::string_view trick) {
    std::string a(trick.substr(trick.starts_with("native:") ? 7 : 0));
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (a.find("loop") == std::string::npos || a.find("ground") != std::string::npos) return {};
    static constexpr std::array<std::pair<std::string_view, int>, 10> families{{{"double_grab", 9}, {"grab_rocket", 10}, {"fs_grab", 1}, {"bs_grab", 2},
                                                                                 {"grab_tail", 3}, {"grab_seatbelt", 4}, {"grab_crail", 5},
                                                                                 {"grab_nose", 6}, {"grab_stale", 7}, {"grab_weddle", 8}}};
    for (const auto& [word, family] : families) {
        const auto at = a.find(word);
        if (at == std::string::npos) continue;
        GrabLoop out;
        out.family = family;
        out.fakie = a.find("fakie") != std::string::npos;
        // The style: the words after the family, without the loop's own ("mid", "loop", a number).
        std::size_t from = at + word.size();
        while (from < a.size()) {
            if (a[from] == '_') {
                ++from;
                continue;
            }
            auto end = a.find('_', from);
            if (end == std::string::npos) end = a.size();
            const auto w = a.substr(from, end - from);
            const bool digits = std::all_of(w.begin(), w.end(), [](char c) { return c >= '0' && c <= '9'; });
            if (w != "mid" && w != "loop" && !digits) out.style += (out.style.empty() ? "" : "_") + w;
            from = end;
        }
        return out;
    }
    return {};
}
// The game's grab style (an Enum.DetailedGrabType value) as a grab loop's style words: "BS_Melon" -> "melon",
// "Tail_FS_Shifty" -> "fs_shifty", "BS" -> "" (the plain one).
inline std::string grab_style_words(int detailed) {
    static constexpr std::array<std::string_view, 39> styles{
        "",          "",         "method",   "japan",     "melon",     "crossbone", "",          "nosebone",  "tailbone",  "tuckknee",
        "stiffy",    "",         "nosebone", "fs_shifty", "bs_shifty", "",          "fs_shifty", "bs_shifty", "",          "fs_shifty",
        "bs_shifty", "",         "tailbone", "fs_shifty", "bs_shifty", "",          "tailbone",  "nosebone",  "tuckknee",  "",
        "tail",      "nose",     "japan",    "",          "left",      "right",     "front",     "back",      ""};
    return detailed >= 0 && static_cast<std::size_t>(detailed) < styles.size() ? std::string(styles[static_cast<std::size_t>(detailed)]) : std::string{};
}
// How well a saved grab loop fits the grab the game is doing (`family` EnumGS.GrabType, `detailed` its style; `fakie` riding
// fakie, when known): -1 not for it; 0 its own style, 1 the plain one (or one with that style in it), 2 another style of the
// same family; 3 more for the other way round than he rides.
inline int grab_loop_rank(const GrabLoop& loop, int family, int detailed, const bool* fakie) {
    if (family < 1 || family > 10 || loop.family != family) return -1;
    const auto wanted = grab_style_words(detailed);
    const int style = loop.style == wanted                                                        ? 0
                      : loop.style.empty() || (!wanted.empty() && loop.style.find(wanted) != std::string::npos) ? 1
                                                                                                    : 2;
    return style + (fakie && loop.fakie != *fakie ? 3 : 0);
}
} // namespace dingosdk::anim_studio
