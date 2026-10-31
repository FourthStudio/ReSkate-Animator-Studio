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
        std::vector<NativeLoopCandidate> loops{{0, "manual", false, false, 0, 0.01f}, {1, "manual", true, false, 0, 0.01f},
            {2, "manual", true, false, 0, 0.01f}, {3, "another grab", true, false, -1, 0.001f}};
        check(native_loop_candidates(loops, false, true) == std::vector<std::size_t>{0}, "Without a modifier only the normal native loop is eligible");
        check(native_loop_candidates(loops, true, true) == std::vector<std::size_t>({1, 2}), "Equivalent modifier saves remain available for playlist cycling");
        loops[1].rank = loops[2].rank = -1;
        check(native_loop_candidates(loops, true, true) == std::vector<std::size_t>{0}, "An unrelated grind/manual/grab never overrides a compatible normal loop");
        loops[1].rank = 0; loops[1].mirrored = true;
        check(native_loop_candidates(loops, true, false) == std::vector<std::size_t>{0}, "An ineligible modifier stance falls back to the normal loop");
        check(native_loop_candidates(loops, true, true) == std::vector<std::size_t>{1}, "Front-foot mode permits a mirrored modifier loop");

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
