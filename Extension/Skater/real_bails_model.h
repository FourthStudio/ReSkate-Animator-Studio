#pragma once
#include "real_bails_math.h"
#include <array>
#include <cstdint>

// Real Bails' muscles (real_bails.h), as plain rules: given the ragdoll's 24 bodies in one physics step, the spin and
// velocity each body should have next. Testable without the game.
//
// The bail is the game's ragdoll: a heavy body falling, rolling and sliding as gravity, momentum, its joints and the
// ground take it. The muscles add only what a falling person's add, on top:
//  - tone: every joint resists being turned fast (damping, as tense muscle does; never a pose it is pulled towards), so
//    the limbs do not flop like a rag, and less so once he is hit (stunned) and as he lies there;
//  - the joints' ranges (real_bails_limits.h): no joint goes past where a person's goes;
//  - one reflex: the arms. Going down forwards or to a side both hands come up and out towards where he will land, one a
//    moment after the other, elbows a little bent; going down backwards they go back and down a little. Once a hand is
//    down they give with the slam, and then they are as loose as the rest of him.
// Only once he lies still: a hand goes slowly to what hurts and, hurt, he curls a little (knees drawn up with his feet
// on the ground, back rounded), lightly; moving again stops it at once. Then he lies there.
//
// A muscle is inside the body: each torque turns one side of its joint against the other through the linked bodies
// (real_bails_body.h), so the body's momentum, linear and angular, stays exactly as the fall made it.
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
    V3 v{}, w{};  // velocity (of its centre of mass), spin
    bool touching{};
    float impact{}; // hardest contact this step, m/s along its normal
    bool has_point{}; V3 point{};   // where that contact was, when known
    bool has_normal{}; V3 normal{}; // and the surface's normal there, when known
};
// What the player can tune (the Animator Studio's SETTINGS > BAILS). The muscles read each one as:
//  - reaction: how quickly the arms come out (their reflex delay is divided by it, and they turn out faster);
//  - head: the neck's tone (how hard it resists the head being thrown; 0 lets it go);
//  - tuck: how far he curls while lying still and hurt (never during the bail);
//  - lie_still: seconds he takes to let go once he lies still;
//  - grip: no longer used: a slide is the ground's friction alone (kept so saved settings still load);
//  - catch_arms: the arms' reflex; catch_step: no longer used (no step: kept so saved settings still load);
//  - grab_hurt, hold_hurt: the hand to what hurts once he lies still, and for how many seconds.
struct Tuning {
    float reaction{1.0f};  // 0.5 .. 2
    float head{1.0f};      // 0 .. 1.5
    float tuck{1.0f};      // 0 .. 1.5
    float lie_still{1.6f}; // 0.5 .. 4 s
    float grip{1.0f};      // 0 .. 2 (unused)
    bool catch_arms{true}, catch_step{true}, grab_hurt{true};
    float hold_hurt{4.5f}; // 1 .. 10 s
};
struct Input {
    std::array<BodyIn, part_count> body{};
    float dt{};
    float strength{1.0f}; // 0.25 .. 2: every muscle's strength and tone
    Tuning tune{};
    // In the game, whose own joint solve takes most of each push back (real_bails_model.cpp, game_gain). Off: the
    // muscles as a person's are, for tests and the offline bench, where every push stays.
    bool in_game{};
};
// What the muscles are doing. Recorded by number: new ones only ever go at the end. (stumble is no longer used.)
enum class Phase : std::uint8_t { idle, brace, impact, tumble, slide, settle, rest, stumble };
const char* name(Phase phase) noexcept;
// How the body goes down, read from its motion.
enum class Fall : std::uint8_t { unknown, forward, backward, left, right, drop, spin };
const char* name(Fall fall) noexcept;

struct Output {
    std::array<V3, part_count> w{}, v{}; // spin, and velocity
    std::array<bool, part_count> write{};
    Phase phase{};
    float tone{};          // the muscles' mean activation, 0..1
    int step{-1};          // no longer used (there is no catch step): always -1
    bool step_landed{};
    std::array<V3, 2> hand_mark{}, foot_mark{}; // where each arm aims its hand (world; NaN while that arm aims nowhere,
                                                // which the test bench reads as no aim), and each foot
    // What was read and done, for the recordings and the tests.
    Fall fall{};
    float time_to_hit{};    // predicted seconds until the body (not a hand or a foot) meets the ground
    float hit_speed{};      // how fast it will meet it, m/s
    Part hits_first{board}; // which part
    bool pain{};            // the lying-still reaction to what hurts is on
    std::array<float, part_count> torque{};     // each joint's muscle torque, by its child body, Nm
    std::array<float, part_count> activation{}; // each joint's muscle activation, by its child body, 0..1
    std::array<bool, part_count> driven{};      // the joint has a goal (the arms' reflex, the pain reaction), not tone alone
    float work{};           // the kinetic energy the muscles added this step, J (negative: they took it out)
};
// The most a joint's muscles turn its child body with at full activation, Nm.
float torque_limit(Part child, float strength) noexcept;

class Muscles {
public:
    // A new bail. `seed` varies it (which arm goes first, when a hand goes to what hurts), so no two look the same.
    void begin(const Input& in, std::uint32_t seed = 0) noexcept;
    // One physics step of the bail.
    Output step(const Input& in) noexcept;
    Phase phase() const noexcept { return phase_; }
    bool started() const noexcept { return started_; }

    static constexpr std::size_t joint_count = 22; // every body but the board and the pelvis

private:
    void enter(Phase next) noexcept;

    bool started_{};
    Phase phase_{Phase::idle};
    double t_{}, in_phase_{};
    double untouched_for_{}, still_for_{}, torso_down_for_{}, torso_off_for_{1};
    bool still_{};
    std::array<M3, part_count> rest_{};    // each body's axes in its parent's at the start (the box stops' middle)
    std::array<M3, joint_count> base_{};   // each joint's pose when the pain reaction began (what it curls from)
    std::array<float, joint_count> act_{}; // each joint's activation, 0..1
    std::array<V3, 2> aim_{};              // each arm's aim in the upper chest's axes (unit), as the reflex turns it
    float chin_{};                         // the chin's tuck going backwards, eased (radians)
    // The fall as read.
    V3 fall_{};                            // where he goes over (horizontal, unit; 0 not yet known)
    bool fall_known_{};
    Fall kind_{Fall::unknown}, wanted_{Fall::unknown};
    double wanted_for_{};
    float ground_{};
    bool ground_known_{};
    // What hit, and how hard.
    Part hurt_{board};
    float hurt_impact_{};
    int hurt_hand_{};                      // the hand that goes to it: -1 left, +1 right, 2 both, 0 none
    bool pain_{};
    double held_{};
    double stun_until_{-1}, last_stun_{-10};
    float stun_depth_{};
    std::array<double, 2> caught_at_{-1, -1};    // when each hand came down on the ground (-1 not yet)
    std::array<double, 2> arm_hit_at_{-10, -10}; // when each arm itself took a hard hit
    std::array<float, 2> arm_delay_{};           // one arm goes first, the other a little later
    float grab_delay_{0.55f};
};
}
