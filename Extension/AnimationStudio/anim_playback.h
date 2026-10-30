#pragma once
#include "anim_runtime_rules.h"
#include "anim_core.h"
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace dingosdk::anim_studio {
// These are observed physics states, not decoded native animation-graph phases.
enum class MotionState { unknown, riding, air, rail, offboard };
// Supported native physics selector values, shared by runtime and replay tests.
// 103 is the crouch/pop preparation observed between 100 and 200. It remains
// grounded; treating it as unavailable discards the entry before every normal pop.
inline MotionState motion_state(std::optional<std::uint32_t> state) noexcept {
    if (!state) return MotionState::unknown;
    // 701 is a grind as well (launches.h other_grind_state: the game's own checks take it with the 400s). Taken for off the
    // board, a trick into it was cut in one frame and the board snapped.
    if (*state == 701) return MotionState::rail;
    if (*state == 300 || *state >= 500) return MotionState::offboard;
    if (*state >= 400 && *state < 500) return MotionState::rail;
    if (*state == 200 || *state == 201) return MotionState::air;
    if (*state == 100 || *state == 101 || *state == 103) return MotionState::riding;
    return MotionState::unknown;
}
enum class EntryKind { unknown, ground, rail };
enum class PlaybackPhase { idle, air, landing, rail_release };
inline const char* entry_name(EntryKind value) noexcept {
    return value == EntryKind::ground ? "ground" : value == EntryKind::rail ? "rail" : "unknown";
}
inline const char* phase_name(PlaybackPhase value) noexcept {
    return value == PlaybackPhase::air ? "air" : value == PlaybackPhase::landing ? "landing" :
           value == PlaybackPhase::rail_release ? "rail_release" : "idle";
}
struct PlaybackSample { float time{-1}, weight{}; bool finished{}; };
// A trick fitted to the game's air, never slower than it was recorded. Seconds into the recording's air for
// `in_air` seconds into the game's, when the game's air is expected to last `target` seconds (the recorded one
// lasted `recorded_air`). A shorter expected air plays it a little faster (at most 1.3 times: the expectation
// is often wrong both ways). A longer one is not slowed down to fit (a tester's varial kickflip: 0.71 s
// expected, 0.46 s real, and a trick slowed for it was still in the air when the board landed). It never
// reaches its landing while the game is still in the air, and it never stands still waiting for it either
// (a low-pop shuv-it on a bigger pop stood frozen for six frames): over its last `ease` seconds it slows,
// feet reaching for the ground, closer and closer to the landing however long the air runs.
inline float fitted_air_time(float in_air, float recorded_air, float target) noexcept {
    if (!std::isfinite(in_air)) return 0;
    in_air = std::max(0.0f, in_air);
    if (!(recorded_air > 1.0f / 30) || !std::isfinite(recorded_air)) return in_air;
    float t = in_air;
    if (target > 0.05f && std::isfinite(target) && target < recorded_air) t = in_air * std::min(recorded_air / target, 1.3f);
    const float land = recorded_air - 1.0f / 60, ease = std::min(0.06f, land * 0.3f), knee = land - ease;
    if (t > knee) t = knee + ease * (1 - std::exp(-(t - knee) / ease));
    return t;
}
// After the game's touchdown, `since` seconds ago: the recording from its own landing (`down`) on, at its own
// pace, so the landing bend happens on the ground. It was at `from` at touchdown; if that was short of its
// landing it catches up rather than jumping, over a time that grows with how far behind it was (80 to 200 ms),
// so it never runs at more than about 2.25 times its pace.
inline float landed_time(float since, float down, float from) noexcept {
    since = std::isfinite(since) ? std::max(0.0f, since) : 0.0f;
    const float behind = std::isfinite(from) ? std::max(0.0f, down - from) : 0.0f;
    const float window = std::clamp(behind * 0.8f, 0.08f, 0.2f);
    const float u = std::clamp(since / window, 0.0f, 1.0f);
    return down + since - behind * (1 - u);
}
struct PlaybackEpisode {
    MotionState previous{MotionState::unknown};
    PlaybackPhase phase{PlaybackPhase::idle};
    EntryKind entry{EntryKind::unknown};
    std::uint64_t id{};
    double takeoff{}, landing{-1}, release_at{}, selected_at{-1}, last_observed{-1};
    float release_weight{}, last_weight{};
    float last_time{-1};   // the recording's time last sampled
    float landed_from{-1}; // where the recording was when the game touched down (-1 while in the air)
    ContactWindow contact;
    GameplayClipClock clock;
    void reset() noexcept { const auto next_id = id; *this = {}; id = next_id; }
    // True only when a new airborne episode begins. A rail exit cannot inherit the entry trick.
    bool observe(double now, MotionState motion) noexcept {
        if (!std::isfinite(now) || (last_observed >= 0 && now < last_observed)) { reset(); return false; }
        last_observed = now;
        if (motion == MotionState::unknown || motion == MotionState::offboard) {
            reset(); previous = motion; return false;
        }
        bool started = false;
        if (motion == MotionState::air && phase != PlaybackPhase::air) {
            const auto from = previous;
            ++id; phase = PlaybackPhase::air; takeoff = now; landing = -1; selected_at = -1; landed_from = -1; last_time = -1;
            entry = from == MotionState::rail ? EntryKind::rail : from == MotionState::riding ? EntryKind::ground : EntryKind::unknown;
            clock.reset(); contact.since.reset(); last_weight = 0; started = true;
        } else if (motion == MotionState::rail && (phase == PlaybackPhase::air || phase == PlaybackPhase::landing)) {
            phase = selected_at >= 0 && clock.time >= 0 ? PlaybackPhase::rail_release : PlaybackPhase::idle;
            release_at = now; release_weight = last_weight; contact.since.reset();
        } else if (motion == MotionState::riding && phase == PlaybackPhase::air && contact.observe(now, true)) {
            phase = selected_at >= 0 ? PlaybackPhase::landing : PlaybackPhase::idle; landing = *contact.since;
        }
        if (motion == MotionState::air) contact.since.reset();
        previous = motion;
        return started;
    }
    bool selection_open(double now) const noexcept { return phase == PlaybackPhase::air && now >= takeoff && now - takeoff < 0.35; }
    void select(double now) noexcept { if (std::isfinite(now)) { selected_at = now; clock.reset(); } }
    // `fit_air`: the trick is fitted to the air actually happening (fitted_air_time, landed_time) instead of
    // running at the seconds it was recorded at. The studio always plays with it on.
    PlaybackSample sample(double now, float up, float down, float duration, float speed, float predicted_air,
                          bool source_timing = false, bool fit_air = false) noexcept {
        PlaybackSample out;
        if (selected_at < 0 || up < 0 || duration <= 0 || up > duration || down > duration ||
            !std::isfinite(now) || !std::isfinite(up) || !std::isfinite(down) ||
            !std::isfinite(duration) || !std::isfinite(speed) || speed <= 0 || !std::isfinite(predicted_air)) return out;
        if (source_timing && (phase == PlaybackPhase::air || phase == PlaybackPhase::landing)) {
            // Recognition can arrive late or refine ollie into a flip. Anchor the
            // source clock at takeoff, never at selection, and keep source seconds
            // through reception instead of stretching air and discarding late keys.
            // Speed is a studio preview control. Slowing only this additive
            // layer while the native trick runs at 1x desynchronizes limbs and
            // board; source-bound gameplay keys always follow source seconds.
            // Fitted to the air (fitted_air_time, landed_time): takeoff on the game's takeoff, the flip at its
            // own pace, the recording's landing on the game's touchdown.
            float time = up + static_cast<float>(std::max(0.0, now - takeoff));
            if (fit_air && down > up) {
                if (phase == PlaybackPhase::air && contact.since) {
                    // The board is down and the game is making sure (ContactWindow): the landing starts at
                    // the touch, not frames later with a jump - the pose in the air with the board on the
                    // ground was it going into the ground.
                    if (landed_from < 0) landed_from = std::clamp(last_time >= 0 ? last_time : down, up, down);
                    time = landed_time(static_cast<float>(now - *contact.since), down, landed_from);
                } else if (phase == PlaybackPhase::air) {
                    time = up + fitted_air_time(static_cast<float>(now - takeoff), down - up, predicted_air);
                    if (last_time >= 0) time = std::max(time, last_time); // a touch that was a bounce never turns it back
                    landed_from = -1;
                } else {
                    if (landed_from < 0) landed_from = std::clamp(last_time >= 0 ? last_time : down, up, down);
                    time = landed_time(static_cast<float>(now - landing), down, landed_from);
                }
            }
            out.time = std::min(duration, time);
            clock.observe_air(now, out.time, 1);
            const auto enter = std::clamp(static_cast<float>(now - selected_at) / 0.08f, 0.0f, 1.0f);
            const auto leave = std::clamp((duration - out.time) / 0.08f, 0.0f, 1.0f);
            out.weight = enter * leave;
            out.finished = out.time >= duration;
        } else if (phase == PlaybackPhase::air) {
            float rate = speed;
            if (predicted_air > 0 && down > up) rate *= (down - up) / predicted_air;
            out.time = clock.advance_air(now, up, rate, down > up ? down : duration);
            out.weight = std::clamp(static_cast<float>(now - selected_at) / 0.08f, 0.0f, 1.0f);
        } else if (phase == PlaybackPhase::landing && down >= 0) {
            const auto after = static_cast<float>(now - landing) * speed;
            const auto span = std::max(0.05f, std::min(0.6f, duration - down));
            const auto u = std::clamp(after / span, 0.0f, 1.0f);
            out.time = clock.land(now, speed, duration, down + after);
            out.weight = 1 - u*u*(3 - 2*u);
        } else if (phase == PlaybackPhase::rail_release) {
            // Entry is a release to the native constrained rail pose, never a fake airborne hold.
            const auto u = std::clamp(static_cast<float>(now - release_at) / 0.08f, 0.0f, 1.0f);
            out.time = clock.time; out.weight = release_weight * (1 - u*u*(3 - 2*u));
        }
        last_weight = out.weight;
        if (out.time >= 0) last_time = out.time;
        if (phase != PlaybackPhase::air && out.weight <= 0) { phase = PlaybackPhase::idle; out.finished = true; }
        return out;
    }
};
// Body/board share one edit schedule per presentation cohort. This token is not a native
// evaluation ID; native poses are read fresh at each publication and never replaced wholesale.
template<class Packet> struct PresentationLatch {
    std::shared_ptr<const Packet> pinned;
    std::uint64_t presentation{~std::uint64_t{0}}, epoch{~std::uint64_t{0}};
    std::shared_ptr<const Packet> get(std::uint64_t frame, std::uint64_t version, std::shared_ptr<const Packet> latest) {
        if (frame != presentation || version != epoch) { presentation = frame; epoch = version; pinned = std::move(latest); }
        return pinned;
    }
};
struct EditPublicationCache {
    std::uint64_t generation{~std::uint64_t{0}}, animation_update{};
    std::uintptr_t buffer{};
    std::vector<Joint> before, after;
    // Multiple publications of one native animation update must not accumulate our own edits.
    // A changed native value is always preserved; only an exact previous write is unwrapped.
    void restore(std::uint64_t gen, std::uint64_t update, std::uintptr_t address, std::span<Joint> pose) const {
        if (gen != generation || update != animation_update || address != buffer || pose.size() != after.size() || pose.size() != before.size()) return;
        for (std::size_t i = 0; i < pose.size(); ++i) {
            if (before[i].rotation != after[i].rotation && pose[i].rotation == after[i].rotation) pose[i].rotation = before[i].rotation;
            for (std::size_t k = 0; k < 3; ++k)
                if (before[i].position[k] != after[i].position[k] && pose[i].position[k] == after[i].position[k])
                    pose[i].position[k] = before[i].position[k];
        }
    }
    void commit(std::uint64_t gen, std::uint64_t update, std::uintptr_t address,
                std::vector<Joint> native, std::vector<Joint> edited) {
        generation = gen; animation_update = update; buffer = address;
        before = std::move(native); after = std::move(edited);
    }
};
// Smooth only edit deltas when selection changes. Absolute skeleton/board coordinates
// are never retained: the same delta can be composed over a moving fresh native pose.
struct EditTransition {
    std::uint64_t generation{~std::uint64_t{0}}, episode{};
    std::uintptr_t source{};
    double started{-1}, observed{-1};
    std::vector<Offset> from, current;
    float from_left{}; // how much of `from` is still in `current`: 1 as a fade starts, 0 when it is done
    float length{-1};  // a fade of its own length (a jump eased, jump_fade), -1 the caller's `seconds`
    bool restarted{};  // the last evaluate began a fade into another animation (or none): PoseFade starts with it
    static constexpr float jump_fade = 0.1f;
    void reset() { *this = {}; }
    // Any joint whose wanted turn is more than `degrees` from the one drawn, or moved more than 25 cm.
    static bool jumps(std::span<const Offset> drawn, std::span<const Offset> wanted, float degrees) noexcept {
        const float most = std::cos(degrees * 0.5f * 3.14159265f / 180);
        for (std::size_t i = 0; i < drawn.size() && i < wanted.size(); ++i) {
            const auto& a = drawn[i].rotation; const auto& b = wanted[i].rotation;
            if (std::abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]) < most) return true;
            float d = 0;
            for (std::size_t k = 0; k < 3; ++k) d += (drawn[i].offset[k] - wanted[i].offset[k]) * (drawn[i].offset[k] - wanted[i].offset[k]);
            if (d > 0.25f * 0.25f) return true;
        }
        return false;
    }
    // `jump_degrees` (0 off): a jump inside one trick - its weight cut at its end, a clip that turns its board round in a
    // frame, frames the game did not draw - eases over jump_fade instead of showing in one frame (48 to 174 degrees in one
    // update were measured, 2026-10-29).
    std::span<const Offset> evaluate(double now, std::uint64_t gen, std::uint64_t attempt,
                                     std::uintptr_t selected, std::span<const Offset> desired, float seconds = 0.15f,
                                     float jump_degrees = 0) {
        if (!std::isfinite(now)) { reset(); return current; }
        if (generation != gen || current.size() != desired.size() || now < observed) {
            generation = gen; current.assign(desired.size(), Offset{}); started = -1;
        }
        restarted = started < 0 || episode != attempt || source != selected;
        if (restarted) {
            from = current; started = now; episode = attempt; source = selected; length = -1;
        } else if (jump_degrees > 0 && from_left <= 0 && jumps(current, desired, jump_degrees)) {
            from = current; started = now; length = jump_fade;
        }
        observed = now;
        // `seconds` (0.15 unless the Crossfade setting says): one trick's keys into the next, or out when a trick is
        // cut short (a new air, a bail), without a snap.
        const float fade = length > 0 ? length : seconds;
        float u = std::clamp(static_cast<float>(now - started) / std::max(fade, 0.02f), 0.0f, 1.0f);
        u = u*u*(3 - 2*u);
        from_left = 1 - u;
        for (std::size_t i = 0; i < desired.size(); ++i) {
            current[i].rotation = u >= 1 ? desired[i].rotation : slerp(from[i].rotation, desired[i].rotation, u);
            for (std::size_t k = 0; k < 3; ++k)
                current[i].offset[k] = u >= 1 ? desired[i].offset[k] : from[i].offset[k] + (desired[i].offset[k] - from[i].offset[k]) * u;
        }
        return current;
    }
};
// ---- one animation into the next: a crossfade in the pose --------------------------------------------------------------
// From one animation into the next - a grind or a manual of yours into a flip of yours, a trick into the game's own next
// move - the pose drawn last is held as it was and blended into the next one's pose (its keys on the game's pose now, or
// the game's own pose when nothing of yours comes next): each joint straight from where it was drawn to where it goes.
// Blended as the changes each one makes to the game's pose instead, the last one's changes were laid on the game's pose of
// another move while it faded: a grind's bent knees on the pop out of it, the legs and feet thrown about. Only the joints
// either animation changes fade; every other one is the game's as it goes.
struct PoseFade {
    std::vector<Joint> from;            // the pose drawn last, as it was when the fade began
    std::vector<unsigned char> joints;  // 1: this joint fades
    double started{-1};
    float length{};
    void reset() noexcept {
        from.clear();
        joints.clear();
        started = -1;
    }
    bool on(double now) const noexcept { return started >= 0 && now >= started && now - started < length; }
    static bool differs(const Joint& a, const Joint& b) noexcept {
        const float dot = std::abs(a.rotation[0] * b.rotation[0] + a.rotation[1] * b.rotation[1] + a.rotation[2] * b.rotation[2] +
                                   a.rotation[3] * b.rotation[3]);
        float moved = 0;
        for (std::size_t k = 0; k < 3; ++k) moved += (a.position[k] - b.position[k]) * (a.position[k] - b.position[k]);
        return dot < 0.9999995f || moved > 0.0005f * 0.0005f; // more than about a tenth of a degree, or half a millimetre
    }
    static bool changes(const Offset& o) noexcept {
        return !(o.rotation == Quat{0, 0, 0, 1}) || o.offset[0] * o.offset[0] + o.offset[1] * o.offset[1] + o.offset[2] * o.offset[2] > 1e-8f;
    }
    // From `drawn` (the pose drawn last) into the animation whose changes to the game's pose now are `next`, over `seconds`.
    void start(double now, float seconds, std::span<const Joint> drawn, std::span<const Joint> native, std::span<const Offset> next) {
        from.assign(drawn.begin(), drawn.end());
        joints.assign(from.size(), 0);
        for (const auto& joint : editable_joints()) {
            const auto i = joint.index;
            if (i >= from.size() || i >= native.size()) continue;
            joints[i] = differs(drawn[i], native[i]) || (i < next.size() && changes(next[i]));
        }
        started = now;
        length = std::max(seconds, 0.02f);
    }
    // The faded pose as changes to the game's pose now, into `out` (the fading joints only): `next` is the next animation's
    // changes to it (none: the game's own pose).
    void apply(double now, std::span<const Joint> native, std::span<const Offset> next, std::span<Offset> out) const noexcept {
        if (!on(now)) return;
        float u = std::clamp(static_cast<float>(now - started) / length, 0.0f, 1.0f);
        u = u * u * (3 - 2 * u);
        for (std::size_t i = 0; i < joints.size() && i < native.size() && i < out.size(); ++i) {
            if (!joints[i]) continue;
            const Offset change = i < next.size() ? next[i] : Offset{};
            const Quat target = normalised(multiply(native[i].rotation, change.rotation));
            const Quat drawn = slerp(from[i].rotation, target, u);
            out[i].rotation = normalised(multiply(conjugate(native[i].rotation), drawn));
            for (std::size_t k = 0; k < 3; ++k) {
                const float to = native[i].position[k] + change.offset[k];
                out[i].offset[k] = from[i].position[k] + (to - from[i].position[k]) * u - native[i].position[k];
            }
        }
    }
};
// On a rail the board's pass gives the board back to the game at once, but the body fades the trick out over the
// crossfade. The game drops the board into the grind, and the fade kept the legs as they were for the landing: the feet
// stood up to 35 cm off the board for 0.18 s (kickflip to BS crooked and BS noseslide, 2026-10-26). Cutting the fade
// snapped the whole body into the grind, and putting the feet on the game's feet at once made them jump 7-10 cm on the
// deck. So the body still fades, and each foot stays on the game's board: where it stood on the trick's board on the
// last frame before the lock, sliding to where the game has it as the fade goes. A saved grind loop that plays
// (`playing`) poses the feet itself.
inline bool feet_stay_on_the_games_board(MotionState motion, bool playing) noexcept { return motion == MotionState::rail && !playing; }
// A later fade can start at the same share (usually 1) as the lock's fade. Its start, not its share, identifies it.
inline bool rail_lock_fade_continues(bool playing, double started, double at_lock) noexcept {
    return !playing && started == at_lock;
}
// ---- SMOOTH transitions: the handover into a grind ----------------------------------------------------------------------
// How the game hands its own trick over (its animation data and its own grinds, read 2026-10-29): the board is the game's
// from the lock on, and the body goes over part by part, shrinking the difference from the pose it leaves rather than
// mixing two poses - the board and legs in 8 animation ticks (1/60 s each), the arms in 16, the hips and torso in 32
// (BlendBehaviour_TrickToInAir, TrickToGrind). Hips and arms here go in 15, the back, neck and head in 24.
inline constexpr float handover_legs = 8.0f / 60, handover_middle = 15.0f / 60, handover_torso = 24.0f / 60;
inline float handover_seconds(std::uint16_t joint) noexcept {
    switch (joint) {
    case 8: case 9: case 10: case 11: case 341: case 342: case 343: case 344: return handover_legs;
    case 42: case 43: case 44: case 45: case 101: case 102: case 103: return handover_torso;
    default: return handover_middle; // the hips, the shoulders and arms
    }
}
// How much of the trick it leaves joint `joint` still shows, `elapsed` seconds into the handover (1 to 0, eased).
inline float handover_left(std::uint16_t joint, float elapsed) noexcept {
    const float u = std::clamp(elapsed / handover_seconds(joint), 0.0f, 1.0f);
    return 1 - u * u * (3 - 2 * u);
}
// In the air before a grind the game turns the board to the rail's angle - 70 degrees in the last 0.1 to 0.13 s before the
// lock, at up to 14 degrees a frame - with the board caught and flat and the skater not turning with it (its own grinds,
// 2026-10-29). A trick of ours still holding the board then draws a board the game is not drawing: from the second frame
// the game's board turns faster than 5 degrees a frame under him (a flat landing stays near 1), after the trick's catch, the board and legs lean over to the game's
// (rail_lean) within rail_lean_seconds, so the lock finds them there.
inline constexpr float rail_turn_degrees_per_second = 300, rail_lean_seconds = 0.1f, board_jump_degrees = 35;
// A foot on the game's board (in the board's frame), `fade_left` of the way back to where it stood at the lock.
inline Joint foot_sliding_to_the_games(const Joint& at_the_lock, const Joint& games, float fade_left) noexcept {
    const float k = std::clamp(fade_left, 0.0f, 1.0f);
    Joint foot = games;
    foot.rotation = slerp(games.rotation, at_the_lock.rotation, k);
    for (std::size_t i = 0; i < 3; ++i) foot.position[i] = games.position[i] + (at_the_lock.position[i] - games.position[i]) * k;
    return foot;
}
// ---- the board's keys ---------------------------------------------------------------------------------------------------
// A trick's board keys turn the board about its own axes as it lay on the ground before the pop (the studio: start_frame).
// In game that is the heading the board had under him while he rolled, kept for a second (seconds, heading relative to
// the placement): the newest from at least 0.15 s before the takeoff - the pop turns the board from about 0.1 s before it,
// a 360 flip's by 55 degrees by the takeoff - and no older than 0.6 s. None when he was not rolling then.
inline std::optional<Quat> rest_heading_before(const std::deque<std::pair<double, Quat>>& headings, double takeoff) noexcept {
    for (auto it = headings.rbegin(); it != headings.rend(); ++it) {
        const double before = takeoff - it->first;
        if (before < 0.15) continue;
        if (before > 0.6) break;
        return it->second;
    }
    return std::nullopt;
}
// After the touchdown, off the game's clip: the game going into its next move (a push, a manual), or only landing? The
// game's own landing bends him away from the trick's clip at once (10 to 21 degrees in 0.07 s, then back to about 14: a
// tester's FS Pop Shuv-it, 2026-10-30). Measured from the least of it, nearly every landing was taken for the next move and
// the trick's own landing let go 0.17 s after the touchdown. The base is the most of it over the landing's first `settle`
// seconds; the next move is the pose going `margin` degrees further than that, two updates running.
struct LandingDrift {
    static constexpr double settle = 0.15;
    static constexpr float margin = 6.0f;
    double since{-1};
    float base{};
    int count{};
    void reset() noexcept { *this = {}; }
    bool next_move(double at, float error) noexcept {
        if (since < 0) {
            since = at;
            base = error;
            count = 0;
            return false;
        }
        if (at - since < settle) {
            base = std::max(base, error);
            return false;
        }
        count = error > base + margin ? count + 1 : 0;
        return count >= 2;
    }
};
// ---- the ground -------------------------------------------------------------------------------------------------------
// The board falls at 20 m/s2 in the game, not the Earth's 9.81: measured 2026-10-25 from the board body's speed and height
// through flat ollies (the speed falls 6 m/s in 0.28 s; from the top it drops 0.58 m in 0.23 s). With 9.81 the time to the
// ground came out twice as long while the board rose, so every trick with a catch was taken for one off a drop on flat
// ground too: its own clock, and the keyed joints and the feet as made in the studio rather than on the game's pose.
inline constexpr float board_gravity = 20.0f;
// Seconds until the board is down: from how fast it goes up now (metres a second, falling below 0) and how high its lowest
// point is over the ground. Below 0: not known.
// `gravity`: the board's gravity now (board_gravity times the trainer's gravity: at twice the gravity the air is shorter, and
// at the game's own the catch was held waiting for a landing that had already come, 2026-10-29).
inline float time_to_ground(float up_speed, float height, float gravity = board_gravity) noexcept {
    if (!std::isfinite(up_speed) || !std::isfinite(height) || !(gravity > 0)) return -1;
    const float h = std::max(0.0f, height);
    return (up_speed + std::sqrt(up_speed * up_speed + 2 * gravity * h)) / gravity;
}
// Seconds until the board comes down where it is flying to, not straight below it: over a gap the ground under the
// board is the gap's bottom, and the trick was taken for one off a drop - its catch slowed and its feet held on the deck
// the whole long way down - though the board lands on the far side moments later (22sh's flips over the gap, 2026-10-25).
// The board's lowest point is `low` over where it is now, flying at (vx, vy, vz) and falling at board_gravity; `ground_at(dx,
// dz, top)` is the ground's height under that point moved (dx, dz), the topmost below `top` (none: nothing there). Samples
// along the path every `step` seconds; past the last, straight down from there. Below 0: not known. `surface`: the height of
// what it lands on (a rail or a ledge stands over the ground it took off from).
// `edge`: the highest step up in the ground on the way there - a rail's side, a ledge's - where a slope (a kicker, a bank)
// rises little by little. A rise between two samples is halved five times towards where it rises most: a step keeps nearly
// all of it in the last 32nd of the way, a slope a 32nd of it.
inline constexpr float edge_looked_at = 0.04f; // a rise between two samples worth halving (metres)
template <class GroundAt>
float time_to_landing(float low, float vx, float vy, float vz, GroundAt&& ground_at, float step = 0.05f, int samples = 16,
                      float* surface = nullptr, float gravity = board_gravity, float* edge = nullptr) {
    if (edge) *edge = 0;
    if (!std::isfinite(low) || !std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return -1;
    const auto path = [&](float t) { return low + vy * t - 0.5f * gravity * t * t; };
    const auto step_between = [&](float ta, float ga, float tb, float gb) {
        for (int i = 0; i < 5; ++i) {
            const float tm = 0.5f * (ta + tb);
            const std::optional<float> gm = ground_at(vx * tm, vz * tm, path(tm) + 0.6f);
            if (!gm || !std::isfinite(*gm)) break;
            if (*gm - ga >= gb - *gm) { tb = tm; gb = *gm; } else { ta = tm; ga = *gm; }
        }
        return gb - ga;
    };
    float last_t = -1, last_gap = 0, last_ground = 0;
    for (int k = 0; k <= samples; ++k) {
        const float t = static_cast<float>(k) * step;
        const float y = path(t);
        // Now: from just under the board. Ahead: from a little over its path, so ground it would meet is found.
        const std::optional<float> ground = ground_at(vx * t, vz * t, k == 0 ? low - 0.03f : y + 0.6f);
        if (!ground || !std::isfinite(*ground)) continue;
        if (edge && last_t >= 0 && *ground - last_ground > edge_looked_at)
            *edge = std::max(*edge, step_between(last_t, last_ground, t, *ground));
        const float gap = y - *ground;
        if (gap <= 0) {
            if (surface) *surface = *ground;
            if (last_t < 0) return 0;
            return last_t + (t - last_t) * last_gap / (last_gap - gap);
        }
        last_t = t;
        last_gap = gap;
        last_ground = *ground;
    }
    if (last_t < 0) return -1;
    if (surface) *surface = last_ground;
    const float down = time_to_ground(vy - gravity * last_t, last_gap, gravity);
    return down < 0 ? -1 : last_t + down;
}
// The skater and his board are bodies in the world the ground is looked for in: a ray from over the board's path, within
// reach of where the board is now (self_reach), hit them, and a mid tre flip on flat ground was taken for one landing on a
// rail - its legs held in the game's catch to the ground, then a snap (1.0.10, 2026-10-27). A hit there higher than the
// board's own lowest point (`low`) is him: the ground is looked for again from just under the board. A rail or a ledge the
// board comes down on is under it until it lands.
inline constexpr float self_reach = 0.8f;
template <class GroundAt>
std::optional<float> ground_not_self(GroundAt&& ground_at, float dx, float dz, float top, float low) {
    const std::optional<float> hit = ground_at(dx, dz, top);
    if (hit && std::isfinite(low) && dx * dx + dz * dz < self_reach * self_reach && *hit > low - 0.02f && top > low - 0.02f)
        return ground_at(dx, dz, low - 0.02f);
    return hit;
}
// The lowest a foot's ankle is drawn: its height over the deck's mount, along the deck's up (on the deck the ankle stands
// 10.4 cm over it; the ground is 8.7 cm under it). `over`: within the deck's outline. Below -infinity: no limit.
// In the air, the deck upright under him: an ankle over the deck less than 9.5 cm over it is lifted to there, unless it is more
// than 8 cm into it (that is a foot past the board, not on it). Rolling on the ground: over the deck, never into it however
// deep the keys put it; off it, never under the ground (a leg keyed out over the edge took its foot into the ground, and a
// hips key 20 cm down took both feet through the deck, while it rolled away from a landing).
inline constexpr float ankle_on_deck = 0.095f, ankle_on_ground = 0.015f;
inline float lowest_ankle(bool rolling, bool over, float height) noexcept {
    if (rolling) return over ? ankle_on_deck : ankle_on_ground;
    return over && height >= -0.08f ? ankle_on_deck : -std::numeric_limits<float>::infinity();
}
} // namespace dingosdk::anim_studio
