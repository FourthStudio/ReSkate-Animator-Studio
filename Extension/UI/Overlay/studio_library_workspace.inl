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
// ---- the LIBRARY in the MARKET's own look: the list of playlists on a sheet of grip tape torn at its edges (the
// playlists on cream paper, the one open on the studio's blue, taped down); everything there is to read on the MARKET's
// crumpled cream paper (what plays on a strip of concrete) in its type - Anton, its spaced capitals, its body face - in
// dark ink; notes in blue marker; and the shop's flat buttons ----
// The ink on paper: words, quieter words, the quietest, the studio's blue as it reads on cream, a warning, a fault.
constexpr ImU32 lib_ink = shop_ink, lib_ink_soft = IM_COL32(58, 60, 66, 255), lib_ink_faint = IM_COL32(104, 106, 112, 255),
                lib_blue = IM_COL32(0, 98, 214, 255), lib_warn = IM_COL32(176, 100, 0, 255), lib_red = IM_COL32(184, 36, 36, 255);
// A row or a box on paper: a shade of the paper, the studio's blue a little for one that is in, a fault's red.
constexpr ImU32 lib_row = IM_COL32(0, 0, 0, 18), lib_row_in = IM_COL32(0, 98, 214, 34), lib_row_bad = IM_COL32(184, 36, 36, 34);
// Whether what is drawn now sits on paper (the buttons outline in ink there, not in white).
bool lib_on_paper = false;
struct OnPaper {
    bool was;
    OnPaper() : was(lib_on_paper) { lib_on_paper = true; }
    ~OnPaper() { lib_on_paper = was; }
    OnPaper(const OnPaper&) = delete;
    OnPaper& operator=(const OnPaper&) = delete;
};
// A button as the shop's: blue, outlined (in ink on paper), cream paper (black on paper) or black (a red one stays the
// studio's own, for deleting).
bool lib_button(ImDrawList* d, const Fonts& f, const char* id, ImVec2 a, ImVec2 size, const std::string& label, Look look = Look::dark,
                bool enabled = true) {
    if (look == Look::danger) return action(d, f, id, a, size, label, look, enabled);
    const ShopLook shop = look == Look::blue    ? ShopLook::blue
                          : look == Look::paper ? (lib_on_paper ? ShopLook::black : ShopLook::paper)
                          : look == Look::dark  ? ShopLook::black
                                                : (lib_on_paper ? ShopLook::ink : ShopLook::line);
    return shop_button(d, f, id, a, size, label, shop, enabled);
}
// A sheet to read on: the MARKET's crumpled cream paper (or concrete), torn at its edges, its shadow under it.
void library_sheet(ImDrawList* d, const Fonts& f, ImVec2 a, ImVec2 b, PaperKind kind = PaperKind::white) {
    const float k = f.k, w = b.x - a.x, h = b.y - a.y;
    if (w < 8 * k || h < 8 * k) return;
    const std::uint32_t seed = seed_of(a) + 11;
    const auto shape = torn::piece(w, h, seed, 2.4f * k, 7 * k, 9 * k);
    draw_paper(d, f, shape, a, 0, ImVec2(0, 0), kind, seed, 0.5f, ImVec2(7, 9), 2.6f);
}
// A row on paper: a shade of it, with a hairline under it.
void lib_row_box(ImDrawList* d, ImVec2 a, ImVec2 b, ImU32 fill, float k) {
    d->AddRectFilled(a, b, fill);
    d->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y), IM_COL32(0, 0, 0, 46), 1.2f * k);
}
// A panel: a sheet of grip tape torn at its edges (darkened a little), as the MARKET's are cut: the list of playlists.
void library_panel_box(ImDrawList* d, const Fonts& f, ImVec2 a, ImVec2 b) {
    const float k = f.k, w = b.x - a.x, h = b.y - a.y;
    if (w < 8 * k || h < 8 * k) return;
    const std::uint32_t seed = seed_of(a);
    const auto shape = torn::piece(w, h, seed, 2.2f * k, 6 * k, 9 * k);
    draw_paper(d, f, shape, a, 0, ImVec2(0, 0), PaperKind::grip, seed, 0.38f, ImVec2(5, 7), 2.2f, IM_COL32(176, 178, 184, 255));
}
// A playlist's card: cream paper, or the studio's blue for the one open (taped down at its corner).
void library_card(ImDrawList* d, const Fonts& f, ImVec2 a, ImVec2 b, bool chosen, bool lit, std::uint32_t seed) {
    const float k = f.k;
    const auto shape = torn::piece(b.x - a.x, b.y - a.y, seed, 1.6f * k, 5 * k, 7 * k);
    draw_paper(d, f, shape, a, 0, ImVec2(0, 0), chosen ? PaperKind::blue : PaperKind::white, seed, 0.4f, ImVec2(4, 6), 2.0f,
               chosen || lit ? IM_COL32_WHITE : IM_COL32(232, 230, 224, 255));
    if (chosen) masking_tape(d, f, ImVec2(a.x + 34 * k, a.y + 1 * k), 58 * k, 17 * k, -9, seed + 5);
}
// Words in the shop's spaced capitals.
void lib_caps(ImDrawList* d, const Fonts& f, ImVec2 at, float size, ImU32 colour, std::string_view words, float spacing = 1.6f) {
    text_spaced(d, mont(f, size), size, at, colour, words, spacing * f.k);
}

// One click: a playlist with every pack and every trick of his own in it, playing in game by default.
void library_use_everything(const studio::View& v) {
    std::string name = "Everything";
    const auto taken = [&](const std::string& n) { return std::any_of(v.playlists.begin(), v.playlists.end(), [&](const auto& l) { return l.name == n; }); };
    for (int n = 2; taken(name); ++n) name = "Everything " + std::to_string(n);
    std::string m;
    if (!studio::new_playlist(name, m)) {
        say(m);
        return;
    }
    std::size_t packs = 0, own = 0;
    for (const auto& p : v.packs) {
        std::string pm;
        if (studio::edit_playlist_pack(name, p.name, true, pm)) ++packs;
    }
    // His own saves that are in no pack (a pack's are in already, with its pack).
    const LibraryIndex index(v);
    for (const auto& p : v.presets) {
        if (p.foreign || p.locked || !p.available || index.origins.contains(p.name)) continue;
        std::string pm;
        if (studio::edit_playlist_member(name, {"", p.name}, true, pm)) ++own;
    }
    studio::play_playlist(name, m);
    studio::set_play_in_game(true);
    auto& route = ui().library;
    route.select(name);
    route.page = 0;
    say(std::format("\"{}\" made: {} {} and {} of your own {}, playing in game now.", name, packs, packs == 1 ? "pack" : "packs", own,
                    own == 1 ? "trick" : "tricks"));
}
#include "studio_library_widgets.inl"
#include "studio_library_browser.inl"
#include "studio_library_playlists.inl"
