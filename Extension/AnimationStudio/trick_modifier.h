#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace dingosdk::anim_studio {
// Local input preferences never travel in an animation pack. The author tag is only a hint.
struct ModifierBinding {
    std::string playlist;
    int button{};
    bool block_native{true};
    // A second button, held together with the first (LB + RB); 0: the one button. Last, so {playlist, button, block} still reads.
    int also{};
    bool operator==(const ModifierBinding&) const = default;
};
inline constexpr std::array<const char*, 9> modifier_button_names{"OFF", "RB / R1", "LB / L1", "A / CROSS", "B / CIRCLE", "X / SQUARE", "Y / TRIANGLE", "L3", "R3"};
inline constexpr std::array<std::uint16_t, 9> modifier_buttons{0, 0x0200, 0x0100, 0x1000, 0x2000, 0x4000, 0x8000, 0x0040, 0x0080};
// One button's bit (0: OFF or not a button), and the bits of a button with a second one held together.
inline std::uint16_t modifier_button(int button) noexcept {
    return button >= 0 && static_cast<std::size_t>(button) < modifier_buttons.size() ? modifier_buttons[static_cast<std::size_t>(button)] : std::uint16_t{};
}
inline std::uint16_t modifier_buttons_mask(int button, int also) noexcept {
    const auto first = modifier_button(button);
    return first ? static_cast<std::uint16_t>(first | modifier_button(also)) : std::uint16_t{};
}
// A binding's buttons: one bit, or two for a playlist on two buttons held together. Without a first button there is none.
inline std::uint16_t modifier_mask(const ModifierBinding& b) noexcept {
    return b.playlist.empty() ? std::uint16_t{} : modifier_buttons_mask(b.button, b.also);
}
// Two buttons held together, rather than one.
inline bool modifier_together(std::uint16_t mask) noexcept { return std::popcount(mask) > 1; }
// The buttons as the pages say them: "RB / R1", "LB / L1 + RB / R1" for two held together, "" for none.
inline std::string modifier_words(int button, int also) {
    if (!modifier_button(button)) return {};
    std::string out = modifier_button_names[static_cast<std::size_t>(button)];
    if (modifier_together(modifier_buttons_mask(button, also))) out += std::string(" + ") + modifier_button_names[static_cast<std::size_t>(also)];
    return out;
}
inline std::string modifier_words(const ModifierBinding& b) { return modifier_words(b.button, b.also); }
// Several playlists can play on buttons, each on its own (2026-11-05: only one could): a playlist has one button, or two
// held together (2026-11-13: LB + RB, its own playlist beside LB's and RB's), and a button or pair has one playlist.
using ModifierBindings = std::vector<ModifierBinding>;
// Every button a playlist plays on.
inline std::uint16_t modifier_masks(const ModifierBindings& all) noexcept {
    std::uint16_t out{};
    for (const auto& b : all) out |= modifier_mask(b);
    return out;
}
// The buttons the game no longer gets while playing: a playlist's one button is kept from it throughout.
inline std::uint16_t modifier_kept(const ModifierBindings& all) noexcept {
    std::uint16_t out{};
    for (const auto& b : all)
        if (const auto mask = modifier_mask(b); b.block_native && !modifier_together(mask)) out |= mask;
    return out;
}
// Two held together are kept from the game only once both are down: each of them alone stays the game's (or another
// playlist's), at the cost that the first of the two pressed reaches the game until the second is down. Which read has
// both down is decided on the game's own read of the pad (overlay::detail::together_hidden), not from the buttons a
// tick saw held: decided a tick late, the second one's press had already reached the game (2026-11-16: with RB + Y
// taken over, Y still got the skater off the board).
inline std::vector<std::uint16_t> modifier_kept_together(const ModifierBindings& all) {
    std::vector<std::uint16_t> out;
    for (const auto& b : all)
        if (const auto mask = modifier_mask(b); b.block_native && modifier_together(mask)) out.push_back(mask);
    return out;
}
// The buttons whose playlist a move takes, of the ones `held` (0: none). Two held together win over either of them
// alone, so LB + RB plays its own playlist with LB's and RB's also set; otherwise the first binding in their order.
inline std::uint16_t held_route(const ModifierBindings& all, std::uint16_t held) noexcept {
    std::uint16_t best{};
    for (const auto& b : all)
        if (const auto mask = modifier_mask(b); mask && (held & mask) == mask && std::popcount(mask) > std::popcount(best)) best = mask;
    return best;
}
// The playlist's binding, or none.
inline const ModifierBinding* binding_of(const ModifierBindings& all, const std::string& playlist) noexcept {
    const auto at = std::find_if(all.begin(), all.end(), [&](const ModifierBinding& b) { return b.playlist == playlist; });
    return at == all.end() ? nullptr : &*at;
}
// Puts `binding` on its playlist: the button or pair it names leaves any other playlist (RB + LB is LB + RB); button 0
// takes the playlist off its buttons. A second button that is no button, or the first again, is no second button.
inline void put_binding(ModifierBindings& all, ModifierBinding binding) {
    if (binding.playlist.empty()) return;
    if (!modifier_button(binding.button)) binding.button = 0;
    if (!binding.button || !modifier_button(binding.also) || binding.also == binding.button) binding.also = 0;
    if (binding.button)
        for (auto& b : all)
            if (b.playlist != binding.playlist && modifier_mask(b) == modifier_mask(binding)) b.button = b.also = 0;
    const auto at = std::find_if(all.begin(), all.end(), [&](const ModifierBinding& b) { return b.playlist == binding.playlist; });
    if (at == all.end()) all.push_back(std::move(binding));
    else *at = std::move(binding);
    std::erase_if(all, [](const ModifierBinding& b) { return b.playlist.empty() || b.button <= 0; });
}
// Read once per episode, not on every recognised/refined trick. Releases cannot swap poses in mid-air.
// `route`: the button (or the two held together) whose playlist it takes (held_route; 0 none).
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
