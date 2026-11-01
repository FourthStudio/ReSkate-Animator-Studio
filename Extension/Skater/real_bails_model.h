#pragma once
#include "real_bails_math.h"
#include <array>
#include <cstdint>

// Real Bails' muscles (real_bails.h), as plain rules: given the ragdoll's 24 bodies in one physics step, the spin
// each body should have next. Testable without the game.
//
// A skater's fall has phases, and each has its reflexes:
//  - brace: from the moment the board is lost until the body hits. The hands go out towards where the body falls
//    (forwards, backwards to the side), the knees bend, the head protects itself (up when falling forwards, chin
//    tucked when falling backwards, away from the ground when falling sideways), the back rounds going backwards. In
//    the air for a while the legs reach down for the ground and the arms swing to stay upright.
//  - impact: the body gives for a moment (low tone).
//  - tumble: rolling fast: tucked, knees in, hands by the head, chin down.
//  - slide: moving along the ground: elbows in, legs held, the head held up off the ground.
//  - settle: almost still: the muscles let go, knees curl a little, and a hand goes to what hit hardest.
//  - rest: lying there; the getup is the game's.
// The muscles turn each joint towards its target with a speed-level servo, capped like real strength, and split each
// change between the two sides of the joint so the body as a whole keeps its spin.
namespace dingosdk::real_bails::model {
using math::M3;
using math::V3;

// The bodies in the game's physics order (Engine/Game/Skater/skater_body.h).
enum Part : std::uint8_t {
    board, head, neck, l_hand, l_forearm, l_arm, l_collar, r_hand, r_forearm, r_arm, r_collar, chest3, chest2, back1,
    spine, l_toe, l_foot, l_shin, l_thigh, r_toe, r_foot, r_shin, r_thigh, pelvis, part_count
};
constexpr std::array<Part, part_count> parent_of{
    board, neck, chest3, l_forearm, l_arm, l_collar, chest3, r_forearm, r_arm, r_collar, chest3, chest2, back1, spine,
    pelvis, l_foot, l_shin, l_thigh, pelvis, r_foot, r_shin, r_thigh, pelvis, pelvis};

struct BodyIn {
    bool ok{};
    V3 p{};       // where it is (its joint with its parent)
    M3 r{};       // its axes in the world (columns)
    V3 v{}, w{};  // velocity, spin
    bool touching{};
    float impact{}; // hardest contact this step, m/s along its normal
    bool has_point{}; V3 point{}; // where that contact was, when known
};
// What the player can tune (the Animator Studio's SETTINGS > BAILS).
struct Tuning {
    float reaction{1.0f};  // 0.5 .. 2: how fast the arms and the catch step go out
    float head{1.0f};      // 0 .. 1.5: how hard the neck holds the head up off the ground
    float tuck{1.0f};      // 0 .. 1.5: how tight he tucks rolling
    float lie_still{1.6f}; // 0.5 .. 4 seconds lying still before he lets go
    float grip{1.0f};      // 0 .. 2: how quickly the ground stops a slide (how heavy he is on it)
    bool catch_arms{true}, catch_step{true}, grab_hurt{true};
    float hold_hurt{4.5f}; // 1 .. 10 seconds holding what hurts (a chin, a knee, an elbow) once he is down
};
struct Input {
    std::array<BodyIn, part_count> body{};
    float dt{};
    float strength{1.0f};
    Tuning tune{};
};
enum class Phase : std::uint8_t { idle, brace, impact, tumble, slide, settle, rest };
const char* name(Phase phase) noexcept;

struct Output {
    std::array<V3, part_count> w{}, v{}; // spin, and the velocity that goes with turning about the joint
    std::array<bool, part_count> write{};
    Phase phase{};
    float tone{};
    int step{-1};          // the leg taking a catch step (0 left, 1 right), -1 none
    bool step_landed{};
};

class Muscles {
public:
    // A new bail: the body's own pose now is what the wrists, ankles and collars hold. `seed` varies the bail (how
    // tight the tuck, which leg bends more), so no two look the same.
    void begin(const Input& in, std::uint32_t seed = 0) noexcept;
    // One physics step of the bail.
    Output step(const Input& in) noexcept;
    Phase phase() const noexcept { return phase_; }
    bool started() const noexcept { return started_; }

private:
    struct Frame { V3 up, right, forward; }; // the torso's
    bool frame(const Input& in, Frame& f) const noexcept;
    void enter(Phase next) noexcept;

    bool started_{};
    Phase phase_{Phase::idle};
    double t_{}, in_phase_{}, still_for_{}, untouched_for_{};
    float handed_{1};                       // +1: forward = up x right; -1: right x up (the world's handedness)
    std::array<M3, part_count> rest_{};     // each body's axes in its parent's at the start (wrists, ankles, collars)
    // The elbows' and knees' hinge axes in the upper arm's and thigh's own axes (left elbow, right elbow, left knee,
    // right knee), from the bent riding pose at the start: they bend only about these.
    std::array<V3, 4> hinge_{};
    V3 fall_{};                             // where the body falls, horizontal, unit (0 when unknown)
    float fall_forward_{}, fall_side_{};    // that in the torso's frame (+ forward, + to its right)
    Part hurt_{board};                      // what hit hardest (board = nothing yet)
    float hurt_impact_{};
    int hurt_hand_{};                       // which hand holds it: -1 left, +1 right, 2 both
    float severity_{0.5f};                  // 0.3 .. 1: how hard the fall is (speed, drop): small hits get small reactions
    float swing_{};                         // the arms' swing in a long fall, radians
    float ground_{};                        // the ground's height, from the contacts (and the feet at the start)
    bool ground_known_{};
    int step_leg_{-1};                      // the leg taking a catch step (0 left, 1 right), -1 none
    bool step_landed_{}, stepped_{};        // its foot is down; a step was taken this bail
    double step_down_at_{-1};               // when the step's foot came down
    std::array<double, 2> caught_at_{-1, -1}; // when each hand came down on the ground in the catch (-1 not yet)
    double held_{};                         // seconds the hurt has been held
    float tuck_{0.55f};                     // how far the knees come in when rolling
    std::array<float, 3> vary_{};           // -1..1: left/right tuck, each leg's bend at rest
};
}
