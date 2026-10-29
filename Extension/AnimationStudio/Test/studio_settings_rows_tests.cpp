#include "Extension/AnimationStudio/studio_settings_rows.h"
#include "Extension/AnimationStudio/studio_rails.h"
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>

// The rows on their own: the studio's setters are stood in for here, each keeping what it was last given.
namespace dingosdk::anim_studio {
namespace {
struct Given {
    std::optional<bool> room, grab, limits, play_in_game, front_foot, catch_marker, drop_preview, guide_seen, smooth;
    std::optional<int> stance;
    std::optional<float> crossfade;
    std::optional<std::string> your_name, grind_rail;
    std::optional<float> grind_rail_size;
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
void set_catch_marker(bool on) { given.catch_marker = on; }
void set_drop_preview_tool(bool on) { given.drop_preview = on; }
void set_guide_seen(bool seen) { given.guide_seen = seen; }
void set_smooth_transitions(bool on) { given.smooth = on; }
void set_grind_rail(const std::string& key) { given.grind_rail = key; }
void set_grind_rail_size(float size) { given.grind_rail_size = size; }
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
using namespace dingosdk::anim_studio::settings_rows;
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
void transitions_picks_smooth_or_classic() {
    reset();
    anim_studio::Settings now;
    now.smooth_transitions = true;
    const auto row = transitions(now);
    check(row.options == std::vector<std::string>{"SMOOTH", "CLASSIC"} && row.selected == 0, "Transitions does not start on SMOOTH");
    row.pick(1);
    check(anim_studio::given.smooth == false, "Picking CLASSIC did not set it");
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
void the_catch_tools_are_on_by_default_and_each_sets_its_own() {
    reset();
    const anim_studio::Settings now;
    const auto marker = catch_marker(now), drop = drop_height_preview(now);
    check(marker.on_off && marker.selected == 1 && drop.on_off && drop.selected == 1, "Catch marker or Drop height preview is not ON by default");
    marker.pick(0);
    check(anim_studio::given.catch_marker == false && !anim_studio::given.drop_preview, "Catch marker OFF did not set it alone");
    drop.pick(0);
    check(anim_studio::given.drop_preview == false, "Drop height preview OFF did not set it");
}
void grind_rail_lists_off_and_the_rails_this_player_has() {
    reset();
    anim_studio::Settings now;
    now.grind_rail = "own_bkrails_generic_flatrailsquare_00001";
    now.rails = {"own_bkrails_generic_flatrailround_00001", "own_bkrails_generic_flatrailsquare_00001", "own_bkrails_generic_snakewavy_00005"};
    const auto row = grind_rail(now);
    check(row.options.size() == 3 && row.options[0] == "OFF" && row.selected == 2, "Grind rail is not OFF and the straight rails the player has");
    row.pick(1);
    check(anim_studio::given.grind_rail == "own_bkrails_generic_flatrailround_00001", "Picking a rail did not set it");
    row.pick(0);
    check(anim_studio::given.grind_rail == "", "Picking OFF did not take the rail away");
    const auto size = rail_size(now);
    check(size.options.size() == anim_studio::studio_rail_sizes.size() && size.options[size.selected].starts_with("60%: 30 cm high"),
          "Rail size does not list the sizes with the chosen rail's height at 60%");
    size.pick(0);
    check(anim_studio::given.grind_rail_size == 1.0f, "Picking 100% did not set the size");
}
void how_it_works_opens_the_guide_again() {
    reset();
    const auto row = how_it_works();
    check(row.kind == Setting::Kind::action, "How it works is not a button");
    row.act();
    check(anim_studio::given.guide_seen == false, "How it works did not open the guide again");
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
        transitions_picks_smooth_or_classic();
        crossfade_slides_in_hundredths_of_a_second();
        report_a_bug_says_where_the_report_went();
        made_by_sets_the_name_and_says_so();
        a_pack_folder_knows_whether_it_is_the_default();
        playing_lists_none_and_each_playlist_and_switches_by_name();
        playing_is_none_and_greyed_with_no_playlists();
        the_mode_is_the_playing_playlists_and_set_by_its_name();
        the_catch_tools_are_on_by_default_and_each_sets_its_own();
        how_it_works_opens_the_guide_again();
        grind_rail_lists_off_and_the_rails_this_player_has();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "animation studio settings tests passed\n";
}
