#pragma once
#include <cstdint>

namespace dingosdk::overlay::detail {
// Finish at the outermost read, after Steam/remappers may have supplied a cached pad.
// The overlay's own raw reads bypass this, so a claimed modifier remains readable by Studio.
// Buttons held together that are kept from the game (`pairs`, two bits each; 0: none), in a read of the pad that has
// `buttons` down: both of a pair from the read that has both, and each of them until it is let go. Without that last
// part the one still held when the other was let go was a fresh press to the game. `kept`: the ones kept at the last
// read of this pad. One alone, not pressed with the other, stays the game's.
template <class Pairs>
std::uint16_t together_hidden(const Pairs& pairs, std::uint16_t buttons, std::uint16_t& kept) noexcept {
    std::uint16_t both{};
    for (const std::uint16_t pair : pairs)
        if (pair && (buttons & pair) == pair) both |= pair;
    kept = static_cast<std::uint16_t>((kept | both) & buttons);
    return kept;
}
// A press that arrives on its own (a keystroke), with the buttons a tick saw `held`: kept when the other one is down.
template <class Pairs>
bool together_hides_press(const Pairs& pairs, std::uint16_t button, std::uint16_t held) noexcept {
    for (const std::uint16_t pair : pairs)
        if ((pair & button) && ((held | button) & pair) == pair) return true;
    return false;
}
template <class State, class Filter>
void finish_gamepad_read(State& output, bool game_poll, bool capture, std::uint16_t hidden, Filter&& filter) noexcept {
    if (!game_poll) return;
    if (capture) {
        output.Gamepad = {};
        output.dwPacketNumber ^= 0x80000000u;
    } else {
        filter(output);
        if (hidden) {
            output.Gamepad.wButtons &= static_cast<std::uint16_t>(~hidden);
            // A change in ownership must be noticed even if the physical packet did not change.
            output.dwPacketNumber ^= 0x40000000u;
        }
    }
}
}
