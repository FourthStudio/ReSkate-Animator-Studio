#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace dingosdk::anim_studio {
// Local input preferences never travel in an animation pack. The author tag is only a hint.
struct ModifierBinding {
    std::string playlist;
    int button{};
    bool block_native{true};
    bool operator==(const ModifierBinding&) const = default;
};
inline constexpr std::array<const char*, 9> modifier_button_names{"OFF", "RB / R1", "LB / L1", "A / CROSS", "B / CIRCLE", "X / SQUARE", "Y / TRIANGLE", "L3", "R3"};
inline constexpr std::array<std::uint16_t, 9> modifier_buttons{0, 0x0200, 0x0100, 0x1000, 0x2000, 0x4000, 0x8000, 0x0040, 0x0080};
inline std::uint16_t modifier_mask(const ModifierBinding& b) noexcept {
    return b.button >= 0 && static_cast<std::size_t>(b.button) < modifier_buttons.size() && !b.playlist.empty()
        ? modifier_buttons[static_cast<std::size_t>(b.button)] : 0;
}
// Read once per episode, not on every recognised/refined trick. Releases cannot swap poses in mid-air.
struct ModifierAttempt {
    bool held{};
    void begin(bool down, bool ready, bool testing) noexcept { held = down && ready && !testing; }
};
// Native holds begin on entry to a grind/manual/grab, rather than at the preceding jump.
// A late native identity refines the same hold; releasing the button cannot change its route.
struct NativeModifierHold {
    int kind{}, identity{};
    bool held{};
    bool observe(int next_kind, int next_identity, bool down, bool ready, bool testing) noexcept {
        const bool began = kind != next_kind || (identity > 0 && next_identity > 0 && identity != next_identity);
        if (began) { held = down && ready && !testing; identity = 0; }
        kind = next_kind;
        if (next_identity > 0) identity = next_identity;
        if (!kind) { identity = 0; held = false; }
        return began;
    }
};
// Match the trick, entry, pop and stance before deciding whether the modifier can replace the normal route.
template <class Choose>
auto choose_trick_route(bool held, Choose&& choose) {
    if (held) {
        auto modified = choose(true);
        if (!modified.empty()) return modified;
    }
    return choose(false);
}
}
