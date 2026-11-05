#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace dingosdk {
using BeforeLevelTransition = void (*)(std::uintptr_t image_base, unsigned next) noexcept;
// Install before game entry, after the exact-build and hook-service checks.
// Failure leaves the runtime's coarse activity feed available.
// The callback runs on the client thread before the native transition handler,
// including shutdown (24), which starts releasing the world's asset domains.
bool start_level_loading_logging(std::uintptr_t image_base,
    BeforeLevelTransition before_transition = nullptr) noexcept;
// Call around the native client tick. Samples initial state and long waits;
// the transition hook also captures states entered and left within one tick.
void poll_level_loading(std::uintptr_t client) noexcept;
// The destination the most recent load named, empty when none has yet.
std::string last_level_destination() noexcept;
// The client reached an active level (in game), so its content loaded.
bool level_content_active() noexcept;
// How many level loads have started since the game did (a map change, a reload): a new number means a new level's assets.
std::uint64_t level_loads_started() noexcept;
// Words to append to a warning about `destination`: which mod a custom level
// came from, what built it and what the mod merge could not use from it.
// Empty for a level no mod registers.
std::string custom_level_hint(std::string_view destination) noexcept;
// The same for a corner notice: one or two short sentences naming the mod and
// whether the merge could use it. Empty for a level no mod registers.
std::string custom_level_notice(std::string_view destination) noexcept;
}
