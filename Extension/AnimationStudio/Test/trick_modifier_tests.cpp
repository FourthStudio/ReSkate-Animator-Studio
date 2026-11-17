#include "Extension/AnimationStudio/trick_modifier.h"
#include "Extension/AnimationStudio/anim_runtime_rules.h"
#include "Extension/AnimationStudio/native_modifier_selection.h"
#include "Engine/Game/Input/playstation_report.h"
#include "Extension/UI/Overlay/polled_gamepad.h"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace dingosdk;
using namespace dingosdk::anim_studio;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        ModifierBinding bind;
        check(!modifier_mask(bind), "A fresh profile does not claim any native button");
        bind.playlist = "Specials"; bind.button = 1;
        check(modifier_mask(bind) == 0x0200, "RB and R1 use the same normalised button");
        bind.button = 99; check(!modifier_mask(bind), "Invalid binds cannot capture unrelated input");
        bind.button = 1; bind.playlist.clear(); check(!modifier_mask(bind), "No playlist means no capture");
        ModifierAttempt attempt;
        attempt.begin(true, true, false);
        check(attempt.held, "Holding a modifier at the beginning selects its route");
        attempt.begin(false, true, false); check(!attempt.held, "The next normal attempt returns to the normal route");
        attempt.begin(true, false, false); check(!attempt.held, "An unloaded playlist cannot activate a modifier");
        attempt.begin(true, true, true); check(!attempt.held, "TEST IN GAME has priority");
        NativeModifierHold hold;
        hold.observe(1, 0, true, true, false);
        check(hold.held, "A grind latches the modifier on rail entry, even before its identity arrives");
        hold.observe(1, 12, false, true, false);
        check(hold.held, "A late grind identity and a physical release do not swap the running route");
        hold.observe(1, 13, false, true, false);
        check(!hold.held, "A genuinely different grind starts a new choice");
        hold.observe(2, 1, true, true, false);
        hold.observe(2, 1, false, false, false);
        check(hold.held, "A running manual can finish its chosen route after a binding is disabled");
        hold.observe(0, 0, true, true, false);
        check(!hold.held, "Leaving the hold clears its modifier");
        hold.observe(3, 2, true, true, true);
        check(!hold.held, "TEST IN GAME also has priority over native holds");
        // Several playlists on buttons, each on its own (2026-11-05).
        ModifierBindings all;
        put_binding(all, {"Street", 1, true});
        put_binding(all, {"Park", 2, false});
        check(all.size() == 2 && modifier_masks(all) == (0x0200 | 0x0100) && modifier_kept(all) == 0x0200, "Two playlists do not play on two buttons");
        check(held_route(all, 0x0100) == 0x0100 && held_route(all, 0) == 0 && held_route(all, 0x0300) == 0x0200,
              "The button held does not pick its own playlist, or the first one's when both are held");
        put_binding(all, {"Park", 1, true});
        check(all.size() == 1 && all[0].playlist == "Park" && all[0].button == 1, "A button moved to another playlist stayed on the one it was on");
        put_binding(all, {"Park", 0, true});
        check(all.empty(), "Button 0 did not take a playlist off its button");
        attempt.begin(0x0100, true, false);
        check(attempt.held && attempt.button == 0x0100, "An attempt does not keep the button whose playlist it takes");
        NativeModifierHold two;
        two.observe(1, 0, 0x0200, true, false);
        check(two.held && two.button == 0x0200, "A grind does not keep the button whose playlist it takes");
        two.observe(0, 0, 0, true, false);
        check(!two.held && two.button == 0, "Leaving the hold did not clear its button");
        // Two buttons held together (2026-11-13): LB + RB is a playlist's own, beside the ones on LB and on RB.
        ModifierBindings pairs;
        put_binding(pairs, {"Street", 1, true});
        put_binding(pairs, {"Park", 2, false});
        put_binding(pairs, {"Both", 2, true, 1});
        check(pairs.size() == 3 && modifier_mask(pairs[2]) == 0x0300 && modifier_together(modifier_mask(pairs[2])) && !modifier_together(modifier_mask(pairs[0])),
              "Two buttons held together took a playlist off one of them, or are not both of them");
        check(modifier_words(pairs[2]) == "LB / L1 + RB / R1" && modifier_words(pairs[0]) == "RB / R1" && modifier_words({"None", 0, true, 1}).empty(),
              "The buttons are not said as they are held");
        check(held_route(pairs, 0x0300) == 0x0300 && held_route(pairs, 0x0200) == 0x0200 && held_route(pairs, 0x0100) == 0x0100,
              "Both held do not play their own playlist before either button's, or one alone plays the pair's");
        check(modifier_kept(pairs) == 0x0200 && modifier_kept_together(pairs) == std::vector<std::uint16_t>{0x0300},
              "Two held together are kept from the game as one button is, or a playlist's one button is not kept");
        put_binding(pairs, {"Street", 1, true, 2});
        check(pairs.size() == 2 && binding_of(pairs, "Street")->also == 2 && !binding_of(pairs, "Both"),
              "RB + LB on another playlist did not take LB + RB off the one it was on");
        check(held_route(pairs, 0x0200) == 0 && held_route(pairs, 0x0300) == 0x0300 && modifier_kept(pairs) == 0,
              "One of two held together plays or is kept from the game alone");
        // RB + Y taken over (2026-11-16: Y still got the skater off the board, decided a tick after the game read it).
        ModifierBindings off_board;
        put_binding(off_board, {"Flips", 1, true, 6});
        put_binding(off_board, {"Loose", 2, false, 6});
        const auto together = modifier_kept_together(off_board);
        check(together == std::vector<std::uint16_t>{0x8200} && modifier_kept(off_board) == 0, "Only the two taken over are kept from the game");
        std::uint16_t kept{};
        check(overlay::detail::together_hidden(together, 0x0200, kept) == 0 && overlay::detail::together_hidden(together, 0x8000, kept) == 0,
              "One of two held together is kept from the game alone");
        check(overlay::detail::together_hidden(together, 0x9200, kept) == 0x8200, "The read that has both down still gives the game the second one's press");
        check(overlay::detail::together_hidden(together, 0x8000, kept) == 0x8000 && overlay::detail::together_hidden(together, 0x8200, kept) == 0x8200,
              "The one still held after the other was let go is a fresh press to the game");
        check(overlay::detail::together_hidden(together, 0x0200, kept) == 0x0200 && overlay::detail::together_hidden(together, 0, kept) == 0 &&
              overlay::detail::together_hidden(together, 0x8000, kept) == 0, "A button let go and pressed alone is not the game's again");
        check(overlay::detail::together_hidden(together, 0x8100, kept) == 0, "Two held together that keep their game actions are kept from the game");
        check(overlay::detail::together_hides_press(together, 0x8000, 0x0200) && overlay::detail::together_hides_press(together, 0x0200, 0x8000) &&
              !overlay::detail::together_hides_press(together, 0x8000, 0) && !overlay::detail::together_hides_press(together, 0x1000, 0x8200),
              "A keystroke of the second one is not kept from the game, or one of another button is");
        put_binding(pairs, {"Street", 1, true, 1});
        check(binding_of(pairs, "Street")->also == 0 && modifier_mask(*binding_of(pairs, "Street")) == 0x0200, "A button is its own second button");
        put_binding(pairs, {"Street", 1, true, 99});
        check(binding_of(pairs, "Street")->also == 0, "A second button that is none was kept");
        put_binding(pairs, {"Street", 0, true, 2});
        check(!binding_of(pairs, "Street"), "A second button alone kept a playlist on a button");
        std::vector<NativeLoopCandidate> loops{{0, "manual", false, false, 0, 0.01f}, {1, "manual", true, false, 0, 0.01f},
            {2, "manual", true, false, 0, 0.01f}, {3, "another grab", true, false, -1, 0.001f}};
        check(native_loop_candidates(loops, false, true) == std::vector<std::size_t>{0}, "Without a modifier only the normal native loop is eligible");
        check(native_loop_candidates(loops, true, true) == std::vector<std::size_t>({1, 2}), "Equivalent modifier saves remain available for playlist cycling");
        loops[1].rank = loops[2].rank = -1;
        check(native_loop_candidates(loops, true, true) == std::vector<std::size_t>{0}, "An unrelated grind/manual/grab never overrides a compatible normal loop");
        loops[1].rank = 0; loops[1].mirrored = true;
        check(native_loop_candidates(loops, true, false) == std::vector<std::size_t>{0}, "An ineligible modifier stance falls back to the normal loop");
        check(native_loop_candidates(loops, true, true) == std::vector<std::size_t>{1}, "Front-foot mode permits a mirrored modifier loop");
        loops[2].rank = 0; loops[2].mirrored = false;
        check(native_loop_candidates(loops, true, true) == std::vector<std::size_t>{2},
              "A native switch version authored on the current foot wins over mirroring another modifier save");
        check(native_loop_candidates(loops, false, true) == std::vector<std::size_t>{0},
              "An exact native modifier stance never enters normal playback without the button");

        // Exercise the same route selector and pop/stance matcher as gameplay, with different eligible groups.
        std::vector<StanceCandidate> normal{{false, false, 0}}, special{{false, false, 0}, {false, false, 0}};
        const LiveStance live{false, false};
        bool front_foot = false;
        std::vector<bool> visited;
        const auto select = [&](bool modified) {
            visited.push_back(modified);
            return choose_stance_variants(modified ? special : normal, live, front_foot);
        };
        check(choose_trick_route(false, select).size() == 1 && visited == std::vector<bool>{false},
              "A normal trick never enters the modifier playlist");
        visited.clear();
        check(choose_trick_route(true, select).size() == 2 && visited == std::vector<bool>{true},
              "Eligible modifier variants replace the normal group");
        special = {{false, false, -1}};
        visited.clear();
        check(choose_trick_route(true, select).size() == 1 && visited == std::vector<bool>({true, false}),
              "A mismatched modifier pop falls back to normal");
        special = {{true, false, 0}};
        visited.clear();
        check(choose_trick_route(true, select).size() == 1 && visited == std::vector<bool>({true, false}),
              "An ineligible exact stance falls back to normal");
        front_foot = true;
        visited.clear();
        check(choose_trick_route(true, select).size() == 1 && visited == std::vector<bool>{true},
              "Front-foot mirroring remains available for modifier tricks");
        special.clear(); normal.clear();
        check(choose_trick_route(true, select).empty(), "No matching custom trick leaves the native animation available");

        // Steam may return cached buttons after an inner XInput hook ran. Filter the final game-facing sample.
        struct Pad { std::uint16_t wButtons; std::int16_t sThumbRX; std::uint8_t bLeftTrigger; };
        struct State { std::uint32_t dwPacketNumber; Pad Gamepad; };
        const State physical{7, {0x1200, 12000, 91}};
        const auto native_filter = [](State& output) { output.Gamepad.wButtons |= 0x0200; };
        auto output = physical;
        overlay::detail::finish_gamepad_read(output, true, false, 0x0200, native_filter);
        check(output.Gamepad.wButtons == 0x1000 && output.Gamepad.sThumbRX == 12000 && output.Gamepad.bLeftTrigger == 91,
              "The final sample removes R1 even if a preceding provider/filter supplies it again, retaining other input");
        check(output.dwPacketNumber != physical.dwPacketNumber, "Capture ownership is visible even on a cached packet");
        output = physical;
        overlay::detail::finish_gamepad_read(output, false, true, 0x0200, native_filter);
        check(output.Gamepad.wButtons == physical.Gamepad.wButtons && output.dwPacketNumber == physical.dwPacketNumber,
              "Studio raw reads bypass both the game filter and capture");
        output = physical;
        overlay::detail::finish_gamepad_read(output, true, false, 0, native_filter);
        check(output.Gamepad.wButtons == physical.Gamepad.wButtons && output.dwPacketNumber == physical.dwPacketNumber,
              "OFF, menus and released leases restore the native button and original packet");
        output = physical;
        overlay::detail::finish_gamepad_read(output, true, true, 0x0200, native_filter);
        check(!output.Gamepad.wButtons && !output.Gamepad.sThumbRX && !output.Gamepad.bLeftTrigger,
              "Full menu capture stays neutral and never runs the game filter");

        // Cover USB, full/simple Bluetooth and Windows-padded DS4 reports for every configurable button.
        struct Report { PlayStationPad kind; bool bt; std::uint8_t id; std::size_t size; };
        const std::array<Report, 7> reports{{
            {PlayStationPad::dualshock4, false, 1, 64}, {PlayStationPad::dualsense, false, 1, 64},
            {PlayStationPad::dualshock4, true, 0x11, 78}, {PlayStationPad::dualshock4, true, 0x11, 547},
            {PlayStationPad::dualsense, true, 0x31, 78},
            {PlayStationPad::dualshock4, true, 1, 78}, {PlayStationPad::dualsense, true, 1, 78}
        }};
        for (const auto [kind, bt, id, size] : reports) {
            for (const auto mask : modifier_buttons) {
                if (!mask) continue;
                std::array<std::uint8_t, 547> report{};
                report[0] = id;
                const unsigned offset = id == 0x31 ? 2 : id == 0x11 ? 3 : 1;
                const bool sense = kind == PlayStationPad::dualsense && (id == 0x31 || !bt);
                auto* data = report.data() + offset;
                data[0] = 180; data[1] = 30; data[2] = 90; data[3] = 220;
                data[sense ? 4 : 7] = 0x35;
                data[sense ? 5 : 8] = 0x67;
                data[sense ? 7 : 4] = 0xf2; // all face buttons, hat right
                data[sense ? 8 : 5] = 0xff;
                auto original = report;
                const auto before = parse_playstation_report(kind, bt, report.data(), size);
                check(before.has_value() && (before->buttons & mask), "The raw report contains the modifier button");
                check(hide_playstation_buttons(kind, bt, report.data(), size, mask), "Recognised report is selectively filtered");
                const auto after = parse_playstation_report(kind, bt, report.data(), size);
                check(after && after->buttons == (before->buttons & ~mask), "Only the modifier disappears from the normalised buttons");
                check(after->left_trigger == before->left_trigger && after->right_trigger == before->right_trigger &&
                      after->left_x == before->left_x && after->right_y == before->right_y, "Other analog controls stay intact");
                // Allow only the two button bytes and their dependent CRC to differ; every other byte must match.
                original[offset + (sense ? 7 : 4)] = report[offset + (sense ? 7 : 4)];
                original[offset + (sense ? 8 : 5)] = report[offset + (sense ? 8 : 5)];
                refresh_playstation_bluetooth_crc(original.data(), size);
                check(report == original, "Counters, sensors and padding are unchanged; Bluetooth CRC is refreshed");
                const auto filtered = report;
                check(!hide_playstation_buttons(kind, bt, report.data(), 2, mask) && report == filtered,
                      "Truncated reports are never written");
            }
        }
        std::cout << "Trick modifier selection and Sony input contracts passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
