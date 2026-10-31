#pragma once
#include <filesystem>

namespace dingosdk::anim_studio {
// The game's sound while the studio is open: every audio session of this process, on every playback device, is muted, and
// each is put back as it was when the studio closes. Asked every client tick; the audio calls run on a thread of their own.
// `marker` is a file kept while the sound is muted: found at a later start (the game ended with the studio open), the
// game's sessions are unmuted then.
void studio_sound(bool studio_open, const std::filesystem::path& marker);
} // namespace dingosdk::anim_studio
