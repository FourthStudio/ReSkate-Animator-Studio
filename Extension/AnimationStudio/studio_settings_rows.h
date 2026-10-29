#pragma once
#include "anim_studio.h"
#include <functional>
#include <string>
#include <vector>

// The Animator Studio's settings as rows: what each is called, what it says about itself, what it is now and how it
// is changed, written once. The studio's SETTINGS and the ReSkate menu's ADDONS > ANIMATOR STUDIO (the ones that act
// in game) each draw the rows they want, in their own order and their own look, so the two never say or do different
// things.
namespace dingosdk::anim_studio::settings_rows {
struct Setting {
    enum class Kind { choice, slider, action, folder, name, info, key, heading };
    Kind kind{};
    std::string label, value, about;
    std::function<void(int)> step; // choice: one along, -1 or 1
    // A choice from a fixed list also has the list, which one it is now and how one is picked. on_off: OFF and ON.
    std::vector<std::string> options;
    int selected{};
    std::function<void(int)> pick;
    bool on_off{};
    float amount{};                   // slider: 0 to 1 of low to high
    std::function<void(float)> slide; // slider: set it, 0 to 1
    float low{0}, high{1};
    std::function<void()> act;                    // action
    bool exporting{}, custom{};                   // folder: which one, and whether it is away from its default
    std::function<void(const std::string&)> edit; // name: set it
    bool enabled{true};
    std::string key_id, own; // key: the action (key_actions) and its own key, as written
};
// What a row says when it is used (where the bug report went, the new name), for the page to show.
using Tell = std::function<void(const std::string&)>;

Setting choice(std::string label, std::string value, std::string about, std::function<void(int)> step);
Setting on_off(std::string label, bool on, std::string about, std::function<void(bool)> set);
Setting slider(std::string label, float amount, std::string value, std::string about, std::function<void(float)> slide);
Setting button_row(std::string label, std::string about, std::function<void()> act);
Setting info_setting(std::string label, std::string value, std::string about);

// STUDIO
Setting studio_room(const anim_studio::Settings&);
Setting grind_rail(const anim_studio::Settings&); // OFF or one of the game's straight rails (studio_rails.h)
Setting catch_marker(const anim_studio::Settings&);
Setting drop_height_preview(const anim_studio::Settings&);
Setting how_it_works(); // opens the first-start guide again
Setting report_a_bug(Tell tell);
// SKATER
Setting editing_stance(const anim_studio::Settings&);
Setting hands_grab_the_board(const anim_studio::Settings&);
Setting joint_limits(const anim_studio::Settings&);
// TRICKS
Setting custom_tricks_in_game(const anim_studio::Settings&);
Setting crossfade(const anim_studio::Settings&);
Setting trick_matching(const anim_studio::Settings&);
Setting transitions(const anim_studio::Settings&);
Setting made_by(const anim_studio::Settings&, Tell tell);
// PACKS
Setting pack_folder(const anim_studio::Settings&, bool exporting);
// PLAYLIST: the one that plays, and which version of a trick in it plays.
Setting playing_playlist(const anim_studio::PlaylistsNow&, Tell tell);
Setting which_version_plays(const anim_studio::PlaylistsNow&, Tell tell);
}
