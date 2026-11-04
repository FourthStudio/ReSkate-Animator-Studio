// Included in the Studio's drawing namespace: uses its fonts, rough tiles and controller navigation.
// Browsing, editing a destination and assigning playback are separate actions throughout this workspace.

// ---- what the LIBRARY looks things up in, built once per change rather than every frame ----
// With hundreds of tricks and packs, building the maps below every frame (copying every pack, each with its slots and
// cover, and every search key) took milliseconds a frame in the LIBRARY alone. They are kept, with a fingerprint of what
// they were built from: every name, label, pack member and playlist entry they read. Working that out is one pass over
// those strings with no allocations, so a change anywhere (a rename, a pack edited, an entry moved) builds them again on
// the next frame and nothing is shown stale.
struct LibraryIndex {
    std::uint64_t key{};
    bool built{};
    const studio::View* view{}; // this frame's; `presets` holds places in its list, which the fingerprint keeps in order
    std::map<std::string, studio::PlaylistMembership> membership;
    std::map<std::string, std::vector<std::string>> origins;
    std::unordered_map<std::string, std::size_t> presets;
    std::set<std::string> imported;
    std::map<std::string, std::string> labels;
    std::size_t available{}; // tricks the open playlist has that can play
    std::size_t own{};       // the player's own saves that can play (QUICK START says how many)
    // For each entry of the open playlist that is a whole pack: its tricks that play there (by their place in presets).
    std::vector<std::vector<std::size_t>> entry_members;
    const studio::Preset* preset(const std::string& name) const {
        const auto at = presets.find(name);
        return at == presets.end() || !view || at->second >= view->presets.size() ? nullptr : &view->presets[at->second];
    }
    const studio::PlaylistMembership& member(const std::string& name) const {
        static const studio::PlaylistMembership none;
        const auto at = membership.find(name); return at == membership.end() ? none : at->second;
    }
    const std::string& label(const std::string& trick) const {
        const auto at = labels.find(trick); return at == labels.end() ? trick : at->second;
    }
};
std::uint64_t library_fingerprint(const studio::View& v) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](std::string_view s) {
        for (const unsigned char c : s) h = (h ^ c) * 1099511628211ull;
        h = (h ^ 0x1f) * 1099511628211ull; // a separator, so "ab" + "c" is not "a" + "bc"
    };
    const auto flag = [&](bool b) { h = (h ^ (b ? 0x31u : 0x30u)) * 1099511628211ull; };
    for (const auto& p : v.presets) {
        mix(p.name); mix(p.label); mix(p.trick); mix(p.pop); mix(p.made_by);
        flag(p.available); flag(p.foreign); flag(p.locked); flag(p.switch_stance.value_or(false));
    }
    mix("|packs");
    for (const auto& pack : v.packs) {
        mix(pack.name); mix(pack.made_by); flag(pack.installed); flag(pack.favorite);
        for (const auto& name : pack.members) mix(name);
        for (const auto& name : pack.pack.off) mix(name);
        mix("|");
    }
    mix(v.playlist_open);
    for (const auto& e : v.playlist) {
        mix(e.pack); mix(e.animation);
        for (const auto& name : e.omitted) mix(name);
    }
    return h;
}
const LibraryIndex& library_index(const studio::View& v) {
    static LibraryIndex index;
    const auto key = library_fingerprint(v);
    index.view = &v;
    if (index.built && index.key == key) return index;
    index = {};
    index.key = key;
    index.built = true;
    index.view = &v;
    std::vector<studio::PresetPack> packs;
    packs.reserve(v.packs.size());
    for (const auto& pack : v.packs) packs.push_back(pack.pack);
    index.membership = studio::playlist_membership_index(v.playlist, packs);
    for (const auto& pack : v.packs)
        for (const auto& name : pack.members) {
            index.origins[name].push_back(pack.name);
            if (pack.installed) index.imported.insert(name);
        }
    for (std::size_t i = 0; i < v.presets.size(); ++i) {
        const auto& p = v.presets[i];
        index.presets.emplace(p.name, i);
        if (!index.labels.contains(p.trick)) index.labels[p.trick] = trick_label(p.trick);
        index.own += !p.foreign && !p.locked && p.available && !index.origins.contains(p.name);
    }
    for (const auto& [name, member] : index.membership) {
        (void)member;
        if (const auto* p = index.preset(name); p && p->available) ++index.available;
    }
    index.entry_members.resize(v.playlist.size());
    for (std::size_t i = 0; i < v.playlist.size(); ++i) {
        const auto& e = v.playlist[i];
        if (!e.animation.empty()) continue;
        const auto pack = std::find_if(v.packs.begin(), v.packs.end(), [&](const auto& p) { return p.name == e.pack; });
        if (pack == v.packs.end()) continue;
        for (const auto& name : pack->members)
            if (!pack->pack.off.contains(name) && !e.omitted.contains(name))
                if (const auto at = index.presets.find(name); at != index.presets.end()) index.entry_members[i].push_back(at->second);
    }
    return index;
}
std::string library_saved_name(const LibraryIndex& index, const std::string& name) {
    const auto* p = index.preset(name); return p && !p->label.empty() ? p->label : name;
}
std::string library_origin(const LibraryIndex& index, const std::string& name) {
    const auto at = index.origins.find(name);
    return at == index.origins.end() ? "Your own save / in no pack" : "From " + listed(at->second);
}
bool library_search(const LibraryIndex& index, const studio::Preset& p, const std::vector<std::string>& terms) {
    return terms.empty() || studio::native_search_matches(studio::native_search_key(
        library_saved_name(index, p.name) + " " + p.name + " " + index.label(p.trick) + " " + p.pop + " " + p.stance +
        (p.switch_stance.value_or(false) ? " switch" : " normal") + " " + p.made_by + " " + library_origin(index, p.name)), terms);
}
// ---- the LIBRARY in the studio's own look: its head as the MARKET's (LIBRARY on torn blue paper, a line in marker, the
// shop's bar), the list of playlists on grip tape (the playlists on cream paper, the one open on the studio's blue, taped
// down), and everything there is to read on dark sheets torn at their edges with rows as the SETTINGS page has them,
// white words in the MARKET's type (Anton, its body face, its spaced capitals), the studio's blue for what is chosen ----
// The ink: words, quieter words, the quietest, the studio's blue as it reads on the dark, a warning, a fault.
constexpr ImU32 lib_ink = white, lib_ink_soft = soft, lib_ink_faint = faint, lib_blue = blue_light, lib_warn = IM_COL32(244, 193, 99, 255),
                lib_red = IM_COL32(255, 159, 151, 255);
