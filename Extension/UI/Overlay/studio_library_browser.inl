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
    // One column of at most 1640 units in the middle of the sheet: on 21:9 the rows ran some 2400 units, the name at one
    // end and its buttons at the other.
    const float w = std::min(b.x - a.x - 32 * k, 1640 * k), x = (a.x + b.x - w) * 0.5f, r = x + w;
    library_sheet(d, f, a, b);
    ImGui::PushID(target.empty() ? "collection" : "picker");
    const int source = segmented(d, f, "##librarysource", {x, a.y + 16 * k}, w, 38 * k,
        {"PACKS", "YOUR TRICKS", "ALL TRICKS"}, browse.source);
    if (source >= 0 && source != browse.source) {
        browse.source = source; browse.pack.clear(); browse.expanded.clear(); browse.scroll = 0;
    }
    const float clear_w = lib_w(f, "CLEAR", 34 * k, 74 * k);
    field(f, "##librarysearch", browse.query, {x, a.y + 70 * k}, w - clear_w - 10 * k, "Search tricks, packs or makers...");
    if (lib_button(d, f, "##clearlibrarysearch", {r - clear_w, a.y + 70 * k}, {clear_w, 34 * k}, "CLEAR", Look::ghost, browse.query[0] != 0)) browse.query.fill(0);
    const auto terms = studio::native_search_terms(browse.query.data());
    const bool query_changed = browse.seen_query != browse.query.data();
    if (query_changed) { browse.seen_query = browse.query.data(); browse.scroll = 0; browse.expanded.clear(); }
    const auto* pack = pack_named(v, browse.pack);
    float y = a.y + 120 * k;
    if (browse.source == 0 && pack) {
        const float back_w = lib_w(f, "< ALL PACKS", 32 * k, 116 * k);
        if (lib_button(d, f, "##librarypackback", {x, y}, {back_w, 32 * k}, "< ALL PACKS", Look::ghost)) {
            browse.pack.clear(); browse.scroll = browse.pack_list_scroll;
        }
        const char* all_label = target.empty() ? "ADD TO PLAYLIST..." : "ADD WHOLE PACK";
        const float all_w = lib_w(f, all_label, 32 * k, 142 * k), manage_w = target.empty() ? lib_w(f, "MANAGE PACK", 32 * k, 130 * k) : 0;
        text(d, anton(f, 23 * k), 23 * k, {x + back_w + 16 * k, y + 3 * k}, lib_ink,
             elide(anton(f, 23 * k), 23 * k, pack->name, w - back_w - all_w - manage_w - 50 * k));
        if (target.empty() && lib_button(d, f, "##librarypacksettings", {r - all_w - 8 * k - manage_w, y}, {manage_w, 32 * k}, "MANAGE PACK", Look::ghost))
            open_pack_page(pack->name);
        if (lib_button(d, f, "##librarypackall", {r - all_w, y}, {all_w, 32 * k}, all_label, Look::blue)) {
            if (target.empty()) library_choose_playlists({pack->name, ""}, pack->name);
            else { std::string m; studio::edit_playlist_pack(target, pack->name, true, m); say(m); }
        }
        y += 46 * k;
    }
    if (browse.source != 0 || pack) {
        text(d, body(f, 12 * k), 12 * k, {x, y + 8 * k}, lib_ink_faint, elide(body(f, 12 * k), 12 * k,
            target.empty() ? "Open a group to preview a trick, edit it or put it in a playlist." : "+ ADD or REMOVE any trick. Every change is saved at once.", w - 272 * k));
        const float bw = std::max(lib_w(f, "OPEN ALL", 28 * k, 124 * k), lib_w(f, "CLOSE ALL", 28 * k, 124 * k));
        if (lib_button(d, f, "##expandall", {r - 2 * bw - 8 * k, y}, {bw, 28 * k}, "OPEN ALL", Look::ghost)) browse.expand_all = true;
        if (lib_button(d, f, "##collapseall", {r - bw, y}, {bw, 28 * k}, "CLOSE ALL", Look::ghost)) browse.expanded.clear();
        y += 42 * k;
    }
    const ImVec2 la(x, y), lb(r, b.y - 14 * k);
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
            if (terms.empty() && lib_button(d, f, "##librarymarket", {x + 12 * k, y}, {lib_w(f, "BROWSE THE MARKET", 38 * k, 210 * k), 38 * k}, "BROWSE THE MARKET", Look::blue))
                open_menu(2);
            y += 58 * k;
        }
        // Packs side by side, two to a row on a 16:9 page (one long row each left most of it empty), each at least 740 wide.
        const float pack_gap = 14 * k, list_w = lb.x - 6 * k - x;
        const int columns = std::max(1, static_cast<int>((list_w + pack_gap) / (740 * k + pack_gap)));
        const float card_w = (list_w - pack_gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        int column = 0;
        for (const auto at : found.packs) {
            const auto* p = &v.packs[at];
            ImGui::PushID(p->name.c_str());
            const float cx = x + static_cast<float>(column) * (card_w + pack_gap);
            const ImVec2 ca(cx, y), cb(cx + card_w, y + 142 * k);
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
            const float fav_w = lib_w(f, "+ FAVORITE", 28 * k, 102 * k);
            const bool title_pressed = hit("##browsetitle", {tx, ca.y + 8 * k}, {cb.x - fav_w - 24 * k, ca.y + 44 * k});
            if (button("##packfavorite", {cb.x - 14 * k - fav_w, ca.y + 9 * k}, {fav_w, 28 * k}, p->favorite ? "FAVORITE" : "+ FAVORITE", p->favorite ? Look::blue : Look::ghost))
                studio::set_pack_favorite(p->name, !p->favorite);
            std::size_t have = 0;
            if (!target.empty()) for (const auto& member : p->members) have += index.member(member).included();
            const auto whole = std::find_if(v.playlist.begin(), v.playlist.end(), [&](const auto& e) { return e.pack == p->name && e.animation.empty(); });
            const bool full = whole != v.playlist.end() && whole->omitted.empty();
            if (shown) {
                text(d, anton(f, 24 * k), 24 * k, {tx, ca.y + 10 * k}, lib_ink, elide(anton(f, 24 * k), 24 * k, p->name, tw - fav_w - 16 * k));
                const auto detail = std::to_string(p->members.size()) + (p->members.size() == 1 ? " trick" : " tricks") +
                    (p->made_by.empty() ? "" : " / by " + p->made_by) +
                    (target.empty() ? "" : full ? " / all in this playlist" : have ? " / " + std::to_string(have) + " in this playlist" : "");
                text(d, body(f, 13 * k), 13 * k, {tx, ca.y + 45 * k}, full ? lib_blue : lib_ink_soft, elide(body(f, 13 * k), 13 * k, detail, tw));
            }
            if (target.empty()) {
                // The collection: look inside, link it to playlists, or edit the pack itself.
                const float open_w = lib_w(f, "OPEN PACK", 36 * k), add_w = lib_w(f, "ADD TO PLAYLIST...", 36 * k), manage_w = lib_w(f, "MANAGE PACK", 36 * k);
                const float spare = std::max(0.0f, (tw - open_w - add_w - manage_w - 16 * k) / 3), unit = std::min(spare, 60 * k);
                float bx = tx;
                if (button("##browsepack", {bx, ca.y + 76 * k}, {open_w + unit, 36 * k}, "OPEN PACK", Look::paper) || cover_pressed || title_pressed)
                    library_open_pack(browse, p->name);
                bx += open_w + unit + 8 * k;
                if (button("##addwholepack", {bx, ca.y + 76 * k}, {add_w + unit, 36 * k}, "ADD TO PLAYLIST...", Look::ghost))
                    library_choose_playlists({p->name, ""}, p->name);
                bx += add_w + unit + 8 * k;
                if (button("##managepack", {bx, ca.y + 76 * k}, {manage_w + unit, 36 * k}, "MANAGE PACK", Look::ghost)) open_pack_page(p->name);
            } else {
                // Adding to a playlist: the whole pack in one press, or into it to pick single tricks.
                const std::string add = full ? "REMOVE PACK" : have ? "ADD THE REST" : "ADD WHOLE PACK";
                const float unit = std::clamp((tw - 8 * k) / 2, std::max(lib_w(f, "ADD WHOLE PACK", 36 * k), lib_w(f, "PICK SINGLE TRICKS", 36 * k)), 300 * k);
                if (button("##addwholepack", {tx, ca.y + 76 * k}, {unit, 36 * k}, add, full ? Look::ghost : Look::blue)) {
                    std::string m; studio::edit_playlist_pack(target, p->name, !full, m); say(m);
                }
                if (button("##browsepack", {tx + unit + 8 * k, ca.y + 76 * k}, {unit, 36 * k}, "PICK SINGLE TRICKS", Look::paper) || cover_pressed || title_pressed)
                    library_open_pack(browse, p->name);
            }
            if (shown)
                text(d, body(f, 11 * k), 11 * k, {tx, ca.y + 120 * k}, lib_ink_faint,
                     elide(body(f, 11 * k), 11 * k, "A whole pack follows the pack: tricks its maker adds later play too.", tw));
            ImGui::PopID();
            if (++column == columns) column = 0, y += 154 * k;
        }
        if (column) y += 154 * k;
    } else {
        if ((query_changed && !terms.empty()) || browse.expand_all)
            for (const auto& [name, members] : found.groups) { (void)members; browse.expanded.insert(name); }
        browse.expand_all = false;
        if (found.groups.empty()) y += paragraph(d, body(f, 16 * k), 16 * k, {x + 10 * k, y + 14 * k}, w - 20 * k, lib_ink_soft,
            terms.empty() ? "No saved tricks here yet. Save a trick in TRICKS, or look in your packs." : "No tricks match. Try another search or CLEAR it.") + 30 * k;
        for (const auto& [name, members] : found.groups) {
            ImGui::PushID(name.c_str());
            const bool expanded = browse.expanded.contains(name);
            // A trick's row: its name at the left with an arrow that says open or closed, how many versions at the right
            // (and how many are in the playlist being added to); the whole row opens and closes it.
            const ImVec2 ga(x, y), gb(x + w - 6 * k, y + 42 * k);
            bool lit = false;
            const bool pressed = hit("##group", ga, gb, &lit);
            if (pressed && !browse.expanded.erase(name)) browse.expanded.insert(name);
            if (seen(ga.y, gb.y)) {
                d->AddRectFilled(ga, gb, lit ? IM_COL32(44, 46, 52, 250) : expanded ? IM_COL32(30, 31, 35, 246) : IM_COL32(22, 23, 26, 240));
                d->AddRect(ga, gb, IM_COL32(255, 255, 255, lit ? 120 : 52), 0, 0, 1 * k);
                if (lit && nav().keys) chosen_outline(d, ga, gb, k);
                const ImVec2 c(ga.x + 20 * k, (ga.y + gb.y) * 0.5f);
                if (expanded) d->AddTriangleFilled({c.x - 6 * k, c.y - 3 * k}, {c.x + 6 * k, c.y - 3 * k}, {c.x, c.y + 4 * k}, lib_blue);
                else d->AddTriangleFilled({c.x - 3 * k, c.y - 6 * k}, {c.x - 3 * k, c.y + 6 * k}, {c.x + 4 * k, c.y}, lib_ink_soft);
                std::size_t in = 0;
                if (!target.empty()) for (const auto at : members) in += index.member(v.presets[at].name).included();
                const std::string count = std::to_string(members.size()) + (members.size() == 1 ? " VERSION" : " VERSIONS") +
                                          (in ? " / " + std::to_string(in) + " IN IT" : "");
                const float cw = spaced_width(mont(f, 11 * k), 11 * k, count, 1.4f * k);
                lib_caps(d, f, {gb.x - 16 * k - cw, ga.y + 15 * k}, 11 * k, in ? lib_blue : lib_ink_soft, count, 1.4f);
                text(d, anton(f, 20 * k), 20 * k, {ga.x + 38 * k, ga.y + 9 * k}, lib_ink,
                     elide(anton(f, 20 * k), 20 * k, upper(name), gb.x - ga.x - cw - 72 * k));
            }
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
