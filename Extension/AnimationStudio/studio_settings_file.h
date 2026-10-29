#pragma once
#include "Engine/Core/Json/json.h"
#include "utf8_text.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dingosdk::anim_studio {
// The first-start guide's version: a guide that changed opens once again for everyone (studio-settings.json "guide_read"
// keeps the one read). 2 (2026-10-28): no ACTIVE, your own tricks straight into playlists, pop heights.
inline constexpr int guide_version = 3; // 3 (2026-10-29): the LIBRARY
// What studio-settings.json keeps, apart from the studio's state: the studio reads it into its state at start and
// writes it back whole after each change. A key the file does not have keeps the value it is given.
inline constexpr int grind_rail_defaults = 2; // 2: the square 4 m rail at 60%, set once over any earlier choice
struct SettingsFile {
    bool front_foot{true};
    bool joint_limits{true};
    int stance{};
    bool grab_board{};
    std::string made_by;
    bool hover_preview{true};
    std::set<std::string> ungrouped;
    std::vector<std::string> favorite_packs;
    std::string export_folder, import_folder;
    float crossfade{0.3f};
    std::map<std::string, std::string> editor_keys;
    bool studio_room{true};
    bool play_in_game{true};
    bool catch_marker{true}, drop_preview{true}; // SETTINGS > STUDIO: the catch's tools shown
    bool guide_seen{};                           // this first-start guide (guide_version) was read
    bool smooth_transitions{true};               // SETTINGS > TRICKS > Transitions: SMOOTH (the handover) or CLASSIC
    std::string grind_rail{"own_bkrails_generic_flatrailsquare_00001"}; // SETTINGS > STUDIO > Grind rail (studio_rails.h), "" none
    float grind_rail_size{0.6f};                                            // SETTINGS > STUDIO > Rail size
};
inline void read_settings_file(const Json& root, SettingsFile& f) {
    if (!root.is_object()) return;
    f.front_foot = root.value("front_foot", f.front_foot);
    f.joint_limits = root.value("joint_limits", f.joint_limits);
    f.stance = std::clamp(root.value("stance", f.stance), 0, 2);
    f.grab_board = root.value("grab_board", f.grab_board);
    f.made_by = utf8_prefix(root.value("made_by", f.made_by.c_str()), 48);
    f.hover_preview = root.value("hover_preview", f.hover_preview);
    f.ungrouped.clear();
    if (root.contains("ungrouped") && root.at("ungrouped").is_array())
        for (const auto& name : root.at("ungrouped"))
            if (name.is_string() && f.ungrouped.size() < 2000) f.ungrouped.insert(name.string());
    f.favorite_packs.clear();
    if (root.contains("favorite_packs") && root.at("favorite_packs").is_array())
        for (const auto& name : root.at("favorite_packs"))
            if (name.is_string() && f.favorite_packs.size() < 500) f.favorite_packs.push_back(name.string());
    f.export_folder = root.value("export_folder", "");
    f.import_folder = root.value("import_folder", "");
    if (root.contains("crossfade") && root.at("crossfade").is_number())
        f.crossfade = std::clamp(static_cast<float>(root.at("crossfade").get<double>()), 0.1f, 0.8f);
    f.editor_keys.clear();
    if (root.contains("editor_keys") && root.at("editor_keys").is_object())
        for (const auto& [action, key] : root.at("editor_keys").items())
            if (key.is_string() && action.size() < 32 && key.string().size() < 32 && f.editor_keys.size() < 64) f.editor_keys[action] = key.string();
    f.studio_room = root.value("studio_room", f.studio_room);
    f.play_in_game = root.value("play_in_game", f.play_in_game);
    f.catch_marker = root.value("catch_marker", f.catch_marker);
    f.drop_preview = root.value("drop_preview", f.drop_preview);
    f.guide_seen = root.value("guide_read", 0) >= guide_version; // "guide_seen" was the first guide's: it opens again
    f.smooth_transitions = root.value("smooth_transitions", f.smooth_transitions);
    // The rail chosen before the square 4 m at 60% became the default (grind_rail_defaults 2, 2026-10-29) is set to it once.
    if (root.value("grind_rail_defaults", 0) >= grind_rail_defaults) {
        if (root.contains("grind_rail") && root.at("grind_rail").is_string()) {
            const auto rail = root.at("grind_rail").string();
            f.grind_rail = rail == "off" ? std::string{} : rail.size() < 96 ? rail : f.grind_rail;
        }
        if (root.contains("grind_rail_size") && root.at("grind_rail_size").is_number())
            f.grind_rail_size = std::clamp(static_cast<float>(root.at("grind_rail_size").get<double>()), 0.3f, 1.0f);
    }
}
inline Json settings_file_json(const SettingsFile& f) {
    Json root = Json::object();
    root["front_foot"] = f.front_foot;
    root["joint_limits"] = f.joint_limits;
    root["stance"] = f.stance;
    root["grab_board"] = f.grab_board;
    root["crossfade"] = f.crossfade;
    if (!f.export_folder.empty()) root["export_folder"] = f.export_folder;
    if (!f.import_folder.empty()) root["import_folder"] = f.import_folder;
    if (!f.made_by.empty()) root["made_by"] = f.made_by;
    root["hover_preview"] = f.hover_preview;
    if (!f.ungrouped.empty()) root["ungrouped"] = Json(std::vector<std::string>(f.ungrouped.begin(), f.ungrouped.end()));
    if (!f.favorite_packs.empty()) root["favorite_packs"] = Json(f.favorite_packs);
    if (!f.editor_keys.empty()) {
        Json keys = Json::object();
        for (const auto& [action, key] : f.editor_keys) keys[action] = key;
        root["editor_keys"] = keys;
    }
    root["studio_room"] = f.studio_room;
    root["play_in_game"] = f.play_in_game;
    root["catch_marker"] = f.catch_marker;
    root["drop_preview"] = f.drop_preview;
    root["guide_read"] = f.guide_seen ? guide_version : 0;
    root["smooth_transitions"] = f.smooth_transitions;
    root["grind_rail"] = f.grind_rail.empty() ? std::string("off") : f.grind_rail;
    root["grind_rail_size"] = f.grind_rail_size;
    root["grind_rail_defaults"] = grind_rail_defaults;
    return root;
}
}
