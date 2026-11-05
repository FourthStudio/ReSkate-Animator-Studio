#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

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
// Several playlists can play on buttons, each on a button of its own (2026-11-05: only one could): a playlist has one
// button at most and a button one playlist. In their order: when two of the buttons are held, the first one's plays.
using ModifierBindings = std::vector<ModifierBinding>;
// Every button a playlist plays on, and the ones of those that the game no longer gets while playing.
inline std::uint16_t modifier_masks(const ModifierBindings& all) noexcept {
    std::uint16_t out{};
    for (const auto& b : all) out |= modifier_mask(b);
    return out;
}
inline std::uint16_t modifier_blocks(const ModifierBindings& all) noexcept {
    std::uint16_t out{};
    for (const auto& b : all) if (b.block_native) out |= modifier_mask(b);
    return out;
}
// The button whose playlist a move takes, of the ones `held`: the first binding's that is held (0: none).
inline std::uint16_t held_route(const ModifierBindings& all, std::uint16_t held) noexcept {
    for (const auto& b : all)
        if (const auto mask = modifier_mask(b); mask && (held & mask) == mask) return mask;
    return 0;
}
// The playlist's binding, or none.
inline const ModifierBinding* binding_of(const ModifierBindings& all, const std::string& playlist) noexcept {
    const auto at = std::find_if(all.begin(), all.end(), [&](const ModifierBinding& b) { return b.playlist == playlist; });
    return at == all.end() ? nullptr : &*at;
}
// Puts `binding` on its playlist: the button it names leaves any other playlist; button 0 takes the playlist off its button.
inline void put_binding(ModifierBindings& all, ModifierBinding binding) {
    if (binding.playlist.empty()) return;
    if (binding.button < 0 || static_cast<std::size_t>(binding.button) >= modifier_buttons.size()) binding.button = 0;
    if (binding.button)
        for (auto& b : all)
            if (b.playlist != binding.playlist && b.button == binding.button) b.button = 0;
    const auto at = std::find_if(all.begin(), all.end(), [&](const ModifierBinding& b) { return b.playlist == binding.playlist; });
    if (at == all.end()) all.push_back(std::move(binding));
    else *at = std::move(binding);
    std::erase_if(all, [](const ModifierBinding& b) { return b.playlist.empty() || b.button <= 0; });
}
// Read once per episode, not on every recognised/refined trick. Releases cannot swap poses in mid-air.
// `route`: the button whose playlist it takes (held_route; 0 none).
struct ModifierAttempt {
    bool held{};
    std::uint16_t button{};
    void begin(std::uint16_t route, bool ready, bool testing) noexcept {
        held = route != 0 && ready && !testing;
        button = held ? route : 0;
    }
};
// Native holds begin on entry to a grind/manual/grab, rather than at the preceding jump.
// A late native identity refines the same hold; releasing the button cannot change its route.
struct NativeModifierHold {
    int kind{}, identity{};
    bool held{};
    std::uint16_t button{}; // the button whose playlist it takes
    bool observe(int next_kind, int next_identity, std::uint16_t route, bool ready, bool testing) noexcept {
        const bool began = kind != next_kind || (identity > 0 && next_identity > 0 && identity != next_identity);
        if (began) { held = route != 0 && ready && !testing; button = held ? route : 0; identity = 0; }
        kind = next_kind;
        if (next_identity > 0) identity = next_identity;
        if (!kind) { identity = 0; held = false; button = 0; }
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
