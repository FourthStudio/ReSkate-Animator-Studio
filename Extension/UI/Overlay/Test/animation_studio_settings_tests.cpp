#include "Extension/UI/Overlay/animation_studio_settings.h"
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>

// The rows on their own: the studio's setters are stood in for here, each keeping what it was last given.
namespace dingosdk::anim_studio {
namespace {
struct Given {
    std::optional<bool> room, grab, limits, play_in_game, front_foot;
    std::optional<int> stance;
    std::optional<float> crossfade;
    std::optional<std::string> your_name;
    int bug_reports{};
    std::optional<std::string> playing, mode_of;
    std::optional<PlaylistMode> mode;
};
Given given;
}
void set_room(bool on) { given.room = on; }
void set_grab(bool on) { given.grab = on; }
void set_joint_limits(bool on) { given.limits = on; }
void set_play_in_game(bool on) { given.play_in_game = on; }
void set_front_foot(bool on) { given.front_foot = on; }
void set_stance(int choice) { given.stance = choice; }
void set_crossfade(float seconds) { given.crossfade = seconds; }
void set_your_name(const std::string& name) { given.your_name = name; }
void play_playlist(const std::string& name, std::string& message) {
    given.playing = name;
    message = name.empty() ? "No playlist is on." : name + " is on.";
}
void set_playlist_mode(const std::string& name, PlaylistMode mode, std::string& message) {
    given.mode_of = name;
    given.mode = mode;
    message = "Mode set.";
}
bool report_bug(std::string& message) {
    ++given.bug_reports;
    message = "The bug report is in ReSkate.Tricks\\bug reports.";
    return true;
}
}

using namespace dingosdk;
using namespace dingosdk::overlay::studio_settings;
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void reset() { anim_studio::given = {}; }

