#pragma once
#include <cstdint>
#include <optional>
#include <string>

// The game's own clock for the trick clip it plays (Float.Anim.TrickPhase): how far into the clip the local skater is,
// 0 to 1. Saved tricks follow it, so their keys land where the game's clip is at any pop height.
namespace dingosdk::anim_studio::trick_phase {
// Hooks the game's setter for the graph's float values, once. False with `why` when this game build's setter is not
// the one this was written for. Client thread.
bool install(std::uintptr_t base, std::string& why);

// The local skater's graph instance (its rig + trick_clip_clock::rig_to_graph_instance): only its writes count, so
// another skater's trick never moves this one's clock. Animation thread, each update.
void follow_instance(std::uintptr_t instance) noexcept;

// How far the game is into the trick clip it plays for that skater, 0 to 1; nothing while it plays none. Any thread.
[[nodiscard]] std::optional<float> progress() noexcept;
} // namespace dingosdk::anim_studio::trick_phase
