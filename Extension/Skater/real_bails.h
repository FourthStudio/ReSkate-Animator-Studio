#pragma once
#include <array>
#include <cstdint>
#include <string>

// Real Bails: muscles for the skater's ragdoll. In a bail the game hands the body to 24 rigid bodies
// (Engine/Game/Skater/skater_body.h) that fall like a rag doll. Real Bails reads them on every physics
// step and turns each joint the way a skater's muscles would (real_bails_model.h): hands out to catch a
// fall forward, chin tucked and a roll going backwards, a held head, limbs that do not flop, then a body
// that lets go once it lies still. Each step it sets the bodies' spin and the velocity that goes with turning
// about their joints, through the physics world's own bodies (Engine/Game/Build/20260929/ragdoll_bodies.h:
// the rig's body records are only the game's mirror of them in a bail). On the ground a joint's turn is split
// between its two sides so the body as a whole keeps its spin; in the air the limbs move alone, as the game
// keeps the body upright there itself. The game's joints, contacts and getup stay its own, and the muscles
// stop as soon as the game starts getting him up.
namespace dingosdk::real_bails {
// Requires the validated build, No Bail started and the skater's physics readable (skater_physics.h).
bool start(std::uintptr_t image_base) noexcept;
bool available() noexcept;
// Client thread, every game tick: settings published, recordings written out.
void tick() noexcept;

void set_enabled(bool on) noexcept;
bool enabled() noexcept;
// 0.25 .. 2: how strong the muscles are (1 = as tuned).
void set_strength(float strength) noexcept;
float strength() noexcept;
// The rest of what can be tuned (the Animator Studio's SETTINGS > BAILS), saved with the profile.
struct Tuning {
    float reaction{1.0f};  // 0.5 .. 2: how fast the arms and the catch step go out
    float head{1.0f};      // 0 .. 1.5: how hard the neck holds the head up
    float tuck{1.0f};      // 0 .. 1.5: how tight he tucks rolling
    float lie_still{1.6f}; // 0.5 .. 4 s lying still before he lets go
    float grip{1.0f};      // 0 .. 2: how quickly the ground stops a slide
    bool catch_arms{true}, catch_step{true}, grab_hurt{true};
    float hold_hurt{4.5f}; // 1 .. 10 s holding what hurts once he is down
};
Tuning tuning() noexcept;
void set_tuning(const Tuning& tuning) noexcept;
// Everything back as tuned (strength too).
void reset_tuning() noexcept;
// Every bail written to logs\RealBails as a table of the bodies' motion, step by step.
void set_recording(bool on) noexcept;
bool recording() noexcept;

// What the physics steps last saw of the local skater.
struct Live {
    bool valid{};
    std::uint32_t state{};       // the physics state (300 = wipeout, 504 = off the board)
    bool ragdoll{}, in_bail{};
    std::uint32_t substate{};    // the off-board substate's offset in its parent (which ragdoll mode), 0 = none
    std::array<float, 3> board_velocity{}, pelvis_velocity{};
    std::array<float, 3> chest_forward{}; // which way his chest faces (from the ragdoll's bodies)
    std::uint64_t bails{};       // bails seen since the game started
    std::uint64_t driven{};      // physics steps the muscles drove
    double bail_seconds{};       // how long the current (or last) bail has lasted
    std::string phase;           // what the muscles are doing
};
Live live() noexcept;
}
