#include "trick_phase.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/20260929/game_state_values.h"
#include "Engine/Game/Build/addresses.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <bit>
#include <string_view>

namespace dingosdk::anim_studio::trick_phase {
namespace {
namespace values = addr::game_state_values;
// The setter's result, whatever it is, goes back to its callers untouched.
using SetFloat = std::uintptr_t (*)(std::uintptr_t setter, std::uintptr_t instance, float value);

std::atomic<SetFloat> original_set_float{};
std::atomic<std::uintptr_t> followed_instance{};
// The latest write, and the latest one inside the clip (0 to 1), each with its time (GetTickCount64).
std::atomic<std::uint32_t> latest_bits{};
std::atomic<std::uint64_t> latest_ms{};
std::atomic<std::uint32_t> inside_bits{};
std::atomic<std::uint64_t> inside_ms{};

// TrickPhase's descriptor is known by its address and its name pointer together, so an address the game frees and
// gives to another value does not answer for it, and a descriptor that moves is found again by its name.
std::atomic<std::uintptr_t> known_descriptor{}, known_name_at{};
// Descriptors (with their name pointers) already found not to be TrickPhase, so a name is read about once per float
// value the graph writes, not once per write.
std::array<std::atomic<std::uint64_t>, 1024> not_trick_phase{};

bool is_trick_phase(std::uintptr_t descriptor) noexcept {
    std::uintptr_t name_at{};
    if (!memory::peek(descriptor + values::descriptor_name, name_at) || !name_at) return false;
    if (descriptor == known_descriptor.load(std::memory_order_relaxed) && name_at == known_name_at.load(std::memory_order_relaxed)) return true;
    const auto identity = descriptor ^ (name_at * 0x9e3779b97f4a7c15ull);
    auto& slot = not_trick_phase[(descriptor >> 4) & (not_trick_phase.size() - 1)];
    if (slot.load(std::memory_order_relaxed) == identity) return false;
    std::array<char, sizeof(values::trick_phase_name)> name{};
    if (memory::peek_cstring(name_at, name.data(), name.size()) >= 0 && std::string_view(name.data()) == values::trick_phase_name) {
        known_descriptor.store(descriptor, std::memory_order_relaxed);
        known_name_at.store(name_at, std::memory_order_relaxed);
        return true;
    }
    slot.store(identity, std::memory_order_relaxed);
    return false;
}
std::uintptr_t set_float_hook(std::uintptr_t setter, std::uintptr_t instance, float value) {
    if (instance && instance == followed_instance.load(std::memory_order_relaxed) && is_trick_phase(setter - values::descriptor_to_setter)) {
        const auto now = GetTickCount64();
        latest_bits.store(std::bit_cast<std::uint32_t>(value), std::memory_order_relaxed);
        latest_ms.store(now, std::memory_order_release);
        if (value >= 0 && value <= 1) {
            inside_bits.store(std::bit_cast<std::uint32_t>(value), std::memory_order_relaxed);
            inside_ms.store(now, std::memory_order_release);
        }
    }
    return original_set_float.load(std::memory_order_acquire)(setter, instance, value);
}
// The empty string, or why the setter could not be hooked. The original is published before the hook is live, so the
// first call through it forwards at once.
std::string hook_setter(std::uintptr_t base) {
    std::array<unsigned char, 32> bytes{};
    if (!base) return "the game is not loaded";
    if (!memory::peek(base + values::float_setter.rva, bytes) || bytes != values::float_setter.bytes)
        return "the game's float setter is not the one this was written for";
    const auto target = reinterpret_cast<void*>(base + values::float_setter.rva);
    void* relay{};
    if (const auto status = hook_prepare(target, reinterpret_cast<void*>(&set_float_hook), &relay); status != HookOk)
        return std::string("the float setter could not be hooked: ") + hook_status_string(status);
    original_set_float.store(reinterpret_cast<SetFloat>(relay), std::memory_order_release);
    if (const auto status = hook_enable(target); status != HookOk) return std::string("the float setter could not be hooked: ") + hook_status_string(status);
    return {};
}
} // namespace

bool install(std::uintptr_t base, std::string& why) {
    // Tried once: the answer, and why not, stay for the rest of the game.
    static const std::string failure = hook_setter(base);
    why = failure;
    return failure.empty();
}
void follow_instance(std::uintptr_t instance) noexcept {
    if (followed_instance.exchange(instance, std::memory_order_relaxed) != instance) latest_ms.store(0, std::memory_order_release);
}
std::optional<float> progress() noexcept {
    // The game writes the phase every tick while a clip plays; one write of none can come in the middle of a clip
    // (seen once in a kickflip, between 0.288 and 0.373), so the clip is over only when none has stood a few ticks.
    constexpr std::uint64_t none_stands_ms = 80, silent_ms = 500;
    const auto now = GetTickCount64();
    const auto latest_at = latest_ms.load(std::memory_order_acquire), inside_at = inside_ms.load(std::memory_order_acquire);
    if (!latest_at || !inside_at || now - inside_at > silent_ms) return std::nullopt;
    const float latest = std::bit_cast<float>(latest_bits.load(std::memory_order_relaxed));
    if (latest >= 0 && latest <= 1) return latest;
    if (now - latest_at >= none_stands_ms) return std::nullopt;
    return std::bit_cast<float>(inside_bits.load(std::memory_order_relaxed));
}
} // namespace dingosdk::anim_studio::trick_phase
