#pragma once
#include <optional>

namespace dingosdk::anim_studio {
// TEST IN GAME may enable playback temporarily; unrelated settings saves keep the user's preference.
struct TemporaryPlayback {
    std::optional<bool> previous;
    void begin(bool& enabled) {
        if (!previous) previous = enabled;
        enabled = true;
    }
    bool saved(bool enabled) const { return previous.value_or(enabled); }
    void finish(bool& enabled) {
        if (previous) enabled = *previous;
        previous.reset();
    }
    void choose(bool& enabled, bool value) { previous.reset(); enabled = value; }
};
}
