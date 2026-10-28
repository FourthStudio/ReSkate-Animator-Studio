#pragma once

namespace dingosdk::overlay::detail {
// Slots 0..3 belong to ImGui, card/map pictures and mod icons. Allocate each
// renderer after the previous range so studio art cannot overwrite plugin SRVs.
inline constexpr unsigned plugin_image_first_slot = 4, plugin_image_slots = 16;
inline constexpr unsigned screen_look_slot = plugin_image_first_slot + plugin_image_slots, screen_look_slots = 3;
// Thunderstore icons for the Mod Manager page (store_icons.cpp), then a viewmodel's pictures (viewmodel.h).
inline constexpr unsigned store_icon_first_slot = screen_look_slot + screen_look_slots, store_icon_slots = 64;
inline constexpr unsigned viewmodel_first_slot = store_icon_first_slot + store_icon_slots, viewmodel_slots = 24;
// The Animator Studio's: pack pictures, the game's frame for the fisheye, the MARKET's wall, sheet and cover preview.
inline constexpr unsigned studio_pack_slot = viewmodel_first_slot + viewmodel_slots;
inline constexpr unsigned studio_frame_slot = studio_pack_slot + 1;
inline constexpr unsigned studio_wall_slot = studio_frame_slot + 1;
inline constexpr unsigned studio_sheet_slot = studio_wall_slot + 1;
inline constexpr unsigned studio_cover_slot = studio_sheet_slot + 1;
inline constexpr unsigned overlay_descriptor_count = studio_cover_slot + 1;
static_assert(studio_pack_slot >= plugin_image_first_slot + plugin_image_slots);
static_assert(studio_pack_slot >= screen_look_slot + screen_look_slots);
static_assert(store_icon_first_slot >= screen_look_slot + screen_look_slots);
static_assert(studio_pack_slot >= viewmodel_first_slot + viewmodel_slots);
static_assert(overlay_descriptor_count == 116);
} // namespace dingosdk::overlay::detail
