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
// keeps the one read). 2: no ACTIVE, your own tricks straight into playlists, pop heights.
inline constexpr int guide_version = 3; // 3: the LIBRARY
// The editor's first-open tour, the same way ("editor_tour_read"): a tour that changes much shows once again.
inline constexpr int editor_tour_version = 1;
// TRICKS > MY TRICKS as a folder: how it was last shown, kept for the next time (trick_browser.h's ViewMode, GroupBy and
// Column, by their numbers). FROM PACKS has its own grouping: by pack, as it always was.
struct TricksView {
    int view{1};        // 0 icons, 1 list, 2 details
    int group_mine{};   // 0 by trick, 1 by type, 2 by pack, 3 none
    int group_packs{2};
    int sort{};         // the column sorted by
    bool descending{};
    bool operator==(const TricksView&) const = default;
};
// What studio-settings.json keeps, apart from the studio's state: the studio reads it into its state at start and
// writes it back whole after each change. A key the file does not have keeps the value it is given.
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
    bool catch_marker{true};                     // SETTINGS > STUDIO: the catch's tools shown
    bool guide_seen{};                           // this first-start guide (guide_version) was read
    bool editor_tour_seen{};                     // the editor's first-open tour (editor_tour_version) was gone through or skipped
    TricksView tricks_view;                      // TRICKS > MY TRICKS: icons, list or details, its groups and its order
    bool smooth_transitions{true};               // SETTINGS > TRICKS > Transitions: SMOOTH (the handover) or CLASSIC
    bool advanced_bones{}, finger_bones{};                                  // SETTINGS > SKATER: their rows shown
    int open_combo{};                                                       // SETTINGS > STUDIO > Controller combo
};
inline void read_settings_file(const Json& root, SettingsFile& f) {
    if (!root.is_object()) return;
    f.front_foot = root.value("front_foot", f.front_foot);
    f.joint_limits = root.value("joint_limits", f.joint_limits);
    f.stance = std::clamp(root.value("stance", f.stance), 0, 2);
    f.grab_board = root.value("grab_board", f.grab_board);
    f.advanced_bones = root.value("advanced_bones", f.advanced_bones);
    f.finger_bones = root.value("finger_bones", f.finger_bones);
    f.open_combo = std::clamp(root.value("open_combo", f.open_combo), 0, 3);
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
    f.guide_seen = root.value("guide_read", 0) >= guide_version; // "guide_seen" was the first guide's: it opens again
    f.smooth_transitions = root.value("smooth_transitions", f.smooth_transitions);
    f.editor_tour_seen = root.value("editor_tour_read", 0) >= editor_tour_version;
    if (root.contains("my_tricks") && root.at("my_tricks").is_object()) {
        const auto& t = root.at("my_tricks");
        auto& v = f.tricks_view;
        v.view = std::clamp(t.value("view", v.view), 0, 2);
        v.group_mine = std::clamp(t.value("group", v.group_mine), 0, 3);
        v.group_packs = std::clamp(t.value("group_packs", v.group_packs), 0, 3);
        v.sort = std::clamp(t.value("sort", v.sort), 0, 7);
        v.descending = t.value("descending", v.descending);
    }
}
inline Json settings_file_json(const SettingsFile& f) {
    Json root = Json::object();
    root["front_foot"] = f.front_foot;
    root["joint_limits"] = f.joint_limits;
    root["stance"] = f.stance;
    root["grab_board"] = f.grab_board;
    root["advanced_bones"] = f.advanced_bones;
    root["finger_bones"] = f.finger_bones;
    root["open_combo"] = f.open_combo;
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
    root["guide_read"] = f.guide_seen ? guide_version : 0;
    root["smooth_transitions"] = f.smooth_transitions;
    root["editor_tour_read"] = f.editor_tour_seen ? editor_tour_version : 0;
    Json tricks = Json::object();
    tricks["view"] = f.tricks_view.view;
    tricks["group"] = f.tricks_view.group_mine;
    tricks["group_packs"] = f.tricks_view.group_packs;
    tricks["sort"] = f.tricks_view.sort;
    tricks["descending"] = f.tricks_view.descending;
    root["my_tricks"] = tricks;
    return root;
}
}
