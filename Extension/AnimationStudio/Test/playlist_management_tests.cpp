#include "Extension/AnimationStudio/playlist_management.h"
#include <iostream>
#include <stdexcept>
using namespace dingosdk::anim_studio;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
    try {
        PresetPack a; a.name = "A"; a.slots["kickflip|low"] = {"First", "Second"}; a.off.insert("Second"); a.pop_rule = "high";
        PresetPack b; b.name = "B"; b.slots["kickflip|low"] = {"First", "Third"};
        const std::vector<PresetPack> packs{a, b};
        std::vector<PlaylistEntry> normal{{"A", ""}, {"B", ""}, {"", "First"}};
        const auto other_playlist = normal;
        check(playlist_membership(normal, packs, "First").packs.size() == 2, "All inclusion paths are visible");
        check(set_playlist_member(normal, packs, {"A", "First"}, false), "A trick included through packs can be removed");
        check(!playlist_membership(normal, packs, "First").included(), "Removal excludes every source path in this destination");
        check(normal[0].omitted.contains("First") && normal[1].omitted.contains("First"), "Exclusions belong to each playlist entry");
        const auto index = playlist_membership_index(normal, packs);
        check(!index.contains("First") && index.contains("Third") && index.at("Third").packs == std::vector<std::string>{"B"},
              "The browser index observes exclusions and source ownership, including shared members");
        check(playlist_membership(other_playlist, packs, "First").included() && !packs[0].off.contains("First"), "Other playlists and packs are unchanged");
        check(set_playlist_member(normal, packs, {"A", "First"}, true), "An excluded pack member can be added again");
        check(normal.size() == 2 && !normal[0].omitted.contains("First"), "Restoring a member does not add a redundant entry");
        check(!set_playlist_member(normal, packs, {"A", "First"}, true), "Repeated additions are idempotent");
        check(set_playlist_member(normal, packs, {"A", "Second"}, true), "An explicit selection can opt into a disabled pack variant");
        std::vector<Preset> presets(3);
        presets[0].name = "First"; presets[1].name = "Second"; presets[2].name = "Third";
        for (auto& p : presets) { p.trick = "kickflip"; p.pop = "low"; }
        select_playlist(presets, packs, normal, PlaylistMode::shuffle);
        check(presets[1].active && presets[1].pack_pop == "high", "An explicit member plays and keeps its source pack's pop rule");
        const auto payload = playlist_entries_json(normal);
        check(playlist_entries_of(dingosdk::Json{{"entries", payload}}) == normal, "Local exclusions survive save and reload");
        auto bad = payload;
        bad.push_back(dingosdk::Json{{"pack", 123}, {"animation", "Bad"}});
        check(playlist_entries_of(dingosdk::Json{{"entries", bad}}) == normal, "Malformed entries cannot discard valid playlists");
        check(set_playlist_pack(normal, "A", true), "Add whole pack can reset a partial selection");
        check(std::count_if(normal.begin(), normal.end(), [](const auto& e) { return e.pack == "A"; }) == 1 && normal[0].omitted.empty(),
              "Whole-pack additions collapse redundant selections and reset local exclusions");
        check(!set_playlist_pack(normal, "A", true), "Adding a whole pack twice is idempotent");
        check(set_playlist_pack(normal, "A", false) && playlist_has_pack(normal, "B"), "Removing one pack keeps unrelated entries");
        select_playlist(presets, packs, std::vector<PlaylistEntry>{{"missing", "First"}}, PlaylistMode::shuffle);
        check(!presets[0].active, "A missing source pack cannot silently drop its rules and play as a personal trick");
        presets[0].available = false;
        select_playlist(presets, packs, std::vector<PlaylistEntry>{{"", "First"}}, PlaylistMode::shuffle);
        check(!presets[0].active, "A missing local clip is not reported as an active custom animation");
        PlaylistEditHistory history{"Target", other_playlist, normal, false};
        auto content = normal;
        check(!history.apply("Another playlist", content), "Undo is tied to the playlist that was actually edited");
        check(history.apply("Target", content) && content == other_playlist && history.undone, "Undo restores the exact entries including exclusions");
        check(history.apply("Target", content) && content == normal && !history.undone, "Redo restores the edited content");
        content.push_back({"", "A new edit"});
        check(!history.apply("Target", content), "Stale undo cannot overwrite a newer edit");
        std::cout << "Playlist membership, exclusions, persistence, source rules and undo passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
