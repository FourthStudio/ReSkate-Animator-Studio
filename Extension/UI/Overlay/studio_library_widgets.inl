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
// WHAT PLAYS, across the top of every part of the LIBRARY: the playlist that plays by default and the one a held button
// plays, each pressed to change it (the links a new player could not find on the page before), playback on or off, and
// the setup guide again.
void library_role_strip(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    const auto bind = studio::modifier_binding();
    const bool enabled = studio::modifier_mask(bind) != 0;
    library_sheet(d, f, a, b);
    masking_tape(d, f, {a.x + 2 * k, a.y + 3 * k}, 38 * k, 13 * k, -38, 401); // over the corner, clear of the words
    const float guide_w = 96 * k, play_w = 152 * k;
    const float slots_w = std::max(200 * k, b.x - a.x - guide_w - play_w - 36 * k), half = slots_w * 0.5f;
    // A slot: its role in capitals, the playlist in Anton, CHANGE at its right; the whole of it is the button.
    const auto slot = [&](const char* id, float x, float w, const std::string& role, ImU32 role_ink, const std::string& name, bool set) {
        const ImVec2 sa(x, a.y + 5 * k), sb(x + w, b.y - 5 * k);
        bool lit = false;
        const bool pressed = hit(id, sa, sb, &lit);
        if (lit) d->AddRectFilled(sa, sb, IM_COL32(255, 255, 255, 20));
        if (lit && nav().keys) chosen_outline(d, sa, sb, k);
        const float change_w = spaced_width(mont(f, 10 * k), 10 * k, "CHANGE >", 1.6f * k) + 10 * k;
        lib_caps(d, f, {sb.x - change_w, a.y + 21 * k}, 10 * k, lit ? lib_blue : lib_ink_faint, "CHANGE >");
        lib_caps(d, f, {x + 14 * k, a.y + 11 * k}, 10 * k, role_ink, role);
        text(d, anton(f, 19 * k), 19 * k, {x + 14 * k, a.y + 26 * k}, set ? lib_ink : lib_ink_faint,
             elide(anton(f, 19 * k), 19 * k, upper(name), w - change_w - 28 * k));
        return pressed;
    };
    const bool playing = !v.playlist_playing.empty();
    if (slot("##roledefault", a.x + 8 * k, half - 8 * k, v.play_in_game ? "PLAYS BY DEFAULT" : "PLAYS BY DEFAULT / PAUSED",
             v.play_in_game && playing ? lib_blue : lib_warn, playing ? v.playlist_playing : "nothing yet - choose one", playing)) {
        route.choosing_default = true;
        route.settings_scroll = 0;
    }
    d->AddLine({a.x + half + 2 * k, a.y + 12 * k}, {a.x + half + 2 * k, b.y - 12 * k}, IM_COL32(255, 255, 255, 40), 1.5f * k);
    const std::string held = std::string("HOLD ") + studio::modifier_button_names[static_cast<std::size_t>(bind.button)];
    if (slot("##rolebutton", a.x + half + 8 * k, half - 8 * k,
             !enabled ? "WHILE A BUTTON IS HELD" : !v.play_in_game ? held + " / PAUSED" : !v.modifier_ready ? held + " / LOADING" : held,
             enabled && v.modifier_ready && v.play_in_game ? lib_blue : lib_ink_faint, bind.playlist.empty() ? "not set up" : bind.playlist,
             !bind.playlist.empty())) {
        route.modifier_setup = true;
        route.settings_scroll = 0;
    }
    // Playback on or off: every playlist at once (the game's own animations while it is off).
    const float px = a.x + slots_w + 18 * k;
    lib_caps(d, f, {px, a.y + 22 * k}, 10 * k, v.play_in_game ? lib_ink_soft : lib_warn, "PLAYBACK");
    if (toggle(d, f, "##playback", {px + play_w - 76 * k, a.y + 13 * k}, v.play_in_game)) {
        studio::set_play_in_game(!v.play_in_game);
        say(v.play_in_game ? "Playback is off: the game's own animations play until you switch it on again."
                           : "Playback is on: your playlists play in game.");
    }
    // The setup guide again, whenever it is wanted (it shows by itself only while something is not set up).
    if (lib_button(d, f, "##showguide", {b.x - guide_w - 6 * k, a.y + 10 * k}, {guide_w - 4 * k, 36 * k}, "GUIDE", Look::ghost)) {
        u.library_guide_hidden = u.library_guide_finished = false;
        u.library_guide_started = true;
    }
}
// A step's badge: a disc with its number in Anton, centred in it (done: the studio's blue with a tick; now: a blue ring;
// still to come: a grey ring). Drawn whole, never cut.
enum class StepState { done, now, later };
void step_badge(ImDrawList* d, const Fonts& f, ImVec2 c, float r, int number, StepState state) {
    const float k = f.k;
    if (state == StepState::done) {
        d->AddCircleFilled(c, r, blue, 28);
        d->AddLine(ImVec2(c.x - r * 0.42f, c.y + r * 0.02f), ImVec2(c.x - r * 0.1f, c.y + r * 0.34f), IM_COL32_WHITE, 2.6f * k);
        d->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.34f), ImVec2(c.x + r * 0.46f, c.y - r * 0.32f), IM_COL32_WHITE, 2.6f * k);
        return;
    }
    const ImU32 ring = state == StepState::now ? blue : IM_COL32(110, 112, 118, 255);
    d->AddCircleFilled(c, r, state == StepState::now ? IM_COL32(8, 34, 64, 255) : IM_COL32(24, 25, 28, 255), 28);
    d->AddCircle(c, r, ring, 28, 2.4f * k);
    const auto n = std::to_string(number);
    const float size = r * 1.25f;
    ImFont* font = anton(f, size);
    const ImVec2 t = text_size(font, size, n.c_str());
    text(d, font, size, ImVec2(c.x - t.x * 0.5f, c.y - t.y * 0.5f), state == StepState::now ? IM_COL32_WHITE : IM_COL32(150, 152, 158, 255), n);
}
// A step of a setup: its number on a blue disc, its name in the shop's capitals.
void library_step(ImDrawList* d, const Fonts& f, ImVec2 at, int number, std::string_view words) {
    const float k = f.k;
    step_badge(d, f, ImVec2(at.x + 15 * k, at.y + 12 * k), 14 * k, number, StepState::now);
    lib_caps(d, f, ImVec2(at.x + 40 * k, at.y + 5 * k), 15 * k, lib_ink, words, 1.8f);
}
// ---- the setup guide: a playlist made, tricks in it, playing by default, tried in game ----
// For a player new to it the LIBRARY's parts (playlists, packs, what plays by default, the button) are a lot at once, so
// until his tricks play the strip at the top walks him through it: four steps, the ones done ticked, the next one's own
// button doing it for him, and the same button on the page ringed in blue (lib_pulse) so he sees where it lives. It shows
// only while something is not set up (no playlist, nothing playing by default, or what plays is empty), and once begun
// stays till TRY IT or put away.
bool lib_guide_on = false; // this frame
int lib_guide_now = -1;    // and its step (library_guide_step), for the page's buttons to ring the one it means
void library_start_create(); // studio_library_playlists.inl
int library_guide_step(const studio::View& v) {
    if (v.playlists.empty()) return 0;
    if (v.playlist_open.empty() || v.playlist.empty()) return 1;
    if (v.playlist_playing != v.playlist_open || !v.play_in_game) return 2;
    return 3;
}
bool library_guide_wanted(const studio::View& v) {
    auto& u = ui();
    const bool unfinished = v.playlists.empty() || v.playlist_playing.empty() || !v.play_in_game ||
                            (v.playlist_open == v.playlist_playing && v.playlist.empty());
    if (unfinished && !u.library_guide_hidden) u.library_guide_started = true;
    return u.library_guide_started && !u.library_guide_finished && !u.library_guide_hidden;
}
void library_guide_strip(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    library_sheet(d, f, a, b);
    static constexpr std::array<const char*, 4> names{"MAKE A PLAYLIST", "ADD TRICKS", "SET AS DEFAULT", "TRY IT IN GAME"};
    static constexpr std::array<const char*, 4> next{"MAKE A PLAYLIST", "+ ADD TRICKS", "SET AS DEFAULT", "CLOSE AND TRY IT"};
    const int step = library_guide_step(v);
    const float mid = (a.y + b.y) * 0.5f, fs = 12 * k;
    const float hide_w = 44 * k, next_w = 220 * k;
    // Every step named when they fit; on a narrow screen only the one to do now is (the others are their numbers).
    const float room = b.x - a.x - 92 * k - next_w - hide_w - 40 * k;
    float all = 0;
    for (const auto* name : names) all += 34 * k + spaced_width(mont(f, fs), fs, name, 1.4f * k) + 22 * k;
    const bool every = all <= room;
    lib_caps(d, f, {a.x + 16 * k, mid - 6 * k}, 11 * k, lib_blue, "SET UP", 2.2f);
    float x = a.x + 92 * k;
    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        const StepState state = i < step ? StepState::done : i == step ? StepState::now : StepState::later;
        step_badge(d, f, {x + 13 * k, mid}, 13 * k, i + 1, state);
        x += 30 * k;
        if (every || i == step) {
            lib_caps(d, f, {x + 4 * k, mid - 7 * k}, fs, state == StepState::now ? lib_ink : state == StepState::done ? lib_ink_soft : lib_ink_faint,
                     names[static_cast<std::size_t>(i)], 1.4f);
            x += 4 * k + spaced_width(mont(f, fs), fs, names[static_cast<std::size_t>(i)], 1.4f * k);
        }
        x += 22 * k;
        if (i + 1 < static_cast<int>(names.size())) d->AddLine({x - 16 * k, mid}, {x - 6 * k, mid}, IM_COL32(255, 255, 255, 60), 2 * k);
    }
    // The next step's own button, and a way to put the guide away.
    if (lib_button(d, f, "##guidenext", {b.x - hide_w - 14 * k - next_w, a.y + 9 * k}, {next_w, 38 * k}, next[static_cast<std::size_t>(step)], Look::blue)) {
        std::string m;
        switch (step) {
        case 0: library_start_create(); break;
        case 1:
            if (v.playlist_open.empty() && !v.playlists.empty()) studio::open_playlist(v.playlists.front().name);
            route.page = 0;
            route.add_to(v.playlist_open.empty() ? v.playlists.front().name : v.playlist_open);
            break;
        case 2:
            studio::play_playlist(v.playlist_open, m);
            studio::set_play_in_game(true);
            say(m);
            break;
        default:
            u.library_guide_finished = true;
            studio::request_close();
            break;
        }
    }
    if (lib_button(d, f, "##guidehide", {b.x - hide_w - 8 * k, a.y + 9 * k}, {hide_w, 38 * k}, "X", Look::ghost)) u.library_guide_hidden = true;
}
void library_modifier_setup(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& route = ui().library; const float k = f.k;
    const auto bind = studio::modifier_binding();
    const float x = a.x + 20 * k, w = b.x - a.x - 40 * k;
    library_sheet(d, f, a, b);
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
    if (bind.playlist.empty()) y += paragraph(d, body(f, 13 * k), 13 * k, {x, y}, w, lib_ink_faint, "Make a playlist first (+ NEW PLAYLIST), then choose it here.");
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
