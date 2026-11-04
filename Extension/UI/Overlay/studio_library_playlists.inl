void library_start_create() {
    auto& u = ui(); u.library.creating = true; u.library.new_name.fill(0); u.library_pending_source.reset();
}
void library_quick_start(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b); // below
void library_create_page(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    (void)v;
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    const float w = std::min(b.x - a.x - 80 * k, 750 * k);
    const ImVec2 ca((a.x + b.x - w) * 0.5f, a.y + 46 * k), cb(ca.x + w, ca.y + 326 * k);
    library_sheet(d, f, ca, cb);
    const float x = ca.x + 28 * k, iw = w - 56 * k;
    text(d, anton(f, 31 * k), 31 * k, {x, ca.y + 24 * k}, lib_ink, "MAKE A PLAYLIST");
    paragraph(d, body(f, 15 * k), 15 * k, {x, ca.y + 80 * k}, iw, lib_ink_soft,
        u.library_pending_source ? "Name your playlist. What you chose goes in it." : "Give it a name, then choose the tricks you want in it.");
    const bool entered = field(f, "##newlibraryplaylistname", route.new_name, {x, ca.y + 134 * k}, iw, "e.g. Everyday, One foot tricks, Darkslides...");
    if (lib_guide_on)
        paragraph(d, body(f, 13 * k), 13 * k, {x, ca.y + 187 * k}, iw, lib_blue,
                  "STEP 1 OF 4 - name it (Everyday, Big flips, anything), then CREATE PLAYLIST. Next you put tricks in it.");
    else
        text(d, body(f, 12 * k), 12 * k, {x, ca.y + 187 * k}, lib_ink_faint, "Making a playlist does not change what plays in game.");
    const float half = (iw - 12 * k) * 0.5f;
    if (lib_button(d, f, "##cancelnewlibraryplaylist", {x, cb.y - 70 * k}, {half, 42 * k}, "CANCEL", Look::ghost)) {
        route.creating = false; u.library_pending_source.reset();
    }
    const bool named = std::any_of(route.new_name.begin(), route.new_name.end(), [](char c) { return c != 0 && c != ' '; });
    if ((lib_button(d, f, "##createnamedplaylist", {x + half + 12 * k, cb.y - 70 * k}, {half, 42 * k}, "CREATE PLAYLIST", Look::blue, named) || entered) && named) {
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
    library_sheet(d, f, ca, cb);
    const float x = ca.x + 28 * k, iw = w - 56 * k;
    text(d, anton(f, 30 * k), 30 * k, {x, ca.y + 24 * k}, lib_ink, clear ? "CLEAR THIS PLAYLIST?" : "DELETE THIS PLAYLIST?");
    paragraph(d, body(f, 17 * k), 17 * k, {x, ca.y + 84 * k}, iw, lib_ink, target);
    paragraph(d, body(f, 15 * k), 15 * k, {x, ca.y + 128 * k}, iw, lib_ink_soft,
        clear ? "Only what is in it is taken out. Your packs and tricks stay. UNDO can put it back."
              : "This playlist goes, and with it what it was set to play (by default, or on the button). Your packs and tricks stay.");
    const float half = (iw - 12 * k) * 0.5f;
    if (lib_button(d, f, "##cancelplaylistchange", {x, cb.y - 70 * k}, {half, 42 * k}, "KEEP IT", Look::paper)) route.confirmation.clear();
    if (lib_button(d, f, "##confirmplaylistchange", {x + half + 12 * k, cb.y - 70 * k}, {half, 42 * k}, clear ? "CLEAR IT" : "DELETE PLAYLIST", Look::danger)) {
        std::string m;
        const bool exists = std::any_of(v.playlists.begin(), v.playlists.end(), [&](const auto& list) { return list.name == target; });
        if (!exists) m = "That playlist no longer exists.";
        else if (clear) studio::clear_playlist_entries(target, m);
        else studio::delete_playlist(target, m);
        say(m); route.confirmation.clear();
    }
}
// WHAT PLAYS > PLAYS BY DEFAULT: the playlist that plays whenever no button is held, chosen from the list (or none: the
// game's own tricks).
void library_default_chooser(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& route = ui().library; const float k = f.k;
    const float x = a.x + 20 * k, w = b.x - a.x - 40 * k;
    library_sheet(d, f, a, b);
    text(d, anton(f, 29 * k), 29 * k, {x, a.y + 18 * k}, lib_ink, "WHAT PLAYS BY DEFAULT");
    if (lib_button(d, f, "##closedefaultchooser", {b.x - 126 * k, a.y + 15 * k}, {106 * k, 36 * k}, "< BACK", Look::paper)) route.choosing_default = false;
    paragraph(d, body(f, 15 * k), 15 * k, {x, a.y + 62 * k}, w - 140 * k, lib_ink_soft,
              "The playlist that plays whenever you do a trick without holding a button. One plays at a time; the others wait.");
    const ImVec2 la(x, a.y + 112 * k), lb(b.x - 20 * k, b.y - 16 * k);
    begin_region(209, la, lb, route.modifier_lists_scroll, k);
    float y = la.y - route.modifier_lists_scroll;
    const auto choose = [&](const std::string& name, const std::string& label, const std::string& about) {
        ImGui::PushID(name.c_str());
        const bool now = name == v.playlist_playing;
        if (lib_button(d, f, "##choosedefault", {x, y}, {std::min(w - 8 * k, 620 * k), 48 * k}, label, now ? Look::blue : Look::dark)) {
            std::string m;
            studio::play_playlist(name, m);
            say(m);
            route.choosing_default = false;
        }
        text(d, body(f, 13 * k), 13 * k, {x + std::min(w - 8 * k, 620 * k) + 16 * k, y + 15 * k}, now ? lib_blue : lib_ink_faint,
             elide(body(f, 13 * k), 13 * k, now ? "plays by default now" : about, std::max(0.0f, w - 660 * k)));
        ImGui::PopID();
        y += 56 * k;
    };
    for (const auto& list : v.playlists)
        choose(list.name, upper(list.name), std::to_string(list.size) + (list.size == 1 ? " pack or trick in it" : " packs and tricks in it"));
    choose({}, "NONE / THE GAME'S OWN TRICKS", "nothing of yours plays without a button");
    end_region(d, la, lb, y + route.modifier_lists_scroll - la.y, route.modifier_lists_scroll, k);
}
void library_playlist_settings(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    const float x = a.x + 20 * k, w = b.x - a.x - 40 * k;
    library_sheet(d, f, a, b);
    text(d, anton(f, 28 * k), 28 * k, {x, a.y + 18 * k}, lib_ink, "PLAYLIST SETTINGS");
    if (lib_button(d, f, "##closelistsettings", {b.x - 120 * k, a.y + 15 * k}, {100 * k, 36 * k}, "DONE", Look::paper)) route.settings = false;
    const ImVec2 la(x, a.y + 74 * k), lb(b.x - 20 * k, b.y - 14 * k);
    begin_region(208, la, lb, route.settings_scroll, k);
    float y = la.y - route.settings_scroll;
    text(d, anton(f, 19 * k), 19 * k, {x, y}, lib_ink, "NAME"); y += 30 * k;
    if (u.playlist_name_for != v.playlist_open && !ImGui::IsAnyItemActive()) {
        std::snprintf(u.playlist_name.data(), u.playlist_name.size(), "%s", v.playlist_open.c_str()); u.playlist_name_for = v.playlist_open;
    }
    const bool entered = field(f, "##librarylistname", u.playlist_name, {x, y}, w - 118 * k, "Playlist name");
    if (lib_button(d, f, "##libraryrenamelist", {b.x - 128 * k, y}, {108 * k, 34 * k}, "RENAME", Look::ghost) || entered) {
        std::string m; studio::rename_playlist(u.playlist_name.data(), m); say(m); u.playlist_name_for.clear();
    }
    y += 64 * k;
    text(d, anton(f, 19 * k), 19 * k, {x, y}, lib_ink, "WHEN SEVERAL VERSIONS OF A TRICK FIT"); y += 32 * k;
    const int picked = segmented(d, f, "##librarymode", {x, y}, w - 8 * k, 38 * k, {"PRIORITY", "IN TURNS", "SHUFFLE"}, static_cast<int>(v.playlist_mode));
    if (picked >= 0 && picked != static_cast<int>(v.playlist_mode)) {
        std::string m; studio::set_playlist_mode(v.playlist_open, static_cast<studio::PlaylistMode>(picked), m); say(m);
    }
    y += 50 * k;
    y += paragraph(d, body(f, 14 * k), 14 * k, {x, y}, w - 8 * k, lib_ink_soft,
        v.playlist_mode == studio::PlaylistMode::fixed ? "PRIORITY plays the highest one in the playlist every time."
        : v.playlist_mode == studio::PlaylistMode::turns ? "IN TURNS plays them one after another, top to bottom."
                                                          : "SHUFFLE picks one at random, never the same one twice running.") + 22 * k;
    text(d, anton(f, 19 * k), 19 * k, {x, y}, lib_ink, "ORDER"); y += 30 * k;
    y += paragraph(d, body(f, 13 * k), 13 * k, {x, y}, w - 8 * k, lib_ink_faint,
        "The order is on the playlist itself: drag a row by its grip (the dots by its number) to another place, and the ones between "
        "shift by one. With a pad or the keys, choose the grip, press A / Enter, move it with up and down, and press A / Enter again. "
        "A trick in a playlist twice (alone and in a pack) plays once, by its first place.") + 20 * k;
    if (lib_button(d, f, "##librarycopy", {x, y}, {std::min(340 * k, w - 8 * k), 38 * k}, "DUPLICATE THIS PLAYLIST", Look::ghost)) {
        std::string m; studio::duplicate_playlist(v.playlist_open, m); say(m); route.settings = false;
    }
    y += 58 * k;
    const auto binding = studio::modifier_binding();
    if (v.playlist_open == v.playlist_playing && lib_button(d, f, "##stopdefaultplaylist", {x, y}, {w - 8 * k, 38 * k}, "STOP PLAYING IT BY DEFAULT (THE GAME'S OWN TRICKS)", Look::ghost)) {
        std::string m; studio::play_playlist({}, m); say(m);
    }
    if (v.playlist_open == v.playlist_playing) y += 52 * k;
    if (v.playlist_open == binding.playlist && lib_button(d, f, "##unassignmodifier", {x, y}, {w - 8 * k, 38 * k}, "TAKE IT OFF THE BUTTON", Look::ghost)) {
        auto live = studio::modifier_binding(); live.playlist.clear(); studio::set_modifier_binding(live);
    }
    if (v.playlist_open == binding.playlist) y += 52 * k;
    y += 16 * k;
    const float half = (w - 20 * k) * 0.5f;
    if (lib_button(d, f, "##libraryclear", {x, y}, {half, 38 * k}, "CLEAR IT...", Look::ghost, !v.playlist.empty())) route.confirmation = "clear:" + v.playlist_open;
    if (lib_button(d, f, "##librarydelete", {x + half + 12 * k, y}, {half, 38 * k}, "DELETE PLAYLIST...", Look::danger)) route.confirmation = "delete:" + v.playlist_open;
    y += 56 * k;
    end_region(d, la, lb, y + route.settings_scroll - la.y, route.settings_scroll, k);
}
// ---- the open playlist, in its order ----
// Each entry is a row: its place and its grip at the left, what it is, and its buttons. The order is what PRIORITY and IN
// TURNS go by, so it is on the page itself and changed there (it was a list of UP / DOWN buttons in SETTINGS before, one
// place a press): dragged by the grip with the mouse, or carried with the pad or the arrow keys (the grip chosen, A or
// Enter picks it up, up and down move it, A or Enter puts it down, B or Esc puts it back).
enum class Grip { none, keys, mouse };
constexpr float lib_entry_h = 64, lib_entry_gap = 8;
Grip library_grip(ImDrawList* d, const Fonts& f, ImVec2 a, ImVec2 b, std::size_t place, bool shown, bool enabled, bool carried) {
    const float k = f.k;
    bool lit = false;
    const bool pressed = hit("##grip", a, b, &lit, enabled);
    // The mouse went down on it this frame (only asked when hit made it a button: when it is scrolled away it is not one).
    const bool down = shown && enabled && ImGui::IsItemActivated();
    if (shown) {
        const ImU32 ink = carried ? lib_blue : lit ? lib_ink : enabled ? lib_ink_soft : lib_ink_faint;
        if (lit && enabled) d->AddRectFilled(a, b, IM_COL32(255, 255, 255, 22));
        const auto n = std::to_string(place + 1);
        const float size = 19 * k;
        const ImVec2 t = text_size(anton(f, size), size, n.c_str());
        text(d, anton(f, size), size, {(a.x + b.x - t.x) * 0.5f, a.y + 7 * k}, ink, n);
        if (enabled) // six dots: the handle that is held
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 2; ++c)
                    d->AddCircleFilled({(a.x + b.x) * 0.5f + (static_cast<float>(c) - 0.5f) * 7 * k, a.y + 39 * k + static_cast<float>(r) * 6.5f * k},
                                       1.9f * k, ink, 8);
        if (lit && nav().keys) chosen_outline(d, a, b, k);
    }
    if (down) return Grip::mouse;
    if (pressed && enabled && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return Grip::keys;
    return Grip::none;
}
// One entry's row. `open` the pack's tricks shown under it; returns how its grip was pressed.
Grip library_entry_row(ImDrawList* d, const Fonts& f, const studio::View& v, const LibraryIndex& index, std::size_t i, ImVec2 a, float w,
                       bool shown, bool can_move, bool carried) {
    auto& route = ui().library; const float k = f.k;
    const auto& e = v.playlist[i];
    const ImVec2 b(a.x + w, a.y + lib_entry_h * k);
    const bool whole = e.animation.empty();
    const auto* pack = whole ? pack_named(v, e.pack) : nullptr;
    const auto* p = whole ? nullptr : index.preset(e.animation);
    const bool missing = whole ? !pack : !p;
    const bool open = whole && route.open_packs.contains(e.pack);
    if (shown) lib_row_box(d, a, b, missing ? lib_row_bad : carried ? IM_COL32(17, 57, 91, 150) : lib_row, k);
    const auto grip = library_grip(d, f, {a.x, a.y}, {a.x + 56 * k, b.y}, i, shown, can_move, carried);
    // The buttons, at the right; drawn only when seen, always there for the pad.
    const auto button = [&](const char* id, float x, float bw, const std::string& label, Look look, bool enabled = true) {
        const ImVec2 ba(x, a.y + 14 * k), size(bw, 36 * k);
        return shown ? lib_button(d, f, id, ba, size, label, look, enabled) : hit(id, ba, {ba.x + bw, ba.y + size.y}, nullptr, enabled);
    };
    float bx = b.x - 12 * k;
    const auto place = [&](float bw) { bx -= bw; const float at = bx; bx -= 8 * k; return at; };
    if (whole) {
        {
            const float rx = place(104 * k);
            if (button("##removepack", rx, 104 * k, "REMOVE", Look::ghost)) {
                std::string m; studio::edit_playlist_pack(v.playlist_open, e.pack, false, m); say(m);
            }
        }
        if (pack) {
            const float cx = place(132 * k);
            if (button("##choosepackmembers", cx, 132 * k, "PICK TRICKS", Look::ghost)) route.add_to(v.playlist_open, e.pack);
            const float sx = place(112 * k);
            const auto count = i < index.entry_members.size() ? index.entry_members[i].size() : 0;
            if (button("##expandqueue", sx, 112 * k, open ? "HIDE" : "SHOW " + std::to_string(count), open ? Look::paper : Look::ghost) &&
                !route.open_packs.erase(e.pack))
                route.open_packs.insert(e.pack);
        }
    } else {
        const float rx = place(104 * k);
        if (button("##removemember", rx, 104 * k, "REMOVE", Look::ghost)) {
            std::string m;
            if (p) studio::edit_playlist_member(v.playlist_open, e, false, m);
            else studio::playlist_remove(i);
            say(m);
        }
        if (p) {
            const float ex = place(80 * k);
            if (button("##edit", ex, 80 * k, p->locked ? "VIEW" : "EDIT", Look::ghost, p->available)) open_or_preview(v, p->name, e.pack);
            const float px = place(104 * k);
            if (button("##preview", px, 104 * k, "PREVIEW", Look::ghost, p->available)) preview_trick(e.pack, p->name, !e.pack.empty());
        }
    }
    // The rest of the row: a pack's opens it, a trick's previews it.
    const float tx = a.x + 64 * k, text_w = std::max(40 * k, bx - tx - 8 * k);
    bool lit = false;
    if (hit("##entrybody", {tx, a.y + 4 * k}, {bx, b.y - 4 * k}, &lit, !missing && (whole || (p && p->available)))) {
        if (whole) { if (!route.open_packs.erase(e.pack)) route.open_packs.insert(e.pack); }
        else preview_trick(e.pack, p->name, !e.pack.empty());
    }
    if (!shown) return grip;
    if (lit && nav().keys) chosen_outline(d, {tx, a.y + 4 * k}, {bx, b.y - 4 * k}, k);
    const float tag_w = tag(d, f, {tx, a.y + 11 * k}, whole ? "PACK" : "TRICK", whole ? blue : IM_COL32(70, 72, 80, 255));
    std::string title, sub;
    ImU32 sub_ink = lib_ink_soft;
    if (whole) {
        title = e.pack;
        const auto count = i < index.entry_members.size() ? index.entry_members[i].size() : 0;
        sub = pack ? std::to_string(count) + (count == 1 ? " trick plays" : " tricks play") + (e.omitted.empty() ? "" : " (" + std::to_string(e.omitted.size()) + " left out here)") +
                         " / follows the pack's updates"
                   : "This pack is gone: get it again, or REMOVE it here";
        if (!pack) sub_ink = lib_red;
    } else if (p) {
        title = library_saved_name(index, p->name);
        sub = index.label(p->trick) + (p->trick.starts_with("native:") ? "" : " / " + (p->pop.empty() ? std::string("any pop") : p->pop + " pop")) + " / " +
              library_stance(v, *p) + " / " + (e.pack.empty() ? std::string("your own") : "from " + e.pack);
        if (!p->available) sub = "Its clip is missing: restore it to play it", sub_ink = lib_red;
    } else {
        title = e.animation;
        sub = "This trick is gone: REMOVE it here", sub_ink = lib_red;
    }
    text(d, body(f, 17 * k), 17 * k, {tx + tag_w + 10 * k, a.y + 10 * k}, missing ? lib_red : lib_ink, elide(body(f, 17 * k), 17 * k, title, text_w - tag_w - 10 * k));
    text(d, body(f, 12 * k), 12 * k, {tx, a.y + 40 * k}, sub_ink, elide(body(f, 12 * k), 12 * k, sub, text_w));
    return grip;
}
void library_playlist_content(ImDrawList* d, const Fonts& f, const studio::View& v, const LibraryIndex& index, ImVec2 a, ImVec2 b) {
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    const float x = a.x + 20 * k, w = b.x - a.x - 40 * k;
    if (v.playlists.empty()) {
        library_quick_start(d, f, v, a, b);
        return;
    }
    library_sheet(d, f, a, b);
    if (v.playlist_open.empty()) {
        text(d, anton(f, 34 * k), 34 * k, {x + 22 * k, a.y + 42 * k}, lib_ink, "YOUR PLAYLISTS, YOUR STYLE");
        paragraph(d, body(f, 17 * k), 17 * k, {x + 22 * k, a.y + 106 * k}, std::min(w - 44 * k, 820 * k), lib_ink_soft,
            "Open a playlist on the left, or make another. One plays by default; another can play while you hold a button.");
        if (lib_button(d, f, "##firstplaylist", {x + 22 * k, a.y + 210 * k}, {310 * k, 46 * k}, "+ NEW PLAYLIST", Look::blue)) library_start_create();
        if (lib_button(d, f, "##browsecollectionfirst", {x + 22 * k, a.y + 274 * k}, {310 * k, 38 * k}, "LOOK THROUGH YOUR COLLECTION", Look::ghost)) route.show_page(1);
        return;
    }
    // The head: its name, + ADD TRICKS and SETTINGS; what it plays as, and the buttons that make it play.
    const float add_w = 196 * k, set_w = 116 * k;
    lib_caps(d, f, {x, a.y + 16 * k}, 11 * k, lib_ink_soft, "PLAYLIST");
    {
        const float room = w - add_w - set_w - 30 * k;
        const std::string name = elide(anton(f, 34 * k), 34 * k, upper(v.playlist_open), room);
        text(d, anton(f, 34 * k), 34 * k, {x, a.y + 30 * k}, lib_ink, name);
        marker_underline(d, {x, a.y + 74 * k}, std::min(text_size(anton(f, 34 * k), 34 * k, name.c_str()).x + 20 * k, room), 4 * k, blue,
                         static_cast<std::uint32_t>(std::hash<std::string>{}(v.playlist_open) % 991));
    }
    const ImVec2 add_a(b.x - 20 * k - set_w - 10 * k - add_w, a.y + 24 * k);
    if (lib_button(d, f, "##libraryaddanimations", add_a, {add_w, 42 * k}, "+ ADD TRICKS", Look::blue)) route.add_to(v.playlist_open);
    if (lib_guide_on && lib_guide_now == 1) lib_pulse(d, add_a, {add_a.x + add_w, add_a.y + 42 * k}, k);
    if (lib_button(d, f, "##librarysettings", {b.x - 20 * k - set_w, a.y + 24 * k}, {set_w, 42 * k}, "SETTINGS", Look::ghost)) {
        route.settings = true; route.settings_scroll = 0;
    }
    const auto binding = studio::modifier_binding();
    const bool normal = v.playlist_open == v.playlist_playing, modifier = v.playlist_open == binding.playlist;
    const float ry = a.y + 92 * k, rh = 36 * k;
    const ImVec2 default_a(x, ry);
    if (lib_button(d, f, "##librarynormalrole", default_a, {214 * k, rh}, normal ? "PLAYS BY DEFAULT" : "PLAY BY DEFAULT", normal ? Look::paper : Look::ghost, !normal)) {
        std::string m; studio::play_playlist(v.playlist_open, m); say(m);
    }
    if (lib_guide_on && lib_guide_now == 2 && !normal) lib_pulse(d, default_a, {default_a.x + 214 * k, default_a.y + rh}, k);
    const std::string held = std::string("ON ") + studio::modifier_button_names[static_cast<std::size_t>(binding.button)];
    if (lib_button(d, f, "##librarymodifierrole", {x + 224 * k, ry}, {214 * k, rh}, modifier ? held + " / SET UP" : "PLAY ON A BUTTON...", modifier ? Look::paper : Look::ghost)) {
        if (!modifier) { auto live = studio::modifier_binding(); live.playlist = v.playlist_open; studio::set_modifier_binding(live); }
        route.modifier_setup = true; route.settings_scroll = 0;
    }
    if (lib_button(d, f, "##libraryundo", {b.x - 20 * k - 112 * k, ry}, {112 * k, rh}, v.playlist_redo ? "REDO" : "UNDO", Look::ghost, v.playlist_can_undo)) {
        std::string m; studio::undo_playlist_entries(v.playlist_open, m); say(m);
    }
    {
        // What it does now, in words, beside the buttons that change it.
        const std::string role = normal && modifier ? "plays by default and on the button" : normal ? "plays by default" : modifier ? "plays while the button is held"
                                 : v.playlist.empty() ? "empty" : "not playing yet - press PLAY BY DEFAULT";
        const bool good = (normal || modifier) && v.play_in_game;
        const float room = b.x - 20 * k - 124 * k - (x + 452 * k);
        text(d, body(f, 13 * k), 13 * k, {x + 452 * k, ry + 10 * k}, good ? lib_blue : lib_warn,
             elide(body(f, 13 * k), 13 * k, (good || v.play_in_game || !(normal || modifier) ? role : role + " / playback is off"), room));
    }
    field(f, "##playlistcontentsearch", route.content_query, {x, a.y + 142 * k}, w - 84 * k, "Search inside this playlist...");
    if (lib_button(d, f, "##clearcontentsearch", {b.x - 94 * k, a.y + 142 * k}, {74 * k, 34 * k}, "CLEAR", Look::ghost, route.content_query[0] != 0)) route.content_query.fill(0);
    if (route.seen_content_query != route.content_query.data()) { route.seen_content_query = route.content_query.data(); route.content_scroll = 0; }
    const auto terms = studio::native_search_terms(route.content_query.data());
    const char* mode = v.playlist_mode == studio::PlaylistMode::fixed ? "PRIORITY" : v.playlist_mode == studio::PlaylistMode::turns ? "IN TURNS" : "SHUFFLE";
    const std::string line = std::to_string(index.available) + (index.available == 1 ? " trick" : " tricks") + " can play / " + mode +
        (!terms.empty() ? " / clear the search to change the order" : v.playlist.size() > 1 ? " / drag the dots by a number to change the order" : "") +
        " / saved as you go";
    text(d, body(f, 12 * k), 12 * k, {x, a.y + 188 * k}, lib_ink_faint, elide(body(f, 12 * k), 12 * k, line, w));
    const ImVec2 la(x, a.y + 212 * k), lb(b.x - 20 * k, b.y - 14 * k);
    // ---- the reorder: where a carried entry is going ----
    auto& carry = u.library_carry;
    auto& n = nav();
    const int frame = ImGui::GetFrameCount();
    const auto& io = ImGui::GetIO();
    if (carry.on) {
        const bool still = carry.list == v.playlist_open && carry.frame >= frame - 2 && terms.empty() && carry.from < v.playlist.size() &&
                           studio::same_playlist_source(v.playlist[carry.from], carry.entry);
        if (!still) carry = {};
    }
    const bool can_move = terms.empty() && v.playlist.size() > 1;
    if (carry.on && !carry.mouse) {
        if (n.carried) carry.to = studio::step_target(carry.to, n.carried, v.playlist.size());
        n.carry_frame = frame;
    }
    n.carried = 0;
    if (carry.on) {
        carry.frame = frame;
        if (carry.mouse) {
            if (!carry.moved && std::abs(io.MousePos.y - carry.press_y) > 6 * k) carry.moved = true;
            // Near the list's top or bottom edge the list scrolls under the drag, faster the closer it is.
            if (carry.moved) route.content_scroll += studio::edge_scroll(io.MousePos.y, la.y, lb.y, 60 * k) * 1100 * k * std::min(io.DeltaTime, 0.05f);
        }
    }
    begin_region(202, la, lb, route.content_scroll, k);
    float y = la.y - route.content_scroll;
    const auto seen = [&](float top, float bottom) { return bottom > la.y && top < lb.y; };
    if (v.playlist.empty()) {
        text(d, anton(f, 26 * k), 26 * k, {x + 14 * k, y + 18 * k}, lib_ink, "LET'S ADD YOUR FIRST TRICKS");
        y += 70 * k;
        y += paragraph(d, body(f, 16 * k), 16 * k, {x + 14 * k, y}, w - 28 * k, lib_ink_soft,
                       "Add a whole pack in one press, or single tricks. You can preview each one first.") + 24 * k;
        const ImVec2 ea(x + 14 * k, y);
        if (lib_button(d, f, "##emptyaddanimations", ea, {260 * k, 42 * k}, "+ ADD TRICKS", Look::blue)) route.add_to(v.playlist_open);
        if (lib_guide_on && lib_guide_now == 1) lib_pulse(d, ea, {ea.x + 260 * k, ea.y + 42 * k}, k);
        y += 64 * k;
    }
    // Where each entry's row and block (with its open pack) were drawn this frame: the drag finds its gap in them.
    std::vector<float> tops, bottoms, block_tops, block_bottoms;
    if (!terms.empty()) {
        std::size_t shown = 0;
        for (const auto& p : v.presets) if (index.member(p.name).included() && library_search(index, p, terms)) {
            std::string from;
            if (p.foreign || index.imported.contains(p.name)) if (const auto at = index.origins.find(p.name); at != index.origins.end() && !at->second.empty()) from = at->second.front();
            library_member_row(d, f, v, index, p, from, v.playlist_open, true, {x, y}, w - 8 * k); y += 90 * k; ++shown;
        }
        if (!shown) y += paragraph(d, body(f, 16 * k), 16 * k, {x + 12 * k, y + 14 * k}, w - 24 * k, lib_ink_soft, "Nothing in this playlist matches. CLEAR the search to see all of it.") + 36 * k;
    } else {
        tops.reserve(v.playlist.size()); bottoms.reserve(v.playlist.size());
        block_tops.reserve(v.playlist.size()); block_bottoms.reserve(v.playlist.size());
        const float row_w = w - 8 * k;
        for (std::size_t i = 0; i < v.playlist.size(); ++i) {
            const auto& e = v.playlist[i];
            ImGui::PushID(static_cast<int>(i));
            const float top = y, bottom = y + lib_entry_h * k;
            const bool carried = carry.on && carry.from == i && (carry.moved || !carry.mouse);
            const auto grip = library_entry_row(d, f, v, index, i, {x, y}, row_w, seen(top, bottom), can_move, carried);
            if (grip == Grip::mouse && !carry.on) {
                carry = {};
                carry.on = carry.mouse = true;
                carry.list = v.playlist_open; carry.entry = e; carry.from = carry.to = i;
                carry.press_y = io.MousePos.y; carry.frame = frame;
            } else if (grip == Grip::keys) {
                if (!carry.on) {
                    carry = {};
                    carry.on = true;
                    carry.list = v.playlist_open; carry.entry = e; carry.from = carry.to = i; carry.frame = frame;
                    n.carry_frame = frame;
                } else if (!carry.mouse) {
                    if (carry.to != carry.from) { std::string m; studio::playlist_move_to(carry.list, carry.entry, carry.to, m); say(m); }
                    carry = {};
                    n.carry_frame = -1;
                }
            }
            // Carried with the pad or the keys: the choice follows it, so the list scrolls with it and A lands it here.
            if (carry.on && !carry.mouse && carry.to == i) n.focus = ImGui::GetID("##grip");
            tops.push_back(top); bottoms.push_back(bottom); block_tops.push_back(top);
            y = bottom + lib_entry_gap * k;
            if (e.animation.empty() && route.open_packs.contains(e.pack) && i < index.entry_members.size()) {
                for (const auto at : index.entry_members[i]) {
                    library_member_row(d, f, v, index, v.presets[at], e.pack, v.playlist_open, true, {x + 56 * k, y}, row_w - 56 * k);
                    y += 90 * k;
                }
            }
            block_bottoms.push_back(y - lib_entry_gap * k);
            ImGui::PopID();
        }
    }
    // The drop marker: a blue bar in the gap it will go into, with the place it takes.
    if (carry.on && carry.mouse && carry.moved) carry.to = studio::target_for_gap(carry.from, studio::gap_at(tops, bottoms, io.MousePos.y));
    if (carry.on && (carry.moved || !carry.mouse) && carry.to != carry.from && !block_tops.empty()) {
        const auto gap = studio::gap_for_target(carry.from, carry.to);
        const float ly = gap < block_tops.size() ? block_tops[gap] - lib_entry_gap * 0.5f * k : block_bottoms.back() + lib_entry_gap * 0.5f * k;
        d->AddRectFilled({x - 4 * k, ly - 2.5f * k}, {x + w - 4 * k, ly + 2.5f * k}, blue);
        d->AddCircleFilled({x - 4 * k, ly}, 6 * k, blue, 16);
        const std::string where = "PLACE " + std::to_string(carry.to + 1);
        const float tw = spaced_width(mont(f, 11 * k), 11 * k, where, 1.4f * k) + 16 * k;
        d->AddRectFilled({x + 64 * k, ly - 11 * k}, {x + 64 * k + tw, ly + 11 * k}, blue);
        lib_caps(d, f, {x + 72 * k, ly - 6 * k}, 11 * k, IM_COL32_WHITE, where, 1.4f);
    }
    // Let go: put down where the marker is (a press that never moved is not a move).
    if (carry.on && carry.mouse && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (carry.moved && carry.to != carry.from) { std::string m; studio::playlist_move_to(carry.list, carry.entry, carry.to, m); say(m); }
        else if (!carry.moved) say("Hold the dots and drag the row up or down to move it.");
        carry = {};
    }
    end_region(d, la, lb, y + route.content_scroll - la.y, route.content_scroll, k);
    if (carry.on) {
        const auto& e = carry.entry;
        const std::string what = e.animation.empty() ? e.pack : library_saved_name(index, e.animation);
        if (carry.mouse && carry.moved) {
            // What is being dragged, held by the pointer.
            const std::string label = elide(body(f, 15 * k), 15 * k, what, 360 * k);
            const float tw = text_size(body(f, 15 * k), 15 * k, label.c_str()).x;
            const ImVec2 pa(io.MousePos.x + 16 * k, io.MousePos.y - 20 * k), pb(pa.x + tw + 28 * k, pa.y + 40 * k);
            d->AddRectFilled({pa.x + 4 * k, pa.y + 5 * k}, {pb.x + 4 * k, pb.y + 5 * k}, IM_COL32(0, 0, 0, 120));
            d->AddRectFilled(pa, pb, IM_COL32(17, 57, 91, 245));
            d->AddRect(pa, pb, blue, 0, 0, 2 * k);
            text(d, body(f, 15 * k), 15 * k, {pa.x + 14 * k, pa.y + 10 * k}, white, label);
        } else if (!carry.mouse) {
            // Carried with the pad or the keys: how to move it, over the bottom of the list.
            const std::string how = "MOVING " + upper(what) + (u.pad_mode ? "   UP / DOWN: ITS PLACE   A: PUT IT DOWN   B: PUT IT BACK"
                                                                         : "   UP / DOWN: ITS PLACE   ENTER: PUT IT DOWN   ESC: PUT IT BACK");
            const ImVec2 ha(la.x, lb.y - 40 * k), hb(lb.x, lb.y);
            d->AddRectFilled(ha, hb, IM_COL32(8, 34, 64, 245));
            d->AddRect(ha, hb, blue, 0, 0, 2 * k);
            lib_caps(d, f, {ha.x + 14 * k, ha.y + 14 * k}, 11 * k, IM_COL32_WHITE, lib_caps_fit(f, 11 * k, how, hb.x - ha.x - 28 * k, 1.2f), 1.2f);
        }
    }
}
// The page's head, as the MARKET's sections: its name on a torn scrap of blue paper and a line in marker beside it, then
// its two parts as the shop's bar has them (the chosen one on a scrap of blue).
void library_head(ImDrawList* d, const Fonts& f, ImVec2 a, int& page) {
    const float k = f.k;
    const ImVec2 size = look_heading(d, f, *look_now, ImVec2(a.x + 4 * k, a.y - 4 * k), "LIBRARY", 44 * k, 71, PaperKind::blue, -1.6f);
    marker_words(d, f, ImVec2(a.x + size.x + 30 * k, a.y + 6 * k), "your tricks. your rules.", 30 * k, -4, blue_light);
    static constexpr std::array<const char*, 2> parts{"PLAYLISTS", "COLLECTION"};
    float x = a.x;
    for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
        const float fs = 16 * k, tw = spaced_width(mont(f, fs), fs, parts[static_cast<std::size_t>(i)], 0.6f * k) + 44 * k;
        const ImVec2 ta(x, a.y + 82 * k), tb(x + tw, a.y + 126 * k);
        ImGui::PushID(i);
        bool lit = false;
        if (hit("##librarypart", ta, tb, &lit) && page != i) page = i;
        ImGui::PopID();
        ImU32 ink = lit ? IM_COL32_WHITE : IM_COL32(243, 243, 239, 178);
        if (page == i) ink = tab_mark(d, f, *look_now, ta, tb, 30 + static_cast<std::uint32_t>(i));
        const auto [ct, cf] = caps_of(mont(f, fs), fs);
        text_spaced(d, mont(f, fs), fs, ImVec2(ta.x + 22 * k, ta.y + (tb.y - ta.y - (cf - ct)) * 0.5f - ct), ink, parts[static_cast<std::size_t>(i)], 0.6f * k);
        if (lit && nav().keys) chosen_outline(d, ta, tb, k);
        x = tb.x + 8 * k;
    }
}
// No playlist yet: set up in one click (every pack and his own tricks, playing by default), or start empty. With nothing
// to put in one yet, the way to get something.
void library_quick_start(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    const float k = f.k;
    const float w = std::min(b.x - a.x - 40 * k, 900 * k), h = std::min(b.y - a.y - 20 * k, 430 * k);
    const ImVec2 ca(a.x + 20 * k, a.y + 22 * k), cb(ca.x + w, ca.y + h);
    const auto shape = torn::piece(w, h, 91, 2.6f * k, 8 * k, 8 * k);
    draw_paper(d, f, shape, ca, -0.008f, ImVec2(w * 0.5f, h * 0.5f), PaperKind::white, 91, 0.5f, ImVec2(8, 10), 3.0f);
    masking_tape(d, f, ImVec2(ca.x + 70 * k, ca.y + 2 * k), 110 * k, 26 * k, -6, 92);
    masking_tape(d, f, ImVec2(cb.x - 60 * k, ca.y + 4 * k), 96 * k, 24 * k, 7, 93);
    const float x = ca.x + 40 * k, iw = w - 80 * k;
    float y = ca.y + 40 * k;
    lib_caps(d, f, ImVec2(x, y), 13 * k, IM_COL32(80, 82, 88, 255), "QUICK START", 2.5f);
    y += 26 * k;
    text(d, anton(f, 46 * k), 46 * k, ImVec2(x, y), shop_ink, "PLAY YOUR TRICKS IN ONE CLICK");
    y += 64 * k;
    const auto own = library_index(v).own;
    const bool any = !v.packs.empty() || own > 0;
    y += paragraph(d, f.bold, 17 * k, ImVec2(x, y), iw * 0.8f, IM_COL32(40, 42, 48, 255),
                   any ? std::format("Puts all your packs ({}) and the tricks you saved that are in no pack ({}) in a playlist called "
                                     "Everything, and plays it in game. Change it, or make more playlists, any time.",
                                     v.packs.size(), own)
                       : std::string("You have no packs or saved tricks yet. Get a pack in the MARKET (it asks where to put it), or make a "
                                     "trick in TRICKS and save it.")) +
         26 * k;
    const float bw = std::min(iw, 420 * k);
    if (any) {
        if (lib_button(d, f, "##libraryeverything", ImVec2(x, y), ImVec2(bw, 56 * k), "USE EVERYTHING I HAVE", Look::blue)) library_use_everything(v);
    } else if (lib_button(d, f, "##librarytomarket", ImVec2(x, y), ImVec2(bw, 56 * k), "GET PACKS IN THE MARKET", Look::blue)) {
        open_menu(2);
    }
    marker_arrow(d, ImVec2(x + bw + 150 * k, y - 30 * k), ImVec2(x + bw + 18 * k, y + 22 * k), 0.35f, 4 * k, IM_COL32(0, 98, 214, 255), 94);
    marker_words(d, f, ImVec2(x + bw + 60 * k, y - 76 * k), any ? "one click." : "start here.", 34 * k, -8, IM_COL32(0, 98, 214, 255));
    y += 76 * k;
    lib_caps(d, f, ImVec2(x, y + 12 * k), 12 * k, IM_COL32(80, 82, 88, 255), "OR");
    if (shop_button(d, f, "##libraryemptyfirst", ImVec2(x + 44 * k, y), ImVec2(std::min(iw - 44 * k, 360 * k), 42 * k), "START WITH AN EMPTY PLAYLIST",
                    ShopLook::ink))
        library_start_create();
}
void library_workspace(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    want_market_art(); // the paper and the grip tape the page is drawn on
    route.select(v.playlist_open);
    const auto& index = library_index(v);
    int page = route.page;
    library_head(d, f, a, page);
    if (!u.playlist_for.empty()) return; // the chooser owns input; its underlying page has no active controls
    const ImVec2 body_a(a.x, a.y + 148 * k);
    // While it is not set up, the guide's strip in place of what plays (on every part of the page, so it is always there).
    lib_guide_on = library_guide_wanted(v);
    lib_guide_now = lib_guide_on ? library_guide_step(v) : -1;
    const ImVec2 strip_a(a.x + 376 * k, a.y + 72 * k), strip_b(b.x, a.y + 128 * k);
    if (lib_guide_on) library_guide_strip(d, f, v, strip_a, strip_b);
    if (route.creating) { library_create_page(d, f, v, body_a, b); return; }
    if (!route.confirmation.empty()) { library_confirm_page(d, f, v, body_a, b); return; }
    if (page != route.page) route.show_page(page);
    if (!lib_guide_on) library_role_strip(d, f, v, strip_a, strip_b);
    if (route.choosing_default) { library_default_chooser(d, f, v, body_a, b); return; }
    if (route.modifier_setup) { library_modifier_setup(d, f, v, body_a, b); return; }
    if (route.adding()) {
        text(d, body(f, 12 * k), 12 * k, {body_a.x + 146 * k, body_a.y + 2 * k}, blue_light, "ADDING TO THIS PLAYLIST");
        text(d, anton(f, 27 * k), 27 * k, {body_a.x + 146 * k, body_a.y + 25 * k}, white, elide(anton(f, 27 * k), 27 * k, upper(route.adding_to), b.x - body_a.x - 440 * k));
        if (lib_button(d, f, "##backtoplaylist", {body_a.x, body_a.y + 8 * k}, {130 * k, 38 * k}, "< PLAYLIST", Look::ghost)) { route.adding_to.clear(); return; }
        if (lib_button(d, f, "##pickerundo", {b.x - 264 * k, body_a.y + 8 * k}, {112 * k, 38 * k}, v.playlist_redo ? "REDO" : "UNDO", Look::ghost, v.playlist_can_undo)) {
            std::string m; studio::undo_playlist_entries(route.adding_to, m); say(m);
        }
        const ImVec2 done_a(b.x - 140 * k, body_a.y + 8 * k);
        if (lib_button(d, f, "##doneadding", done_a, {140 * k, 38 * k}, "DONE", Look::blue)) { route.adding_to.clear(); return; }
        if (lib_guide_on && lib_guide_now >= 2) lib_pulse(d, done_a, {done_a.x + 140 * k, done_a.y + 38 * k}, k);
        if (lib_guide_on)
            text(d, body(f, 13 * k), 13 * k, {body_a.x + 146 * k, body_a.y + 60 * k}, blue_light,
                 elide(body(f, 13 * k), 13 * k, "STEP 2 OF 4 - ADD WHOLE PACK puts a whole pack in; PICK SINGLE TRICKS to choose some (PREVIEW plays one first). Then DONE.",
                       b.x - body_a.x - 150 * k));
        else
            text(d, body(f, 12 * k), 12 * k, {body_a.x + 146 * k, body_a.y + 60 * k}, soft, "Add only what you want. Every change is saved; DONE goes back to the playlist.");
        library_browser(d, f, v, index, route.adding_to, {body_a.x, body_a.y + 88 * k}, b);
        return;
    }
    if (route.page == 1) {
        text(d, anton(f, 23 * k), 23 * k, {body_a.x + 4 * k, body_a.y + 7 * k}, white, "YOUR COLLECTION");
        if (lib_button(d, f, "##librarynewpack", {b.x - 162 * k, body_a.y}, {162 * k, 36 * k}, "+ MAKE A PACK", Look::ghost)) { make_pack(v); u.pack_page = true; }
        library_browser(d, f, v, index, {}, {body_a.x, body_a.y + 54 * k}, b); return;
    }
    const float nav_w = 266 * k, gap = 18 * k;
    const ImVec2 na(body_a.x, body_a.y), nb(na.x + nav_w, b.y), ca(nb.x + gap, body_a.y);
    library_panel_box(d, f, na, nb);
    lib_caps(d, f, {na.x + 16 * k, na.y + 22 * k}, 14 * k, white, "YOUR PLAYLISTS", 2.0f);
    const ImVec2 new_a(na.x + 12 * k, na.y + 58 * k);
    if (lib_button(d, f, "##librarynewlist", new_a, {nav_w - 24 * k, 38 * k}, "+ NEW PLAYLIST", Look::blue)) library_start_create();
    if (lib_guide_on && lib_guide_now == 0) lib_pulse(d, new_a, {new_a.x + nav_w - 24 * k, new_a.y + 38 * k}, k);
    const auto binding = studio::modifier_binding();
    const ImVec2 la(na.x + 10 * k, na.y + 112 * k), lb(nb.x - 10 * k, nb.y - 12 * k);
    begin_region(201, la, lb, route.playlists_scroll, k);
    float y = la.y - route.playlists_scroll;
    for (const auto& list : v.playlists) {
        ImGui::PushID(list.name.c_str()); const bool selected = list.name == v.playlist_open;
        const ImVec2 ta(la.x + 4 * k, y + 6 * k), tb(lb.x - 9 * k, y + 98 * k);
        bool lit = false;
        const bool pressed = hit("##destination", ta, tb, &lit);
        if (tb.y > la.y && ta.y < lb.y) {
            // Cream paper, the open one on blue and taped down; its words in black on cream, white on blue.
            library_card(d, f, ta, tb, selected, lit, 300 + static_cast<std::uint32_t>(std::hash<std::string>{}(list.name) % 997));
            const ImU32 ink = selected ? IM_COL32_WHITE : shop_ink, sub = selected ? IM_COL32(214, 230, 255, 255) : IM_COL32(86, 88, 96, 255);
            lib_caps(d, f, {ta.x + 16 * k, ta.y + 14 * k}, 10 * k, sub, (selected ? "OPEN / " : "") + std::to_string(list.size) + (list.size == 1 ? " ENTRY" : " ENTRIES"));
            text(d, anton(f, 22 * k), 22 * k, {ta.x + 16 * k, ta.y + 30 * k}, ink, elide(anton(f, 22 * k), 22 * k, upper(list.name), nav_w - 52 * k));
            const bool normal = list.name == v.playlist_playing, held = list.name == binding.playlist;
            const char* role = normal && held ? "default + button" : normal ? "plays by default" : held ? "on the button" : "not playing";
            // What it does, in marker: blue when it plays, grey when it is only saved; smaller if it would run off the card.
            float role_size = 20 * k;
            while (role_size > 12 * k && sharp(marker(f, role_size), role_size)->CalcTextSizeA(role_size, FLT_MAX, 0, role).x > tb.x - ta.x - 30 * k)
                role_size -= 1 * k;
            marker_words(d, f, {ta.x + 16 * k, ta.y + 62 * k}, role, role_size, -1.5f,
                         normal || held ? (selected ? IM_COL32_WHITE : IM_COL32(0, 98, 214, 255)) : (selected ? IM_COL32(200, 216, 240, 255) : IM_COL32(120, 122, 128, 255)));
            if (lit && nav().keys) chosen_outline(d, ta, tb, k);
        }
        if (pressed) { studio::open_playlist(list.name); route.select(list.name); }
        ImGui::PopID(); y += 104 * k;
    }
    if (v.playlists.empty()) y += paragraph(d, body(f, 14 * k), 14 * k, {la.x + 8 * k, y}, nav_w - 36 * k, soft, "No playlists yet: QUICK START makes one for you.") + 22 * k;
    end_region(d, la, lb, y + route.playlists_scroll - la.y, route.playlists_scroll, k);
    if (route.settings) library_playlist_settings(d, f, v, ca, b);
    else library_playlist_content(d, f, v, index, ca, b);
}
