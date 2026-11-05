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
        // ---- moving an entry to another place: an insert, never a swap ----
        {
            std::vector<int> list;
            for (int i = 1; i <= 30; ++i) list.push_back(i);
            check(move_to_index(list, 21, 3), "Item 22 can go to place 4");
            check(list[3] == 22 && list[4] == 4 && list[21] == 21 && list[22] == 23,
                  "The items from place 4 to 21 each shift down by one; nothing after the old place moves");
            check(move_to_index(list, 3, 21) && list[21] == 22 && list[3] == 4 && list[20] == 21,
                  "Moving it back down shifts the ones between up by one and restores the order");
            for (int i = 0; i < 30; ++i) check(list[static_cast<std::size_t>(i)] == i + 1, "A move and its reverse are exact");
            check(move_to_index(list, 0, 29) && list.back() == 1 && list.front() == 2, "To the end");
            check(move_to_index(list, 29, 0) && list.front() == 1 && list.back() == 30, "To the start");
            check(!move_to_index(list, 5, 5), "Staying in place is no change");
            check(!move_to_index(list, 30, 0) && !move_to_index(list, 0, 30), "Places outside the list are refused");
            std::vector<int> empty;
            check(!move_to_index(empty, 0, 0), "An empty list cannot move");
        }
        {
            // A marker in gap g puts the carried item at g, or at g - 1 when the gap is below it (it leaves its own place).
            check(target_for_gap(21, 3) == 3 && target_for_gap(3, 22) == 21, "Gaps above and below map to places");
            check(target_for_gap(5, 5) == 5 && target_for_gap(5, 6) == 5, "The gaps either side of an item leave it in place");
            for (std::size_t from = 0; from < 6; ++from)
                for (std::size_t to = 0; to < 6; ++to)
                    check(target_for_gap(from, gap_for_target(from, to)) == to, "A carried place and its marker's gap agree");
            const std::vector<float> tops{0, 70, 140, 400}, bottoms{60, 130, 390, 460};
            check(gap_at(tops, bottoms, -10) == 0 && gap_at(tops, bottoms, 29) == 0, "Above the first row's middle: gap 0");
            check(gap_at(tops, bottoms, 31) == 1 && gap_at(tops, bottoms, 200) == 2, "Rows of any height");
            check(gap_at(tops, bottoms, 300) == 3 && gap_at(tops, bottoms, 1000) == 4, "Under the last row's middle: the end");
            check(step_target(0, -1, 5) == 0 && step_target(4, 1, 5) == 4 && step_target(2, 1, 5) == 3, "Pad steps stop at the ends");
            check(edge_scroll(500, 0, 1000, 60) == 0, "No scroll in the middle");
            check(edge_scroll(0, 0, 1000, 60) == -1 && edge_scroll(1000, 0, 1000, 60) == 1, "Full speed at the edges");
            check(edge_scroll(30, 0, 1000, 60) < 0 && edge_scroll(30, 0, 1000, 60) > -1, "Slower further from the edge");
            check(edge_scroll(5, 0, 100, 60) == 0, "A list too short to have edge zones does not scroll");
        }
        {
            std::vector<PlaylistEntry> entries{{"A", ""}, {"", "One"}, {"B", ""}, {"", "Two"}, {"A", "First"}};
            check(move_playlist_entry(entries, {"", "Two"}, 0) && entries[0].animation == "Two" && entries[1].pack == "A" &&
                      entries[4].animation == "First",
                  "A playlist entry is moved by what it is, and the others keep their order");
            check(!move_playlist_entry(entries, {"", "Gone"}, 0), "An entry that is not there is not moved");
            check(!move_playlist_entry(entries, {"", "Two"}, 0), "Dropping it where it is is no change");
            check(move_playlist_entry(entries, {"", "Two"}, 99) && entries.back().animation == "Two", "A place past the end is the end");
            check(!move_playlist_entry(entries, {"A", "Two"}, 0), "Its source pack is part of what it is");
        }
        {
            const std::vector<std::string> lists{"Everyday", "Big flips"};
            check(offer_target(lists, "Big flips", "Everyday") == OfferTarget{"Big flips", false, false}, "The default playlist first");
            check(offer_target(lists, "", "Big flips") == OfferTarget{"Big flips", false, true}, "None playing: the open one, made the default");
            check(offer_target(lists, "Gone", "") == OfferTarget{"Everyday", false, true}, "A default that is gone counts as none");
            check(offer_target({}, "", "") == OfferTarget{"My tricks", true, true}, "No playlist at all: a new one, made the default");
            const std::vector<std::string> taken{"My tricks", "My tricks 2"};
            check(free_playlist_name_like(taken, "My tricks") == "My tricks 3" && free_playlist_name_like(lists, "My tricks") == "My tricks",
                  "A made playlist never takes a name in use");
        }
        std::cout << "Playlist membership, exclusions, persistence, source rules, undo and moves passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
