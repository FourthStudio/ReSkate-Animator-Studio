void library_choose_playlists(const studio::PlaylistEntry& source, const std::string& name) {
    auto& u = ui();
    u.playlist_for = name; u.playlist_for_entry = source; u.library_modal_scroll = 0;
}
std::string library_stance(const studio::View& v, const studio::Preset& p) {
    static constexpr std::array<const char*, 4> names{"Normal", "Fakie", "Switch", "Switch fakie"};
    return names[static_cast<std::size_t>(std::clamp(studio::stance_version(p, v.own_lead_right), 0, 3))];
}
void library_member_row(ImDrawList* d, const Fonts& f, const studio::View& v, const LibraryIndex& index,
                        const studio::Preset& p, const std::string& source, const std::string& target,
                        bool content, ImVec2 a, float w) {
    const float k = f.k;
    const bool have = !target.empty() && index.member(p.name).included();
    const ImVec2 b(a.x + w, a.y + 82 * k);
    ImGui::PushID((source + "|" + p.name).c_str());
    const float action_x = b.x - 304 * k, text_w = std::max(40 * k, w - 338 * k);
    const auto register_controls = [&](bool draw) {
        bool lit = false;
        if (hit("##rowpreview", ImVec2(a.x + 4 * k, a.y + 4 * k), ImVec2(action_x - 10 * k, b.y - 4 * k), &lit, p.available))
            preview_trick(source, p.name, !source.empty());
        const auto button = [&](const char* id, float x, float bw, const std::string& label, Look look, bool enabled) {
            if (!draw) return hit(id, {x, a.y + 23 * k}, {x + bw, a.y + 59 * k}, nullptr, enabled);
            return lib_button(d, f, id, {x, a.y + 23 * k}, {bw, 36 * k}, label, look, enabled);
        };
        if (button("##preview", action_x, 92 * k, "PREVIEW", Look::ghost, p.available)) preview_trick(source, p.name, !source.empty());
        if (button("##edit", action_x + 100 * k, 68 * k, p.locked ? "VIEW" : "EDIT", Look::ghost, p.available)) open_or_preview(v, p.name, source);
        const bool can = target.empty() || p.available || have || content;
        const auto label = target.empty() ? "PLAYLISTS..." : content || have ? "REMOVE" : "+ ADD";
        if (button("##membership", action_x + 176 * k, 116 * k, label, have || content ? Look::ghost : Look::blue, can)) {
            if (target.empty()) library_choose_playlists({source, p.name}, p.name);
            else { std::string m; studio::edit_playlist_member(target, {source, p.name}, !(have || content), m); say(m); }
        }
        return lit;
    };
    const auto& n = nav();
    const bool focused = n.keys && (n.focus == ImGui::GetID("##rowpreview") || n.focus == ImGui::GetID("##membership") ||
        n.focus == ImGui::GetID("##preview") || n.focus == ImGui::GetID("##edit"));
    if (n.clipped && (b.y <= n.clip_a.y || a.y >= n.clip_b.y) && !focused) {
        // Offscreen navigation still needs its targets for focus scrolling, without thousands of hidden text meshes.
        register_controls(false); ImGui::PopID(); return;
    }
    lib_row_box(d, a, b, have && !content ? lib_row_in : lib_row, k);
    const bool lit = register_controls(true);
    if (lit && nav().keys) chosen_outline(d, a, {action_x - 10 * k, b.y}, k);
    text(d, body(f, 17 * k), 17 * k, {a.x + 16 * k, a.y + 10 * k}, lib_ink, elide(body(f, 17 * k), 17 * k, library_saved_name(index, p.name), text_w));
    const auto properties = index.label(p.trick) + (p.trick.starts_with("native:") ? "" : " / " + (p.pop.empty() ? std::string("Any pop") : p.pop + " pop")) + " / " + library_stance(v, p) + (p.modifier_hint ? " / modifier hint" : "");
    text(d, body(f, 12 * k), 12 * k, {a.x + 16 * k, a.y + 35 * k}, lib_ink_soft, elide(body(f, 12 * k), 12 * k, properties, text_w));
    const auto origin = !p.available ? "Clip missing / restore this animation to preview it" :
        (have && !content ? "In this playlist / " : "") + library_origin(index, p.name);
    text(d, body(f, 11 * k), 11 * k, {a.x + 16 * k, a.y + 57 * k}, !p.available ? lib_red : have && !content ? lib_blue : lib_ink_faint,
        elide(body(f, 11 * k), 11 * k, origin, text_w));
    ImGui::PopID();
}
void library_role_strip(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    const float k = f.k;
    const auto bind = studio::modifier_binding();
    const bool enabled = studio::modifier_mask(bind) != 0;
    const float resume_w = v.play_in_game ? 0.f : 102 * k;
    const float setup_w = 142 * k, half = (b.x - a.x - setup_w - resume_w - 24 * k) * 0.5f;
    // NOW PLAYING: what plays by default and what plays while the button is held, on a strip of grip tape, the default one
    // taped down with its name in the shop's capitals.
    library_sheet(d, f, a, b, PaperKind::concrete);
    const OnPaper on_paper;
    masking_tape(d, f, {a.x + 26 * k, a.y + 2 * k}, 52 * k, 15 * k, -7, 401);
    const ImU32 paused = lib_warn;
    lib_caps(d, f, {a.x + 14 * k, a.y + 10 * k}, 10 * k, v.play_in_game ? lib_blue : paused, v.play_in_game ? "PLAYS BY DEFAULT" : "PAUSED / DEFAULT");
    text(d, anton(f, 19 * k), 19 * k, {a.x + 14 * k, a.y + 25 * k}, lib_ink,
         elide(anton(f, 19 * k), 19 * k, upper(v.playlist_playing.empty() ? "the game's own animations" : v.playlist_playing), half - 24 * k));
    d->AddLine({a.x + half, a.y + 10 * k}, {a.x + half, b.y - 10 * k}, IM_COL32(0, 0, 0, 70), 1.5f * k);
    lib_caps(d, f, {a.x + half + 14 * k, a.y + 10 * k}, 10 * k, enabled && v.modifier_ready ? lib_blue : lib_ink_faint,
             enabled ? !v.play_in_game ? "ON THE BUTTON / PAUSED" : !v.modifier_ready ? "ON THE BUTTON / NOT READY"
                 : std::string("HOLD ") + studio::modifier_button_names[static_cast<std::size_t>(bind.button)] : "ON THE BUTTON / OFF");
    text(d, anton(f, 19 * k), 19 * k, {a.x + half + 14 * k, a.y + 25 * k}, bind.playlist.empty() ? lib_ink_faint : lib_ink,
         elide(anton(f, 19 * k), 19 * k, upper(bind.playlist.empty() ? "nothing set" : bind.playlist), half - 24 * k));
    if (!v.play_in_game && lib_button(d, f, "##resumeplayback", {b.x - setup_w - resume_w, a.y + 9 * k}, {resume_w - 8 * k, 36 * k}, "RESUME", Look::blue))
        studio::set_play_in_game(true);
    if (lib_button(d, f, "##editmodifier", {b.x - setup_w, a.y + 9 * k}, {setup_w - 10 * k, 36 * k}, "BUTTON SETUP", Look::ghost)) {
        ui().library.modifier_setup = true; ui().library.settings_scroll = 0;
    }
}
// A step of a setup: its number circled in blue marker, its name in the shop's capitals.
void library_step(ImDrawList* d, const Fonts& f, ImVec2 at, int number, std::string_view words) {
    const float k = f.k;
    const auto n = std::to_string(number);
    const ImVec2 c(at.x + 14 * k, at.y + 12 * k);
    marker_words(d, f, ImVec2(c.x - 7 * k, c.y - 17 * k), n, 28 * k, -6, lib_blue);
    marker_circle(d, c, 17 * k, 15 * k, 3 * k, lib_blue, 500 + static_cast<std::uint32_t>(number));
    lib_caps(d, f, ImVec2(at.x + 42 * k, at.y + 4 * k), 15 * k, lib_ink, words, 1.8f);
}
void library_modifier_setup(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& route = ui().library; const float k = f.k;
    const auto bind = studio::modifier_binding();
    const float x = a.x + 20 * k, w = b.x - a.x - 40 * k;
    library_sheet(d, f, a, b);
    const OnPaper on_paper;
    text(d, anton(f, 29 * k), 29 * k, {x, a.y + 18 * k}, lib_ink, route.choosing_modifier_playlist ? "CHOOSE A BUTTON PLAYLIST" : "ONE INPUT. TWO VERSIONS.");
    if (lib_button(d, f, "##closemodifierhelp", {b.x - 126 * k, a.y + 15 * k}, {106 * k, 36 * k}, route.choosing_modifier_playlist ? "< BACK" : "DONE", Look::paper)) {
        if (route.choosing_modifier_playlist) route.choosing_modifier_playlist = false;
        else route.modifier_setup = false;
    }
    const ImVec2 la(x, a.y + 74 * k), lb(b.x - 20 * k, b.y - 16 * k);
    if (route.choosing_modifier_playlist) {
        begin_region(207, la, lb, route.modifier_lists_scroll, k);
        float y = la.y - route.modifier_lists_scroll;
        const auto choose = [&](const std::string& name, const std::string& label) {
            ImGui::PushID(name.c_str());
            if (lib_button(d, f, "##choosemodifierlist", {x, y}, {w - 8 * k, 48 * k}, label, name == bind.playlist ? Look::blue : Look::dark)) {
                auto live = studio::modifier_binding(); live.playlist = name; studio::set_modifier_binding(live);
                route.choosing_modifier_playlist = false;
            }
            ImGui::PopID(); y += 56 * k;
        };
        choose({}, "NONE / DISABLE THE MODIFIER");
        for (const auto& list : v.playlists) choose(list.name, list.name);
        end_region(d, la, lb, y + route.modifier_lists_scroll - la.y, route.modifier_lists_scroll, k);
        return;
    }
    begin_region(204, la, lb, route.settings_scroll, k);
    float y = la.y - route.settings_scroll;
    y += paragraph(d, body(f, 16 * k), 16 * k, {x, y}, w, lib_ink_soft,
        "Keep your regular kickflip. Put a dolphin flip version of the same kickflip input in another playlist, then hold a button to use it. Grinds, manuals and grabs work the same way.") + 22 * k;
    const float half = (w - 16 * k) * 0.5f;
    lib_row_box(d, {x, y}, {x + half, y + 82 * k}, lib_row, k);
    lib_row_box(d, {x + half + 16 * k, y}, {x + w - 8 * k, y + 82 * k}, lib_row_in, k);
    text(d, body(f, 12 * k), 12 * k, {x + 16 * k, y + 14 * k}, lib_ink_soft, "KICKFLIP INPUT");
    text(d, anton(f, 22 * k), 22 * k, {x + 16 * k, y + 38 * k}, lib_ink, "YOUR REGULAR KICKFLIP");
    text(d, body(f, 12 * k), 12 * k, {x + half + 32 * k, y + 14 * k}, lib_blue, "BUTTON + KICKFLIP INPUT");
    text(d, anton(f, 22 * k), 22 * k, {x + half + 32 * k, y + 38 * k}, lib_ink, "YOUR DOLPHIN FLIP");
    y += 106 * k;
    library_step(d, f, {x, y}, 1, "CHOOSE THE PLAYLIST FOR THE BUTTON"); y += 40 * k;
    if (lib_button(d, f, "##selectmodifierplaylist", {x, y}, {w - 8 * k, 42 * k}, bind.playlist.empty() ? "CHOOSE A PLAYLIST..." : bind.playlist + " / CHANGE...", Look::dark))
        route.choosing_modifier_playlist = true;
    y += 54 * k;
    if (!bind.playlist.empty() && lib_button(d, f, "##editmodifiercontent", {x, y}, {236 * k, 32 * k}, "EDIT THIS PLAYLIST", Look::ghost)) {
        studio::open_playlist(bind.playlist); route.select(bind.playlist); route.page = 0; route.modifier_setup = false;
    }
    if (bind.playlist.empty()) y += paragraph(d, body(f, 13 * k), 13 * k, {x, y}, w, lib_ink_faint, "Create a playlist in Library > Playlists first, then choose it here.");
    y += 52 * k;
    if (studio::modifier_mask(bind) && !v.modifier_ready)
        y += paragraph(d, body(f, 13 * k), 13 * k, {x, y}, w - 8 * k, lib_warn,
            "No alternate animations are ready. Add playable animations to this playlist and allow it to finish loading. While inactive, the button keeps its game action.") + 20 * k;
    library_step(d, f, {x, y}, 2, "CHOOSE A BUTTON TO HOLD"); y += 40 * k;
    const int columns = 3; const float cell = (w - 24 * k) / static_cast<float>(columns);
    for (std::size_t i = 0; i < studio::modifier_button_names.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (lib_button(d, f, "##modifierbutton", {x + static_cast<float>(i % 3) * (cell + 8 * k), y + static_cast<float>(i / 3) * 44 * k}, {cell, 36 * k},
                   studio::modifier_button_names[i], bind.button == static_cast<int>(i) ? Look::blue : Look::ghost)) {
            auto live = studio::modifier_binding(); live.button = static_cast<int>(i); studio::set_modifier_binding(live);
        }
        ImGui::PopID();
    }
    y += 146 * k;
    library_step(d, f, {x, y}, 3, "KEEP OR TAKE OVER ITS GAME ACTION"); y += 40 * k;
    if (lib_button(d, f, "##reservenative", {x, y}, {std::min(w - 8 * k, 430 * k), 38 * k},
               bind.block_native ? "ON / USE THIS BUTTON FOR ANIMATIONS" : "OFF / KEEP ITS ORIGINAL ACTION TOO", bind.block_native ? Look::blue : Look::ghost)) {
        auto live = studio::modifier_binding(); live.block_native = !live.block_native; studio::set_modifier_binding(live);
    }
    y += 50 * k;
    y += paragraph(d, body(f, 13 * k), 13 * k, {x, y}, w - 8 * k, lib_ink_soft,
        "With RB / R1 this prevents the native darkcatch. Menus keep their normal controls. Hold before the move starts; releasing during the move keeps the animation you chose. A missing compatible version falls back to regular playback.") + 18 * k;
    if (bind.playlist == v.playlist_playing && !bind.playlist.empty())
        y += paragraph(d, body(f, 14 * k), 14 * k, {x, y}, w - 8 * k, lib_warn,
            "Both inputs currently use the same playlist. Choose a different one here to get different animations.") + 16 * k;
    if (lib_button(d, f, "##collectmodifierhints", {x, y}, {std::min(w - 8 * k, 460 * k), 38 * k}, "MAKE A PLAYLIST FROM MODIFIER TAGS", Look::paper)) {
        std::string m; studio::playlist_from_modifier_tags(m); say(m);
    }
    y += 50 * k;
    y += paragraph(d, body(f, 12 * k), 12 * k, {x, y}, w - 8 * k, lib_ink_faint,
        "Tags are optional recommendations from animation makers. You can put any animation in either playlist. These settings are saved automatically. L3 / R3 may also be used by your Studio opening shortcut.") + 14 * k;
    end_region(d, la, lb, y + route.settings_scroll - la.y, route.settings_scroll, k);
}
