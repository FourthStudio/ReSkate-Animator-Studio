void library_open_pack(studio::LibraryBrowseState& browse, const std::string& name) {
    if (browse.pack != name) {
        browse.pack_list_scroll = browse.scroll; browse.pack = name; browse.scroll = 0;
        browse.expanded.clear();
    }
}
void library_browser(ImDrawList* d, const Fonts& f, const studio::View& v, const LibraryIndex& index,
                     const std::string& target, ImVec2 a, ImVec2 b) {
    auto& route = ui().library; auto& browse = route.browse(); const float k = f.k;
    const float x = a.x + 16 * k, w = b.x - a.x - 32 * k;
    library_panel_box(d, f, a, b);
    ImGui::PushID(target.empty() ? "collection" : "picker");
    const int source = segmented(d, f, "##librarysource", {x, a.y + 16 * k}, w, 38 * k,
        {"PACKS", "YOUR ANIMATIONS", "ALL ANIMATIONS"}, browse.source);
    if (source >= 0 && source != browse.source) {
        browse.source = source; browse.pack.clear(); browse.expanded.clear(); browse.scroll = 0;
    }
    field(f, "##librarysearch", browse.query, {x, a.y + 70 * k}, w - 84 * k, "Search animations, tricks, packs or creators...");
    if (action(d, f, "##clearlibrarysearch", {b.x - 90 * k, a.y + 70 * k}, {74 * k, 34 * k}, "CLEAR", Look::ghost, browse.query[0] != 0)) browse.query.fill(0);
    const auto terms = studio::native_search_terms(browse.query.data());
    const bool query_changed = browse.seen_query != browse.query.data();
    if (query_changed) { browse.seen_query = browse.query.data(); browse.scroll = 0; browse.expanded.clear(); }
    const auto* pack = pack_named(v, browse.pack);
    float y = a.y + 120 * k;
    if (browse.source == 0 && pack) {
        if (action(d, f, "##librarypackback", {x, y}, {116 * k, 32 * k}, "< ALL PACKS", Look::ghost)) {
            browse.pack.clear(); browse.scroll = browse.pack_list_scroll;
        }
        text(d, f.heading, 23 * k, {x + 132 * k, y + 3 * k}, white, elide(f.heading, 23 * k, pack->name, w - 500 * k));
        if (action(d, f, "##librarypacksettings", {b.x - 276 * k, y}, {110 * k, 32 * k}, "MANAGE PACK", Look::ghost)) open_pack_page(pack->name);
        if (action(d, f, "##librarypackall", {b.x - 158 * k, y}, {142 * k, 32 * k}, target.empty() ? "ADD TO PLAYLIST..." : "ADD PACK", Look::blue)) {
            if (target.empty()) library_choose_playlists({pack->name, ""}, pack->name);
            else { std::string m; studio::edit_playlist_pack(target, pack->name, true, m); say(m); }
        }
        y += 46 * k;
    }
    if (browse.source != 0 || pack) {
        text(d, f.bold, 12 * k, {x, y + 8 * k}, faint, elide(f.bold, 12 * k,
            target.empty() ? "Open a group to preview, edit or add a variation to a playlist." : "Use + ADD or REMOVE on any variation. Changes are saved immediately.", w - 272 * k));
        const float bw = 124 * k;
        if (action(d, f, "##expandall", {b.x - 16 * k - 2 * bw - 8 * k, y}, {bw, 28 * k}, "EXPAND ALL", Look::ghost)) browse.expand_all = true;
        if (action(d, f, "##collapseall", {b.x - 16 * k - bw, y}, {bw, 28 * k}, "COLLAPSE ALL", Look::ghost)) browse.expanded.clear();
        y += 42 * k;
    }
    const ImVec2 la(x, y), lb(b.x - 16 * k, b.y - 14 * k);
    begin_region(203, la, lb, browse.scroll, k);
    y = la.y - browse.scroll;
    if (browse.source == 0 && !pack) {
        std::vector<const studio::View::PackView*> shown;
        for (const auto& p : v.packs) {
            bool match = terms.empty() || studio::native_search_matches(studio::native_search_key(p.name + " " + p.made_by), terms);
            if (!match) for (const auto& member : p.members)
                if (const auto* saved = index.preset(member); saved && library_search(index, *saved, terms)) { match = true; break; }
            if (match) shown.push_back(&p);
        }
        std::stable_sort(shown.begin(), shown.end(), [](const auto* l, const auto* r) { return l->favorite && !r->favorite; });
        if (shown.empty()) {
            y += paragraph(d, f.heading, 25 * k, {x + 12 * k, y + 14 * k}, w - 24 * k, white, terms.empty() ? "YOUR PACK COLLECTION STARTS HERE" : "NO PACKS MATCH THIS SEARCH") + 30 * k;
            y += paragraph(d, f.bold, 15 * k, {x + 12 * k, y}, w - 24 * k, soft,
                terms.empty() ? "Get animation packs from the Market, or use YOUR ANIMATIONS to find your own saves." : "Try a different name, or search ALL ANIMATIONS for individual tricks.") + 22 * k;
            if (terms.empty() && action(d, f, "##librarymarket", {x + 12 * k, y}, {210 * k, 38 * k}, "BROWSE THE MARKET", Look::blue)) open_menu(2);
            y += 58 * k;
        }
        for (const auto* p : shown) {
            ImGui::PushID(p->name.c_str());
            const ImVec2 ca(x, y), cb(lb.x - 6 * k, y + 142 * k);
            flat_rect(d, ca, cb, row_fill_alt, seed_of(ca), k * 0.6f);
            const ImVec2 cover_a(ca.x + 9 * k, ca.y + 9 * k), cover_b(ca.x + 133 * k, cb.y - 9 * k);
            pack_cover(d, f, *p, cover_a, cover_b);
            const bool cover_pressed = hit("##browsecover", cover_a, cover_b);
            const float tx = ca.x + 151 * k, tw = cb.x - tx - 16 * k;
            const bool title_pressed = hit("##browsetitle", {tx, ca.y + 8 * k}, {cb.x - 126 * k, ca.y + 44 * k});
            text(d, f.heading, 24 * k, {tx, ca.y + 10 * k}, white, elide(f.heading, 24 * k, p->name, tw - 120 * k));
            if (action(d, f, "##packfavorite", {cb.x - 116 * k, ca.y + 9 * k}, {102 * k, 28 * k}, p->favorite ? "FAVORITE" : "+ FAVORITE", p->favorite ? Look::blue : Look::ghost))
                studio::set_pack_favorite(p->name, !p->favorite);
            std::size_t have = 0;
            if (!target.empty()) for (const auto& member : p->members) have += index.member(member).included();
            const auto detail = std::to_string(p->members.size()) + " animations" + (p->made_by.empty() ? "" : " / by " + p->made_by) +
                (target.empty() ? "" : " / " + std::to_string(have) + " in this playlist");
            text(d, f.bold, 13 * k, {tx, ca.y + 45 * k}, soft, elide(f.bold, 13 * k, detail, tw));
            const float unit = std::min(240 * k, (tw - 16 * k) / 3);
            if (action(d, f, "##browsepack", {tx, ca.y + 76 * k}, {unit, 36 * k}, "OPEN PACK", Look::paper) || cover_pressed || title_pressed)
                library_open_pack(browse, p->name);
            const auto whole = std::find_if(v.playlist.begin(), v.playlist.end(), [&](const auto& e) { return e.pack == p->name && e.animation.empty(); });
            const bool full = whole != v.playlist.end() && whole->omitted.empty();
            const std::string add = target.empty() ? "ADD TO PLAYLIST..." : full ? "REMOVE PACK" : have ? "ADD REMAINING" : "ADD PACK";
            if (action(d, f, "##addwholepack", {tx + unit + 8 * k, ca.y + 76 * k}, {unit, 36 * k}, add, Look::ghost)) {
                if (target.empty()) library_choose_playlists({p->name, ""}, p->name);
                else { std::string m; studio::edit_playlist_pack(target, p->name, !full, m); say(m); }
            }
            if (action(d, f, "##managepack", {tx + 2 * (unit + 8 * k), ca.y + 76 * k}, {unit, 36 * k}, "MANAGE PACK", Look::ghost)) open_pack_page(p->name);
            text(d, f.bold, 11 * k, {tx, ca.y + 120 * k}, faint, "Open the pack to choose just one animation. Add pack follows its enabled variations.");
            ImGui::PopID(); y += 154 * k;
        }
    } else {
        std::map<std::string, std::vector<const studio::Preset*>> groups;
        for (const auto& p : v.presets) {
            if (browse.source == 1 && (p.foreign || p.locked || index.imported.contains(p.name))) continue;
            if (browse.source == 0 && (!pack || !pack->pack.has(p.name))) continue;
            if (library_search(index, p, terms)) groups[index.label(p.trick)].push_back(&p);
        }
        if ((query_changed && !terms.empty()) || browse.expand_all)
            for (const auto& [name, members] : groups) { (void)members; browse.expanded.insert(name); }
        browse.expand_all = false;
        if (groups.empty()) y += paragraph(d, f.bold, 16 * k, {x + 10 * k, y + 14 * k}, w - 20 * k, soft,
            terms.empty() ? "No saved animations here yet. Save a trick in TRICKS, or explore your installed packs." : "No animations match. Try another search or clear the filter.") + 30 * k;
        for (const auto& [name, members] : groups) {
            ImGui::PushID(name.c_str());
            const bool expanded = browse.expanded.contains(name);
            if (action(d, f, "##group", {x, y}, {w - 6 * k, 42 * k}, name + " / " + std::to_string(members.size()) +
                       (members.size() == 1 ? " variation / " : " variations / ") + (expanded ? "HIDE" : "SHOW"), Look::dark)) {
                if (!browse.expanded.erase(name)) browse.expanded.insert(name);
            }
            y += 50 * k;
            if (expanded) for (const auto* p : members) {
                std::string from = browse.source == 0 && pack ? pack->name : std::string{};
                if (from.empty() && (p->foreign || index.imported.contains(p->name)))
                    if (const auto at = index.origins.find(p->name); at != index.origins.end() && !at->second.empty()) from = at->second.front();
                library_member_row(d, f, v, index, *p, from, target, false, {x + 4 * k, y}, w - 10 * k);
                y += 90 * k;
            }
            ImGui::PopID();
        }
    }
    end_region(d, la, lb, y + browse.scroll - la.y, browse.scroll, k);
    ImGui::PopID();
}
