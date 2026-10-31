#pragma once
#include "anim_presets.h"

namespace dingosdk::anim_studio {
inline bool same_playlist_source(const PlaylistEntry& a, const PlaylistEntry& b) {
    return a.pack == b.pack && a.animation == b.animation;
}
inline const PresetPack* playlist_pack(std::span<const PresetPack> packs, const std::string& name) {
    const auto at = std::find_if(packs.begin(), packs.end(), [&](const auto& p) { return p.name == name; });
    return at == packs.end() ? nullptr : &*at;
}
inline bool playlist_has_pack(std::span<const PlaylistEntry> entries, const std::string& pack) {
    return std::any_of(entries.begin(), entries.end(), [&](const auto& e) { return e.pack == pack && e.animation.empty(); });
}
struct PlaylistMembership {
    bool direct{};
    std::vector<std::string> packs;
    bool included() const { return direct || !packs.empty(); }
};
inline PlaylistMembership playlist_membership(std::span<const PlaylistEntry> entries, std::span<const PresetPack> packs,
                                             const std::string& animation) {
    PlaylistMembership out;
    for (const auto& e : entries) {
        const auto* pack = e.pack.empty() ? nullptr : playlist_pack(packs, e.pack);
        if (!e.pack.empty() && (!pack || !pack->has(animation))) continue;
        if (!e.animation.empty()) out.direct = out.direct || e.animation == animation;
        else if (pack && !pack->off.contains(animation) && !e.omitted.contains(animation)) out.packs.push_back(e.pack);
    }
    return out;
}
// Build once per rendered destination rather than rescanning every pack for every visible save.
inline std::map<std::string, PlaylistMembership> playlist_membership_index(std::span<const PlaylistEntry> entries,
                                                                         std::span<const PresetPack> packs) {
    std::map<std::string, PlaylistMembership> out;
    for (const auto& e : entries) {
        const auto* pack = e.pack.empty() ? nullptr : playlist_pack(packs, e.pack);
        if (!e.pack.empty() && !pack) continue;
        if (!e.animation.empty()) {
            if (!pack || pack->has(e.animation)) out[e.animation].direct = true;
        } else if (pack) {
            for (const auto& [slot, names] : pack->slots) {
                (void)slot;
                for (const auto& name : names)
                    if (!pack->off.contains(name) && !e.omitted.contains(name)) {
                        auto& sources = out[name].packs;
                        if (std::find(sources.begin(), sources.end(), e.pack) == sources.end()) sources.push_back(e.pack);
                    }
            }
        }
    }
    return out;
}
// One checkbox means one animation in this destination, regardless of how many source entries include it.
// Removing it creates playlist-local exclusions for whole packs; it never edits a pack or another playlist.
inline bool set_playlist_member(std::vector<PlaylistEntry>& entries, std::span<const PresetPack> packs,
                                PlaylistEntry source, bool included) {
    if (source.animation.empty()) return false;
    const auto* pack = source.pack.empty() ? nullptr : playlist_pack(packs, source.pack);
    if (!source.pack.empty() && (!pack || !pack->has(source.animation))) return false;
    const auto before = entries;
    if (included) {
        if (playlist_membership(entries, packs, source.animation).included()) return false;
        const auto whole = std::find_if(entries.begin(), entries.end(), [&](const auto& e) {
            return !source.pack.empty() && e.pack == source.pack && e.animation.empty();
        });
        if (whole != entries.end() && pack && !pack->off.contains(source.animation)) whole->omitted.erase(source.animation);
        else { source.omitted.clear(); entries.push_back(std::move(source)); }
    } else {
        std::erase_if(entries, [&](const auto& e) { return e.animation == source.animation; });
        for (auto& e : entries)
            if (e.animation.empty())
                if (const auto* owner = playlist_pack(packs, e.pack); owner && owner->has(source.animation)) e.omitted.insert(source.animation);
    }
    return entries != before;
}
inline bool set_playlist_pack(std::vector<PlaylistEntry>& entries, const std::string& pack, bool included) {
    if (pack.empty()) return false;
    const auto before = entries;
    const auto first = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.pack == pack; });
    const auto position = static_cast<std::size_t>(first - entries.begin());
    std::erase_if(entries, [&](const auto& e) { return e.pack == pack; });
    if (included) entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(std::min(position, entries.size())), {pack, "", {}});
    return entries != before;
}
inline Json playlist_entries_json(const std::vector<PlaylistEntry>& entries) {
    Json out = Json::array();
    for (const auto& e : entries) {
        Json one{{"pack", e.pack}, {"animation", e.animation}};
        if (e.animation.empty() && !e.omitted.empty()) one["omitted"] = Json(std::vector<std::string>(e.omitted.begin(), e.omitted.end()));
        out.push_back(std::move(one));
    }
    return out;
}
inline std::vector<PlaylistEntry> playlist_entries_of(const Json& root) {
    std::vector<PlaylistEntry> out;
    if (!root.contains("entries") || !root.at("entries").is_array()) return out;
    for (const auto& e : root.at("entries")) {
        if (!e.is_object() || out.size() >= 2000) continue;
        if (e.contains("pack") && !e.at("pack").is_string()) continue;
        if (e.contains("animation") && !e.at("animation").is_string()) continue;
        PlaylistEntry entry{e.value("pack", ""), e.value("animation", ""), {}};
        if (entry.pack.empty() && entry.animation.empty()) continue;
        if (entry.animation.empty() && e.contains("omitted") && e.at("omitted").is_array())
            for (const auto& name : e.at("omitted"))
                if (name.is_string() && !name.string().empty() && entry.omitted.size() < 4000) entry.omitted.insert(name.string());
        if (std::none_of(out.begin(), out.end(), [&](const auto& old) { return same_playlist_source(old, entry); })) out.push_back(std::move(entry));
    }
    return out;
}
struct PlaylistEditHistory {
    std::string name;
    std::vector<PlaylistEntry> before, after;
    bool undone{};
    bool available(const std::string& target, const std::vector<PlaylistEntry>& current) const {
        return !name.empty() && name == target && current == (undone ? before : after);
    }
    bool apply(const std::string& target, std::vector<PlaylistEntry>& current) {
        if (!available(target, current)) return false;
        current = undone ? after : before;
        undone = !undone;
        return true;
    }
};
}
