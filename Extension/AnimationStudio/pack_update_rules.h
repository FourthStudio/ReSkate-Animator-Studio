#pragma once
// What a pack from another PC may do to the tricks saved here: on their own, to be tested. Downloaded pack files can hold anything.
#include "anim_core.h"
#include "anim_presets.h"
#include <algorithm>
#include <string>

namespace dingosdk::anim_studio {
// A pack's member names the trick's file only when its name is the file's name. file_safe removes characters and cuts at
// 48, so "Kickfli/../p" reads as the trick p but files as Kickflip: an update could remove Kickflip while it checks p.
inline bool pack_member_names_its_files(const std::string& name) { return name == file_safe(name); }
// An update replaces another maker's trick, but not one edited and saved here.
inline bool update_may_replace(const Animation& saved, bool made_elsewhere) noexcept { return made_elsewhere && !saved.changed_here; }
// An edit saved here stays the trick that plays, in its old place. The maker's new version goes in beside it, switched off, so
// the player can choose it. Without this the edit went behind it and a FIXED playlist never played the edit again.
inline void keep_edit_ahead_of_update(PresetPack& pack, const std::string& edit, const std::string& update) {
    for (auto& [slot, list] : pack.slots)
        if (const auto at = std::find(list.begin(), list.end(), update); at != list.end()) {
            list.insert(at, edit);
            pack.off.insert(update);
        }
}
// A trick with this PC's mark: the player's own, whichever pack names it. A trick with no mark counts as its pack's.
inline bool marked_as_made_here(const Animation& saved, bool made_elsewhere) noexcept { return !saved.maker_key.empty() && !made_elsewhere; }
}
