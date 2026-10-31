// Included in the Studio's drawing namespace: uses its fonts, rough tiles and controller navigation.
// Browsing, editing a destination and assigning playback are separate actions throughout this workspace.
std::vector<studio::PresetPack> library_packs(const studio::View& v) {
    std::vector<studio::PresetPack> out;
    for (const auto& pack : v.packs) out.push_back(pack.pack);
    return out;
}
struct LibraryIndex {
    std::vector<studio::PresetPack> packs;
    std::map<std::string, studio::PlaylistMembership> membership;
    std::map<std::string, std::vector<std::string>> origins;
    std::map<std::string, const studio::Preset*> presets;
    std::set<std::string> imported;
    std::map<std::string, std::string> labels;
    explicit LibraryIndex(const studio::View& v) : packs(library_packs(v)), membership(studio::playlist_membership_index(v.playlist, packs)) {
        for (const auto& pack : v.packs) for (const auto& name : pack.members) {
            origins[name].push_back(pack.name); if (pack.installed) imported.insert(name);
        }
        for (const auto& p : v.presets) {
            presets[p.name] = &p;
            if (!labels.contains(p.trick)) labels[p.trick] = trick_label(p.trick);
        }
    }
    const studio::Preset* preset(const std::string& name) const {
        const auto at = presets.find(name); return at == presets.end() ? nullptr : at->second;
    }
    const studio::PlaylistMembership& member(const std::string& name) const {
        static const studio::PlaylistMembership none;
        const auto at = membership.find(name); return at == membership.end() ? none : at->second;
    }
    const std::string& label(const std::string& trick) const {
        const auto at = labels.find(trick); return at == labels.end() ? trick : at->second;
    }
};
std::string library_saved_name(const LibraryIndex& index, const std::string& name) {
    const auto* p = index.preset(name); return p && !p->label.empty() ? p->label : name;
}
std::string library_origin(const LibraryIndex& index, const std::string& name) {
    const auto at = index.origins.find(name);
    return at == index.origins.end() ? "Personal save / no pack" : listed(at->second);
}
bool library_search(const LibraryIndex& index, const studio::Preset& p, const std::vector<std::string>& terms) {
    return terms.empty() || studio::native_search_matches(studio::native_search_key(
        library_saved_name(index, p.name) + " " + p.name + " " + index.label(p.trick) + " " + p.pop + " " + p.stance +
        (p.switch_stance.value_or(false) ? " switch" : " normal") + " " + p.made_by + " " + library_origin(index, p.name)), terms);
}
void library_panel_box(ImDrawList* d, const Fonts& f, ImVec2 a, ImVec2 b) {
    flat_rect(d, a, b, IM_COL32(15, 17, 21, 235), seed_of(a), f.k);
    d->AddLine(a, ImVec2(b.x, a.y), IM_COL32(255, 255, 255, 38), f.k);
}
#include "studio_library_widgets.inl"
#include "studio_library_browser.inl"
#include "studio_library_playlists.inl"