// A row, as the SETTINGS page's: dark grey, the studio's blue for one that is in, a fault's red.
constexpr ImU32 lib_row = IM_COL32(30, 31, 35, 246), lib_row_in = IM_COL32(17, 57, 91, 245), lib_row_bad = IM_COL32(66, 31, 33, 245);
// A button as the shop's: blue, outlined, cream paper or black (a red one stays the studio's own, for deleting).
bool lib_button(ImDrawList* d, const Fonts& f, const char* id, ImVec2 a, ImVec2 size, const std::string& label, Look look = Look::dark,
                bool enabled = true) {
    if (look == Look::danger) return action(d, f, id, a, size, label, look, enabled);
    const ShopLook shop = look == Look::blue ? ShopLook::blue : look == Look::paper ? ShopLook::paper : look == Look::dark ? ShopLook::black : ShopLook::line;
    return shop_button(d, f, id, a, size, label, shop, enabled);
}
// A sheet to read on: near black, torn at its edges (a little of the grip tape's grain, its light torn rim), its shadow
// under it. (Cream paper read too bright over a whole page, and the grip tape itself too busy under words.)
void library_sheet(ImDrawList* d, const Fonts& f, ImVec2 a, ImVec2 b) {
    const float k = f.k, w = b.x - a.x, h = b.y - a.y;
    if (w < 8 * k || h < 8 * k) return;
    const std::uint32_t seed = seed_of(a) + 11;
    const auto shape = torn::piece(w, h, seed, 2.2f * k, 6 * k, 9 * k);
    draw_paper(d, f, shape, a, 0, ImVec2(0, 0), PaperKind::grip, seed, 0.45f, ImVec2(6, 8), 2.0f, IM_COL32(112, 114, 120, 255));
}
// A row: as the SETTINGS page's.
void lib_row_box(ImDrawList* d, ImVec2 a, ImVec2 b, ImU32 fill, float k) {
    flat_rect(d, a, b, fill, seed_of(a), k * 0.45f);
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
// Spaced capitals cut short with "..." to fit `width` (elide measures without the spacing, which runs longer).
std::string lib_caps_fit(const Fonts& f, float size, std::string words, float width, float spacing = 1.6f) {
    ImFont* font = mont(f, size);
    if (spaced_width(font, size, words, spacing * f.k) <= width) return words;
    while (!words.empty() && spaced_width(font, size, words + "...", spacing * f.k) > width) words.pop_back();
    return words + "...";
}
// "Do this next" round the button the setup guide is on: a blue outline that breathes, so the step is found on the page
// and not only named in the strip.
void lib_pulse(ImDrawList* d, ImVec2 a, ImVec2 b, float k) {
    const float t = static_cast<float>(ImGui::GetTime());
    const float grow = (2.0f + 3.0f * (0.5f + 0.5f * std::sin(t * 4.2f))) * k;
    const auto alpha = static_cast<int>(150 + 90 * (0.5f + 0.5f * std::sin(t * 4.2f)));
    d->AddRect(ImVec2(a.x - grow, a.y - grow), ImVec2(b.x + grow, b.y + grow), IM_COL32(90, 180, 255, alpha), 0, 0, 2.6f * k);
}

// One click: a playlist with every pack and every trick of his own in it, playing in game by default.
void library_use_everything(const studio::View& v) {
    std::vector<std::string> names;
    for (const auto& list : v.playlists) names.push_back(list.name);
    const auto name = studio::free_playlist_name_like(names, "Everything");
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
    const auto& index = library_index(v);
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

// ---- "add it to a playlist?": after a pack is got in the MARKET, and on the SAVED card after a new trick ----
// A new player got a pack and then found nothing of it played: getting it and linking it to what plays were two jobs on
// two pages. So the moment one arrives the studio asks, with the likely answer first.
std::vector<std::string> library_playlist_names(const studio::View& v) {
    std::vector<std::string> names;
    names.reserve(v.playlists.size());
    for (const auto& list : v.playlists) names.push_back(list.name);
    return names;
}
// What ADD does for `entry`, in words for its button ("ADD TO EVERYDAY", "ADD AND PLAY IT").
std::string offer_add_label(const studio::View& v) {
    const auto names = library_playlist_names(v);
    const auto target = studio::offer_target(names, v.playlist_playing, v.playlist_open);
    return target.make ? "PLAY IT IN GAME" : target.play ? "ADD TO " + upper(target.name) + " + PLAY IT" : "ADD TO " + upper(target.name);
}
// ADD: into the playlist that plays by default; with none playing, the open one (or a new one) made to play.
void offer_add(const studio::View& v, const studio::PlaylistEntry& entry) {
    const auto names = library_playlist_names(v);
    const auto target = studio::offer_target(names, v.playlist_playing, v.playlist_open, studio::free_playlist_name_like(names, "My tricks"));
    std::string m;
    if (target.make && !studio::new_playlist(target.name, m)) {
        say(m);
        return;
    }
    const auto name = studio::add_to_playlist(target.name, entry, m);
    if (name.empty()) {
        say(m);
        return;
    }
    if (target.play) {
        std::string pm;
        studio::play_playlist(name, pm);
        studio::set_play_in_game(true);
        m += " It plays by default now.";
    } else if (!v.play_in_game) {
        m += " Playback is paused: switch PLAYBACK on in the LIBRARY.";
    }
    say(m);
}
// NEW PLAYLIST... typed and made: the new one, with `entry` in it (and playing by default when nothing else did).
void offer_new_playlist(const studio::View& v, const studio::PlaylistEntry& entry, const std::string& wanted) {
    std::string m;
    if (!studio::new_playlist(wanted, m)) {
        say(m);
        return;
    }
    const auto name = studio::view().playlist_open;
    if (studio::add_to_playlist(name, entry, m).empty()) {
        say(m);
        return;
    }
    if (v.playlist_playing.empty()) {
        std::string pm;
        studio::play_playlist(name, pm);
        studio::set_play_in_game(true);
        m += " It plays by default now, as nothing else did.";
    }
    say(m);
}
void library_choose_playlists(const studio::PlaylistEntry& source, const std::string& name);  // studio_library_widgets.inl
// The question itself: what it is, where ADD puts it, and the four answers. Drawn on the "ADD IT TO A PLAYLIST?" card and
// on the SAVED card. `name_for`: the name NEW PLAYLIST... suggests. Returns how tall it came out; `answered` is set when
// a button closed the question.
float offer_block(ImDrawList* d, const Fonts& f, const studio::View& v, const studio::PlaylistEntry& entry, const std::string& name_for,
                  ImVec2 at, float w, bool& answered) {
    auto& u = ui();
    const float k = f.k;
    float y = at.y;
    const bool whole = entry.animation.empty();
    ImGui::PushID("offer");
    if (u.offer_naming) {
        text(d, body(f, 14 * k), 14 * k, {at.x, y}, lib_ink_soft, "Name the new playlist:");
        y += 26 * k;
        const bool entered = field(f, "##offername", u.offer_name, {at.x, y}, w - 300 * k, "e.g. Everyday, Big flips...");
        const bool named = std::any_of(u.offer_name.begin(), u.offer_name.end(), [](char c) { return c != 0 && c != ' '; });
        if ((action(d, f, "##offercreate", {at.x + w - 290 * k, y}, {180 * k, 34 * k}, "CREATE + ADD", Look::blue, named) || entered) && named) {
            offer_new_playlist(v, entry, u.offer_name.data());
            u.offer_naming = false;
            answered = true;
        }
        if (action(d, f, "##offerbacknew", {at.x + w - 100 * k, y}, {100 * k, 34 * k}, "BACK", Look::ghost)) u.offer_naming = false;
        y += 44 * k;
        ImGui::PopID();
        return y - at.y;
    }
    const float gap = 10 * k, bh = 42 * k;
    const float main_w = std::min(w * 0.44f, 380 * k), rest = (w - main_w - 3 * gap) / 3;
    if (action(d, f, "##offeradd", {at.x, y}, {main_w, bh}, offer_add_label(v), Look::blue)) {
        offer_add(v, entry);
        answered = true;
    }
    float x = at.x + main_w + gap;
    if (action(d, f, "##offerother", {x, y}, {rest, bh}, "OTHER PLAYLIST...", Look::dark, !v.playlists.empty())) {
        library_choose_playlists(entry, whole ? entry.pack : entry.animation);
        answered = true;
    }
    x += rest + gap;
    if (action(d, f, "##offernew", {x, y}, {rest, bh}, "NEW PLAYLIST...", Look::dark)) {
        u.offer_naming = true;
        std::snprintf(u.offer_name.data(), u.offer_name.size(), "%s", name_for.c_str());
    }
    x += rest + gap;
    if (action(d, f, "##offerlater", {x, y}, {rest, bh}, "NOT NOW", Look::ghost)) answered = true;
    y += bh + 10 * k;
    const auto names = library_playlist_names(v);
    const auto target = studio::offer_target(names, v.playlist_playing, v.playlist_open);
    const std::string where = target.make ? "Nothing of yours plays yet: ADD makes a playlist, puts it in and plays it by default."
                              : target.play ? "No playlist plays by default yet: ADD puts it in " + target.name + " and plays that one."
                                            : "ADD puts it in " + target.name + ", the playlist that plays by default." +
                                                  (v.play_in_game ? "" : " (Playback is paused.)");
    y += paragraph(d, body(f, 13 * k), 13 * k, {at.x, y}, w, lib_ink_faint, where);
    ImGui::PopID();
    return y - at.y;
}
// The packs got since the last frame: asked about one at a time, unless one is linked already (an UPDATE of a pack that is
// in a playlist keeps its place and is not asked about again).
void library_offers(const studio::View& v) {
    auto& u = ui();
    for (auto& pack : studio::market::take_fresh_installs())
        if (std::find(u.offer_packs.begin(), u.offer_packs.end(), pack) == u.offer_packs.end() && pack != u.offer_pack)
            u.offer_packs.push_back(std::move(pack));
    if (!u.offer_pack.empty() || u.offer_packs.empty()) return;
    const auto next = u.offer_packs.front();
    // Read into the library on the market's thread a moment ago; not in this frame's view yet: asked about next frame.
    if (!pack_named(v, next)) {
        static int waited = 0;
        if (++waited < 600) return;
        waited = 0;
    } else if (studio::playlists_with_pack(next).empty()) {
        u.offer_pack = next;
        u.offer_naming = false;
    }
    u.offer_packs.erase(u.offer_packs.begin());
}
std::pair<ImVec2, ImVec2> card_box(ImDrawList* d, const Fonts& f, ImVec2 display, float w, float h, const std::string& title, ImU32 tape); // below
void offer_card(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 display) {
    auto& u = ui();
    const float k = f.k;
    const auto* pack = pack_named(v, u.offer_pack);
    if (!pack) {
        u.offer_pack.clear();
        return;
    }
    const auto [a, b] = card_box(d, f, display, 940 * k, 420 * k, "ADD IT TO A PLAYLIST?", blue);
    const float x = a.x + 30 * k, w = b.x - a.x - 60 * k;
    float y = a.y + 92 * k;
    const float cover = 96 * k;
    pack_cover(d, f, *pack, {x, y}, {x + cover, y + cover});
    lib_caps(d, f, {x + cover + 20 * k, y + 4 * k}, 11 * k, lib_blue, "JUST GOT IN THE MARKET");
    text(d, anton(f, 30 * k), 30 * k, {x + cover + 20 * k, y + 24 * k}, white, elide(anton(f, 30 * k), 30 * k, upper(pack->name), w - cover - 20 * k));
    text(d, body(f, 14 * k), 14 * k, {x + cover + 20 * k, y + 66 * k}, soft,
         elide(body(f, 14 * k), 14 * k, std::to_string(pack->members.size()) + (pack->members.size() == 1 ? " trick" : " tricks") +
                                              (pack->made_by.empty() ? "" : " / by " + pack->made_by) + " / it plays once it is in a playlist that plays",
               w - cover - 20 * k));
    y += cover + 26 * k;
    bool answered = false;
    offer_block(d, f, v, {pack->name, ""}, pack->name, {x, y}, w, answered);
    if (answered) u.offer_pack.clear();
}
#include "studio_library_widgets.inl"
#include "studio_library_browser.inl"
#include "studio_library_playlists.inl"
