#pragma once
#include <array>
#include <cstdint>
#include <string>

// Real Bails: muscles for the skater's ragdoll. In a bail the game hands the body to 24 rigid bodies
// (Engine/Game/Skater/skater_body.h) that fall like a rag doll. Real Bails reads them on every physics
// step and adds what a falling person's muscles add (real_bails_model.h): tone in every joint (so the limbs
// do not flop), a person's joint ranges, and the hands coming out to catch the fall; once he lies still, a
// hand to what hurts. The fall itself stays the ragdoll's. Each step it sets the bodies' spin and velocity
// through the physics world's own bodies (Engine/Game/Build/20260929/ragdoll_bodies.h: the rig's body
// records are only the game's mirror of them in a bail). Every muscle torque turns one side of its joint
// against the other through the linked bodies, so the body as a whole keeps its momentum. The game's
// joints, contacts and getup stay its own, and the muscles stop as soon as the game starts getting him up.
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
// (real_bails_model.h's Tuning says how each one is read.)
struct Tuning {
    float reaction{1.0f};  // 0.5 .. 2: how fast the arms come out
    float head{1.0f};      // 0 .. 1.5: the neck's tone
    float tuck{1.0f};      // 0 .. 1.5: how far he curls up lying still and hurt
    float lie_still{1.6f}; // 0.5 .. 4 s lying still before he lets go
    float grip{1.0f};      // 0 .. 2: no longer used (a slide is the ground's friction); kept so settings load
    bool catch_arms{true}, catch_step{true}, grab_hurt{true}; // catch_step: no longer used; kept so settings load
    float hold_hurt{4.5f}; // 1 .. 10 s holding what hurts once he lies still
    float weight{1.0f};    // 0 .. 2: how heavy his arms and legs are (their tone, and how they land)
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
