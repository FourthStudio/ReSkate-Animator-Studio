#pragma once
#include <cstdint>

namespace dingosdk::overlay::detail {
// Finish at the outermost read, after Steam/remappers may have supplied a cached pad.
// The overlay's own raw reads bypass this, so a claimed modifier remains readable by Studio.
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