void an_on_off_row_is_off_and_on_and_sets_its_setting() {
    reset();
    anim_studio::Settings now;
    now.room = true;
    const auto row = studio_room(now);
    check(row.kind == Setting::Kind::choice && row.on_off, "Studio room is not an on / off row");
    check(row.options == std::vector<std::string>{"OFF", "ON"} && row.selected == 1 && row.value == "ON", "Studio room does not show ON");
    row.pick(0);
    check(anim_studio::given.room == false, "Picking OFF did not turn Studio room off");
    reset();
    row.step(1); // the studio's arrows
    check(anim_studio::given.room == false, "A step on an ON row did not turn it off");
}
void each_on_off_row_sets_its_own_setting() {
    reset();
    const anim_studio::Settings now;
    hands_grab_the_board(now).pick(1);
    joint_limits(now).pick(0);
    custom_tricks_in_game(now).pick(0);
    check(anim_studio::given.grab == true && anim_studio::given.limits == false && anim_studio::given.play_in_game == false,
          "An on / off row set another setting, or not its own");
}
void editing_stance_names_the_games_stance_and_goes_round() {
    reset();
    anim_studio::Settings now;
    now.stance_choice = 0;
    now.game_goofy = true;
    const auto row = editing_stance(now);
    check(row.options.size() == 3 && row.value == "THE GAME'S (GOOFY)", "Editing stance does not name the game's stance");
    row.step(-1);
    check(anim_studio::given.stance == 2, "A step back from THE GAME'S did not go round to GOOFY");
    row.pick(1);
    check(anim_studio::given.stance == 1, "Picking REGULAR did not set it");
}
void trick_matching_picks_front_foot_or_exact_stance() {
    reset();
    anim_studio::Settings now;
    now.front_foot = true;
    const auto row = trick_matching(now);
    check(row.value == "FRONT FOOT" && row.selected == 0, "Trick matching does not show FRONT FOOT");
    row.pick(1);
    check(anim_studio::given.front_foot == false, "Picking EXACT STANCE did not set it");
}
void crossfade_slides_in_hundredths_of_a_second() {
    reset();
    anim_studio::Settings now;
    now.crossfade = 0.3f;
    const auto row = crossfade(now);
    check(row.kind == Setting::Kind::slider && row.low == 0.1f && row.high == 0.8f && row.value == "0.30 s", "Crossfade is not a slider of 0.1 to 0.8 s");
    check(std::abs(row.amount - 0.2f / 0.7f) < 1e-5f, "Crossfade's slider is not where 0.30 s is");
    row.slide(row.amount);
    check(!anim_studio::given.crossfade, "Crossfade was set again with no change");
    row.slide(1.0f);
    check(anim_studio::given.crossfade && std::abs(*anim_studio::given.crossfade - 0.8f) < 1e-5f, "The end of the slider did not set 0.8 s");
}
void report_a_bug_says_where_the_report_went() {
    reset();
    std::string told;
    report_a_bug([&](const std::string& said) { told = said; }).act();
    check(anim_studio::given.bug_reports == 1, "Report a bug made no report");
    check(told == "The bug report is in ReSkate.Tricks\\bug reports.", "Report a bug did not say what the report said");
}
void made_by_sets_the_name_and_says_so() {
    reset();
    anim_studio::Settings now;
    now.your_name = "norms";
    std::string told;
    const auto row = made_by(now, [&](const std::string& said) { told = said; });
    check(row.kind == Setting::Kind::name && row.value == "norms", "Made by does not show the name");
    row.edit("someone");
    check(anim_studio::given.your_name == "someone" && told == "Made by someone from now on.", "Made by did not set the name or say so");
}
void a_pack_folder_knows_whether_it_is_the_default() {
    anim_studio::Settings now;
    now.export_path = "C:\\skate\\ReSkate.Tricks\\exports";
    now.import_folder = "D:\\packs";
    now.import_path = "D:\\packs";
    const auto out = pack_folder(now, true), in = pack_folder(now, false);
    check(out.exporting && !out.custom && out.value == now.export_path && out.label == "Export folder", "The export folder row is wrong");
    check(!in.exporting && in.custom && in.value == "D:\\packs" && in.label == "Import folder", "The import folder row is wrong");
}
anim_studio::PlaylistsNow two_playlists(const std::string& on) {
    anim_studio::PlaylistsNow now;
    now.lists = {{"Street", 4, anim_studio::PlaylistMode::fixed}, {"Park", 0, anim_studio::PlaylistMode::shuffle}};
    now.on = on;
    return now;
}
void playing_lists_none_and_each_playlist_and_switches_by_name() {
    reset();
    std::string told;
    const auto row = playing_playlist(two_playlists("Park"), [&](const std::string& said) { told = said; });
    check(row.options == std::vector<std::string>{"NONE", "Street (4)", "Park (0)"}, "Playing does not list NONE and each playlist with its size");
    check(row.selected == 2 && row.value == "Park (0)" && row.enabled, "Playing does not show the playlist that is on");
    row.pick(1);
    check(anim_studio::given.playing == "Street" && told == "Street is on.", "Picking a playlist did not switch it on by its name");
    row.pick(0);
    check(anim_studio::given.playing == "", "Picking NONE did not switch playlists off");
}
void playing_is_none_and_greyed_with_no_playlists() {
    const auto row = playing_playlist({}, [](const std::string&) {});
    check(row.options == std::vector<std::string>{"NONE"} && row.selected == 0 && !row.enabled, "Playing is not NONE and greyed with no playlists");
}
void the_mode_is_the_playing_playlists_and_set_by_its_name() {
    reset();
    const auto row = which_version_plays(two_playlists("Park"), [](const std::string&) {});
    check(row.options == std::vector<std::string>{"FIXED", "IN TURNS", "SHUFFLE"} && row.value == "SHUFFLE" && row.enabled,
          "The mode is not the playing playlist's");
    row.pick(1);
    check(anim_studio::given.mode_of == "Park" && anim_studio::given.mode == anim_studio::PlaylistMode::turns,
          "Picking IN TURNS did not set it on the playing playlist");
    check(!which_version_plays(two_playlists(""), [](const std::string&) {}).enabled, "The mode can be set with no playlist on");
}
}
int main() {
    try {
        an_on_off_row_is_off_and_on_and_sets_its_setting();
        each_on_off_row_sets_its_own_setting();
        editing_stance_names_the_games_stance_and_goes_round();
        trick_matching_picks_front_foot_or_exact_stance();
        crossfade_slides_in_hundredths_of_a_second();
        report_a_bug_says_where_the_report_went();
        made_by_sets_the_name_and_says_so();
        a_pack_folder_knows_whether_it_is_the_default();
        playing_lists_none_and_each_playlist_and_switches_by_name();
        playing_is_none_and_greyed_with_no_playlists();
        the_mode_is_the_playing_playlists_and_set_by_its_name();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "animation studio settings tests passed\n";
}
