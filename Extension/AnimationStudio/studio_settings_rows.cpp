#include "studio_settings_rows.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <utility>

namespace dingosdk::anim_studio::settings_rows {
namespace studio = anim_studio;
namespace {
constexpr float shortest_crossfade = 0.1f, longest_crossfade = 0.8f; // seconds, as set_crossfade keeps it
// One of `options`: a step goes round the list.
Setting one_of(std::string label, std::vector<std::string> options, int selected, std::string about, std::function<void(int)> pick) {
    const int count = static_cast<int>(options.size());
    Setting s = choice(std::move(label), options[static_cast<std::size_t>(selected)], std::move(about),
                       [selected, count, pick](int by) { pick((selected + by + count) % count); });
    s.options = std::move(options);
    s.selected = selected;
    s.pick = std::move(pick);
    return s;
}
}

Setting choice(std::string label, std::string value, std::string about, std::function<void(int)> step) {
    Setting s;
    s.kind = Setting::Kind::choice;
    s.label = std::move(label);
    s.value = std::move(value);
    s.about = std::move(about);
    s.step = std::move(step);
    return s;
}
Setting on_off(std::string label, bool on, std::string about, std::function<void(bool)> set) {
    Setting s = one_of(std::move(label), {"OFF", "ON"}, on ? 1 : 0, std::move(about), [set = std::move(set)](int picked) { set(picked == 1); });
    s.on_off = true;
    return s;
}
Setting slider(std::string label, float amount, std::string value, std::string about, std::function<void(float)> slide) {
    Setting s;
    s.kind = Setting::Kind::slider;
    s.label = std::move(label);
    s.amount = amount;
    s.value = std::move(value);
    s.about = std::move(about);
    s.slide = std::move(slide);
    return s;
}
Setting button_row(std::string label, std::string about, std::function<void()> act) {
    Setting s;
    s.kind = Setting::Kind::action;
    s.label = std::move(label);
    s.about = std::move(about);
    s.act = std::move(act);
    return s;
}
Setting info_setting(std::string label, std::string value, std::string about) {
    Setting s;
    s.kind = Setting::Kind::info;
    s.label = std::move(label);
    s.value = std::move(value);
    s.about = std::move(about);
    return s;
}

Setting studio_room(const studio::Settings& now) {
    return on_off("Studio room", now.room,
                  "On: the studio opens in the game's own lit studio room, as skate.'s Skaters page shows it. Off: it opens where "
                  "you are. Takes effect the next time it opens.",
                  [](bool on) { studio::set_room(on); });
}
Setting controller_combo(const studio::Settings& now) {
    return one_of("Controller combo", {studio::open_combo_names.begin(), studio::open_combo_names.end()}, std::clamp(now.open_combo, 0, 3),
                  "What you hold on a controller (half a second) to open the studio, and to close it again. Another mod using the "
                  "same buttons: pick another, or OFF and open it with F8 or from the ReSkate menu (ADDONS > ANIMATOR STUDIO).",
                  [](int picked) { studio::set_open_combo(picked); });
}
Setting catch_marker(const studio::Settings& now) {
    return on_off("Catch marker", now.catch_marker,
                  "On: the CATCH flag shows on the timeline, and CATCH HOLD in THIS TRICK. Off: both are hidden, for a cleaner "
                  "timeline. A trick's catch still plays in game as it was set.",
                  [](bool on) { studio::set_catch_marker(on); });
}
Setting drop_height_preview(const studio::Settings& now) {
    return on_off("Drop height preview", now.drop_preview_tool,
                  "On: DROP PREVIEW shows in THIS TRICK, to play the trick here as off a drop, its catch slowed as in game. Off: "
                  "it is hidden and the trick plays as it is. Only the studio's view: nothing in game changes.",
                  [](bool on) { studio::set_drop_preview_tool(on); });
}
Setting how_it_works() {
    return button_row("How it works",
                      "The guide from the first start again: how packs play, Exact stance and Front foot, putting your own "
                      "tricks in a playlist, and pop heights.",
                      [] { studio::set_guide_seen(false); });
}
Setting report_a_bug(Tell tell) {
    return button_row("Report a bug",
                      "Something went wrong? Press this afterwards, from the pause menu or the studio: the last 25 seconds of play "
                      "before you opened them (kept however long you take), the game's log, your studio settings and which build "
                      "this is go into one zip, and Explorer shows it. Send that zip to us with what happened, the trick and your "
                      "stance. Nothing is sent by itself.",
                      [tell = std::move(tell)] {
                          std::string m;
                          studio::report_bug(m);
                          tell(m);
                      });
}

Setting editing_stance(const studio::Settings& now) {
    return one_of("Editing stance", {now.game_goofy ? "THE GAME'S (GOOFY)" : "THE GAME'S (REGULAR)", "REGULAR", "GOOFY"},
                  std::clamp(now.stance_choice, 0, 2),
                  "Which foot leads while you edit, in the studio only: your stance in game is the game's own option. A trick "
                  "made leading with the other foot is mirrored into this stance the way the game mirrors its own, so the leg "
                  "you grab is the leg that moves. The game's: the stance set in the game's own options.",
                  [](int picked) { studio::set_stance(picked); });
}
Setting hands_grab_the_board(const studio::Settings& now) {
    return on_off("Hands grab the board", now.grab_board,
                  "A hand brought near the deck closes its fingers round it, more the nearer it gets. Shown only: the fingers are "
                  "not keyed or saved.",
                  [](bool on) { studio::set_grab(on); });
}
Setting advanced_bones(const studio::Settings& now) {
    return on_off("Advanced bones", now.advanced_bones,
                  "On: the toes, and the spine and neck between the usual joints (middle back, upper chest, upper neck), on the "
                  "timeline and on him to key. Off: the usual joints only. A joint you keyed shows either way.",
                  [](bool on) { studio::set_advanced_bones(on); });
}
Setting finger_bones(const studio::Settings& now) {
    return on_off("Fingers", now.finger_bones,
                  "On: every finger on the timeline and on him to key, three joints each from the knuckle out, to close a hand "
                  "round the board for a grab. They play in game like any other key. A finger you keyed shows either way.",
                  [](bool on) { studio::set_finger_bones(on); });
}
Setting joint_limits(const studio::Settings& now) {
    return on_off("Joint limits", now.limits,
                  "On: knees and elbows bend one way and every joint stays in the range the game's own animation uses, while you "
                  "pose and in game. Off: joints go wherever you push them.",
                  [](bool on) { studio::set_joint_limits(on); });
}

Setting custom_tricks_in_game(const studio::Settings& now) {
    return on_off("Custom tricks in game", now.play_in_game,
                  "On: your saved tricks play in game. The game leads: it says which clip it plays and how far into it it is, and "
                  "your keys go onto its pose there, so the trick looks as the studio shows it and lands when the game lands.",
                  [](bool on) { studio::set_play_in_game(on); });
}
Setting crossfade(const studio::Settings& now) {
    constexpr float range = longest_crossfade - shortest_crossfade;
    Setting s = slider("Crossfade", std::clamp((now.crossfade - shortest_crossfade) / range, 0.0f, 1.0f), std::format("{:.2f} s", now.crossfade),
                       "How smoothly your trick hands back to the game when it goes into its next move - a manual, a push, the "
                       "next trick. Short is snappy, long is buttery.",
                       // Set in hundredths of a second: a drag saves the settings only when that changes, not every frame.
                       [was = now.crossfade](float amount) {
                           const float seconds = std::round((shortest_crossfade + amount * range) * 100) / 100;
                           if (std::abs(seconds - was) > 0.005f) studio::set_crossfade(seconds);
                       });
    s.low = shortest_crossfade;
    s.high = longest_crossfade;
    return s;
}
Setting trick_matching(const studio::Settings& now) {
    return one_of("Trick matching", {"FRONT FOOT", "EXACT STANCE"}, now.front_foot ? 0 : 1,
                  "Front foot: a trick plays in every stance, mirrored onto your front foot when the other foot leads, as the game "
                  "does. Exact stance: only the one made on the foot that leads now; the game's own otherwise.",
                  [](int picked) { studio::set_front_foot(picked == 0); });
}
Setting transitions(const studio::Settings& now) {
    return one_of("Transitions", {"SMOOTH", "CLASSIC"}, now.smooth_transitions ? 0 : 1,
                  "Smooth: into a grind your trick hands over the way the game hands over its own - the board and legs first, "
                  "then the arms, then the back and head, the trick going on meanwhile. Classic: as before, to compare.",
                  [](int picked) { studio::set_smooth_transitions(picked == 0); });
}
Setting made_by(const studio::Settings& now, Tell tell) {
    Setting s;
    s.kind = Setting::Kind::name;
    s.label = "Made by";
    s.value = now.your_name;
    s.about = "Your name. It goes with every trick you save and every pack you make, so a trick passed on says whose it is.";
    s.edit = [tell = std::move(tell)](const std::string& name) {
        studio::set_your_name(name);
        tell("Made by " + name + " from now on.");
    };
    return s;
}

Setting pack_folder(const studio::Settings& now, bool exporting) {
    Setting s;
    s.kind = Setting::Kind::folder;
    s.exporting = exporting;
    s.label = exporting ? "Export folder" : "Import folder";
    s.value = exporting ? now.export_path : now.import_path;
    s.custom = !(exporting ? now.export_folder : now.import_folder).empty();
    s.about = exporting ? "Where EXPORT PACK and EXPORT ALL TRICKS put their zip. It opens by itself after an export. CHOOSE picks "
                          "another folder, DEFAULT goes back to ReSkate.Tricks\\exports."
                        : "Where IMPORT looks for packs: put a pack's zip there (your Downloads folder works well) and press IMPORT. "
                          "Imported zips are moved into an imported folder inside it.";
    return s;
}

Setting playing_playlist(const studio::PlaylistsNow& now, Tell tell) {
    std::vector<std::string> options{"NONE"};
    int selected = 0;
    for (const auto& list : now.lists) {
        if (list.name == now.on) selected = static_cast<int>(options.size());
        options.push_back(std::format("{} ({})", list.name, list.size));
    }
    Setting s = one_of("Playing", std::move(options), selected,
                       "The playlist that decides what of yours plays in game, from the top down. One plays at a time; NONE: "
                       "nothing of yours plays. Playlists are made in the studio: the LIBRARY.",
                       [lists = now.lists, tell = std::move(tell)](int picked) {
                           std::string m;
                           studio::play_playlist(picked == 0 ? std::string{} : lists[static_cast<std::size_t>(picked - 1)].name, m);
                           tell(m);
                       });
    s.enabled = !now.lists.empty();
    return s;
}
Setting which_version_plays(const studio::PlaylistsNow& now, Tell tell) {
    const auto on = std::find_if(now.lists.begin(), now.lists.end(), [&](const auto& list) { return list.name == now.on; });
    Setting s = one_of("Which version plays", {"FIXED", "IN TURNS", "SHUFFLE"},
                       on == now.lists.end() ? 0 : static_cast<int>(on->mode),
                       "When the playlist that plays has more than one version of a trick (the same trick, pop and stance). Fixed: "
                       "the one highest in the playlist plays every time (a pack's slot on Alternate still takes turns). In turns: "
                       "one after another, top to bottom, attempt by attempt. Shuffle: any of them at random, never the same one "
                       "twice running.",
                       [name = now.on, tell = std::move(tell)](int picked) {
                           std::string m;
                           studio::set_playlist_mode(name, static_cast<studio::PlaylistMode>(picked), m);
                           tell(m);
                       });
    s.enabled = on != now.lists.end();
    return s;
}
}
