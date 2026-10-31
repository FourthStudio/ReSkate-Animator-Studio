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
    std::map<std::string, std::string> labels;
    explicit LibraryIndex(const studio::View& v) : packs(library_packs(v)), membership(studio::playlist_membership_index(v.playlist, packs)) {
        for (const auto& pack : v.packs) for (const auto& name : pack.members) origins[name].push_back(pack.name);
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
void library_panel_box(ImDrawList* d, const Fonts& f, ImVec2 a, ImVec2 b) {
    flat_rect(d, a, b, IM_COL32(15, 17, 21, 235), seed_of(a), f.k);
    d->AddLine(a, ImVec2(b.x, a.y), IM_COL32(255, 255, 255, 38), f.k);
}
void library_member_row(ImDrawList* d, const Fonts& f, const studio::View& v, const LibraryIndex& index,
                        const studio::Preset& p, const std::string& source, ImVec2 a, float w) {
    const float k = f.k;
    const auto& membership = index.member(p.name);
    const bool have = membership.included();
    const ImVec2 b(a.x + w, a.y + 70 * k);
    ImGui::PushID((source + "|" + p.name).c_str());
    const float button_w = 76 * k, text_w = std::max(30 * k, w - 126 * k);
    auto& n = nav();
    const bool focused = n.keys && (n.focus == ImGui::GetID("##membership") || n.focus == ImGui::GetID("##preview") || n.focus == ImGui::GetID("##edit"));
    if (n.clipped && (b.y <= n.clip_a.y || a.y >= n.clip_b.y) && !focused) {
        // Keep navigation targets for automatic scrolling, but do not build text and geometry for thousands of hidden rows.
        hit("##membership", ImVec2(a.x + 4 * k, a.y + 7 * k), ImVec2(b.x - 94 * k, b.y - 7 * k), nullptr,
            !v.playlist_open.empty() && (p.available || have));
        hit("##preview", ImVec2(b.x - button_w - 8 * k, a.y + 8 * k), ImVec2(b.x - 8 * k, a.y + 33 * k), nullptr, p.available);
        hit("##edit", ImVec2(b.x - button_w - 8 * k, a.y + 38 * k), ImVec2(b.x - 8 * k, a.y + 63 * k), nullptr, p.available);
        ImGui::PopID();
        return;
    }
    flat_rect(d, a, b, have ? IM_COL32(19, 42, 54, 246) : IM_COL32(28, 30, 35, 242), seed_of(a), k * 0.5f);
    bool lit = false;
    if (hit("##membership", ImVec2(a.x + 4 * k, a.y + 7 * k), ImVec2(b.x - 94 * k, b.y - 7 * k), &lit, !v.playlist_open.empty() && (p.available || have))) {
        std::string m; studio::edit_playlist_member(v.playlist_open, {source, p.name}, !have, m); say(m);
    }
    tick_box(d, ImVec2(a.x + 22 * k, a.y + 25 * k), 9 * k, have, v.playlist_open.empty() || (!p.available && !have) ? faint : have || lit ? blue_light : soft, k);
    if (lit && nav().keys) chosen_outline(d, a, ImVec2(b.x - 94 * k, b.y), k);
    text(d, f.bold, 16 * k, ImVec2(a.x + 44 * k, a.y + 8 * k), white, elide(f.bold, 16 * k, library_saved_name(index, p.name), text_w));
    const std::string properties = index.label(p.trick) + " / " + (p.pop.empty() ? "ANY" : upper(p.pop)) + " / " +
        (p.switch_stance.value_or(false) ? "SWITCH" : "NORMAL") + (p.fakie.value_or(false) ? " FAKIE" : "") + (p.modifier_hint ? " / MODIFIER HINT" : "");
    text(d, f.bold, 11 * k, ImVec2(a.x + 44 * k, a.y + 31 * k), soft, elide(f.bold, 11 * k, properties, text_w));
    const std::string origin = !p.available ? "MISSING CLIP / restore or reinstall this animation" : have ? membership.direct ? "Added individually / " + library_origin(index, p.name)
        : "Included via " + listed(membership.packs) : library_origin(index, p.name);
    text(d, f.bold, 11 * k, ImVec2(a.x + 44 * k, a.y + 49 * k), have ? blue_light : faint, elide(f.bold, 11 * k, origin, text_w));
    if (action(d, f, "##preview", ImVec2(b.x - button_w - 8 * k, a.y + 8 * k), ImVec2(button_w, 25 * k), "PREVIEW", Look::ghost, p.available))
        preview_trick(source, p.name, !source.empty());
    if (action(d, f, "##edit", ImVec2(b.x - button_w - 8 * k, a.y + 38 * k), ImVec2(button_w, 25 * k), p.locked ? "VIEW" : "EDIT", Look::ghost, p.available))
        open_or_preview(v, p.name, source);
    ImGui::PopID();
}
void library_modifier_intro(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); const float k = f.k;
    library_panel_box(d, f, a, b);
    const auto binding = studio::modifier_binding();
    const float w = b.x - a.x, half = (w - 40 * k) * 0.5f;
    text(d, f.heading, 22 * k, ImVec2(a.x + 18 * k, a.y + 10 * k), white, "ONE INPUT. TWO STYLES.");
    text(d, f.bold, 13 * k, ImVec2(a.x + 18 * k, a.y + 39 * k), soft, "Keep your everyday animations. Hold a button for your special variations.");
    const auto role = [&](float x, const char* label, const std::string& value, ImU32 ink) {
        text(d, f.heading, 13 * k, ImVec2(x, a.y + 65 * k), ink, label);
        text(d, f.bold, 16 * k, ImVec2(x, a.y + 85 * k), white, elide(f.bold, 16 * k, value, half - 150 * k));
    };
    role(a.x + 18 * k, "NORMAL INPUT", v.playlist_playing.empty() ? "Native game animations" : v.playlist_playing, soft);
    const std::string modifier = binding.playlist.empty() ? "Choose a modifier playlist" : binding.playlist;
    role(a.x + half + 36 * k, "INPUT + MODIFIER", modifier, blue_light);
    if (action(d, f, "##editnormal", ImVec2(a.x + half - 104 * k, a.y + 73 * k), ImVec2(112 * k, 32 * k), "EDIT NORMAL", Look::ghost, !v.playlist_playing.empty()))
        studio::open_playlist(v.playlist_playing);
    if (action(d, f, "##editmodifier", ImVec2(b.x - 136 * k, a.y + 73 * k), ImVec2(116 * k, 32 * k), "SET UP", Look::blue)) {
        u.library_modifier_help = !u.library_modifier_help;
        if (u.library_modifier_help) u.library_modal_scroll = 0;
        if (!binding.playlist.empty()) studio::open_playlist(binding.playlist);
    }
    const std::string hint = !v.play_in_game ? "CUSTOM PLAYBACK IS OFF / your playlists are kept, but the game uses its native animations."
        : !studio::modifier_mask(binding) ? "Modifier OFF / choose its playlist and a button in SET UP."
        : !v.modifier_ready ? "Modifier playlist has no eligible animations yet, or is still loading. Add saved tricks to it below."
        : std::string(studio::modifier_button_names[static_cast<std::size_t>(binding.button)]) + " held at the start / no matching variation: normal playlist / release does not swap the move.";
    text(d, f.bold, 11 * k, ImVec2(a.x + 18 * k, a.y + 115 * k), !v.play_in_game ? blue_light : faint,
        elide(f.bold, 11 * k, hint, w - (!v.play_in_game ? 232 : 36) * k));
    if (!v.play_in_game && action(d, f, "##libraryenableplayback", ImVec2(b.x - 204 * k, a.y + 110 * k), ImVec2(186 * k, 26 * k), "ENABLE PLAYBACK", Look::blue))
        studio::set_play_in_game(true);
}
void library_modifier_setup(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); const float k = f.k;
    const auto binding = studio::modifier_binding();
    library_panel_box(d, f, a, b);
    text(d, f.heading, 24 * k, ImVec2(a.x + 18 * k, a.y + 14 * k), white, "TRICK MODIFIER / HOW IT WORKS");
    if (action(d, f, "##closemodifierhelp", ImVec2(b.x - 94 * k, a.y + 12 * k), ImVec2(76 * k, 30 * k), "CLOSE", Look::ghost)) u.library_modifier_help = false;
    const float x = a.x + 18 * k, w = b.x - a.x - 36 * k;
    float y = a.y + 56 * k;
    y += paragraph(d, f.bold, 15 * k, ImVec2(x, y), w, soft,
        "Example: keep Kickflip in your normal playlist. Put a Dolphin Flip variation of the same Kickflip input in another playlist. "
        "Assign that playlist below, then hold the modifier before starting the kickflip. Grinds, manuals and grabs use the same principle.") + 20 * k;
    const auto row = [&](const studio::settings_rows::Setting& setting) {
        ImGui::PushID(setting.label.c_str());
        text(d, f.bold, 16 * k, ImVec2(x, y + 9 * k), white, setting.label);
        const float value_w = std::min(440 * k, w * 0.57f);
        if (action(d, f, "##previous", ImVec2(x + w - value_w, y), ImVec2(36 * k, 34 * k), "<", Look::ghost) && setting.step) setting.step(-1);
        text(d, f.bold, 15 * k, ImVec2(x + w - value_w + 48 * k, y + 9 * k), blue_light, elide(f.bold, 15 * k, setting.value, value_w - 100 * k));
        if (action(d, f, "##next", ImVec2(x + w - 36 * k, y), ImVec2(36 * k, 34 * k), ">", Look::ghost) && setting.step) setting.step(1);
        y += 44 * k;
        y += paragraph(d, f.bold, 12 * k, ImVec2(x, y), w, faint, setting.about) + 18 * k;
        ImGui::PopID();
    };
    row(studio::settings_rows::modifier_playlist(studio::playlists_now(), binding));
    row(studio::settings_rows::modifier_button(binding));
    row(studio::settings_rows::modifier_native_button(binding));
    y += paragraph(d, f.bold, 13 * k, ImVec2(x, y), w, soft,
        "Tags are recommendations, not restrictions. Any saved animation can go in either playlist. "
        "The chosen route stays fixed through the move. Missing variations fall back to normal; turning the modifier OFF restores its native button.") + 20 * k;
    if (binding.playlist == v.playlist_playing && !binding.playlist.empty())
        y += paragraph(d, f.bold, 13 * k, ImVec2(x, y), w, IM_COL32(244, 193, 99, 255),
            "Both roles use the same playlist. Choose another modifier playlist if you want different animations.") + 14 * k;
    if (action(d, f, "##collectmodifierhints", ImVec2(x, y), ImVec2(std::min(w, 420 * k), 40 * k), "CREATE PLAYLIST FROM MODIFIER HINTS", Look::paper)) {
        std::string m; studio::playlist_from_modifier_tags(m); say(m);
    }
}
void library_workspace(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui(); const float k = f.k;
    const LibraryIndex index(v);
    const auto binding = studio::modifier_binding();
    page_heading(d, f, a, "LIBRARY", "Choose a destination. Browse your collection. Tick exactly the animations you want.");
    if (action(d, f, "##librarynewpack", ImVec2(b.x - 152 * k, a.y + 5 * k), ImVec2(152 * k, 36 * k), "+ NEW PACK", Look::ghost)) {
        make_pack(v); u.pack_page = true;
    }
    const ImVec2 intro_a(a.x, a.y + 76 * k), intro_b(b.x, a.y + 216 * k);
    library_modifier_intro(d, f, v, intro_a, intro_b);
    const float top = intro_b.y + 16 * k, gap = 16 * k;
    const float nav_w = 240 * k, queue_w = std::clamp((b.x - a.x) * 0.26f, 330 * k, 410 * k);
    const ImVec2 na(a.x, top), nb(a.x + nav_w, b.y), qa(b.x - queue_w, top), qb = b;
    const ImVec2 ba(nb.x + gap, top), bb(qa.x - gap, b.y);
    library_panel_box(d, f, na, nb); library_panel_box(d, f, ba, bb); library_panel_box(d, f, qa, qb);
    text(d, f.heading, 20 * k, ImVec2(na.x + 14 * k, top + 14 * k), white, "PLAYLISTS");
    text(d, f.bold, 11 * k, ImVec2(na.x + 14 * k, top + 41 * k), faint, "SELECT ONE TO EDIT");
    if (action(d, f, "##librarynewlist", ImVec2(na.x + 12 * k, top + 67 * k), ImVec2(nav_w - 24 * k, 36 * k), "+ NEW PLAYLIST", Look::blue)) {
        std::string m; studio::new_playlist(m); say(m);
    }
    const ImVec2 nla(na.x + 10 * k, top + 119 * k), nlb(nb.x - 10 * k, nb.y - 12 * k);
    begin_region(201, nla, nlb, u.library_nav_scroll, k);
    float ny = nla.y - u.library_nav_scroll;
    if (v.playlists.empty()) ny += paragraph(d, f.bold, 14 * k, ImVec2(nla.x + 8 * k, ny), nav_w - 36 * k, soft, "Start with a playlist. Adding animations will not switch what plays.");
    for (const auto& list : v.playlists) {
        ImGui::PushID(list.name.c_str());
        const ImVec2 ca(nla.x, ny), cb(nlb.x - 5 * k, ny + 72 * k);
        bool lit = false;
        if (hit("##destination", ca, cb, &lit)) studio::open_playlist(list.name);
        flat_rect(d, ca, cb, list.name == v.playlist_open ? IM_COL32(17, 65, 110, 250) : lit ? row_selected : row_fill_alt, seed_of(ca), k * 0.4f);
        if (list.name == v.playlist_open) d->AddRectFilled(ca, ImVec2(ca.x + 4 * k, cb.y), blue);
        text(d, f.bold, 15 * k, ImVec2(ca.x + 12 * k, ca.y + 10 * k), white, elide(f.bold, 15 * k, list.name, nav_w - 48 * k));
        std::string role = std::to_string(list.size) + " entries";
        if (list.name == v.playlist_playing) role += " / NORMAL";
        if (list.name == binding.playlist) role += " / MODIFIER";
        text(d, f.bold, 10 * k, ImVec2(ca.x + 12 * k, ca.y + 39 * k), list.name == binding.playlist ? blue_light : soft,
             elide(f.bold, 10 * k, role, nav_w - 48 * k));
        if (lit && nav().keys) chosen_outline(d, ca, cb, k);
        ImGui::PopID(); ny += 80 * k;
    }
    end_region(d, nla, nlb, ny + u.library_nav_scroll - nla.y, u.library_nav_scroll, k);
    if (u.library_seen_target != v.playlist_open) {
        u.library_seen_target = v.playlist_open; u.library_queue_scroll = 0; u.library_queue_open.clear(); u.library_confirm.clear(); u.playlist_name_for.clear();
    }
    // Queue: editing a playlist never implicitly makes it the normal or modifier playlist.
    const float qx = qa.x + 14 * k, qw = qb.x - qa.x - 28 * k;
    if (u.library_confirm.empty() && !v.playlist_open.empty()) {
    text(d, f.heading, 17 * k, ImVec2(qx, top + 14 * k), blue_light, "DESTINATION");
    if (action(d, f, "##foldqueuepacks", ImVec2(qb.x - 113 * k, top + 9 * k), ImVec2(99 * k, 26 * k), "FOLD PACKS", Look::ghost, !u.library_queue_open.empty())) {
        u.library_queue_open.clear(); u.library_queue_scroll = 0;
    }
    if (u.playlist_name_for != v.playlist_open && !ImGui::IsAnyItemActive()) {
        std::snprintf(u.playlist_name.data(), u.playlist_name.size(), "%s", v.playlist_open.c_str()); u.playlist_name_for = v.playlist_open;
    }
    if (!v.playlist_open.empty()) {
        const bool entered = field(f, "##librarylistname", u.playlist_name, ImVec2(qx, top + 43 * k), qw - 60 * k, "Playlist name");
        if (action(d, f, "##libraryrenamelist", ImVec2(qb.x - 64 * k, top + 43 * k), ImVec2(50 * k, 34 * k), "SAVE", Look::ghost) || entered) {
            std::string m; studio::rename_playlist(u.playlist_name.data(), m); say(m); u.playlist_name_for.clear();
        }
    }
    const bool selected = !v.playlist_open.empty(), normal = selected && v.playlist_open == v.playlist_playing;
    const bool special = selected && v.playlist_open == binding.playlist;
    if (action(d, f, "##librarynormalrole", ImVec2(qx, top + 90 * k), ImVec2(qw, 32 * k), normal ? "STOP NORMAL PLAYBACK" : "PLAY AS NORMAL", normal ? Look::paper : Look::ghost, selected)) {
        std::string m; studio::play_playlist(normal ? std::string{} : v.playlist_open, m); say(m);
    }
    if (action(d, f, "##librarymodifierrole", ImVec2(qx, top + 130 * k), ImVec2(qw, 32 * k), special ? "MODIFIER PLAYLIST / SET UP" : "USE WITH MODIFIER", special ? Look::blue : Look::ghost, selected)) {
        if (!special) { auto live = studio::modifier_binding(); live.playlist = v.playlist_open; studio::set_modifier_binding(live); }
        u.library_modifier_help = true; u.library_modal_scroll = 0;
    }
    const int chosen = static_cast<int>(v.playlist_mode);
    const int picked = segmented(d, f, "##librarymode", ImVec2(qx, top + 177 * k), qw, 31 * k, {"FIXED", "IN TURNS", "SHUFFLE"}, chosen);
    if (selected && picked >= 0 && picked != chosen) { std::string m; studio::set_playlist_mode(v.playlist_open, static_cast<studio::PlaylistMode>(picked), m); say(m); }
    text(d, f.bold, 10 * k, ImVec2(qx, top + 217 * k), faint, chosen == 0 ? "First matching version wins. Reorder below." : chosen == 1 ? "Matching versions alternate in this order." : "Matching versions shuffle, without repeating.");
    const float utility_y = top + 243 * k, third = (qw - 12 * k) / 3;
    if (action(d, f, "##libraryundo", ImVec2(qx, utility_y), ImVec2(third, 30 * k), v.playlist_redo ? "REDO" : "UNDO", Look::ghost, selected && v.playlist_can_undo)) {
        std::string m; studio::undo_playlist_entries(v.playlist_open, m); say(m);
    }
    if (action(d, f, "##librarycopy", ImVec2(qx + third + 6 * k, utility_y), ImVec2(third, 30 * k), "COPY", Look::ghost, selected)) {
        std::string m; studio::duplicate_playlist(v.playlist_open, m); say(m);
    }
    if (action(d, f, "##libraryclear", ImVec2(qx + 2 * (third + 6 * k), utility_y), ImVec2(third, 30 * k), "CLEAR", Look::ghost, selected && !v.playlist.empty()))
        u.library_confirm = "clear:" + v.playlist_open;
    const ImVec2 qla(qx, top + 289 * k), qlb(qb.x - 12 * k, qb.y - 49 * k);
    begin_region(202, qla, qlb, u.library_queue_scroll, k);
    float qy = qla.y - u.library_queue_scroll;
    if (v.playlist.empty()) qy += paragraph(d, f.bold, 14 * k, ImVec2(qx, qy), qw - 14 * k, soft,
        selected ? "Your playlist is empty. Browse packs or saved tricks and tick the ones you want here." : "Choose or create a playlist at the left, then tick animations to add them.");
    for (std::size_t i = 0; i < v.playlist.size(); ++i) {
        const auto& e = v.playlist[i];
        ImGui::PushID(static_cast<int>(i));
        const bool whole = e.animation.empty();
        const auto* pack = whole ? pack_named(v, e.pack) : nullptr;
        const auto* saved = whole ? nullptr : index.preset(e.animation);
        const bool missing = whole ? !pack : !saved || !saved->available || (!e.pack.empty() && !pack_named(v, e.pack));
        const std::string label = whole ? e.pack : library_saved_name(index, e.animation);
        const ImVec2 ca(qx, qy), cb(qlb.x - 6 * k, qy + 76 * k);
        flat_rect(d, ca, cb, missing ? IM_COL32(66, 31, 33, 245) : row_fill_alt, seed_of(ca), k * 0.5f);
        if (whole && hit("##expandqueue", ca, ImVec2(cb.x - 80 * k, ca.y + 38 * k))) {
            if (!u.library_queue_open.erase(e.pack)) u.library_queue_open.insert(e.pack);
        }
        text(d, f.bold, 14 * k, ImVec2(ca.x + 10 * k, ca.y + 9 * k), white,
             elide(f.bold, 14 * k, std::to_string(i + 1) + ". " + (whole ? u.library_queue_open.contains(e.pack) ? "- " : "+ " : "") + label, qw - 110 * k));
        const std::string detail = missing ? "MISSING SOURCE / remove or reinstall" : whole ? "WHOLE PACK / " + std::to_string(e.omitted.size()) + " excluded here"
            : e.pack.empty() ? library_origin(index, e.animation) : "Single trick from " + e.pack;
        text(d, f.bold, 10 * k, ImVec2(ca.x + 10 * k, ca.y + 38 * k), missing ? IM_COL32(255, 159, 151, 255) : faint, elide(f.bold, 10 * k, detail, qw - 24 * k));
        if (action(d, f, "##queueup", ImVec2(cb.x - 98 * k, ca.y + 7 * k), ImVec2(28 * k, 24 * k), "^", Look::ghost, i > 0)) studio::playlist_move(i, -1);
        if (action(d, f, "##queuedown", ImVec2(cb.x - 66 * k, ca.y + 7 * k), ImVec2(28 * k, 24 * k), "v", Look::ghost, i + 1 < v.playlist.size())) studio::playlist_move(i, 1);
        if (action(d, f, "##queueremove", ImVec2(cb.x - 34 * k, ca.y + 7 * k), ImVec2(26 * k, 24 * k), "X", Look::ghost)) studio::playlist_remove(i);
        qy += 84 * k;
        if (whole && pack && u.library_queue_open.contains(e.pack))
            for (const auto& member : pack->members)
                if (const auto* p = index.preset(member)) {
                    const bool in = index.member(member).included();
                    ImGui::PushID(member.c_str());
                    if (action(d, f, "##queuechild", ImVec2(qx + 12 * k, qy), ImVec2(qw - 20 * k, 32 * k),
                               (in ? "[+] " : "[ ] ") + library_saved_name(index, p->name), in ? Look::dark : Look::ghost)) {
                        std::string m; studio::edit_playlist_member(v.playlist_open, {e.pack, member}, !in, m); say(m);
                    }
                    ImGui::PopID(); qy += 38 * k;
                }
        ImGui::PopID();
    }
    end_region(d, qla, qlb, qy + u.library_queue_scroll - qla.y, u.library_queue_scroll, k);
    if (action(d, f, "##librarydelete", ImVec2(qx, qb.y - 38 * k), ImVec2(qw, 27 * k), "DELETE PLAYLIST", Look::ghost, selected))
        u.library_confirm = "delete:" + v.playlist_open;
    }
    if (u.library_confirm.empty() && v.playlist_open.empty()) {
        text(d, f.heading, 20 * k, ImVec2(qx, top + 18 * k), white, "YOUR DESTINATION");
        paragraph(d, f.bold, 15 * k, ImVec2(qx, top + 59 * k), qw, soft,
            "Create a playlist at the left, then tick the animations you want in it. You can browse and preview your collection first. Choose its playback role when you are ready.");
    }
    // Source browser: viewing a pack never adds the whole pack; every member has its own checkbox.
    if (!u.library_modifier_help) {
    const bool selected = !v.playlist_open.empty();
    const float bx = ba.x + 14 * k, bw = bb.x - ba.x - 28 * k;
    text(d, f.heading, 20 * k, ImVec2(bx, top + 14 * k), white, "YOUR COLLECTION");
    text(d, f.bold, 11 * k, ImVec2(bx, top + 42 * k), soft, elide(f.bold, 11 * k,
        selected ? "TICKS ADD TO " + upper(v.playlist_open) + " / not necessarily the playlist playing" : "Choose a destination before adding animations", bw));
    const int source = segmented(d, f, "##librarysource", ImVec2(bx, top + 68 * k), bw, 34 * k, {"PACKS", "MY TRICKS", "ALL SAVED"}, u.library_source);
    if (source >= 0 && source != u.library_source) { u.library_source = source; u.library_pack.clear(); u.library_expanded.clear(); u.library_browse_scroll = 0; }
    field(f, "##librarysearch", u.pack_query, ImVec2(bx, top + 114 * k), bw - 72 * k, "Search names, tricks, packs, makers, pop or stance");
    if (action(d, f, "##clearlibrarysearch", ImVec2(bb.x - 74 * k, top + 114 * k), ImVec2(60 * k, 34 * k), "CLEAR", Look::ghost, u.pack_query[0] != 0)) u.pack_query.fill(0);
    const auto terms = studio::native_search_terms(u.pack_query.data());
    const bool query_changed = u.library_seen_query != u.pack_query.data();
    if (query_changed) { u.library_seen_query = u.pack_query.data(); u.library_browse_scroll = 0; u.library_expanded.clear(); }
    float by = top + 164 * k;
    const auto* browsing = pack_named(v, u.library_pack);
    if (u.library_source == 0 && browsing) {
        if (action(d, f, "##librarypackback", ImVec2(bx, by), ImVec2(84 * k, 30 * k), "< PACKS", Look::ghost)) { u.library_pack.clear(); u.library_browse_scroll = 0; }
        text(d, f.heading, 17 * k, ImVec2(bx + 98 * k, by + 7 * k), white, elide(f.heading, 17 * k, browsing->name, bw - 350 * k));
        if (action(d, f, "##librarypacksettings", ImVec2(bb.x - 212 * k, by), ImVec2(86 * k, 30 * k), "MANAGE", Look::ghost)) open_pack_page(browsing->name);
        if (action(d, f, "##librarypackall", ImVec2(bb.x - 118 * k, by), ImVec2(104 * k, 30 * k), "ADD ALL", Look::blue, selected)) {
            std::string m; studio::edit_playlist_pack(v.playlist_open, browsing->name, true, m); say(m);
        }
        by += 42 * k;
    }
    if (u.library_source != 0 || browsing) {
        const float half = (bw - 12 * k) * 0.5f;
        if (action(d, f, "##expandall", ImVec2(bx, by), ImVec2(half, 28 * k), "EXPAND ALL", Look::ghost)) u.library_expand_all = true;
        if (action(d, f, "##collapseall", ImVec2(bx + half + 12 * k, by), ImVec2(half, 28 * k), "COLLAPSE ALL", Look::ghost)) u.library_expanded.clear();
        by += 40 * k;
    }
    const ImVec2 bla(bx, by), blb(bb.x - 12 * k, bb.y - 12 * k);
    begin_region(203, bla, blb, u.library_browse_scroll, k);
    float cy = bla.y - u.library_browse_scroll;
    if (u.library_source == 0 && !browsing) {
        std::vector<const studio::View::PackView*> shown;
        for (const auto& pack : v.packs) {
            bool found = terms.empty() || studio::native_search_matches(studio::native_search_key(pack.name + " " + pack.made_by), terms);
            if (!found) for (const auto& member : pack.members) if (const auto* p = index.preset(member); p && library_search(index, *p, terms)) { found = true; break; }
            if (found) shown.push_back(&pack);
        }
        std::stable_sort(shown.begin(), shown.end(), [](const auto* x, const auto* y) { return x->favorite && !y->favorite; });
        if (shown.empty()) cy += paragraph(d, f.bold, 15 * k, ImVec2(bx, cy + 12 * k), bw, soft, v.packs.empty() ? "No packs yet. Your own saves are in MY TRICKS. Get packs in the MARKET, or create one above." : "No packs match this search. Clear the search or try ALL SAVED.");
        for (const auto* pack : shown) {
            ImGui::PushID(pack->name.c_str());
            const ImVec2 ca(bx, cy), cb(blb.x - 5 * k, cy + 126 * k);
            flat_rect(d, ca, cb, row_fill_alt, seed_of(ca), k * 0.6f);
            const ImVec2 cover_a(ca.x + 8 * k, ca.y + 8 * k), cover_b(ca.x + 118 * k, cb.y - 8 * k);
            pack_cover(d, f, *pack, cover_a, cover_b);
            const bool cover_pressed = hit("##browsecover", cover_a, cover_b);
            const float tx = ca.x + 132 * k, tw = cb.x - tx - 18 * k;
            text(d, f.heading, 19 * k, ImVec2(tx, ca.y + 10 * k), white, elide(f.heading, 19 * k, pack->name, tw - 108 * k));
            if (action(d, f, "##packfavorite", ImVec2(cb.x - 112 * k, ca.y + 7 * k), ImVec2(98 * k, 25 * k),
                       pack->favorite ? "FAVORITE" : "+ FAVORITE", pack->favorite ? Look::blue : Look::ghost)) studio::set_pack_favorite(pack->name, !pack->favorite);
            std::size_t have = 0;
            for (const auto& member : pack->members) have += index.member(member).included();
            text(d, f.bold, 11 * k, ImVec2(tx, ca.y + 37 * k), soft, elide(f.bold, 11 * k,
                std::to_string(pack->members.size()) + " tricks / " + std::to_string(have) + " selected here" + (pack->made_by.empty() ? "" : " / by " + pack->made_by), tw));
            const float unit = (tw - 16 * k) / 3;
            const auto whole = std::find_if(v.playlist.begin(), v.playlist.end(), [&](const auto& e) { return e.pack == pack->name && e.animation.empty(); });
            const bool whole_complete = whole != v.playlist.end() && whole->omitted.empty();
            if (action(d, f, "##browsepack", ImVec2(tx, ca.y + 66 * k), ImVec2(unit, 32 * k), "CHOOSE TRICKS", Look::paper) || cover_pressed) {
                if (u.library_pack != pack->name) u.library_expanded.clear();
                u.library_pack = pack->name; u.library_browse_scroll = 0;
            }
            if (action(d, f, "##addwholepack", ImVec2(tx + unit + 8 * k, ca.y + 66 * k), ImVec2(unit, 32 * k),
                       whole_complete ? "REMOVE PACK" : have ? "ADD REMAINING" : "ADD ALL", Look::ghost, selected)) {
                std::string m; studio::edit_playlist_pack(v.playlist_open, pack->name, !whole_complete, m); say(m);
            }
            if (action(d, f, "##managepack", ImVec2(tx + 2 * (unit + 8 * k), ca.y + 66 * k), ImVec2(unit, 32 * k), "MANAGE", Look::ghost)) open_pack_page(pack->name);
            text(d, f.bold, 10 * k, ImVec2(tx, ca.y + 107 * k), faint, "Browsing never adds a whole pack. Choose just one trick if you like.");
            ImGui::PopID(); cy += 138 * k;
        }
    } else {
        std::map<std::string, std::vector<const studio::Preset*>> groups;
        for (const auto& p : v.presets) {
            if (u.library_source == 1 && p.foreign) continue;
            if (u.library_source == 0 && (!browsing || !browsing->pack.has(p.name))) continue;
            if (library_search(index, p, terms)) groups[index.label(p.trick)].push_back(&p);
        }
        if ((query_changed && !terms.empty()) || u.library_expand_all)
            for (const auto& [key, members] : groups) { (void)members; u.library_expanded.insert(key); }
        u.library_expand_all = false;
        if (groups.empty()) cy += paragraph(d, f.bold, 15 * k, ImVec2(bx, cy + 10 * k), bw, soft, terms.empty() ? "No saved tricks here yet. Save an animation in TRICKS or browse your installed packs." : "No saved tricks match. Try fewer words or clear the search.");
        for (const auto& [key, members] : groups) {
            ImGui::PushID(key.c_str());
            const bool expanded = u.library_expanded.contains(key);
            if (action(d, f, "##group", ImVec2(bx, cy), ImVec2(bw - 5 * k, 38 * k),
                       (expanded ? "-  " : "+  ") + key + "  /  " + std::to_string(members.size()) + " saved", Look::dark)) {
                if (!u.library_expanded.erase(key)) u.library_expanded.insert(key);
            }
            cy += 46 * k;
            if (expanded) for (const auto* p : members) {
                std::string from = browsing && u.library_source == 0 ? browsing->name : std::string{};
                if (from.empty() && p->foreign) if (const auto at = index.origins.find(p->name); at != index.origins.end() && !at->second.empty()) from = at->second.front();
                library_member_row(d, f, v, index, *p, from, ImVec2(bx + 7 * k, cy), bw - 12 * k);
                cy += 78 * k;
            }
            ImGui::PopID();
        }
    }
    end_region(d, bla, blb, cy + u.library_browse_scroll - bla.y, u.library_browse_scroll, k);
    }
    // Clear/delete are local confirmations; cancellation never discards a pack or animation.
    if (!u.library_confirm.empty()) {
        const bool clear = u.library_confirm.starts_with("clear:");
        const std::string target = u.library_confirm.substr(clear ? 6 : 7);
        const ImVec2 ca(qa.x, qa.y), cb(qb.x, std::min(qb.y, qa.y + 250 * k));
        d->AddRectFilled(ca, cb, IM_COL32(20, 22, 27, 255));
        text(d, f.heading, 20 * k, ImVec2(qx, ca.y + 18 * k), white, clear ? "CLEAR THIS PLAYLIST?" : "DELETE THIS PLAYLIST?");
        paragraph(d, f.bold, 14 * k, ImVec2(qx, ca.y + 54 * k), qw, soft,
            target + ": packs and animation files are kept. " + (clear ? "UNDO can restore the contents." : "Its playback assignment will be removed."));
        if (action(d, f, "##cancelplaylistchange", ImVec2(qx, cb.y - 86 * k), ImVec2(qw, 30 * k), "KEEP IT", Look::paper)) u.library_confirm.clear();
        if (action(d, f, "##confirmplaylistchange", ImVec2(qx, cb.y - 46 * k), ImVec2(qw, 30 * k), clear ? "CLEAR CONTENTS" : "DELETE PLAYLIST", Look::danger)) {
            std::string m;
            if (clear) studio::clear_playlist_entries(target, m);
            else { studio::open_playlist(target); studio::delete_playlist(m); }
            say(m); u.library_confirm.clear();
        }
    }
    if (u.library_modifier_help) {
        const ImVec2 ca(ba.x, ba.y), cb(bb.x, bb.y);
        // Draw inside its own scroll region so explanations and controls remain accessible at every supported scale.
        d->AddRectFilled(ca, cb, IM_COL32(12, 15, 20, 255));
        begin_region(204, ca, cb, u.library_modal_scroll, k);
        library_modifier_setup(d, f, v, ImVec2(ca.x, ca.y - u.library_modal_scroll), ImVec2(cb.x - 8 * k, ca.y + 690 * k - u.library_modal_scroll));
        end_region(d, ca, cb, 700 * k, u.library_modal_scroll, k);
    }
}
