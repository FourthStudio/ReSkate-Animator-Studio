#pragma once
#include "real_bails_math.h"
#include <array>
#include <cstdint>

// Real Bails' muscles (real_bails.h), as plain rules: given the ragdoll's 24 bodies in one physics step, the spin and
// velocity each body should have next. Testable without the game.
//
// The bail is the game's ragdoll: a heavy body falling, rolling and sliding as gravity, momentum, its joints and the
// ground take it. The muscles add only what a falling person's add:
//  - tone: the arms and legs resist being flung about (a damper on each joint's own turning, never a pose) and land
//    heavily; the joints' ranges (real_bails_limits.h) hold whatever the slam does;
//  - the reflexes of a fall: from the moment the bail starts the arms reach the way his momentum takes him (forwards,
//    back and down, or out to the side), one a moment before the other; the chin goes to the chest going over backwards,
//    sideways or tumbling (face first the head turns the cheek to the ground); with his feet still on the board, the foot
//    on it kicks it away; a body part that is hit flinches (tenses and pulls back from it), briefly;
//  - once he is down and slow (still sliding is fine): what hurts most is brought to the hands and the hands go to it
//    (the other hand to a hurt elbow or wrist, both to a knee drawn up, to the head or to the groin), he curls a little
//    over it, and an arm or a leg left up in the air is put down on the ground or the body. Moving fast again stops it.
//
// The game decides what of each push stays (measured from the user's recordings for update 1.2): of a change to a
// joint's turning it keeps a share that grows with how fast that joint already turns: 1-4% of any push, however large, on
// a joint turning under about 0.6 rad/s (the game holds a still joint where it is: an arm left in the air stays there),
// 8-13% at 1-2.5 rad/s, 30-45% over 4, in the air and on the ground alike; a joint's own motion it keeps (93-96% a step).
// So every move here is made while the joints still move (the reach for what hurts starts as he slows, not once he lies
// still), at a pace the game lets through until it arrives, where the game's hold then keeps it; and what the muscles
// hand the game is scaled by the share it keeps at that joint's speed now, bounded (not by a running average of it,
// which lagged behind and flicked the arms when a joint got going).
//
// A muscle is inside the body: each torque turns one side of its joint against the other through the linked bodies
// (real_bails_body.h), so the body's momentum, linear and angular, stays exactly as the fall made it. The one outside
// push is the board's, when the foot kicks it away: what the board gets, the leg gets back.
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
    bool has_normal{}; V3 normal{}; // and the surface's normal there, when known (either way round)
    bool on_board{};                // that contact was with the skateboard (the game's own feet-on-board test reads it)
};
// The skateboard, as far as the runtime can read it.
struct BoardIn {
    bool ok{};       // read this step, and its readings follow on from the last steps (the board's own, not a stale copy)
    V3 p{};          // where it is (its root body)
    V3 v{};          // how fast it goes
    bool writable{}; // the runtime sets the board's velocity by Output.board_dv when it is kicked (else the kick is the
                     // leg's alone: the foot pushes the board through the game's own contact)
};
// What the player can tune (the Animator Studio's SETTINGS > BAILS). The muscles read each one as:
//  - reaction: how quickly the arms come out (their reflex delay is divided by it, and they turn out faster), and how
//    quickly a hand gets to what hurts;
//  - head: head protection: how hard he pulls the chin to the chest going over backwards, sideways or tumbling (going down
//    face first: how far the head turns, cheek to the ground), and the neck's tone; 0 lets the head go;
//  - tuck: how far he curls up when he is down and hurt (a knee drawn to the chest, the back rounded, knees up);
//  - lie_still: seconds he takes to let go once he lies still;
//  - grip: no longer used: a slide is the ground's friction alone (kept so saved settings still load);
//  - catch_arms: the arms' reach; catch_step: no longer used (no step: kept so saved settings still load);
//  - grab_hurt, hold_hurt: the reaction to what hurts once he is down, and about how many seconds he holds it;
//  - weight: Heavy limbs (the arms' and legs' tone, and how they land);
//  - kick_board: the foot on the board kicks it away as the bail starts.
struct Tuning {
    float reaction{1.0f};  // 0.5 .. 2
    float head{1.0f};      // 0 .. 1.5
    float tuck{1.0f};      // 0 .. 1.5
    float lie_still{1.6f}; // 0.5 .. 4 s
    float grip{1.0f};      // 0 .. 2 (unused)
    bool catch_arms{true}, catch_step{true}, grab_hurt{true};
    float hold_hurt{4.5f}; // 1 .. 10 s
    float weight{1.0f};    // 0 .. 2: Heavy limbs (the arms' and legs' tone, and how they land)
    bool kick_board{true};
};
struct Input {
    std::array<BodyIn, part_count> body{};
    BoardIn board{};
    float dt{};
    float strength{1.0f}; // 0.25 .. 2: every muscle's strength and tone
    Tuning tune{};
    // In the game, whose own joint solve takes part of each push back (real_bails_model.cpp, game_gain). Off: the
    // muscles as a person's are, for tests and the offline bench, where every push stays.
    bool in_game{};
};
// What the muscles are doing. Recorded by number: new ones only ever go at the end. (stumble is no longer used.)
enum class Phase : std::uint8_t { idle, brace, impact, tumble, slide, settle, rest, stumble };
const char* name(Phase phase) noexcept;
// How the body goes down, read from its motion.
enum class Fall : std::uint8_t { unknown, forward, backward, left, right, drop, spin };
const char* name(Fall fall) noexcept;
// What hurts most, and so what he holds once he is down. Recorded by number: new ones only ever go at the end.
enum class Hurt : std::uint8_t {
    none, head, left_arm, right_arm, left_hand, right_hand, left_leg, right_leg, left_hip, right_hip, back, left_ribs,
    right_ribs, groin
};
const char* name(Hurt hurt) noexcept;

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
    bool pain{};            // the reaction to what hurts is on
    Hurt hurt{};            // what hurts most (none until something hit hard enough)
    float hurt_amount{};    // how much it has taken: the sum of its hits' (m/s over 1.5) x its place's share, easing off
    bool down{};            // he is down and slow enough to react to it (not tumbling, flying or sliding fast)
    std::array<bool, 2> leg_lifted{}; // the reaction to what hurts draws this leg's knee up off the ground (the hugged
                                      // knee, the curl of a racked groin): the one time a leg is lifted lying down
    float flinch{};         // the strongest reflex to a hit going on now, 0..1
    int kick_leg{};         // the leg kicking the board away now: -1 left, +1 right, 0 none
    bool board_kick{};      // this step the board is kicked: its velocity changes by board_dv
    V3 board_dv{};          // (written to the board only when BoardIn.writable; the leg gets the board's push back then)
    std::array<float, part_count> torque{};     // each joint's muscle torque, by its child body, Nm
    std::array<float, part_count> activation{}; // each joint's muscle activation, by its child body, 0..1
    std::array<bool, part_count> driven{};      // the joint has a goal (a reach, the chin, the kick, what hurts), not tone
    std::array<bool, part_count> reflex{};      // the joint is flinching from a hit (a brief push, never a pose)
    float work{};           // the kinetic energy the muscles added this step, J (negative: they took it out)
    // Recorded: each hand's part in the reaction to what hurts (Reach by number) and how far its palm is from where it
    // goes (m, 0 when it goes nowhere), and in the game the mean of what the joints' pushes were handed over scaled by.
    std::array<std::uint8_t, 2> reach{};
    std::array<float, 2> reach_gap{};
    float gain{1.0f};
};
// A hand's part in the reaction to what hurts. Recorded by number: new ones only ever go at the end.
enum class Reach : std::uint8_t { none, going, on_spot, resting, gave_up };
// The most a joint's muscles turn its child body with at full activation, Nm.
float torque_limit(Part child, float strength) noexcept;

