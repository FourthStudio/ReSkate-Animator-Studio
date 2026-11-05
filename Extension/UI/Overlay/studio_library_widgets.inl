void library_choose_playlists(const studio::PlaylistEntry& source, const std::string& name) {
    auto& u = ui();
    u.playlist_for = name; u.playlist_for_entry = source; u.library_modal_scroll = 0;
}
std::string library_stance(const studio::View& v, const studio::Preset& p) {
    static constexpr std::array<const char*, 4> names{"NORMAL", "FAKIE", "SWITCH", "SWITCH FAKIE"}; // in capitals, as TRICKS has them
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
// plays, each a plate with its own CHANGE button (the links a new player could not find on the page before), and the setup
// guide again. Playback has no switch: your playlists always play (the user, 2026-11-05). The words are 13 units and the names Anton 24, so they read at 720p. Each plate is as
// wide as what is on it, and the strip as long as its plates and switches, against the page's right edge as the sheet
// under it: no wide empty stretch between CHANGE and what it changes, or across the strip on a wide screen.
std::size_t library_entries(const studio::View& v, const std::string& name) {
    for (const auto& list : v.playlists)
        if (list.name == name) return list.size;
    return 0;
}
void library_role_strip(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    const auto bind = studio::modifier_binding();
    const bool enabled = studio::modifier_mask(bind) != 0;
    const float caps = 13 * k, name_size = 24 * k, mid = (a.y + b.y) * 0.5f, gap = 12 * k;
    const float guide_w = lib_w(f, "GUIDE", 36 * k, 96 * k), change_w = lib_w(f, "CHANGE", 32 * k, 92 * k);
    const float tail = 24 * k + guide_w + 14 * k; // GUIDE
    const float most = std::max(220 * k, (b.x - a.x - 14 * k - tail - gap) * 0.5f);
    // What each plate shows, and how wide that makes it.
    struct Plate {
        std::string role, name, meta;
        ImU32 role_ink{};
        bool set{};
        float meta_w{}, name_w{}, w{};
    };
    const auto measure = [&](Plate& p) {
        p.meta_w = p.meta.empty() ? 0 : spaced_width(mont(f, 12 * k), 12 * k, p.meta, 1.2f * k) + 16 * k;
        p.name_w = text_size(anton(f, name_size), name_size, upper(p.name).c_str()).x;
        const float role_w = spaced_width(mont(f, caps), caps, p.role, 1.6f * k);
        // A little over what is measured: at 720p the small sizes' rounded advances run a few per cent wider.
        p.w = std::clamp(std::max(role_w, p.name_w + p.meta_w) * 1.05f + 16 * k + change_w + 26 * k, 260 * k, most);
    };
    const bool playing = !v.playlist_playing.empty();
    const auto entries = [&](const std::string& name) {
        const auto n = library_entries(v, name);
        return std::to_string(n) + (n == 1 ? " ENTRY" : " ENTRIES");
    };
    Plate normal{v.play_in_game ? "PLAYS BY DEFAULT" : "PLAYS BY DEFAULT / PLAYBACK OFF", playing ? v.playlist_playing : "nothing yet",
                 playing ? entries(v.playlist_playing) : std::string{}, v.play_in_game && playing ? lib_blue : lib_warn, playing};
    const std::string held = std::string("HOLD ") + studio::modifier_button_names[static_cast<std::size_t>(bind.button)];
    // With no button chosen it says so ("no button yet"), not the playlist that would play on one: a name there read as
    // if it already played on a button.
    Plate button{!enabled ? "ON A BUTTON" : !v.play_in_game ? held + " / OFF" : !v.modifier_ready ? held + " / LOADING" : held,
                 enabled ? bind.playlist : "no button yet", enabled ? entries(bind.playlist) : std::string{},
                 enabled && v.modifier_ready && v.play_in_game ? lib_blue : lib_ink_soft, enabled};
    measure(normal);
    measure(button);
    a.x = std::max(a.x, b.x - (14 * k + normal.w + gap + button.w + tail));
    library_sheet(d, f, a, b);
    masking_tape(d, f, {a.x + 2 * k, a.y + 3 * k}, 38 * k, 13 * k, -38, 401); // over the corner, clear of the words
    float x = a.x + 14 * k;
    const auto draw = [&](const char* id, const Plate& p) {
        const ImVec2 pa(x, a.y + 8 * k), pb(x + p.w, b.y - 8 * k);
        d->AddRectFilled(pa, pb, IM_COL32(255, 255, 255, 12));
        d->AddRect(pa, pb, IM_COL32(255, 255, 255, 38), 0, 0, 1 * k);
        const float text_room = p.w - 16 * k - change_w - 20 * k;
        lib_caps(d, f, {pa.x + 14 * k, pa.y + 8 * k}, caps, p.role_ink, lib_caps_fit(f, caps, p.role, text_room));
        const bool show_meta = p.meta_w > 0 && p.name_w + p.meta_w <= text_room;
        const std::string shown = elide(anton(f, name_size), name_size, upper(p.name), text_room - (show_meta ? p.meta_w : 0));
        text(d, anton(f, name_size), name_size, {pa.x + 14 * k, pa.y + 25 * k}, p.set ? lib_ink : lib_ink_faint, shown);
        if (show_meta)
            lib_caps(d, f, {pa.x + 26 * k + text_size(anton(f, name_size), name_size, shown.c_str()).x, pa.y + 36 * k}, 12 * k, lib_ink_soft, p.meta, 1.2f);
        const bool pressed = lib_button(d, f, id, {pb.x - 12 * k - change_w, (pa.y + pb.y) * 0.5f - 16 * k}, {change_w, 32 * k}, "CHANGE", Look::ghost);
        x = pb.x + gap;
        return pressed;
    };
    if (draw("##roledefault", normal)) {
        route.choosing_default = true;
        route.modifier_lists_scroll = 0;
    }
    if (draw("##rolebutton", button)) {
        route.modifier_setup = true;
        route.settings_scroll = 0;
    }
    if (lib_button(d, f, "##showguide", {b.x - 14 * k - guide_w, mid - 18 * k}, {guide_w, 36 * k}, "GUIDE", Look::ghost)) {
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
// The playlist the guide is about: the one that plays by default, else the one open (else the first).
std::string library_guide_list(const studio::View& v) {
    if (!v.playlist_playing.empty()) return v.playlist_playing;
    if (!v.playlist_open.empty()) return v.playlist_open;
    return v.playlists.empty() ? std::string{} : v.playlists.front().name;
}
// 0 no playlist; 1 the guide's playlist is empty; 2 none plays by default, or playback is off; 3 set up: try it. (Step 3
// was "not the open one" before, so opening another playlist asked to replace a default that played fine, and playback
// being off was never named.)
int library_guide_step(const studio::View& v) {
    if (v.playlists.empty()) return 0;
    if (library_entries(v, library_guide_list(v)) == 0) return 1;
    if (v.playlist_playing.empty() || !v.play_in_game) return 2;
    return 3;
}
bool library_guide_wanted(const studio::View& v) {
    auto& u = ui();
    if (library_guide_step(v) < 3 && !u.library_guide_hidden) u.library_guide_started = true;
    return u.library_guide_started && !u.library_guide_finished && !u.library_guide_hidden;
}
void library_guide_strip(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); auto& route = u.library; const float k = f.k;
    library_sheet(d, f, a, b);
    // Step 3 is SET AS DEFAULT while nothing plays by default, TURN PLAYBACK ON when one does but playback is off.
    const bool playback_off = !v.playlist_playing.empty() && !v.play_in_game;
    const std::array<const char*, 4> names{"MAKE A PLAYLIST", "ADD TRICKS", playback_off ? "TURN PLAYBACK ON" : "SET AS DEFAULT", "TRY IT IN GAME"};
    const std::array<const char*, 4> next{"MAKE A PLAYLIST", "+ ADD TRICKS", playback_off ? "TURN PLAYBACK ON" : "SET AS DEFAULT", "CLOSE AND TRY IT"};
    const int step = library_guide_step(v);
    const float mid = (a.y + b.y) * 0.5f, fs = 13 * k;
    const float hide_w = lib_w(f, "X", 38 * k, 44 * k), next_w = lib_w(f, next[static_cast<std::size_t>(step)], 38 * k, 220 * k);
    // Every step named when they fit; on a narrow screen only the one to do now is (the others are their numbers).
    const float room = b.x - a.x - 92 * k - next_w - hide_w - 40 * k;
    float all = 0;
    for (const auto* name : names) all += 34 * k + spaced_width(mont(f, fs), fs, name, 1.4f * k) + 22 * k;
    const bool every = all <= room;
    lib_caps(d, f, {a.x + 16 * k, mid - 7 * k}, 13 * k, lib_blue, "SET UP", 2.0f);
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
    if (lib_button(d, f, "##guidenext", {b.x - hide_w - 14 * k - next_w, mid - 19 * k}, {next_w, 38 * k}, next[static_cast<std::size_t>(step)], Look::blue)) {
        std::string m;
        const auto list = library_guide_list(v);
        switch (step) {
        case 0: library_start_create(); break;
        case 1:
            studio::open_playlist(list);
            route.page = 0;
            route.add_to(list);
            break;
        case 2:
            if (v.playlist_playing.empty()) {
                studio::play_playlist(list, m);
                say(m);
            } else say("Playback is on: your playlists play in game.");
            studio::set_play_in_game(true);
            break;
        default:
            u.library_guide_finished = true;
            studio::request_close();
            break;
        }
    }
    if (lib_button(d, f, "##guidehide", {b.x - hide_w - 8 * k, mid - 19 * k}, {hide_w, 38 * k}, "X", Look::ghost)) u.library_guide_hidden = true;
}
void library_modifier_setup(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& route = ui().library; const float k = f.k;
    const auto bind = studio::modifier_binding();
    // A column of at most 1200 units: on 21:9 the page's full width made 2000-unit buttons.
    const float x = a.x + 20 * k, w = std::min(b.x - a.x - 40 * k, 1200 * k);
    library_sheet(d, f, a, b);
    text(d, anton(f, 29 * k), 29 * k, {x, a.y + 18 * k}, lib_ink, route.choosing_modifier_playlist ? "CHOOSE A BUTTON PLAYLIST" : "ONE INPUT. TWO VERSIONS.");
    const char* close_label = route.choosing_modifier_playlist ? "< BACK" : "DONE";
    const float close_w = lib_w(f, close_label, 36 * k, 106 * k);
    if (lib_button(d, f, "##closemodifierhelp", {x + w - close_w, a.y + 15 * k}, {close_w, 36 * k}, close_label, Look::paper)) {
        if (route.choosing_modifier_playlist) route.choosing_modifier_playlist = false;
        else route.modifier_setup = false;
    }
    const ImVec2 la(x, a.y + 74 * k), lb(x + w, b.y - 16 * k);
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
    if (!bind.playlist.empty() && lib_button(d, f, "##editmodifiercontent", {x, y}, {lib_w(f, "EDIT THIS PLAYLIST", 32 * k, 236 * k), 32 * k}, "EDIT THIS PLAYLIST", Look::ghost)) {
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
    const char* reserve = bind.block_native ? "ON / USE THIS BUTTON FOR TRICKS" : "OFF / KEEP ITS ORIGINAL ACTION TOO";
    if (lib_button(d, f, "##reservenative", {x, y}, {std::min(w - 8 * k, lib_w(f, reserve, 38 * k, 430 * k)), 38 * k}, reserve,
                   bind.block_native ? Look::blue : Look::ghost)) {
        auto live = studio::modifier_binding(); live.block_native = !live.block_native; studio::set_modifier_binding(live);
    }
    y += 50 * k;
    y += paragraph(d, body(f, 13 * k), 13 * k, {x, y}, w - 8 * k, lib_ink_soft,
        "With RB / R1 this prevents the native darkcatch. Menus keep their normal controls. Hold before the move starts; releasing during the move keeps the animation you chose. A missing compatible version falls back to regular playback.") + 18 * k;
    if (bind.playlist == v.playlist_playing && !bind.playlist.empty())
        y += paragraph(d, body(f, 14 * k), 14 * k, {x, y}, w - 8 * k, lib_warn,
            "Both inputs currently use the same playlist. Choose a different one here to get different animations.") + 16 * k;
    if (lib_button(d, f, "##collectmodifierhints", {x, y}, {std::min(w - 8 * k, lib_w(f, "MAKE A PLAYLIST FROM MODIFIER TAGS", 38 * k, 460 * k)), 38 * k},
                   "MAKE A PLAYLIST FROM MODIFIER TAGS", Look::paper)) {
        std::string m; studio::playlist_from_modifier_tags(m); say(m);
    }
    y += 50 * k;
    y += paragraph(d, body(f, 12 * k), 12 * k, {x, y}, w - 8 * k, lib_ink_faint,
        "Tags are optional recommendations from animation makers. You can put any animation in either playlist. These settings are saved automatically. L3 / R3 may also be used by your Studio opening shortcut.") + 14 * k;
    end_region(d, la, lb, y + route.settings_scroll - la.y, route.settings_scroll, k);
}
