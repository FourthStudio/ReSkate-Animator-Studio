void library_start_create() {
    auto& u = ui(); u.library.creating = true; u.library.new_name.fill(0); u.library_pending_source.reset();
}
void library_create_page(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    (void)v;
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    const float w = std::min(b.x - a.x - 80 * k, 750 * k);
    const ImVec2 ca((a.x + b.x - w) * 0.5f, a.y + 46 * k), cb(ca.x + w, ca.y + 326 * k);
    library_panel_box(d, f, ca, cb);
    const float x = ca.x + 28 * k, iw = w - 56 * k;
    text(d, f.heading, 31 * k, {x, ca.y + 24 * k}, white, "MAKE A PLAYLIST");
    paragraph(d, f.bold, 15 * k, {x, ca.y + 80 * k}, iw, soft,
        u.library_pending_source ? "Name your playlist. The animation or pack you chose will be added to it." : "Give it a name, then choose the animations you want in it.");
    const bool entered = field(f, "##newlibraryplaylistname", route.new_name, {x, ca.y + 134 * k}, iw, "e.g. Everyday, One foot tricks, Darkslides...");
    text(d, f.bold, 12 * k, {x, ca.y + 187 * k}, faint, "Creating a playlist does not change what plays in game.");
    const float half = (iw - 12 * k) * 0.5f;
    if (action(d, f, "##cancelnewlibraryplaylist", {x, cb.y - 70 * k}, {half, 42 * k}, "CANCEL", Look::ghost)) {
        route.creating = false; u.library_pending_source.reset();
    }
    const bool named = std::any_of(route.new_name.begin(), route.new_name.end(), [](char c) { return c != 0 && c != ' '; });
    if ((action(d, f, "##createnamedplaylist", {x + half + 12 * k, cb.y - 70 * k}, {half, 42 * k}, "CREATE PLAYLIST", Look::blue, named) || entered) && named) {
        std::string m;
        if (studio::new_playlist(route.new_name.data(), m)) {
            const auto name = studio::view().playlist_open;
            route.creating = false; route.select(name); route.page = 0;
            if (u.library_pending_source) {
                studio::add_to_playlist(name, *u.library_pending_source, m); u.library_pending_source.reset();
            } else route.add_to(name);
        }
        say(m);
    }
}
void library_confirm_page(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& route = ui().library; const float k = f.k;
    const bool clear = route.confirmation.starts_with("clear:");
    const auto target = route.confirmation.substr(clear ? 6 : 7);
    const float w = std::min(b.x - a.x - 80 * k, 760 * k);
    const ImVec2 ca((a.x + b.x - w) * 0.5f, a.y + 46 * k), cb(ca.x + w, ca.y + 310 * k);
    library_panel_box(d, f, ca, cb);
    const float x = ca.x + 28 * k, iw = w - 56 * k;
    text(d, f.heading, 30 * k, {x, ca.y + 24 * k}, white, clear ? "CLEAR THIS PLAYLIST?" : "DELETE THIS PLAYLIST?");
    paragraph(d, f.bold, 17 * k, {x, ca.y + 84 * k}, iw, white, target);
    paragraph(d, f.bold, 15 * k, {x, ca.y + 128 * k}, iw, soft,
        clear ? "Only its contents are removed. Your packs and animation files stay. Undo can restore the contents."
              : "This playlist and its playback assignments are removed. Your packs and animation files stay.");
    const float half = (iw - 12 * k) * 0.5f;
    if (action(d, f, "##cancelplaylistchange", {x, cb.y - 70 * k}, {half, 42 * k}, "KEEP IT", Look::paper)) route.confirmation.clear();
    if (action(d, f, "##confirmplaylistchange", {x + half + 12 * k, cb.y - 70 * k}, {half, 42 * k}, clear ? "CLEAR CONTENTS" : "DELETE PLAYLIST", Look::danger)) {
        std::string m;
        const bool exists = std::any_of(v.playlists.begin(), v.playlists.end(), [&](const auto& list) { return list.name == target; });
        if (!exists) m = "That playlist no longer exists.";
        else if (clear) studio::clear_playlist_entries(target, m);
        else studio::delete_playlist(target, m);
        say(m); route.confirmation.clear();
    }
}
void library_playlist_settings(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    const float x = a.x + 20 * k, w = b.x - a.x - 40 * k;
    library_panel_box(d, f, a, b);
    text(d, f.heading, 28 * k, {x, a.y + 18 * k}, white, "PLAYLIST SETTINGS");
    if (action(d, f, "##closelistsettings", {b.x - 120 * k, a.y + 15 * k}, {100 * k, 36 * k}, "DONE", Look::paper)) route.settings = false;
    const ImVec2 la(x, a.y + 74 * k), lb(b.x - 20 * k, b.y - 14 * k);
    begin_region(208, la, lb, route.settings_scroll, k);
    float y = la.y - route.settings_scroll;
    text(d, f.heading, 19 * k, {x, y}, white, "NAME"); y += 30 * k;
    if (u.playlist_name_for != v.playlist_open && !ImGui::IsAnyItemActive()) {
        std::snprintf(u.playlist_name.data(), u.playlist_name.size(), "%s", v.playlist_open.c_str()); u.playlist_name_for = v.playlist_open;
    }
    const bool entered = field(f, "##librarylistname", u.playlist_name, {x, y}, w - 118 * k, "Playlist name");
    if (action(d, f, "##libraryrenamelist", {b.x - 128 * k, y}, {108 * k, 34 * k}, "RENAME", Look::ghost) || entered) {
        std::string m; studio::rename_playlist(u.playlist_name.data(), m); say(m); u.playlist_name_for.clear();
    }
    y += 64 * k;
    text(d, f.heading, 19 * k, {x, y}, white, "WHEN SEVERAL VERSIONS MATCH"); y += 32 * k;
    const int picked = segmented(d, f, "##librarymode", {x, y}, w - 8 * k, 38 * k, {"PRIORITY", "IN TURNS", "SHUFFLE"}, static_cast<int>(v.playlist_mode));
    if (picked >= 0 && picked != static_cast<int>(v.playlist_mode)) {
        std::string m; studio::set_playlist_mode(v.playlist_open, static_cast<studio::PlaylistMode>(picked), m); say(m);
    }
    y += 50 * k;
    y += paragraph(d, f.bold, 14 * k, {x, y}, w - 8 * k, soft,
        v.playlist_mode == studio::PlaylistMode::fixed ? "Priority uses the first compatible version. Move its pack or animation higher below."
        : v.playlist_mode == studio::PlaylistMode::turns ? "In turns alternates compatible versions in playlist order." : "Shuffle chooses compatible versions at random without repeating the same one immediately.") + 22 * k;
    if (action(d, f, "##librarycopy", {x, y}, {std::min(340 * k, w - 8 * k), 38 * k}, "DUPLICATE THIS PLAYLIST", Look::ghost)) {
        std::string m; studio::duplicate_playlist(v.playlist_open, m); say(m); route.settings = false;
    }
    y += 58 * k;
    text(d, f.heading, 19 * k, {x, y}, white, "SOURCE ORDER"); y += 30 * k;
    y += paragraph(d, f.bold, 13 * k, {x, y}, w - 8 * k, faint, "A pack follows its future updates. An individual animation stays selected on its own. An animation included more than once plays once, using its first source's rules.") + 18 * k;
    for (std::size_t i = 0; i < v.playlist.size(); ++i) {
        const auto& e = v.playlist[i]; ImGui::PushID(static_cast<int>(i));
        const auto label = std::to_string(i + 1) + ". " + (e.animation.empty() ? e.pack : shown_name(v, e.animation));
        text(d, f.bold, 15 * k, {x + 8 * k, y + 10 * k}, white, elide(f.bold, 15 * k, label, w - 190 * k));
        if (action(d, f, "##queueup", {b.x - 184 * k, y}, {76 * k, 34 * k}, "UP", Look::ghost, i > 0)) studio::playlist_move(i, -1);
        if (action(d, f, "##queuedown", {b.x - 100 * k, y}, {80 * k, 34 * k}, "DOWN", Look::ghost, i + 1 < v.playlist.size())) studio::playlist_move(i, 1);
        ImGui::PopID(); y += 44 * k;
    }
    y += 24 * k;
    const auto binding = studio::modifier_binding();
    if (v.playlist_open == v.playlist_playing && action(d, f, "##stopdefaultplaylist", {x, y}, {w - 8 * k, 38 * k}, "USE ORIGINAL GAME ANIMATIONS BY DEFAULT", Look::ghost)) {
        std::string m; studio::play_playlist({}, m); say(m);
    }
    if (v.playlist_open == v.playlist_playing) y += 52 * k;
    if (v.playlist_open == binding.playlist && action(d, f, "##unassignmodifier", {x, y}, {w - 8 * k, 38 * k}, "REMOVE THIS PLAYLIST FROM THE BUTTON", Look::ghost)) {
        auto live = studio::modifier_binding(); live.playlist.clear(); studio::set_modifier_binding(live);
    }
    if (v.playlist_open == binding.playlist) y += 52 * k;
    y += 16 * k;
    const float half = (w - 20 * k) * 0.5f;
    if (action(d, f, "##libraryclear", {x, y}, {half, 38 * k}, "CLEAR CONTENTS...", Look::ghost, !v.playlist.empty())) route.confirmation = "clear:" + v.playlist_open;
    if (action(d, f, "##librarydelete", {x + half + 12 * k, y}, {half, 38 * k}, "DELETE PLAYLIST...", Look::danger)) route.confirmation = "delete:" + v.playlist_open;
    y += 56 * k;
    end_region(d, la, lb, y + route.settings_scroll - la.y, route.settings_scroll, k);
}
void library_playlist_content(ImDrawList* d, const Fonts& f, const studio::View& v, const LibraryIndex& index, ImVec2 a, ImVec2 b) {
    auto& route = ui().library; const float k = f.k;
    const float x = a.x + 20 * k, w = b.x - a.x - 40 * k;
    library_panel_box(d, f, a, b);
    if (v.playlist_open.empty()) {
        text(d, f.heading, 34 * k, {x + 22 * k, a.y + 42 * k}, white, "YOUR PLAYLISTS, YOUR STYLE");
        paragraph(d, f.bold, 17 * k, {x + 22 * k, a.y + 106 * k}, std::min(w - 44 * k, 820 * k), soft,
            "Make a playlist, then add the animation packs and individual tricks you want. Choose one to play by default, and another to use while holding a button.");
        if (action(d, f, "##firstplaylist", {x + 22 * k, a.y + 210 * k}, {310 * k, 46 * k}, "MAKE YOUR FIRST PLAYLIST", Look::blue)) library_start_create();
        if (action(d, f, "##browsecollectionfirst", {x + 22 * k, a.y + 274 * k}, {310 * k, 38 * k}, "EXPLORE YOUR COLLECTION", Look::ghost)) route.show_page(1);
        return;
    }
    text(d, f.bold, 11 * k, {x, a.y + 15 * k}, soft, "YOU ARE EDITING");
    text(d, f.heading, 33 * k, {x, a.y + 37 * k}, white, elide(f.heading, 33 * k, v.playlist_open, w - 350 * k));
    if (action(d, f, "##libraryaddanimations", {b.x - 328 * k, a.y + 28 * k}, {210 * k, 42 * k}, "+ ADD ANIMATIONS", Look::blue)) route.add_to(v.playlist_open);
    if (action(d, f, "##librarysettings", {b.x - 110 * k, a.y + 28 * k}, {90 * k, 42 * k}, "SETTINGS", Look::ghost)) {
        route.settings = true; route.settings_scroll = 0;
    }
    const auto binding = studio::modifier_binding();
    const bool normal = v.playlist_open == v.playlist_playing, modifier = v.playlist_open == binding.playlist;
    const auto status = normal && modifier ? "Used by default and by the button" : normal ? "Playing by default" : modifier ? "Assigned to the button" : "Not assigned to an input yet";
    text(d, f.bold, 13 * k, {x, a.y + 87 * k}, normal || modifier ? blue_light : IM_COL32(244, 193, 99, 255), status);
    if (action(d, f, "##librarynormalrole", {x, a.y + 116 * k}, {210 * k, 34 * k}, normal ? "PLAYING BY DEFAULT" : "PLAY BY DEFAULT", normal ? Look::paper : Look::ghost, !normal)) {
        std::string m; studio::play_playlist(v.playlist_open, m); say(m);
    }
    if (action(d, f, "##librarymodifierrole", {x + 222 * k, a.y + 116 * k}, {216 * k, 34 * k}, modifier ? "EDIT BUTTON ASSIGNMENT" : "USE WITH A BUTTON", Look::ghost)) {
        auto live = studio::modifier_binding(); live.playlist = v.playlist_open; studio::set_modifier_binding(live);
        route.modifier_setup = true; route.settings_scroll = 0;
    }
    if (action(d, f, "##libraryundo", {b.x - 132 * k, a.y + 116 * k}, {112 * k, 34 * k}, v.playlist_redo ? "REDO" : "UNDO", Look::ghost, v.playlist_can_undo)) {
        std::string m; studio::undo_playlist_entries(v.playlist_open, m); say(m);
    }
    field(f, "##playlistcontentsearch", route.content_query, {x, a.y + 170 * k}, w - 84 * k, "Search inside this playlist...");
    if (action(d, f, "##clearcontentsearch", {b.x - 94 * k, a.y + 170 * k}, {74 * k, 34 * k}, "CLEAR", Look::ghost, route.content_query[0] != 0)) route.content_query.fill(0);
    if (route.seen_content_query != route.content_query.data()) { route.seen_content_query = route.content_query.data(); route.content_scroll = 0; }
    const auto terms = studio::native_search_terms(route.content_query.data());
    std::size_t available = 0;
    for (const auto& [name, member] : index.membership) { (void)member; if (const auto* p = index.preset(name); p && p->available) ++available; }
    text(d, f.bold, 12 * k, {x, a.y + 220 * k}, faint,
        std::to_string(available) + " available animations / " + (v.playlist_mode == studio::PlaylistMode::fixed ? "Priority" : v.playlist_mode == studio::PlaylistMode::turns ? "In turns" : "Shuffle") + " / changes save automatically");
    const ImVec2 la(x, a.y + 250 * k), lb(b.x - 20 * k, b.y - 14 * k);
    begin_region(202, la, lb, route.content_scroll, k);
    float y = la.y - route.content_scroll;
    if (v.playlist.empty()) {
        text(d, f.heading, 26 * k, {x + 14 * k, y + 18 * k}, white, "LET'S ADD YOUR FIRST ANIMATIONS");
        y += 70 * k;
        y += paragraph(d, f.bold, 16 * k, {x + 14 * k, y}, w - 28 * k, soft, "Add one animation, a few variations, or a whole pack. You can preview each one before choosing it.") + 24 * k;
        if (action(d, f, "##emptyaddanimations", {x + 14 * k, y}, {260 * k, 42 * k}, "+ ADD ANIMATIONS", Look::blue)) route.add_to(v.playlist_open);
        y += 64 * k;
    }
    if (!terms.empty()) {
        std::size_t shown = 0;
        for (const auto& p : v.presets) if (index.member(p.name).included() && library_search(index, p, terms)) {
            std::string from;
            if (p.foreign || index.imported.contains(p.name)) if (const auto at = index.origins.find(p.name); at != index.origins.end() && !at->second.empty()) from = at->second.front();
            library_member_row(d, f, v, index, p, from, v.playlist_open, true, {x, y}, w - 8 * k); y += 90 * k; ++shown;
        }
        if (!shown) y += paragraph(d, f.bold, 16 * k, {x + 12 * k, y + 14 * k}, w - 24 * k, soft, "No animations in this playlist match. Clear the search to see all its packs and animations.") + 36 * k;
    } else for (std::size_t i = 0; i < v.playlist.size(); ++i) {
        const auto& e = v.playlist[i]; ImGui::PushID(static_cast<int>(i));
        if (e.animation.empty()) {
            const auto* pack = pack_named(v, e.pack);
            std::vector<const studio::Preset*> members;
            if (pack) for (const auto& name : pack->members)
                if (!pack->pack.off.contains(name) && !e.omitted.contains(name)) if (const auto* p = index.preset(name)) members.push_back(p);
            const ImVec2 ca(x, y), cb(lb.x - 6 * k, y + 112 * k);
            flat_rect(d, ca, cb, pack ? row_fill_alt : IM_COL32(66, 31, 33, 245), seed_of(ca), k);
            text(d, f.heading, 22 * k, {x + 14 * k, y + 12 * k}, white, elide(f.heading, 22 * k, e.pack, w - 370 * k));
            text(d, f.bold, 12 * k, {x + 14 * k, y + 43 * k}, pack ? soft : IM_COL32(255, 159, 151, 255),
                pack ? std::to_string(members.size()) + " animations / pack updates followed" : "Pack missing / reinstall it or remove this entry");
            const bool open = route.open_packs.contains(e.pack);
            if (action(d, f, "##expandqueue", {x + 14 * k, y + 72 * k}, {180 * k, 28 * k}, open ? "HIDE ANIMATIONS" : "SHOW ANIMATIONS", Look::ghost, pack != nullptr)) {
                if (!route.open_packs.erase(e.pack)) route.open_packs.insert(e.pack);
            }
            if (action(d, f, "##choosepackmembers", {cb.x - 326 * k, y + 18 * k}, {178 * k, 36 * k}, "CHOOSE ANIMATIONS", Look::paper, pack != nullptr)) route.add_to(v.playlist_open, e.pack);
            if (action(d, f, "##removepack", {cb.x - 138 * k, y + 18 * k}, {124 * k, 36 * k}, "REMOVE PACK", Look::ghost)) {
                std::string m; studio::edit_playlist_pack(v.playlist_open, e.pack, false, m); say(m);
            }
            y += 124 * k;
            if (open) for (const auto* p : members) {
                library_member_row(d, f, v, index, *p, e.pack, v.playlist_open, true, {x + 12 * k, y}, w - 22 * k); y += 90 * k;
            }
        } else if (const auto* p = index.preset(e.animation)) {
            library_member_row(d, f, v, index, *p, e.pack, v.playlist_open, true, {x, y}, w - 8 * k); y += 90 * k;
        } else {
            text(d, f.bold, 16 * k, {x + 12 * k, y + 12 * k}, IM_COL32(255, 159, 151, 255), elide(f.bold, 16 * k, e.animation + " / missing animation", w - 150 * k));
            if (action(d, f, "##removemissing", {lb.x - 116 * k, y}, {108 * k, 36 * k}, "REMOVE", Look::ghost)) studio::playlist_remove(i);
            y += 50 * k;
        }
        ImGui::PopID();
    }
    end_region(d, la, lb, y + route.content_scroll - la.y, route.content_scroll, k);
}
void library_workspace(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    route.select(v.playlist_open);
    const LibraryIndex index(v);
    page_heading(d, f, a, "LIBRARY", "Your playlists and animation collection.");
    if (!u.playlist_for.empty()) return; // the chooser owns input; its underlying page has no active controls
    const ImVec2 body_a(a.x, a.y + 148 * k);
    if (route.creating) { library_create_page(d, f, v, body_a, b); return; }
    if (!route.confirmation.empty()) { library_confirm_page(d, f, v, body_a, b); return; }
    const int page = segmented(d, f, "##librarypage", {a.x, a.y + 78 * k}, 358 * k, 46 * k, {"PLAYLISTS", "COLLECTION"}, route.page);
    if (page >= 0 && page != route.page) route.show_page(page);
    library_role_strip(d, f, v, {a.x + 376 * k, a.y + 72 * k}, {b.x, a.y + 128 * k});
    if (route.modifier_setup) { library_modifier_setup(d, f, v, body_a, b); return; }
    if (route.adding()) {
        text(d, f.bold, 12 * k, {body_a.x + 146 * k, body_a.y + 2 * k}, blue_light, "ADDING TO THIS PLAYLIST");
        text(d, f.heading, 27 * k, {body_a.x + 146 * k, body_a.y + 25 * k}, white, elide(f.heading, 27 * k, route.adding_to, b.x - body_a.x - 500 * k));
        if (action(d, f, "##backtoplaylist", {body_a.x, body_a.y + 8 * k}, {130 * k, 38 * k}, "< PLAYLIST", Look::ghost)) { route.adding_to.clear(); return; }
        if (action(d, f, "##pickerundo", {b.x - 264 * k, body_a.y + 8 * k}, {112 * k, 38 * k}, v.playlist_redo ? "REDO" : "UNDO", Look::ghost, v.playlist_can_undo)) {
            std::string m; studio::undo_playlist_entries(route.adding_to, m); say(m);
        }
        if (action(d, f, "##doneadding", {b.x - 140 * k, body_a.y + 8 * k}, {140 * k, 38 * k}, "DONE", Look::blue)) { route.adding_to.clear(); return; }
        text(d, f.bold, 12 * k, {body_a.x + 146 * k, body_a.y + 60 * k}, soft, "Add only what you want. Every change is saved; Done returns to the playlist.");
        library_browser(d, f, v, index, route.adding_to, {body_a.x, body_a.y + 88 * k}, b);
        return;
    }
    if (route.page == 1) {
        text(d, f.heading, 23 * k, {body_a.x + 4 * k, body_a.y + 7 * k}, white, "YOUR COLLECTION");
        if (action(d, f, "##librarynewpack", {b.x - 162 * k, body_a.y}, {162 * k, 36 * k}, "+ MAKE A PACK", Look::ghost)) { make_pack(v); u.pack_page = true; }
        library_browser(d, f, v, index, {}, {body_a.x, body_a.y + 54 * k}, b); return;
    }
    const float nav_w = 266 * k, gap = 18 * k;
    const ImVec2 na(body_a.x, body_a.y), nb(na.x + nav_w, b.y), ca(nb.x + gap, body_a.y);
    library_panel_box(d, f, na, nb);
    text(d, f.heading, 22 * k, {na.x + 16 * k, na.y + 15 * k}, white, "YOUR PLAYLISTS");
    if (action(d, f, "##librarynewlist", {na.x + 12 * k, na.y + 58 * k}, {nav_w - 24 * k, 38 * k}, "+ NEW PLAYLIST", Look::blue)) library_start_create();
    const auto binding = studio::modifier_binding();
    const ImVec2 la(na.x + 10 * k, na.y + 112 * k), lb(nb.x - 10 * k, nb.y - 12 * k);
    begin_region(201, la, lb, route.playlists_scroll, k);
    float y = la.y - route.playlists_scroll;
    for (const auto& list : v.playlists) {
        ImGui::PushID(list.name.c_str()); const bool selected = list.name == v.playlist_open;
        const ImVec2 ta(la.x, y), tb(lb.x - 5 * k, y + 94 * k);
        bool lit = false;
        const bool pressed = hit("##destination", ta, tb, &lit);
        flat_rect(d, ta, tb, selected ? IM_COL32(17, 57, 91, 245) : lit ? row_selected : row_fill_alt, seed_of(ta), k * 0.4f);
        if (selected) d->AddRectFilled(ta, {ta.x + 4 * k, tb.y}, blue);
        text(d, f.bold, 10 * k, {ta.x + 14 * k, ta.y + 10 * k}, selected ? blue_light : faint, selected ? "EDITING" : "OPEN PLAYLIST");
        text(d, f.bold, 17 * k, {ta.x + 14 * k, ta.y + 33 * k}, white, elide(f.bold, 17 * k, list.name, nav_w - 46 * k));
        const auto role = list.name == v.playlist_playing && list.name == binding.playlist ? "Default + button"
            : list.name == v.playlist_playing ? "Default playlist" : list.name == binding.playlist ? "Button playlist" : "Saved / not playing";
        text(d, f.bold, 11 * k, {ta.x + 14 * k, ta.y + 66 * k}, soft, role);
        if (lit && nav().keys) chosen_outline(d, ta, tb, k);
        if (pressed) { studio::open_playlist(list.name); route.select(list.name); }
        ImGui::PopID(); y += 104 * k;
    }
    if (v.playlists.empty()) y += paragraph(d, f.bold, 14 * k, {la.x + 8 * k, y}, nav_w - 36 * k, soft, "Make your first playlist to get started.") + 22 * k;
    end_region(d, la, lb, y + route.playlists_scroll - la.y, route.playlists_scroll, k);
    if (route.settings) library_playlist_settings(d, f, v, ca, b);
    else library_playlist_content(d, f, v, index, ca, b);
}