class Muscles {
public:
    // A new bail. `seed` varies it (which arm goes first, how soon he reacts), so no two look the same.
    void begin(const Input& in, std::uint32_t seed = 0) noexcept;
    // One physics step of the bail.
    Output step(const Input& in) noexcept;
    Phase phase() const noexcept { return phase_; }
    bool started() const noexcept { return started_; }

    static constexpr std::size_t joint_count = 22; // every body but the board and the pelvis

private:
    void enter(Phase next) noexcept;
    float draw() noexcept; // the bail's own small random numbers, -1..1, from its seed

    std::uint32_t seed_{}, draws_{};
    bool started_{};
    Phase phase_{Phase::idle};
    double t_{}, in_phase_{};
    double untouched_for_{}, still_for_{}, torso_down_for_{}, torso_off_for_{1};
    bool still_{};
    std::array<M3, part_count> rest_{};    // each body's axes in its parent's at the start (the box stops' middle)
    std::array<M3, joint_count> base_{};   // each joint's pose when the reaction to what hurts (re)started (from where it moves)
    std::array<float, joint_count> act_{}; // each joint's activation, 0..1
    std::array<V3, 2> aim_{};              // each arm's aim in the upper chest's axes (unit), as the reach turns it
    std::array<float, 2> amp_{};           // how far each arm gets to reach (short of time: it arrives bent, nearer the chest)
    std::array<float, 2> arm_delay_{};     // the lead arm goes first, the other a little later
    std::array<float, 2> arm_jitter_{};
    float arm_gap_{0.04f};
    int lead_{};                           // the arm that leads: -1 left, +1 right, 0 not chosen yet
    int lead_seed_{1};                     // which leads when the fall has no side to it
    float chin_{};                         // the chin's tuck, eased (radians)
    float turn_{}, lift_{}, side_{};       // the head's turn and lift going down face first, and its bend away from the
                                           // ground going down sideways, eased (radians)
    int turn_side_{1};                     // which way the head turns then (+1 to his left)
    // The fall as read.
    V3 fall_{};                            // where he goes over (horizontal, unit; 0 not yet known)
    bool fall_known_{};
    Fall kind_{Fall::unknown}, wanted_{Fall::unknown};
    double wanted_for_{};
    float ground_{};
    bool ground_known_{};
    // The hits.
    std::array<double, part_count> landed_at_{}; // when each body last came down hard on something (a limb's thud)
    // What the game keeps of what the muscles hand it (in the game): each joint's last error to the turning it is pushed
    // towards, and when it last overshot (the error came back the other way, more than half as large): a while after
    // that it is handed over at half.
    std::array<V3, part_count> error_{};
    std::array<double, part_count> overshot_at_{};
    std::array<double, part_count> goal_since_{}; // when each joint's move began (it builds up from there)
    std::array<bool, part_count> was_goal_{};
    std::array<double, 4> limb_hit_at_{};        // each limb's last hit (left arm, right arm, left leg, right leg)
    std::array<float, 4> limb_hit_size_{};
    std::array<V3, 4> limb_hit_from_{};          // the way the hit pushed it (world, unit)
    std::array<V3, 4> limb_hit_point_{};         // where (world)
    std::array<float, 4> limb_hit_delay_{};      // its withdrawal's latency
    double head_hit_at_{-10};                    // the last hit to the head or the upper body (the chin flinches in)
    float head_hit_size_{};
    float startle_{1};                           // how big a startle is now: it halves on repeats, recovering over seconds
    double stun_until_{-1}, last_stun_{-10}, last_hard_{-10};
    float stun_depth_{};
    double first_hit_at_{-1};                    // when he first came down (-1 not yet)
    double weak_at_{-10};                        // a hit: his strength drops and comes back over 0.4 s from here
    float weak_depth_{};
    std::array<double, 2> caught_at_{-1, -1};    // when each hand came down on the ground (-1 not yet)
    std::array<float, 2> caught_bend_{};         // its elbow's bend then
    std::array<double, 2> arm_hit_at_{-10, -10}; // when each arm itself took a hard hit
    // What hurts, by place (Hurt by number): how much it has taken (easing off over the bail), and its last hit's body,
    // spot (in that body's axes from its joint), time and hardest speed.
    static constexpr std::size_t place_count = 14;
    std::array<float, place_count> damage_{}, region_speed_{};
    std::array<Part, place_count> region_part_{};
    std::array<V3, place_count> region_local_{};
    std::array<bool, place_count> region_known_{};
    std::array<double, place_count> region_at_{};
    std::array<float, part_count> off_for_{};      // how long each body has been off everything (a hit counts once:
    std::array<bool, part_count> was_off_{};       // a new one after a tenth of a second off it, or 1 m/s harder than
    std::array<float, part_count> last_impact_{};  // the hardest of this contact so far)
    // What he holds, and the reaction to it.
    Part hurt_part_{board};
    Hurt hurt_{Hurt::none};
    float hurt_speed_{};
    V3 hurt_local_{};                            // where it was hit, in the hurt body's axes from its joint
    bool hurt_local_known_{};
    double hurt_at_{-10};
    float hurt_delay_{0.2f};
    bool slow_{}, down_{};
    double down_for_{};
    bool pain_{};
    double held_{};
    double pain_since_{-1};                      // when the reaction (re)started
    double let_go_at_{-1};                       // when he stopped holding on (he lets the rhythm go from there; -1 not)
    std::array<Reach, 2> reach_{};               // each hand's part in it
    std::array<double, 2> on_spot_at_{-1, -1};   // when each hand got to its spot
    std::array<double, 2> gave_up_at_{-1, -1};   // or gave up on it
    std::array<float, 2> best_gap_{};            // the nearest each has got, and when it last got nearer
    std::array<double, 2> nearer_at_{};
    std::array<float, 2> hand_delay_{};          // when each hand sets off after the reaction starts (the nearer first)
    std::array<double, 2> arm_down_at_{-1, -1};  // an arm left up in the air, being put down: since when
    std::array<double, 2> leg_down_at_{-1, -1};  // and a leg
    int roll_side_{1};                           // racked: the side he curls over onto (+1 his left)
    float rock_phase_{};
    float asym_{};                               // how much more one side curls than the other (-1..1, seeded)
    // The board.
    std::array<double, 2> board_at_{-10, -10};   // when each foot last touched the board
    float kick_onset_{0.2f};
    bool kick_tried_{};
    int kick_leg_{};                             // -1 left, +1 right, 0 none
    double kick_at_{-1};
    bool kick_shot_{};
    V3 kick_dir_{}, kick_target_{}, board_dv_{};
};
}
