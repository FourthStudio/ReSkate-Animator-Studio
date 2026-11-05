#pragma once
#include "Engine/Game/Build/fingerprint.h"
#include <cstddef>
#include <cstdint>

namespace dingosdk::game::build::v20260929::ragdoll_bodies {
// Supported SHA-256 fbce74d5e28ef525dbba2cb4adbebc13405bdbd88f31bc940bca45e4ae88b8f9.
// Real Bails (Extension/Skater/real_bails.h): the skater's ragdoll bodies in the physics world itself.
//
// The rig's 26 body records (skater_state.h: rig physics +0x20, 0x130 each) are the game's mirror of the world: in a
// bail it fills them from the world every step and drops what is written to them (2026-11-01: a spin or velocity
// written with its pending flag, before or after the skater's velocity update, the skeleton response or the animation
// publication, was gone the next step every time, the flag cleared). The world's own bodies are reached by handle,
// as the game's board sync (0x140fc06b0, called from 0x140fe519e with the board holder's handles at +0x718) does:
// 16 bytes, the physics system, the body's slot and the slot's generation. A handle is current while the system's
// generation array (+0x680) holds the same generation for the slot (0x1425d7b10).
struct BodyHandle {
    std::uintptr_t system{};
    std::uint32_t slot{}, generation{};
};
static_assert(sizeof(BodyHandle) == 16);
inline constexpr std::uintptr_t system_generations_offset = 0x680; // system: uint32 per slot
inline constexpr std::uint32_t no_slot = 0xffffffff;
// The world's setters the board sync calls: (const BodyHandle*, const float[4]*). Both refuse a non-finite value
// and keep the body under its own top speed.
inline constexpr Fingerprint set_linear_velocity{0x25da600, {
    0x40,0x53,0x48,0x83,0xec,0x30,0x8b,0x02,0x4c,0x8b,0xca,0x25,0x00,0x00,0x80,0x7f,
    0x48,0x8b,0xd9,0x3d,0x00,0x00,0x80,0x7f,0x0f,0x84,0x76,0x01,0x00,0x00,0x8b,0x42}};
inline constexpr Fingerprint set_angular_velocity{0x25d8530, {
    0x40,0x53,0x48,0x83,0xec,0x30,0x8b,0x02,0x4c,0x8b,0xca,0x25,0x00,0x00,0x80,0x7f,
    0x48,0x8b,0xd9,0x3d,0x00,0x00,0x80,0x7f,0x0f,0x84,0x76,0x01,0x00,0x00,0x8b,0x42}};
inline constexpr Fingerprint handle_valid{0x25d7b10, {
    0x8b,0x41,0x08,0x83,0xf8,0xff,0x74,0x19,0x44,0x8b,0xc0,0x48,0x8b,0x01,0x48,0x8b,
    0x90,0x80,0x06,0x00,0x00,0x8b,0x41,0x0c,0x42,0x39,0x04,0x82,0x75,0x03,0xb0,0x01}};
// The board's handles (the board holder, core+0x430, +0x718: one per board body).
inline constexpr std::uintptr_t board_handles_offset = 0x718;
// The ragdoll's handles: in the skater's core from +0x6f8, one per ragdoll body, handle k for the rig's body k + 1
// (skater_body.h: head first, pelvis last). Found 2026-11-01 by the board's physics system pointer turning up there
// 23 times in a row, then confirmed in a bail: a spin set through handle 8 turned the right upper arm (body 9), 17 the
// left thigh, 21 the right thigh, 1 the neck. The entry after the 23rd is empty (slot 0xffffffff).
inline constexpr std::uintptr_t rig_handles_offset = 0x6f8; // core
inline constexpr std::size_t rig_handle_count = 23;
}
