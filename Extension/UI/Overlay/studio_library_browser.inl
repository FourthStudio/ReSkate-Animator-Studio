void library_open_pack(studio::LibraryBrowseState& browse, const std::string& name) {
    if (browse.pack != name) {
        browse.pack_list_scroll = browse.scroll; browse.pack = name; browse.scroll = 0;
        browse.expanded.clear();
    }
}
// What a browser shows for its search: the packs, or the tricks in groups by the game's trick. Searching every trick's
// words each frame took the most time in the LIBRARY with a big collection, so the result is kept until the search, the
// source, the pack or the library itself (library_fingerprint) changes.
struct LibraryFound {
    std::uint64_t key{};
    bool built{};
    int source{-1};
    std::string query, pack;
    std::vector<std::size_t> packs;                                          // places in View::packs, favorites first
    std::vector<std::pair<std::string, std::vector<std::size_t>>> groups;   // a trick's name -> places in View::presets
};
const LibraryFound& library_found(const studio::View& v, const LibraryIndex& index, const studio::LibraryBrowseState& browse,
                                  const std::vector<std::string>& terms, const studio::View::PackView* pack) {
    static LibraryFound found;
    if (found.built && found.key == index.key && found.source == browse.source && found.query == browse.query.data() &&
        found.pack == (pack ? pack->name : std::string{}))
        return found;
    found = {};
    found.built = true;
    found.key = index.key;
    found.source = browse.source;
    found.query = browse.query.data();
    found.pack = pack ? pack->name : std::string{};
    if (browse.source == 0 && !pack) {
        for (std::size_t i = 0; i < v.packs.size(); ++i) {
            const auto& p = v.packs[i];
            bool match = terms.empty() || studio::native_search_matches(studio::native_search_key(p.name + " " + p.made_by), terms);
            if (!match) for (const auto& member : p.members)
                if (const auto* saved = index.preset(member); saved && library_search(index, *saved, terms)) { match = true; break; }
            if (match) found.packs.push_back(i);
        }
        std::stable_sort(found.packs.begin(), found.packs.end(), [&](std::size_t l, std::size_t r) { return v.packs[l].favorite && !v.packs[r].favorite; });
        return found;
    }
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < v.presets.size(); ++i) {
        const auto& p = v.presets[i];
        if (browse.source == 1 && (p.foreign || p.locked || index.imported.contains(p.name))) continue;
        if (browse.source == 0 && (!pack || !pack->pack.has(p.name))) continue;
        if (library_search(index, p, terms)) groups[index.label(p.trick)].push_back(i);
    }
    for (auto& [name, members] : groups) found.groups.emplace_back(name, std::move(members));
    return found;
}
void library_browser(ImDrawList* d, const Fonts& f, const studio::View& v, const LibraryIndex& index,
                     const std::string& target, ImVec2 a, ImVec2 b) {
    auto& route = ui().library; auto& browse = route.browse(); const float k = f.k;
    const float x = a.x + 16 * k, w = b.x - a.x - 32 * k;
    library_sheet(d, f, a, b);
    ImGui::PushID(target.empty() ? "collection" : "picker");
    const int source = segmented(d, f, "##librarysource", {x, a.y + 16 * k}, w, 38 * k,
        {"PACKS", "YOUR TRICKS", "ALL TRICKS"}, browse.source);
    if (source >= 0 && source != browse.source) {
        browse.source = source; browse.pack.clear(); browse.expanded.clear(); browse.scroll = 0;
    }
    field(f, "##librarysearch", browse.query, {x, a.y + 70 * k}, w - 84 * k, "Search tricks, packs or makers...");
    if (lib_button(d, f, "##clearlibrarysearch", {b.x - 90 * k, a.y + 70 * k}, {74 * k, 34 * k}, "CLEAR", Look::ghost, browse.query[0] != 0)) browse.query.fill(0);
    const auto terms = studio::native_search_terms(browse.query.data());
    const bool query_changed = browse.seen_query != browse.query.data();
    if (query_changed) { browse.seen_query = browse.query.data(); browse.scroll = 0; browse.expanded.clear(); }
    const auto* pack = pack_named(v, browse.pack);
    float y = a.y + 120 * k;
    if (browse.source == 0 && pack) {
        if (lib_button(d, f, "##librarypackback", {x, y}, {116 * k, 32 * k}, "< ALL PACKS", Look::ghost)) {
            browse.pack.clear(); browse.scroll = browse.pack_list_scroll;
        }
        text(d, anton(f, 23 * k), 23 * k, {x + 132 * k, y + 3 * k}, lib_ink, elide(anton(f, 23 * k), 23 * k, pack->name, w - 470 * k));
        if (target.empty() && lib_button(d, f, "##librarypacksettings", {b.x - 296 * k, y}, {130 * k, 32 * k}, "MANAGE PACK", Look::ghost))
            open_pack_page(pack->name);
        if (lib_button(d, f, "##librarypackall", {b.x - 158 * k, y}, {142 * k, 32 * k}, target.empty() ? "ADD TO PLAYLIST..." : "ADD WHOLE PACK", Look::blue)) {
            if (target.empty()) library_choose_playlists({pack->name, ""}, pack->name);
            else { std::string m; studio::edit_playlist_pack(target, pack->name, true, m); say(m); }
        }
        y += 46 * k;
    }
    if (browse.source != 0 || pack) {
        text(d, body(f, 12 * k), 12 * k, {x, y + 8 * k}, lib_ink_faint, elide(f.bold, 12 * k,
            target.empty() ? "Open a group to preview a trick, edit it or put it in a playlist." : "+ ADD or REMOVE any trick. Every change is saved at once.", w - 272 * k));
        const float bw = 124 * k;
        if (lib_button(d, f, "##expandall", {b.x - 16 * k - 2 * bw - 8 * k, y}, {bw, 28 * k}, "OPEN ALL", Look::ghost)) browse.expand_all = true;
        if (lib_button(d, f, "##collapseall", {b.x - 16 * k - bw, y}, {bw, 28 * k}, "CLOSE ALL", Look::ghost)) browse.expanded.clear();
        y += 42 * k;
    }
    const ImVec2 la(x, y), lb(b.x - 16 * k, b.y - 14 * k);
    begin_region(203, la, lb, browse.scroll, k);
    y = la.y - browse.scroll;
    const auto& found = library_found(v, index, browse, terms, pack);
    // Only what can be seen is drawn; what is scrolled away still takes its place for the pad (begin_region scrolls to it).
    const auto seen = [&](float top, float bottom) { return bottom > la.y && top < lb.y; };
    if (browse.source == 0 && !pack) {
        if (found.packs.empty()) {
            y += paragraph(d, anton(f, 25 * k), 25 * k, {x + 12 * k, y + 14 * k}, w - 24 * k, lib_ink, terms.empty() ? "YOUR PACK COLLECTION STARTS HERE" : "NO PACKS MATCH THIS SEARCH") + 30 * k;
            y += paragraph(d, body(f, 15 * k), 15 * k, {x + 12 * k, y}, w - 24 * k, lib_ink_soft,
                terms.empty() ? "Get trick packs from the MARKET, or use YOUR TRICKS to find your own saves." : "Try another name, or search ALL TRICKS for single tricks.") + 22 * k;
            if (terms.empty() && lib_button(d, f, "##librarymarket", {x + 12 * k, y}, {210 * k, 38 * k}, "BROWSE THE MARKET", Look::blue)) open_menu(2);
            y += 58 * k;
        }
        for (const auto at : found.packs) {
            const auto* p = &v.packs[at];
            ImGui::PushID(p->name.c_str());
            const ImVec2 ca(x, y), cb(lb.x - 6 * k, y + 142 * k);
            const bool shown = seen(ca.y, cb.y);
            const auto button = [&](const char* id, ImVec2 ba, ImVec2 size, const std::string& label, Look look, bool enabled = true) {
                return shown ? lib_button(d, f, id, ba, size, label, look, enabled) : hit(id, ba, {ba.x + size.x, ba.y + size.y}, nullptr, enabled);
            };
            const ImVec2 cover_a(ca.x + 9 * k, ca.y + 9 * k), cover_b(ca.x + 133 * k, cb.y - 9 * k);
            const float tx = ca.x + 151 * k, tw = cb.x - tx - 16 * k;
            if (shown) {
                lib_row_box(d, ca, cb, lib_row, k);
                pack_cover(d, f, *p, cover_a, cover_b);
            }
            const bool cover_pressed = hit("##browsecover", cover_a, cover_b);
            const bool title_pressed = hit("##browsetitle", {tx, ca.y + 8 * k}, {cb.x - 126 * k, ca.y + 44 * k});
            if (button("##packfavorite", {cb.x - 116 * k, ca.y + 9 * k}, {102 * k, 28 * k}, p->favorite ? "FAVORITE" : "+ FAVORITE", p->favorite ? Look::blue : Look::ghost))
                studio::set_pack_favorite(p->name, !p->favorite);
            std::size_t have = 0;
            if (!target.empty()) for (const auto& member : p->members) have += index.member(member).included();
            const auto whole = std::find_if(v.playlist.begin(), v.playlist.end(), [&](const auto& e) { return e.pack == p->name && e.animation.empty(); });
            const bool full = whole != v.playlist.end() && whole->omitted.empty();
            if (shown) {
                text(d, anton(f, 24 * k), 24 * k, {tx, ca.y + 10 * k}, lib_ink, elide(anton(f, 24 * k), 24 * k, p->name, tw - 120 * k));
                const auto detail = std::to_string(p->members.size()) + (p->members.size() == 1 ? " trick" : " tricks") +
                    (p->made_by.empty() ? "" : " / by " + p->made_by) +
                    (target.empty() ? "" : full ? " / all in this playlist" : have ? " / " + std::to_string(have) + " in this playlist" : "");
                text(d, body(f, 13 * k), 13 * k, {tx, ca.y + 45 * k}, full ? lib_blue : lib_ink_soft, elide(body(f, 13 * k), 13 * k, detail, tw));
            }
            if (target.empty()) {
                // The collection: look inside, link it to playlists, or edit the pack itself.
                const float unit = std::min(240 * k, (tw - 16 * k) / 3);
                if (button("##browsepack", {tx, ca.y + 76 * k}, {unit, 36 * k}, "OPEN PACK", Look::paper) || cover_pressed || title_pressed)
                    library_open_pack(browse, p->name);
                if (button("##addwholepack", {tx + unit + 8 * k, ca.y + 76 * k}, {unit, 36 * k}, "ADD TO PLAYLIST...", Look::ghost))
                    library_choose_playlists({p->name, ""}, p->name);
                if (button("##managepack", {tx + 2 * (unit + 8 * k), ca.y + 76 * k}, {unit, 36 * k}, "MANAGE PACK", Look::ghost)) open_pack_page(p->name);
            } else {
                // Adding to a playlist: the whole pack in one press, or into it to pick single tricks.
                const float unit = std::min(260 * k, (tw - 8 * k) / 2);
                const std::string add = full ? "REMOVE PACK" : have ? "ADD THE REST" : "ADD WHOLE PACK";
                if (button("##addwholepack", {tx, ca.y + 76 * k}, {unit, 36 * k}, add, full ? Look::ghost : Look::blue)) {
                    std::string m; studio::edit_playlist_pack(target, p->name, !full, m); say(m);
                }
                if (button("##browsepack", {tx + unit + 8 * k, ca.y + 76 * k}, {unit, 36 * k}, "PICK SINGLE TRICKS", Look::paper) || cover_pressed || title_pressed)
                    library_open_pack(browse, p->name);
            }
            if (shown)
                text(d, body(f, 11 * k), 11 * k, {tx, ca.y + 120 * k}, lib_ink_faint,
                     elide(body(f, 11 * k), 11 * k, "A whole pack follows the pack: tricks its maker adds later play too.", tw));
            ImGui::PopID(); y += 154 * k;
        }
    } else {
        if ((query_changed && !terms.empty()) || browse.expand_all)
            for (const auto& [name, members] : found.groups) { (void)members; browse.expanded.insert(name); }
        browse.expand_all = false;
        if (found.groups.empty()) y += paragraph(d, body(f, 16 * k), 16 * k, {x + 10 * k, y + 14 * k}, w - 20 * k, lib_ink_soft,
            terms.empty() ? "No saved tricks here yet. Save a trick in TRICKS, or look in your packs." : "No tricks match. Try another search or CLEAR it.") + 30 * k;
        for (const auto& [name, members] : found.groups) {
            ImGui::PushID(name.c_str());
            const bool expanded = browse.expanded.contains(name);
            const std::string label = name + " / " + std::to_string(members.size()) + (members.size() == 1 ? " version / " : " versions / ") + (expanded ? "HIDE" : "SHOW");
            const bool pressed = seen(y, y + 42 * k) ? lib_button(d, f, "##group", {x, y}, {w - 6 * k, 42 * k}, label, Look::dark)
                                                     : hit("##group", {x, y}, {x + w - 6 * k, y + 42 * k});
            if (pressed && !browse.expanded.erase(name)) browse.expanded.insert(name);
            y += 50 * k;
            if (expanded) for (const auto at : members) {
                const auto* p = &v.presets[at];
                std::string from = browse.source == 0 && pack ? pack->name : std::string{};
                if (from.empty() && (p->foreign || index.imported.contains(p->name)))
                    if (const auto origin = index.origins.find(p->name); origin != index.origins.end() && !origin->second.empty()) from = origin->second.front();
                library_member_row(d, f, v, index, *p, from, target, false, {x + 4 * k, y}, w - 10 * k);
                y += 90 * k;
            }
            ImGui::PopID();
        }
    }
    end_region(d, la, lb, y + browse.scroll - la.y, browse.scroll, k);
    ImGui::PopID();
}
