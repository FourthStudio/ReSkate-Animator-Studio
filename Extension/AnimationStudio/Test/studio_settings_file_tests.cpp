#include "Extension/AnimationStudio/studio_settings_file.h"
#include <iostream>
#include <stdexcept>

using namespace dingosdk;
using namespace dingosdk::anim_studio;
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
SettingsFile reloaded(const SettingsFile& saved) {
    SettingsFile read;
    read_settings_file(Json::parse(settings_file_json(saved).dump(2)), read);
    return read;
}
void studio_room_and_custom_tricks_survive_a_save_and_reload() {
    SettingsFile saved;
    saved.studio_room = false;
    saved.play_in_game = false;
    const auto read = reloaded(saved);
    check(!read.studio_room, "Studio room came back on after it was saved off");
    check(!read.play_in_game, "Custom tricks in game came back on after it was saved off");
    saved.studio_room = saved.play_in_game = true;
    const auto again = reloaded(saved);
    check(again.studio_room && again.play_in_game, "Studio room and Custom tricks in game did not come back on");
}
void a_file_from_before_keeps_both_on() {
    // A settings file written before these two were saved has neither key.
    SettingsFile read;
    read_settings_file(Json::parse(R"({"front_foot": false, "crossfade": 0.5})"), read);
    check(read.studio_room && read.play_in_game, "A file without the keys turned Studio room or Custom tricks in game off");
    check(!read.front_foot && read.crossfade == 0.5f, "The keys the file has were not read");
}
void every_saved_setting_survives_a_save_and_reload() {
    SettingsFile saved;
    saved.front_foot = false;
    saved.joint_limits = false;
    saved.stance = 2;
    saved.grab_board = true;
    saved.made_by = "norms";
    saved.hover_preview = false;
    saved.ungrouped = {"kickflip"};
    saved.favorite_packs = {"street"};
    saved.export_folder = "D:\\exports";
    saved.import_folder = "D:\\imports";
    saved.crossfade = 0.25f;
    saved.editor_keys = {{"snap", "G"}};
    const auto read = reloaded(saved);
    check(!read.front_foot && !read.joint_limits && read.stance == 2 && read.grab_board && read.made_by == "norms" && !read.hover_preview,
          "A switch or the name did not come back as saved");
    check(read.ungrouped == saved.ungrouped && read.favorite_packs == saved.favorite_packs && read.editor_keys == saved.editor_keys,
          "A list did not come back as saved");
    check(read.export_folder == saved.export_folder && read.import_folder == saved.import_folder && read.crossfade == 0.25f,
          "A folder or the crossfade did not come back as saved");
}
void the_catch_tools_and_the_guide_survive_a_save_and_reload() {
    SettingsFile saved;
    saved.catch_marker = false;
    saved.guide_seen = true;
    const auto read = reloaded(saved);
    check(!read.catch_marker, "Catch marker came back on after it was saved off");
    check(read.guide_seen, "The guide opens again after it was read");
}
void a_file_from_before_shows_the_catch_tools_and_the_guide() {
    // Settings saved before these keys: the catch tools stay as they were (shown), and the guide opens once.
    SettingsFile read;
    read_settings_file(Json::parse(R"({"front_foot": true})"), read);
    check(read.catch_marker, "A file without the key hid the catch tools");
    check(!read.guide_seen, "A file without the key does not open the guide");
    // The first guide read (its key from before): the new guide opens once.
    SettingsFile older;
    read_settings_file(Json::parse(R"({"guide_seen": true})"), older);
    check(!older.guide_seen, "The changed guide does not open for someone who read the first one");
    // A file from when the drop preview was a setting: its key is passed over.
    SettingsFile with_drop;
    read_settings_file(Json::parse(R"({"catch_marker": false, "drop_preview": false})"), with_drop);
    check(!with_drop.catch_marker, "A file with the old drop_preview key did not load");
}
void values_out_of_range_are_kept_in_it() {
    SettingsFile read;
    read_settings_file(Json::parse(R"({"stance": 7, "crossfade": 3.0})"), read);
    check(read.stance == 2 && read.crossfade == 0.8f, "A stance or crossfade out of range was kept");
}
}
int main() {
    try {
        studio_room_and_custom_tricks_survive_a_save_and_reload();
        a_file_from_before_keeps_both_on();
        every_saved_setting_survives_a_save_and_reload();
        values_out_of_range_are_kept_in_it();
        the_catch_tools_and_the_guide_survive_a_save_and_reload();
        a_file_from_before_shows_the_catch_tools_and_the_guide();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "studio settings file tests passed\n";
}
