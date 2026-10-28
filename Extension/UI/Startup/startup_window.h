#pragma once

#include <string>
#include <string_view>

// The window ReSkate opens the moment it loads, before the game has a window of
// its own: startup takes several seconds (longer when mods are merging) and the
// game shows nothing until its swapchain exists, which is also when the overlay
// can first draw. It closes by itself as soon as the game's window appears.
//
// The picture is ReSkate.Splash.png beside ReSkate.dll when present, otherwise
// the one built into the DLL from assets/startup_splash.png, otherwise a plain
// ReSkate card. Everything here returns at once: the window has a thread of its
// own.
namespace dingosdk::startup {

void open() noexcept;
// "Starting ReSkate + Animator Studio…" (the build's own name): the window's heading until mods merge,
// and again after.
std::string starting_heading();
// What the window says (UTF-8). `progress` runs 0..1; below 0 is unknown.
void status(std::string_view heading, std::string_view detail = {}, float progress = -1.0f) noexcept;
// Takes the window down now, e.g. when startup failed.
void close() noexcept;
// Whether the window made it onto the screen.
[[nodiscard]] bool shown() noexcept;

} // namespace dingosdk::startup
