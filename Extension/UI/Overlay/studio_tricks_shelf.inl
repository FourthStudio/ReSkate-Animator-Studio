// Included in the Studio's drawing namespace, after the LIBRARY's code: it uses its fonts, sheets and buttons.
// ---- TRICKS > MY TRICKS and FROM PACKS as a folder ----------------------------------------------------
// With 200 and more saved tricks a card each made a long, busy page. Now they are shown the way a PC shows a folder of
// files: as ICONS, a LIST, or a table of DETAILS with sortable columns; grouped BY TRICK, BY KIND or BY PACK (or not), each
// group folding away; and a FILTER tab beside the folders (kind, stance, pop, pack, maker, in a playlist or not, locked or
// not) that works with the search. A click chooses a trick and its card opens beside the list with all that can be done
// with it; Ctrl + click and Shift + click (or SELECT, on a pad) choose more, and the card then does it to all of them; a
// double click opens it. Only the rows on screen are drawn, and what they show is worked out again only when the library,
// the filters or the order change (trick_browser.h), never every frame.
namespace tb = studio::trick_browser;

struct TricksShelf {
    // How it is shown: read from the studio's settings once, saved back as it is changed (studio::set_tricks_view).
    bool prefs_read{};
    tb::ViewMode mode{tb::ViewMode::list};
    tb::GroupBy group_mine{tb::GroupBy::trick}, group_packs{tb::GroupBy::pack};
    tb::Sort sort;
    tb::Filter filter; // its source and search are set from the page each frame
    std::set<std::string> folded;
    tb::Selection picked;
    bool select_mode{};
    bool filter_tab{};
    float scroll{}, filter_scroll{}, bulk_scroll{};
    std::string page_seen; // TRICKS' trick_page as the shelf last left it: changed elsewhere, the choice follows it
    std::string reveal;    // scrolled to once it is laid out (shelf_reveal)
    int reveal_tries{};
    bool confirm_bulk_delete{};
    int bulk_open{}; // the card for several: its chooser open (1 playlists, 2 packs)
    // What was worked out, and from what.
    std::size_t library_size{}, presets_size{}, packs_size{};
    std::uint64_t revision_seen{}, data_mark{};
    double marked_at{-1}, playlists_at{-1};
    std::vector<tb::Item> items;
    std::unordered_map<std::string, std::size_t> where; // a trick's name: its place in items
    std::unordered_map<std::string, int> saved_of;       // the game's trick: how many saves of it (its card says so)
    std::size_t own_count{}, pack_count{};
    std::map<std::string, std::vector<std::string>> playlists;
    std::uint64_t generation{}, shown_mark{}, layout_mark{}, shown_generation{};
    std::vector<tb::Group> groups;
    tb::Counts counts;
    tb::Layout layout;
    std::vector<std::string> order; // the names as shown: what a Shift + click runs through
    std::size_t columns{1};
    std::string search_seen;
};
TricksShelf& shelf() {
    static auto* s = new TricksShelf;
    return *s;
}

// ---- keeping it up to date ----
// Everything the items are made from, as one number. Worked out only when the studio's state has moved, and at most four
// times a second (while a trick plays the state moves every frame).
std::uint64_t shelf_data_mark(const studio::View& v) {
    tb::Mark m;
    for (const auto& name : v.library) m.add(name);
    for (const auto& p : v.presets) {
        m.add(p.name);
        m.add(p.label);
        m.add(p.trick);
        m.add(p.pop);
        m.add(p.made_by);
        m.add(p.stance);
        m.add(static_cast<std::uint64_t>(p.saved_at));
        m.add(std::uint64_t{(p.locked ? 1u : 0u) | (p.foreign ? 2u : 0u) | (p.included ? 4u : 0u) | (p.available ? 8u : 0u) |
                            (studio::preset_leads_right(p) ? 16u : 0u) | (p.fakie.value_or(false) ? 32u : 0u) |
                            (p.switch_stance.value_or(false) ? 64u : 0u)});
    }
    for (const auto& pack : v.packs) {
        m.add(pack.name);
        m.add(std::uint64_t{pack.installed});
        for (const auto& member : pack.members) m.add(member);
    }
    for (const auto& name : v.ungrouped) m.add(name);
    m.add(std::uint64_t{v.own_lead_right});
    for (const auto& list : v.playlists) {
        m.add(list.name);
        m.add(list.size);
    }
    m.add(v.playlist_playing);
    return m.value;
}
void shelf_read_prefs(const studio::View& v) {
    auto& s = shelf();
    if (s.prefs_read) return;
    s.prefs_read = true;
    const auto& t = v.tricks_view;
    s.mode = static_cast<tb::ViewMode>(std::clamp(t.view, 0, 2));
    s.group_mine = static_cast<tb::GroupBy>(std::clamp(t.group_mine, 0, 3));
    s.group_packs = static_cast<tb::GroupBy>(std::clamp(t.group_packs, 0, 3));
    s.sort = {static_cast<tb::Column>(std::clamp(t.sort, 0, static_cast<int>(tb::column_count) - 1)), t.descending};
}
void shelf_save_prefs() {
    const auto& s = shelf();
    studio::TricksView t;
    t.view = static_cast<int>(s.mode);
    t.group_mine = static_cast<int>(s.group_mine);
    t.group_packs = static_cast<int>(s.group_packs);
    t.sort = static_cast<int>(s.sort.by);
    t.descending = s.sort.descending;
    studio::set_tricks_view(t);
}
// The items, made again when what they are made from changed; the playlists read again every two seconds (an animation
// left out of one changes no number the mark sees).
void shelf_refresh(const studio::View& v) {
    auto& s = shelf();
    shelf_read_prefs(v);
    const double now = ImGui::GetTime();
    bool rebuild = s.marked_at < 0 || v.library.size() != s.library_size || v.presets.size() != s.presets_size || v.packs.size() != s.packs_size;
    if (!rebuild && v.revision != s.revision_seen && now - s.marked_at > 0.25) {
        s.revision_seen = v.revision;
        s.marked_at = now;
        const auto mark = shelf_data_mark(v);
        rebuild = mark != s.data_mark;
    }
    if (!rebuild && now - s.playlists_at > 2.0) {
        auto lists = studio::playlists_by_animation();
        s.playlists_at = now;
        if (lists != s.playlists) {
            s.playlists = std::move(lists);
            rebuild = true;
        }
    }
    if (!rebuild) return;
    s.library_size = v.library.size();
    s.presets_size = v.presets.size();
    s.packs_size = v.packs.size();
    s.revision_seen = v.revision;
    s.marked_at = now;
    s.data_mark = shelf_data_mark(v);
    if (s.playlists_at != now) {
        s.playlists = studio::playlists_by_animation();
        s.playlists_at = now;
    }
    std::unordered_map<std::string, const studio::Preset*> presets;
    for (const auto& p : v.presets) presets.emplace(p.name, &p);
    std::unordered_map<std::string, std::vector<std::string>> holding;
    std::set<std::string> in_installed;
    for (const auto& pack : v.packs)
        for (const auto& member : pack.members) {
            holding[member].push_back(pack.name);
            if (pack.installed) in_installed.insert(member);
        }
    // The game's name and kind for each trick, once a trick (both look through the whole catalog).
    std::unordered_map<std::string, std::pair<std::string, tb::Type>> trick_info;
    s.items.clear();
    s.items.reserve(v.library.size());
    s.where.clear();
    s.saved_of.clear();
    s.own_count = s.pack_count = 0;
    for (const auto& name : v.library) {
        tb::Item it;
        it.name = name;
        const auto found = presets.find(name);
        const studio::Preset* p = found == presets.end() ? nullptr : found->second;
        it.shown = p && !p->label.empty() ? p->label : name;
        if (p) {
            it.trick = p->trick;
            auto [info, fresh] = trick_info.try_emplace(p->trick);
            if (fresh) info->second = {trick_label(p->trick), tb::type_of(p->trick, kind_of(p->trick))};
            it.trick_label = info->second.first;
            it.type = info->second.second;
            it.stance = std::clamp(studio::stance_version(*p, v.own_lead_right), 0, 3);
            it.pop = p->trick.starts_with("native:") ? -1 : tb::pop_of(p->pop);
            it.maker = p->made_by;
            it.modified = p->saved_at;
            it.plays = p->included;
            it.locked = p->locked;
            ++s.saved_of[p->trick];
        } else {
            it.trick_label = "A piece of the game's animation";
            it.pop = -1;
        }
        if (const auto h = holding.find(name); h != holding.end()) it.packs = h->second;
        if (const auto l = s.playlists.find(name); l != s.playlists.end()) it.playlists = l->second;
        it.from_pack = (p && (p->locked || p->foreign)) || in_installed.contains(name);
        it.apart = v.ungrouped.contains(name);
        std::string words = it.shown + " " + name + " " + it.trick + " " + it.trick_label + " " + tb::type_names[static_cast<std::size_t>(it.type)] + " " +
                            (it.pop >= 0 ? std::string(tb::pop_names[static_cast<std::size_t>(it.pop)]) + " pop " : std::string{}) +
                            tb::stance_names[static_cast<std::size_t>(it.stance)] + " " + it.maker;
        for (const auto& pack : it.packs) words += " " + pack;
        it.key = studio::native_search_key(words);
        ++(it.from_pack ? s.pack_count : s.own_count);
        s.where.emplace(name, s.items.size());
        s.items.push_back(std::move(it));
    }
    ++s.generation;
    // What is chosen and gone (deleted, renamed) is not chosen.
    tb::keep_existing(s.picked, [&](const std::string& n) { return s.where.contains(n); });
}
const std::unordered_map<std::string, int>& shelf_saved_of() { return shelf().saved_of; }
const tb::Item* shelf_item(const std::string& name) {
    const auto& s = shelf();
    const auto at = s.where.find(name);
    return at == s.where.end() ? nullptr : &s.items[at->second];
}
std::pair<std::size_t, std::size_t> shelf_counts() { return {shelf().own_count, shelf().pack_count}; }
bool& shelf_filter_tab() { return shelf().filter_tab; }
int shelf_filters_on() { return shelf().filter.active(); }
const std::vector<std::string>* shelf_playlists_of(const std::string& name) {
    const auto* it = shelf_item(name);
    return it ? &it->playlists : nullptr;
}
// Every saved trick a search finds, yours and those from packs, in the library's order (TRICKS' search results).
std::vector<std::string> shelf_matching(const std::vector<std::string>& terms) {
    std::vector<std::string> out;
    for (const auto& it : shelf().items)
        if (studio::native_search_matches(it.key, terms)) out.push_back(it.name);
    return out;
}

// ---- what a kind of trick looks like ----
ImU32 type_colour(tb::Type t) {
    switch (t) {
    case tb::Type::flip: return IM_COL32(90, 180, 255, 255);
    case tb::Type::shuv: return IM_COL32(184, 132, 255, 255);
    case tb::Type::grind: return IM_COL32(255, 156, 52, 255);
    case tb::Type::manual: return IM_COL32(82, 206, 120, 255);
    case tb::Type::grab: return IM_COL32(246, 208, 70, 255);
    case tb::Type::other: break;
    }
    return IM_COL32(186, 188, 196, 255);
}
// A board drawn for the kind: a deck on its wheels and what is done with it - turned over (a flip), spun flat round (a
// shuv), on a rail (a grind or slide), up on its back wheels (a manual), held (a grab), popped up (the rest). `s` is how
// wide it is.
void trick_glyph(ImDrawList* d, ImVec2 c, float s, tb::Type type, ImU32 colour) {
    const float line = std::max(1.4f, s * 0.07f);
    const auto at = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    // The deck with its kicks and wheels, turned by `tilt` (radians) about `pivot` and lifted by `lift`.
    const auto deck = [&](float tilt, float lift, ImVec2 pivot) {
        const float cs = std::cos(tilt), sn = std::sin(tilt);
        const auto p = [&](float x, float y) {
            const float dx = x * s, dy = (y + lift) * s;
            return ImVec2(pivot.x + dx * cs - dy * sn, pivot.y + dx * sn + dy * cs);
        };
        const ImVec2 board[4]{p(-0.46f, -0.08f), p(-0.34f, 0.0f), p(0.34f, 0.0f), p(0.46f, -0.08f)};
        d->AddPolyline(board, 4, colour, ImDrawFlags_None, line * 1.5f);
        d->AddCircleFilled(p(-0.24f, 0.1f), s * 0.058f, colour, 12);
        d->AddCircleFilled(p(0.24f, 0.1f), s * 0.058f, colour, 12);
    };
    const auto head = [&](ImVec2 tip, float dx, float dy) {
        const float len = std::max(1e-3f, std::sqrt(dx * dx + dy * dy)), ux = dx / len, uy = dy / len, h = s * 0.13f;
        d->AddTriangleFilled(ImVec2(tip.x + ux * h * 0.55f, tip.y + uy * h * 0.55f), ImVec2(tip.x - ux * h * 0.6f - uy * h * 0.6f, tip.y - uy * h * 0.6f + ux * h * 0.6f),
                             ImVec2(tip.x - ux * h * 0.6f + uy * h * 0.6f, tip.y - uy * h * 0.6f - ux * h * 0.6f), colour);
    };
    // Part of an ellipse round `centre`, from one angle to the other, its arrow head at the end.
    const auto turn = [&](ImVec2 centre, float rx, float ry, float from, float to) {
        constexpr int steps = 22;
        std::array<ImVec2, steps + 1> pts{};
        for (int i = 0; i <= steps; ++i) {
            const float t = from + (to - from) * static_cast<float>(i) / steps;
            pts[static_cast<std::size_t>(i)] = ImVec2(centre.x + std::cos(t) * rx * s, centre.y + std::sin(t) * ry * s);
        }
        d->AddPolyline(pts.data(), steps + 1, colour, ImDrawFlags_None, line);
        head(pts[steps], pts[steps].x - pts[steps - 1].x, pts[steps].y - pts[steps - 1].y);
    };
    const ImU32 ground = with_alpha(colour, 110);
    switch (type) {
    case tb::Type::flip:
        deck(0, 0.04f, c);
        turn(at(0, 0.02f), 0.2f, 0.4f, -2.5f, 2.3f);
        break;
    case tb::Type::shuv:
        deck(0, 0, c);
        turn(at(0, 0.03f), 0.52f, 0.2f, 0.6f, 5.7f);
        break;
    case tb::Type::grind:
        deck(0, -0.14f, c);
        d->AddLine(at(-0.52f, 0.1f), at(0.52f, 0.1f), colour, line * 1.3f);
        d->AddLine(at(-0.3f, 0.1f), at(-0.3f, 0.38f), colour, line);
        d->AddLine(at(0.3f, 0.1f), at(0.3f, 0.38f), colour, line);
        break;
    case tb::Type::manual:
        d->AddLine(at(-0.52f, 0.25f), at(0.52f, 0.25f), ground, line);
        deck(-0.34f, 0, at(0.04f, 0.0f));
        break;
    case tb::Type::grab:
        deck(0, 0.1f, c);
        d->AddLine(at(0.2f, -0.44f), at(0.04f, -0.12f), colour, line * 1.3f); // the arm
        d->AddCircleFilled(at(0.02f, -0.08f), s * 0.085f, colour, 14);        // the hand, on the deck's edge
        break;
    case tb::Type::other:
        deck(0, 0.18f, c);
        d->AddLine(at(0, 0.0f), at(0, -0.34f), colour, line * 1.2f);
        head(at(0, -0.36f), 0, -1);
        break;
    }
}

// ---- choosing and opening ----
// Opens it in the editor (asking about changes not saved there); one locked to its maker is watched instead.
void shelf_open(const studio::View& v, const std::string& name) {
    const auto* p = preset_named(v, name);
    if (p && p->locked) preview_trick({}, name, false);
    else open_saved(name);
}
// A press on an item. With the pad, A on the one already chosen opens it (the mouse opens with a double click).
void shelf_click(const studio::View& v, const std::string& name) {
    auto& s = shelf();
    auto& u = ui();
    const auto& io = ImGui::GetIO();
    const bool pad = nav().keys;
    if (pad && !s.select_mode && s.picked.names.size() == 1 && s.picked.names.contains(name)) {
        shelf_open(v, name);
        return;
    }
    tb::click(s.picked, s.order, name, io.KeyCtrl || s.select_mode, io.KeyShift && !pad);
    u.trick_page = s.picked.names.size() == 1 ? *s.picked.names.begin() : std::string{};
    s.page_seen = u.trick_page;
    u.piece_card.clear();
    u.label_for.clear();
    u.confirm_delete.clear();
    s.confirm_bulk_delete = false;
    s.bulk_open = 0;
}
void shelf_reveal(const std::string& name) {
    auto& s = shelf();
    s.reveal = name;
    s.reveal_tries = 30;
}
void shelf_clear_choice() {
    auto& s = shelf();
    s.picked = {};
    s.confirm_bulk_delete = false;
    s.bulk_open = 0;
    ui().trick_page.clear();
    s.page_seen.clear();
}
// B, Esc: SELECT off, then what is chosen let go. True when there was something.
bool shelf_back() {
    auto& s = shelf();
    if (s.select_mode) {
        s.select_mode = false;
        return true;
    }
    if (s.picked.names.size() > 1) {
        shelf_clear_choice();
        return true;
    }
    return false;
}

// ---- the pieces of the list ----
constexpr ImU32 tricks_row = IM_COL32(30, 31, 35, 246), tricks_row_lit = IM_COL32(46, 48, 55, 250), tricks_row_chosen = IM_COL32(17, 57, 91, 248);
const char* shelf_stance(const tb::Item& it) { return it.stance == 0 ? "NORMAL" : stance_word(it.stance, nollie_trick(it.trick)); }
std::string shelf_more(const std::vector<std::string>& names) {
    if (names.empty()) return "-";
    return names.size() == 1 ? names.front() : names.front() + " +" + std::to_string(names.size() - 1);
}
// Small tags at a row's right, laid leftwards from `right`: its pop, its stance when it is not your own, IN DEFAULT when it
// is in the default playlist (the one that plays on normal input), LOCKED. Returns where they stop.
float shelf_tags(ImDrawList* d, const Fonts& f, const tb::Item& it, float right, float y, float leftmost) {
    const float k = f.k;
    const auto put = [&](const std::string& label, ImU32 fill) {
        const float w = tag_width(f, label);
        if (right - w < leftmost) return;
        right -= w;
        tag(d, f, ImVec2(right, y), label, fill);
        right -= 6 * k;
    };
    if (it.locked) put("LOCKED", IM_COL32(120, 34, 36, 255));
    if (it.plays) put("IN DEFAULT", IM_COL32(36, 150, 76, 255));
    if (it.stance != 0) put(shelf_stance(it), IM_COL32(196, 104, 0, 255));
    if (it.pop >= 0) put(tb::pop_names[static_cast<std::size_t>(it.pop)], it.pop == 0 ? IM_COL32(255, 255, 255, 36) : blue_dim);
    return right;
}
// A group's heading: its fold, its name, how many, and (choosing several) a way to choose all of it.
void shelf_group_row(ImDrawList* d, const Fonts& f, const tb::Group& g, bool folded, ImVec2 a, ImVec2 b) {
    auto& s = shelf();
    const float k = f.k, h = b.y - a.y, cy = a.y + h * 0.5f;
    ImGui::PushID(("group|" + g.key).c_str());
    float right = b.x;
    if (s.select_mode) {
        const float bw = 96 * k;
        right -= bw;
        const bool all = std::all_of(g.items.begin(), g.items.end(), [&](std::size_t i) { return s.picked.names.contains(s.items[i].name); });
        if (action(d, f, "##groupall", ImVec2(right, a.y + 4 * k), ImVec2(bw, h - 8 * k), all ? "NONE" : "ALL", Look::ghost)) {
            for (const auto i : g.items) {
                if (all) s.picked.names.erase(s.items[i].name);
                else s.picked.names.insert(s.items[i].name);
            }
            ui().trick_page = s.picked.names.size() == 1 ? *s.picked.names.begin() : std::string{};
            s.page_seen = ui().trick_page;
        }
        right -= 10 * k;
    }
    bool lit = false;
    if (hit("##fold", a, ImVec2(right, b.y), &lit)) {
        if (folded) s.folded.erase(g.key);
        else s.folded.insert(g.key);
    }
    around_peek(d, a, ImVec2(right, b.y), [&] {
        if (lit) d->AddRectFilled(a, ImVec2(right, b.y), IM_COL32(255, 255, 255, 14));
        if (lit && nav().keys) chosen_outline(d, a, ImVec2(right, b.y), k);
        const float cx = a.x + 14 * k, t = 6 * k;
        if (folded) d->AddTriangleFilled(ImVec2(cx - t * 0.55f, cy - t), ImVec2(cx + t * 0.75f, cy), ImVec2(cx - t * 0.55f, cy + t), white);
        else d->AddTriangleFilled(ImVec2(cx - t, cy - t * 0.55f), ImVec2(cx + t, cy - t * 0.55f), ImVec2(cx, cy + t * 0.75f), blue_light);
        const float fs = 19 * k;
        ImFont* font = anton(f, fs);
        const auto [ct, cf] = caps_of(font, fs);
        const std::string count = std::to_string(g.items.size());
        const float cw = text_size(mont(f, 12 * k), 12 * k, count.c_str()).x;
        const auto title = elide(font, fs, upper(g.title), right - a.x - 64 * k - cw);
        const float tw = text_size(font, fs, title.c_str()).x;
        text(d, font, fs, ImVec2(a.x + 32 * k, cy - (cf - ct) * 0.5f - ct), IM_COL32_WHITE, title);
        const auto [nt, nf] = caps_of(mont(f, 12 * k), 12 * k);
        text(d, mont(f, 12 * k), 12 * k, ImVec2(a.x + 44 * k + tw, cy - (nf - nt) * 0.5f - nt), soft, count);
        const float line_x = a.x + 56 * k + tw + cw;
        if (line_x < right - 8 * k) d->AddRectFilled(ImVec2(line_x, cy), ImVec2(right - 8 * k, cy + 1.5f * k), IM_COL32(255, 255, 255, 34));
    });
    ImGui::PopID();
}
// The tick box that shows SELECT is on, at the row's left.
float shelf_tick(ImDrawList* d, const Fonts& f, float x, float cy, bool chosen) {
    if (!shelf().select_mode) return x;
    tick_box(d, ImVec2(x + 9 * f.k, cy), 8 * f.k, chosen, chosen ? white : soft, f.k);
    return x + 28 * f.k;
}
// A row of the LIST: its kind's board, its name, then the game's name for its trick; its tags at the right.
void shelf_list_row(ImDrawList* d, const Fonts& f, const studio::View& v, const tb::Item& it, ImVec2 a, ImVec2 b, bool chosen, bool lit) {
    const float k = f.k, cy = (a.y + b.y) * 0.5f;
    flat_rect(d, a, b, chosen ? tricks_row_chosen : lit ? tricks_row_lit : tricks_row, seed_of(a), k * 0.45f);
    if (it.name == v.name && v.has_clip) d->AddRectFilled(ImVec2(a.x, a.y + 5 * k), ImVec2(a.x + 4 * k, b.y - 5 * k), blue);
    float x = shelf_tick(d, f, a.x + 12 * k, cy, chosen);
    trick_glyph(d, ImVec2(x + 15 * k, cy + 1 * k), 28 * k, it.type, type_colour(it.type));
    x += 40 * k;
    const float tags_end = shelf_tags(d, f, it, b.x - 12 * k, cy - 10 * k, x + 200 * k);
    const float room = tags_end - x - 14 * k;
    ImFont* name_font = body(f, 16 * k);
    const auto name = elide(name_font, 16 * k, it.shown, room * 0.68f);
    const float nw = text_size(name_font, 16 * k, name.c_str()).x;
    text(d, name_font, 16 * k, ImVec2(x, cy - 10 * k), white, name);
    if (const float left = room - nw - 18 * k; left > 40 * k)
        text(d, body(f, 13 * k), 13 * k, ImVec2(x + nw + 14 * k, cy - 7.5f * k), soft, elide(body(f, 13 * k), 13 * k, it.trick_label, left));
    if (lit && nav().keys) chosen_outline(d, a, b, k);
}
// A row of DETAILS: a cell a column.
void shelf_details_row(ImDrawList* d, const Fonts& f, const studio::View& v, const tb::Item& it, const std::vector<std::pair<tb::Column, float>>& cols,
                       ImVec2 a, ImVec2 b, bool chosen, bool lit) {
    const float k = f.k, cy = (a.y + b.y) * 0.5f, fs = 14 * k;
    flat_rect(d, a, b, chosen ? tricks_row_chosen : lit ? tricks_row_lit : tricks_row, seed_of(a), k * 0.45f);
    if (it.name == v.name && v.has_clip) d->AddRectFilled(ImVec2(a.x, a.y + 5 * k), ImVec2(a.x + 4 * k, b.y - 5 * k), blue);
    ImFont* font = body(f, fs);
    float x = a.x + 10 * k;
    for (const auto& [column, width] : cols) {
        const float room = width - 14 * k;
        std::string value;
        ImU32 ink = soft;
        switch (column) {
        case tb::Column::name: {
            float nx = shelf_tick(d, f, x, cy, chosen);
            trick_glyph(d, ImVec2(nx + 12 * k, cy + 1 * k), 22 * k, it.type, type_colour(it.type));
            nx += 32 * k;
            const float w = std::max(0.0f, x + room - nx - (it.locked ? 20 * k : 0));
            text(d, body(f, 15 * k), 15 * k, ImVec2(nx, cy - 9 * k), white, elide(body(f, 15 * k), 15 * k, it.shown, w));
            if (it.locked) { // a little padlock
                const ImVec2 l(x + room - 12 * k, cy - 1 * k);
                d->AddRectFilled(ImVec2(l.x - 6 * k, l.y), ImVec2(l.x + 6 * k, l.y + 9 * k), IM_COL32(230, 120, 120, 255), 1.5f * k);
                d->AddCircle(ImVec2(l.x, l.y), 4 * k, IM_COL32(230, 120, 120, 255), 12, 1.8f * k);
            }
            x += width;
            continue;
        }
        case tb::Column::trick: value = it.trick_label; break;
        case tb::Column::stance:
            value = shelf_stance(it);
            if (it.stance != 0) ink = IM_COL32(240, 160, 70, 255);
            break;
        case tb::Column::pop: value = it.pop < 0 ? "-" : tb::pop_names[static_cast<std::size_t>(it.pop)]; break;
        case tb::Column::pack: value = shelf_more(it.packs); break;
        case tb::Column::maker: value = it.maker.empty() ? "-" : it.maker; break;
        case tb::Column::modified: value = edited_words(it.modified); break;
        case tb::Column::playlists:
            value = shelf_more(it.playlists);
            if (it.plays) ink = IM_COL32(110, 214, 140, 255);
            break;
        }
        text(d, font, fs, ImVec2(x, cy - 8.5f * k), ink, elide(font, fs, value, room));
        x += width;
    }
    if (lit && nav().keys) chosen_outline(d, a, b, k);
}
// DETAILS' heading: the columns' names; pressed, the list is sorted by that one (again: the other way round).
void shelf_details_head(ImDrawList* d, const Fonts& f, const std::vector<std::pair<tb::Column, float>>& cols, ImVec2 a, ImVec2 b) {
    auto& s = shelf();
    const float k = f.k, cy = (a.y + b.y) * 0.5f, fs = 11 * k;
    d->AddRectFilled(ImVec2(a.x, b.y - 1.5f * k), ImVec2(b.x, b.y), IM_COL32(255, 255, 255, 40));
    float x = a.x + 10 * k;
    for (const auto& [column, width] : cols) {
        ImGui::PushID(static_cast<int>(column));
        bool lit = false;
        if (hit("##sortby", ImVec2(x - 6 * k, a.y), ImVec2(x + width - 6 * k, b.y), &lit)) {
            if (s.sort.by == column) s.sort.descending = !s.sort.descending;
            else s.sort = {column, column == tb::Column::modified}; // newest first, the way anyone looks for the latest
            shelf_save_prefs();
        }
        ImGui::PopID();
        const bool sorted = s.sort.by == column;
        if (lit) d->AddRectFilled(ImVec2(x - 6 * k, a.y), ImVec2(x + width - 6 * k, b.y - 1.5f * k), IM_COL32(255, 255, 255, 18));
        if (lit && nav().keys) chosen_outline(d, ImVec2(x - 6 * k, a.y), ImVec2(x + width - 6 * k, b.y), k);
        const std::string label = column == tb::Column::name ? std::string("NAME") : tb::column_names[static_cast<std::size_t>(column)];
        const auto [ct, cf] = caps_of(mont(f, fs), fs);
        const float w = text_spaced(d, mont(f, fs), fs, ImVec2(x + (column == tb::Column::name && s.select_mode ? 28 * k : 0), cy - (cf - ct) * 0.5f - ct),
                                    sorted ? white : soft, label, 1.2f * k);
        if (sorted) { // which way: up for A to Z (and oldest first), down for the other way
            const float ax = x + w + (column == tb::Column::name && s.select_mode ? 36 * k : 8 * k), t = 4.5f * k;
            if (s.sort.descending) d->AddTriangleFilled(ImVec2(ax - t, cy - t * 0.5f), ImVec2(ax + t, cy - t * 0.5f), ImVec2(ax, cy + t * 0.7f), blue_light);
            else d->AddTriangleFilled(ImVec2(ax - t, cy + t * 0.5f), ImVec2(ax + t, cy + t * 0.5f), ImVec2(ax, cy - t * 0.7f), blue_light);
        }
        x += width;
    }
}
// An icon: its kind's board big on a tile, its name under it, its pop and stance.
void shelf_tile(ImDrawList* d, const Fonts& f, const studio::View& v, const tb::Item& it, ImVec2 a, ImVec2 b, bool chosen, bool lit) {
    const float k = f.k, w = b.x - a.x;
    flat_rect(d, a, b, chosen ? tricks_row_chosen : lit ? tricks_row_lit : tricks_row, seed_of(a), k * 0.6f);
    if (chosen) d->AddRect(a, b, blue_light, 0, 0, 2 * k);
    if (it.name == v.name && v.has_clip) d->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, a.y + 4 * k), blue);
    const ImU32 colour = type_colour(it.type);
    const ImVec2 c((a.x + b.x) * 0.5f, a.y + 52 * k);
    d->AddCircleFilled(c, 40 * k, with_alpha(colour, 26), 36);
    trick_glyph(d, ImVec2(c.x, c.y + 2 * k), 64 * k, it.type, colour);
    if (shelf().select_mode) tick_box(d, ImVec2(a.x + 18 * k, a.y + 18 * k), 8 * k, chosen, chosen ? white : soft, k);
    if (it.locked) tag(d, f, ImVec2(b.x - tag_width(f, "LOCKED") - 8 * k, a.y + 8 * k), "LOCKED", IM_COL32(120, 34, 36, 255));
    else if (it.plays) tag(d, f, ImVec2(b.x - tag_width(f, "IN DEFAULT") - 8 * k, a.y + 8 * k), "IN DEFAULT", IM_COL32(36, 150, 76, 255));
    ImFont* font = body(f, 15 * k);
    const auto name = elide(font, 15 * k, it.shown, w - 20 * k);
    const float nw = text_size(font, 15 * k, name.c_str()).x;
    text(d, font, 15 * k, ImVec2(a.x + (w - nw) * 0.5f, a.y + 102 * k), white, name);
    std::string line = it.pop < 0 ? std::string("PIECE") : std::string(tb::pop_names[static_cast<std::size_t>(it.pop)]) + " POP";
    if (it.stance != 0) line += "  /  " + std::string(shelf_stance(it));
    const auto shown = elide(mont(f, 10.5f * k), 10.5f * k, line, w - 20 * k);
    const float lw = text_size(mont(f, 10.5f * k), 10.5f * k, shown.c_str()).x;
    text(d, mont(f, 10.5f * k), 10.5f * k, ImVec2(a.x + (w - lw) * 0.5f, a.y + 126 * k), it.stance != 0 ? IM_COL32(240, 160, 70, 255) : soft, shown);
    if (lit && nav().keys) chosen_outline(d, a, b, k);
}

// ---- the FILTER tab ----
// A choice of the filter: its name and how many it would show (with the other filters as they are). One that would show
// nothing cannot be pressed until it is on.
float filter_chip_width(const Fonts& f, const std::string& label, std::size_t count) {
    const float k = f.k;
    return text_size(mont(f, 12 * k), 12 * k, label.c_str()).x + text_size(mont(f, 11 * k), 11 * k, std::to_string(count).c_str()).x + 34 * k;
}
bool filter_chip(ImDrawList* d, const Fonts& f, const char* id, ImVec2 at, const std::string& label, std::size_t count, bool on) {
    const float k = f.k, h = 30 * k;
    const ImVec2 b(at.x + filter_chip_width(f, label, count), at.y + h);
    const bool enabled = on || count > 0;
    bool lit = false;
    const bool pressed = hit(id, at, b, &lit, enabled);
    d->AddRectFilled(at, b, on ? (lit ? IM_COL32(40, 152, 255, 255) : blue) : lit ? IM_COL32(255, 255, 255, 26) : IM_COL32(5, 6, 8, 110));
    if (!on) d->AddRect(at, b, IM_COL32(255, 255, 255, enabled ? (lit ? 200 : 100) : 40), 0, 0, 1.4f * k);
    if (lit && nav().keys) chosen_outline(d, at, b, k);
    ImFont* font = mont(f, 12 * k);
    const auto [ct, cf] = caps_of(font, 12 * k);
    const float tw = text_size(font, 12 * k, label.c_str()).x;
    text(d, font, 12 * k, ImVec2(at.x + 11 * k, at.y + (h - (cf - ct)) * 0.5f - ct), enabled ? white : faint, label);
    ImFont* count_font = mont(f, 11 * k);
    const auto [st, sf] = caps_of(count_font, 11 * k);
    text(d, count_font, 11 * k, ImVec2(at.x + 21 * k + tw, at.y + (h - (sf - st)) * 0.5f - st), on ? IM_COL32(220, 236, 255, 255) : enabled ? soft : faint,
         std::to_string(count));
    return pressed;
}
void shelf_filter_panel(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    (void)v;
    auto& s = shelf();
    const float k = f.k;
    library_sheet(d, f, a, b);
    const float x = a.x + 16 * k, w = b.x - a.x - 32 * k;
    // At the top, always in view: what is shown, and CLEAR.
    {
        const std::string shown = std::format("{} OF {} SHOWN", s.counts.shown, s.counts.total);
        lib_caps(d, f, ImVec2(x, a.y + 18 * k), 11 * k, s.filter.active() ? lib_blue : soft, shown);
        if (lib_button(d, f, "##clearfilters", ImVec2(b.x - 16 * k - 96 * k, a.y + 10 * k), ImVec2(96 * k, 30 * k), "CLEAR", Look::ghost, s.filter.active() > 0))
            s.filter.clear();
    }
    const ImVec2 la(a.x, a.y + 50 * k), lb(b.x, b.y - 8 * k);
    begin_region(25, la, lb, s.filter_scroll, k);
    float y = la.y + 4 * k - s.filter_scroll;
    const auto section = [&](const char* name) {
        lib_caps(d, f, ImVec2(x, y), 11 * k, soft, name, 1.8f);
        d->AddRectFilled(ImVec2(x, y + 17 * k), ImVec2(x + w, y + 18.5f * k), IM_COL32(255, 255, 255, 30));
        y += 26 * k;
    };
    // Chips laid along, onto the next line where one does not fit.
    float cx = x;
    const auto chip_at = [&](const std::string& label, std::size_t count) {
        const float cw = std::min(filter_chip_width(f, label, count), w);
        if (cx > x && cx + cw > x + w) cx = x, y += 36 * k;
        const ImVec2 at(cx, y);
        cx += cw + 8 * k;
        return at;
    };
    const auto end_section = [&] {
        y += 46 * k;
        cx = x;
    };
    const auto bits = [&](const char* id, unsigned& set, std::size_t index, const std::string& label, std::size_t count) {
        ImGui::PushID(static_cast<int>(index));
        const bool on = set & (1u << index);
        if (filter_chip(d, f, id, chip_at(label, count), label, count, on)) set ^= 1u << index;
        ImGui::PopID();
    };
    section("KIND");
    for (std::size_t i = 0; i < tb::type_count; ++i) bits("##fkind", s.filter.types, i, tb::type_names[i], s.counts.types[i]);
    end_section();
    section("STANCE");
    for (std::size_t i = 0; i < 4; ++i) bits("##fstance", s.filter.stances, i, tb::stance_names[i], s.counts.stances[i]);
    end_section();
    section("POP");
    for (std::size_t i = 0; i < 4; ++i) bits("##fpop", s.filter.pops, i, tb::pop_names[i], s.counts.pops[i]);
    end_section();
    const auto either = [&](const char* id, int& value, int mine, const std::string& label, std::size_t count) {
        if (filter_chip(d, f, id, chip_at(label, count), label, count, value == mine)) value = value == mine ? 0 : mine;
    };
    section("PLAYLISTS");
    either("##finlist", s.filter.playlists, 1, "IN A PLAYLIST", s.counts.in_playlist);
    either("##fnolist", s.filter.playlists, 2, "IN NONE", s.counts.in_none);
    end_section();
    section("EDITING");
    either("##feditable", s.filter.locks, 2, "CAN BE EDITED", s.counts.editable);
    either("##flocked", s.filter.locks, 1, "LOCKED", s.counts.locked);
    end_section();
    // Packs and makers: as many as there are, the names cut to fit.
    const auto names = [&](const char* id, std::set<std::string>& set, const std::map<std::string, std::size_t>& counts, const char* none) {
        std::vector<std::pair<std::string, std::size_t>> list(counts.begin(), counts.end());
        std::stable_sort(list.begin(), list.end(), [](const auto& l, const auto& r) {
            if (l.first.empty() != r.first.empty()) return r.first.empty(); // "none" last
            return tb::compare_text(l.first, r.first) < 0;
        });
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& [name, count] = list[i];
            const std::string label = name.empty() ? std::string(none) : elide(mont(f, 12 * k), 12 * k, upper(name), w - 60 * k);
            ImGui::PushID(static_cast<int>(i));
            const bool on = set.contains(name);
            if (filter_chip(d, f, id, chip_at(label, count), label, count, on)) {
                if (on) set.erase(name);
                else set.insert(name);
            }
            ImGui::PopID();
        }
        // A pack or maker chosen that is gone stays shown, so it can be taken off.
        for (const auto& gone : set)
            if (!counts.contains(gone)) {
                ImGui::PushID(gone.c_str());
                if (filter_chip(d, f, id, chip_at(upper(gone), 0), elide(mont(f, 12 * k), 12 * k, upper(gone), w - 60 * k), 0, true)) {
                    set.erase(gone);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
    };
    section("PACK");
    names("##fpack", s.filter.packs, s.counts.packs, "IN NO PACK");
    end_section();
    section("MADE BY");
    names("##fmaker", s.filter.makers, s.counts.makers, "NOT SAID");
    end_section();
    end_region(d, la, lb, y + s.filter_scroll - la.y, s.filter_scroll, k);
}

// ---- the card for several chosen ----
// What can be done to all of them at once: copy them, put them in a playlist or a pack, set their pop, delete them.
void shelf_bulk(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& s = shelf();
    const float k = f.k;
    flat_rect(d, a, b, IM_COL32(18, 19, 22, 250), seed_of(a), k);
    if (action(d, f, "##bulkclose", ImVec2(b.x - 52 * k, a.y + 14 * k), ImVec2(38 * k, 34 * k), "X", Look::ghost)) {
        shelf_clear_choice();
        return;
    }
    const std::vector<std::string> names(s.picked.names.begin(), s.picked.names.end());
    std::size_t locked = 0, foreign = 0, pieces = 0;
    for (const auto& name : names)
        if (const auto* it = shelf_item(name)) {
            locked += it->locked;
            foreign += it->from_pack;
            pieces += it->pop < 0;
        }
    const float x = a.x + 26 * k, w = b.x - a.x - 52 * k;
    float y = a.y + 18 * k;
    title_text(d, f, 34 * k, ImVec2(x, y), white, std::format("{} TRICKS CHOSEN", names.size()), w - 50 * k);
    y += 44 * k;
    std::string about = "What you do here is done to every one of them.";
    if (locked) about += std::format(" {} {} locked to {} maker: left out of copies and edits.", locked, locked == 1 ? "is" : "are", locked == 1 ? "its" : "their");
    y += paragraph(d, body(f, 14 * k), 14 * k, ImVec2(x, y), w, soft, about) + 12 * k;
    begin_region(26, ImVec2(a.x, y), ImVec2(b.x, b.y - 10 * k), s.bulk_scroll, k);
    const float top = y;
    y -= s.bulk_scroll;
    // Which they are: the first few by name.
    {
        std::string list;
        const std::size_t shown = std::min<std::size_t>(names.size(), 6);
        for (std::size_t i = 0; i < shown; ++i) list += (i ? ", " : "") + shown_name(v, names[i]);
        if (names.size() > shown) list += std::format(" and {} more", names.size() - shown);
        y += paragraph(d, body(f, 14 * k), 14 * k, ImVec2(x, y), w, IM_COL32(222, 222, 226, 255), list) + 16 * k;
    }
    const auto say_count = [&](std::size_t done, const std::string& what) {
        say(std::format("{} of {} {}.", done, names.size(), what));
    };
    if (lib_button(d, f, "##bulkcopy", ImVec2(x, y), ImVec2(w, 42 * k), std::format("COPY {}", names.size() - locked), Look::blue, names.size() > locked)) {
        std::vector<std::string> made, not_reached;
        std::string m;
        studio::copy_saved(names, made, not_reached, m);
        say(m);
        // The copies are chosen (and shown) to work on; any not reached yet stay chosen, to press again.
        s.picked.names = not_reached.empty() ? std::set<std::string>(made.begin(), made.end()) : std::set<std::string>(not_reached.begin(), not_reached.end());
        s.picked.anchor.clear();
        if (!made.empty()) shelf_reveal(made.front());
        ui().trick_page = s.picked.names.size() == 1 ? *s.picked.names.begin() : std::string{};
        s.page_seen = ui().trick_page;
    }
    y += 52 * k;
    // Into a playlist: the playlists, a press each.
    if (lib_button(d, f, "##bulklists", ImVec2(x, y), ImVec2(w, 42 * k), s.bulk_open == 1 ? "PUT IN A PLAYLIST  -" : "PUT IN A PLAYLIST  +", Look::dark)) s.bulk_open = s.bulk_open == 1 ? 0 : 1;
    y += 50 * k;
    if (s.bulk_open == 1) {
        const auto add_all = [&](const std::string& list) {
            std::size_t done = 0;
            for (const auto& name : names) {
                const auto holding = packs_holding(v, name);
                const auto installed = std::find_if(holding.begin(), holding.end(), [](const auto* pack) { return pack->installed; });
                std::string m;
                done += studio::edit_playlist_member(list, {installed == holding.end() ? std::string{} : (*installed)->name, name}, true, m);
            }
            say_count(done, "put in " + list);
        };
        for (const auto& list : v.playlists) {
            ImGui::PushID(list.name.c_str());
            const std::string label = list.name + (list.name == v.playlist_playing ? "  (DEFAULT)" : "");
            if (lib_button(d, f, "##bulklist", ImVec2(x + 16 * k, y), ImVec2(w - 16 * k, 38 * k), label, Look::ghost)) add_all(list.name);
            ImGui::PopID();
            y += 44 * k;
        }
        if (lib_button(d, f, "##bulknewlist", ImVec2(x + 16 * k, y), ImVec2(w - 16 * k, 38 * k), "+ NEW PLAYLIST WITH THEM", Look::paper)) {
            std::string m;
            if (studio::new_playlist(m)) {
                const auto now = studio::view();
                add_all(now.playlist_open);
            } else say(m);
        }
        y += 50 * k;
    }
    // Into one of your packs (another maker's tricks never go in yours).
    if (lib_button(d, f, "##bulkpacks", ImVec2(x, y), ImVec2(w, 42 * k), s.bulk_open == 2 ? "PUT IN A PACK  -" : "PUT IN A PACK  +", Look::dark, names.size() > foreign))
        s.bulk_open = s.bulk_open == 2 ? 0 : 2;
    y += 50 * k;
    if (s.bulk_open == 2) {
        const auto add_all = [&](const std::string& pack) {
            std::size_t done = 0;
            for (const auto& name : names) {
                const auto* it = shelf_item(name);
                if (!it || it->from_pack) continue;
                std::string m;
                done += studio::set_pack_member(pack, name, true, m);
            }
            say_count(done, "put in " + pack);
        };
        float cx = x + 16 * k;
        for (const auto& pack : v.packs) {
            if (pack.installed) continue;
            const float cw = std::min(chip_width(f, pack.name), w - 16 * k);
            if (cx > x + 16 * k && cx + cw > x + w) cx = x + 16 * k, y += 36 * k;
            ImGui::PushID(pack.name.c_str());
            if (chip(d, f, "##bulkpack", ImVec2(cx, y), pack.name, false)) add_all(pack.name);
            ImGui::PopID();
            cx += cw + 8 * k;
        }
        const float nw = chip_width(f, "+ NEW PACK");
        if (cx > x + 16 * k && cx + nw > x + w) cx = x + 16 * k, y += 36 * k;
        if (chip(d, f, "##bulknewpack", ImVec2(cx, y), "+ NEW PACK", false)) {
            const auto pack = unique_pack_name(v);
            std::string m;
            if (studio::create_preset_pack(m, pack)) add_all(pack);
            else say(m);
        }
        y += 48 * k;
    }
    // Their pop, all at once (a piece of the game's animation has none, and is left as it is).
    heading(d, f, ImVec2(x, y), w, "Plays on");
    y += 32 * k;
    {
        const int picked = segmented(d, f, "##bulkpop", ImVec2(x, y), w, 36 * k, {"ANY POP", "LOW", "MID", "HIGH"}, -1, names.size() > pieces + locked);
        if (picked >= 0) {
            static constexpr std::array<const char*, 4> pops{"", "low", "mid", "high"};
            std::size_t done = 0;
            for (const auto& name : names) {
                const auto* it = shelf_item(name);
                if (!it || it->pop < 0 || it->locked) continue;
                std::string m;
                done += studio::set_saved_pop(name, pops[static_cast<std::size_t>(picked)], m);
            }
            say_count(done, std::string("set to ") + (picked == 0 ? "any pop" : std::string(pops[static_cast<std::size_t>(picked)]) + " pop"));
        }
        y += 50 * k;
    }
    if (action(d, f, "##bulkdelete", ImVec2(x, y), ImVec2(w, 40 * k), s.confirm_bulk_delete ? std::format("PRESS AGAIN TO DELETE {}", names.size())
                                                                                          : std::format("DELETE {} TRICKS", names.size()),
               Look::danger)) {
        if (!s.confirm_bulk_delete) s.confirm_bulk_delete = true;
        else {
            std::size_t done = 0;
            for (const auto& name : names) {
                std::string m;
                std::vector<std::string> gone;
                done += studio::remove(name, m, &gone);
                for (const auto& pack : gone) studio::market::forget_installed(pack);
            }
            say_count(done, "deleted, and taken out of their packs and playlists");
            shelf_clear_choice();
        }
    }
    y += 46 * k;
    if (s.confirm_bulk_delete)
        y += paragraph(d, body(f, 13 * k), 13 * k, ImVec2(x, y), w, skate_theme::warning,
                       "They come out of every pack and playlist they are in. A pack you got from the MARKET that is left empty goes too.") + 10 * k;
    end_region(d, ImVec2(a.x, top), ImVec2(b.x, b.y - 10 * k), y + s.bulk_scroll - top, s.bulk_scroll, k);
}

// ---- nothing to show ----
// MY TRICKS with nothing saved (FROM PACKS with no pack tricks): a card on cream paper, as the LIBRARY's QUICK START, saying
// how a trick gets here, with the way to the game's tricks and to the MARKET. The bars are left out: there is nothing to
// sort or group. (It was one line and a button at the top of a big empty sheet, under bars that had nothing to work on.)
void shelf_empty_card(ImDrawList* d, const Fonts& f, ImVec2 a, ImVec2 b, bool packs) {
    auto& u = ui();
    const float k = f.k;
    const float w = std::min(b.x - a.x - 40 * k, 900 * k);
    const ImVec2 ca(a.x + 20 * k, a.y + 22 * k);
    const float x = ca.x + 40 * k, iw = w - 80 * k;
    struct Step {
        const char *title, *words;
    };
    static constexpr std::array<Step, 3> steps{{
        {"OPEN ONE OF THE GAME'S TRICKS", "Flip tricks, nollie tricks, grinds, grabs, manuals: choose a kind on the left, then a trick and its pop."},
        {"POSE HIM AND KEY IT", "Click a joint on him, turn it, and press KEY. Scrub the timeline to watch it move."},
        {"SAVE IT", "SAVE puts it here, in MY TRICKS. TEST IN GAME lets you skate it straight away."},
    }};
    const char* intro = packs ? "Packs you get in the MARKET, or import, put their tricks here, each pack together. Those locked to their maker "
                                "play and preview; the rest open to change like your own."
                              : "Every trick you save lands here: a folder you can show as icons, a list or details, group and filter. It "
                                "takes three steps.";
    ImFont* intro_font = body(f, 17 * k);
    ImFont* step_font = body(f, 15 * k);
    const auto tall = [&](ImFont* font, float size, const char* words, float width) { return sharp(font, size)->CalcTextSizeA(size, FLT_MAX, width, words).y; };
    const float bw = 300 * k, bh = 52 * k, gap = 16 * k;
    const bool side_by_side = 2 * bw + gap <= iw;
    float h = 40 * k + 26 * k + 64 * k + tall(intro_font, 17 * k, intro, iw) + 28 * k;
    if (!packs)
        for (const auto& step : steps) h += 28 * k + tall(step_font, 15 * k, step.words, iw - 44 * k) + 18 * k;
    h += 8 * k + (packs || side_by_side ? bh : 2 * bh + 12 * k) + 40 * k;
    h = std::min(h, b.y - ca.y - 10 * k);
    const ImVec2 cb(ca.x + w, ca.y + h);
    draw_paper(d, f, kept_piece(w, h, 91, 2.6f * k, 8 * k, 8 * k), ca, 0, ImVec2(0, 0), PaperKind::white, 91, 0.5f, ImVec2(8, 10), 3.0f);
    masking_tape(d, f, ImVec2(ca.x + 70 * k, ca.y + 2 * k), 110 * k, 26 * k, -6, 92);
    masking_tape(d, f, ImVec2(cb.x - 60 * k, ca.y + 4 * k), 96 * k, 24 * k, 7, 93);
    float y = ca.y + 40 * k;
    lib_caps(d, f, ImVec2(x, y), 13 * k, IM_COL32(80, 82, 88, 255), packs ? "FROM PACKS" : "MY TRICKS", 2.5f);
    y += 26 * k;
    const char* headline = packs ? "NO TRICKS FROM PACKS YET" : "MAKE YOUR FIRST TRICK";
    text(d, anton(f, 46 * k), 46 * k, ImVec2(x, y), shop_ink, elide(anton(f, 46 * k), 46 * k, headline, iw));
    y += 64 * k;
    y += paragraph(d, intro_font, 17 * k, ImVec2(x, y), iw, IM_COL32(40, 42, 48, 255), intro) + 28 * k;
    if (!packs)
        for (std::size_t i = 0; i < steps.size(); ++i) {
            step_badge(d, f, ImVec2(x + 15 * k, y + 11 * k), 14 * k, static_cast<int>(i) + 1, StepState::now);
            lib_caps(d, f, ImVec2(x + 44 * k, y + 4 * k), 14 * k, shop_ink, steps[i].title, 1.6f);
            y += 28 * k;
            y += paragraph(d, step_font, 15 * k, ImVec2(x + 44 * k, y), iw - 44 * k, IM_COL32(60, 62, 68, 255), steps[i].words) + 18 * k;
        }
    y += 8 * k;
    if (!packs && lib_button(d, f, "##tofliptricks", ImVec2(x, y), ImVec2(bw, bh), "OPEN FLIP TRICKS", Look::blue)) {
        u.trick_group = "Flip tricks";
        u.cards_scroll = 0;
    }
    const ImVec2 market_at = packs ? ImVec2(x, y) : side_by_side ? ImVec2(x + bw + gap, y) : ImVec2(x, y + bh + 12 * k);
    if (shop_button(d, f, "##tomarket", market_at, ImVec2(bw, bh), "GET PACKS IN THE MARKET", packs ? ShopLook::blue : ShopLook::ink)) open_menu(2);
}
// A search or the filters let nothing through: a note in the list saying so, with the way back.
void shelf_no_match(ImDrawList* d, const Fonts& f, ImVec2 a, float width) {
    auto& u = ui();
    auto& s = shelf();
    const bool searched = !s.filter.terms.empty(), filtered = s.filter.active() > 0;
    std::string words;
    if (searched && filtered)
        words = std::format("No trick here has \"{}\" in it and passes the filters that are on. Search for less, or take a filter off.", u.asset_filter);
    else if (searched)
        words = std::format("No trick here has \"{}\" in its name, its trick, its pack or its maker. Try fewer letters, or another word.", u.asset_filter);
    else words = "No trick here passes all the filters that are on together. Take one off, or clear them.";
    const char* button = searched && filtered ? "CLEAR SEARCH AND FILTERS" : searched ? "CLEAR THE SEARCH" : "CLEAR THE FILTERS";
    const float w = std::min(width, 720 * f.k);
    bool pressed = false;
    empty_note(d, f, ImVec2(a.x + (width - w) * 0.5f, a.y), w, "nothing matches.", words, button, &pressed, 23);
    if (pressed) {
        if (filtered) s.filter.clear();
        if (searched) {
            u.asset_query.fill(0);
            u.asset_filter.clear();
        }
    }
}

// ---- the page ----
// `game_matches`: how many of the game's own tricks the search finds too (a way to them is offered under the list).
void shelf_page(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b, std::size_t game_matches) {
    auto& u = ui();
    auto& s = shelf();
    const float k = f.k;
    const bool packs = u.trick_group == "*packs";
    s.filter.source = packs ? tb::Source::packs : tb::Source::own;
    if (u.asset_filter != s.search_seen) {
        s.search_seen = u.asset_filter;
        s.filter.terms = studio::native_search_terms(u.asset_filter);
        s.scroll = 0;
    }
    // The trick whose card is open was changed elsewhere (TO MY TRICKS after a save, its card's X, Esc, a delete): the
    // choice follows it.
    if (u.trick_page != s.page_seen) {
        if (u.trick_page.empty()) {
            if (s.picked.names.size() <= 1) s.picked = {};
        } else {
            s.picked.names = {u.trick_page};
            s.picked.anchor = u.trick_page;
            shelf_reveal(u.trick_page);
        }
        s.page_seen = u.trick_page;
    }
    auto& group_by = packs ? s.group_packs : s.group_mine;
    // What is shown: worked out again only when the items, the filters, the order or the grouping changed.
    {
        tb::Mark m;
        m.add(s.generation);
        m.add(static_cast<std::uint64_t>(s.filter.source));
        for (const auto& t : s.filter.terms) m.add(t);
        m.add(s.filter.types);
        m.add(s.filter.stances);
        m.add(s.filter.pops);
        m.add(std::uint64_t{0xfa});
        for (const auto& p : s.filter.packs) m.add(p);
        m.add(std::uint64_t{0xfb});
        for (const auto& p : s.filter.makers) m.add(p);
        m.add(static_cast<std::uint64_t>(s.filter.playlists * 4 + s.filter.locks));
        m.add(static_cast<std::uint64_t>(s.sort.by) * 2 + s.sort.descending);
        m.add(static_cast<std::uint64_t>(group_by));
        if (m.value != s.shown_mark) {
            s.shown_mark = m.value;
            s.groups = tb::arrange(s.items, s.filter, s.sort, group_by);
            s.counts = tb::count(s.items, s.filter);
            ++s.shown_generation;
        }
    }
    const bool several = s.picked.names.size() > 1;
    const bool drawer = !s.picked.names.empty() && (several || saved_trick(v, u.trick_page));
    const float drawer_w = 470 * k;
    const float right = drawer ? b.x - drawer_w - 24 * k : b.x;
    const float w = right - a.x;
    if (s.counts.total == 0) {
        shelf_empty_card(d, f, a, ImVec2(right, b.y), packs);
        if (drawer && !several) trick_drawer(d, f, v, ImVec2(b.x - drawer_w, a.y), b);
        return;
    }
    // ---- the bar: how it is shown and grouped, then the order, SELECT and folding ----
    const float bar_h = 36 * k;
    float y = a.y;
    {
        static const std::vector<std::string> views{"ICONS", "LIST", "DETAILS"};
        const float vw = std::min(246 * k, w * 0.36f);
        if (const int picked = segmented(d, f, "##shelfview", ImVec2(a.x, y), vw, bar_h, views, static_cast<int>(s.mode)); picked >= 0) {
            s.mode = static_cast<tb::ViewMode>(picked);
            shelf_save_prefs();
        }
        static const std::vector<std::string> groupings{"BY TRICK", "BY KIND", "BY PACK", "NO GROUPS"};
        const float gx = a.x + vw + 14 * k, gw = std::min(400 * k, right - gx);
        if (const int picked = segmented(d, f, "##shelfgroup", ImVec2(gx, y), gw, bar_h, groupings, static_cast<int>(group_by)); picked >= 0) {
            group_by = static_cast<tb::GroupBy>(picked);
            shelf_save_prefs();
        }
    }
    y += bar_h + 8 * k;
    {
        lib_caps(d, f, ImVec2(a.x, y + 12 * k), 11 * k, soft, "SORT", 1.6f);
        const float sx = a.x + 50 * k, sw = std::min(320 * k, w * 0.46f);
        static const std::vector<std::string> sorts{"NAME", "TRICK", "POP", "NEWEST"};
        const int chosen = s.sort.by == tb::Column::name ? 0 : s.sort.by == tb::Column::trick ? 1 : s.sort.by == tb::Column::pop ? 2 : s.sort.by == tb::Column::modified ? 3 : -1;
        if (const int picked = segmented(d, f, "##shelfsort", ImVec2(sx, y), sw, bar_h, sorts, chosen); picked >= 0 && picked != chosen) {
            static constexpr std::array<tb::Column, 4> columns{tb::Column::name, tb::Column::trick, tb::Column::pop, tb::Column::modified};
            s.sort = {columns[static_cast<std::size_t>(picked)], picked == 3};
            shelf_save_prefs();
        }
        // Which way round: an arrow up (A to Z, oldest first, low to high) or down.
        const ImVec2 da(sx + sw + 8 * k, y), db(da.x + 44 * k, y + bar_h);
        if (action(d, f, "##shelfdir", da, ImVec2(db.x - da.x, bar_h), "", Look::dark)) {
            s.sort.descending = !s.sort.descending;
            shelf_save_prefs();
        }
        const ImVec2 c((da.x + db.x) * 0.5f, (da.y + db.y) * 0.5f);
        const float t = 6 * k;
        d->AddLine(ImVec2(c.x, c.y - t * 1.4f), ImVec2(c.x, c.y + t * 1.4f), white, 2 * k);
        if (s.sort.descending) d->AddTriangleFilled(ImVec2(c.x - t, c.y + t * 0.4f), ImVec2(c.x + t, c.y + t * 0.4f), ImVec2(c.x, c.y + t * 1.6f), white);
        else d->AddTriangleFilled(ImVec2(c.x - t, c.y - t * 0.4f), ImVec2(c.x + t, c.y - t * 0.4f), ImVec2(c.x, c.y - t * 1.6f), white);
        // At the right: SELECT (choose several with a press each, as a pad must) and folding every group.
        float rx = right;
        const char* select = s.select_mode ? "SELECTING" : "SELECT";
        const float selw = chip_width(f, select);
        rx -= selw;
        if (chip(d, f, "##shelfselect", ImVec2(rx, y + 4 * k), select, s.select_mode)) s.select_mode = !s.select_mode;
        const bool grouped = group_by != tb::GroupBy::none && !s.groups.empty();
        if (grouped) {
            const bool any_open = std::any_of(s.groups.begin(), s.groups.end(), [&](const tb::Group& g) { return !s.folded.contains(g.key); });
            const char* fold = any_open ? "FOLD ALL" : "OPEN ALL";
            const float fw = chip_width(f, fold);
            if (rx - 8 * k - fw > db.x + 10 * k) {
                rx -= 8 * k + fw;
                if (chip(d, f, "##shelffold", ImVec2(rx, y + 4 * k), fold, false)) {
                    if (any_open)
                        for (const auto& g : s.groups) s.folded.insert(g.key);
                    else s.folded.clear();
                }
            }
        }
    }
    y += bar_h + 10 * k;
    // ---- a line: how many are shown, and how to choose ----
    {
        const std::string count = s.counts.shown == s.counts.total ? std::format("{} {}", s.counts.total, s.counts.total == 1 ? "TRICK" : "TRICKS")
                                                                     : std::format("SHOWING {} OF {}", s.counts.shown, s.counts.total);
        float lx = a.x;
        lx += text_spaced(d, mont(f, 11 * k), 11 * k, ImVec2(lx, y + 6 * k), s.counts.shown == s.counts.total ? soft : lib_blue, count, 1.4f * k) + 14 * k;
        if (const int n = s.filter.active(); n > 0) {
            const std::string filters = std::format("{} {} ON", n, n == 1 ? "FILTER" : "FILTERS");
            lx += text_spaced(d, mont(f, 11 * k), 11 * k, ImVec2(lx, y + 6 * k), lib_blue, filters, 1.4f * k) + 10 * k;
            if (action(d, f, "##shelfclear", ImVec2(lx, y), ImVec2(80 * k, 26 * k), "CLEAR", Look::ghost)) s.filter.clear();
            lx += 92 * k;
        }
        const std::string hint = nav().keys ? (s.select_mode ? "A TICKS ONE   /   B ENDS SELECT" : "A CHOOSES   /   A AGAIN OPENS   /   SELECT CHOOSES SEVERAL")
                                 : s.picked.names.size() > 1 ? std::format("{} CHOSEN   /   CTRL + CLICK ADDS OR TAKES OFF   /   ESC LETS GO", s.picked.names.size())
                                                             : std::string("CLICK CHOOSES   /   DOUBLE CLICK OPENS   /   CTRL OR SHIFT + CLICK CHOOSE SEVERAL");
        const float hw = spaced_width(mont(f, 10.5f * k), 10.5f * k, hint, 1.2f * k);
        if (right - lx > hw + 10 * k) text_spaced(d, mont(f, 10.5f * k), 10.5f * k, ImVec2(right - hw, y + 7 * k), faint, hint, 1.2f * k);
    }
    y += 34 * k;
    // The game's own tricks the search finds too: the way to them.
    if (game_matches > 0) {
        const std::string said = std::format("{} of the game's own tricks match \"{}\" too.", game_matches, u.asset_filter);
        text(d, body(f, 14 * k), 14 * k, ImVec2(a.x, y + 7 * k), soft, elide(body(f, 14 * k), 14 * k, said, w - 170 * k));
        if (lib_button(d, f, "##seegame", ImVec2(right - 150 * k, y), ImVec2(150 * k, 32 * k), "SEE THEM", Look::ghost)) {
            u.trick_group = "*search";
            u.cards_scroll = 0;
        }
        y += 42 * k;
    }
    // ---- the list ----
    const ImVec2 sheet_a(a.x, y), sheet_b(right, b.y);
    library_sheet(d, f, sheet_a, sheet_b);
    const float pad = 12 * k;
    ImVec2 la(sheet_a.x + pad, sheet_a.y + pad), lb(sheet_b.x - pad, sheet_b.y - pad);
    const float list_w = lb.x - la.x - 10 * k; // the scroll bar's room at the right
    if (s.counts.shown == 0) {
        shelf_no_match(d, f, ImVec2(la.x, la.y + 30 * k), list_w);
    } else {
        // The columns of DETAILS, its heading kept at the top while the rows scroll under it.
        std::vector<std::pair<tb::Column, float>> cols;
        if (s.mode == tb::ViewMode::details) {
            cols = tb::fit_columns(list_w - 20 * k, k);
            shelf_details_head(d, f, cols, la, ImVec2(la.x + list_w, la.y + 30 * k));
            la.y += 36 * k;
        }
        // How it is laid out: again only when what is shown, the view, the folds or the width changed.
        // ICONS as many across as fit; LIST rows two or more across once there is room (one row 2000 units long on a 21:9
        // screen left a name far from its tags); DETAILS a table, an item a row.
        const float tile_min = s.mode == tb::ViewMode::list ? 600 * k : 184 * k, tile_gap = s.mode == tb::ViewMode::list ? 16 * k : 12 * k;
        const std::size_t columns =
            s.mode == tb::ViewMode::details ? 1 : static_cast<std::size_t>(std::max(1.0f, std::floor((list_w + tile_gap) / (tile_min + tile_gap))));
        tb::Metrics metrics;
        metrics.header_h = 40 * k;
        metrics.row_h = s.mode == tb::ViewMode::details ? 38 * k : 44 * k;
        metrics.tile_h = 152 * k;
        metrics.gap = s.mode == tb::ViewMode::icons ? tile_gap : 4 * k;
        metrics.group_gap = 14 * k;
        metrics.columns = columns;
        {
            tb::Mark m;
            m.add(s.shown_generation);
            m.add(static_cast<std::uint64_t>(s.mode));
            m.add(columns);
            m.add(static_cast<std::uint64_t>(k * 1000));
            for (const auto& g : s.folded) m.add(g);
            if (m.value != s.layout_mark) {
                s.layout_mark = m.value;
                s.layout = tb::lay_out(s.groups, s.folded, s.mode, metrics);
                s.order.clear();
                for (const auto i : tb::shown_order(s.groups, s.folded)) s.order.push_back(s.items[i].name);
            }
        }
        // Ctrl + A: everything shown chosen (not while typing).
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && !ImGui::GetIO().WantTextInput && !s.order.empty()) {
            s.picked.names = std::set<std::string>(s.order.begin(), s.order.end());
            s.picked.anchor = s.order.front();
            u.trick_page = s.picked.names.size() == 1 ? s.order.front() : std::string{};
            s.page_seen = u.trick_page;
        }
        // Scrolled to one asked for (TO MY TRICKS after a save, the copies just made); its group unfolded first. It is
        // looked for a few frames: copies just made are in the list only once the library has been read again.
        if (!s.reveal.empty()) {
            bool found = false;
            for (const auto& row : s.layout.rows) {
                if (row.header) continue;
                const auto& g = s.groups[row.group];
                for (std::size_t j = 0; j < row.count && !found; ++j)
                    if (s.items[g.items[row.first + j]].name == s.reveal) {
                        found = true;
                        const float view_h = lb.y - la.y;
                        if (row.top < s.scroll) s.scroll = std::max(0.0f, row.top - 16 * k);
                        else if (row.top + row.height > s.scroll + view_h) s.scroll = row.top + row.height - view_h + 16 * k;
                    }
                if (found) break;
            }
            if (!found)
                for (const auto& g : s.groups)
                    if (s.folded.contains(g.key) && std::any_of(g.items.begin(), g.items.end(), [&](std::size_t i) { return s.items[i].name == s.reveal; })) {
                        s.folded.erase(g.key); // laid out again next frame, and scrolled to then
                        break;
                    }
            if (found || --s.reveal_tries <= 0) s.reveal.clear();
        }
        u.grid_a = la;
        u.grid_b = lb;
        u.grid_gap = metrics.gap;
        begin_region(24, la, lb, s.scroll, k);
        const auto [from, to] = tb::visible(s.layout, s.scroll, s.scroll + (lb.y - la.y), 2);
        const float tile_w = columns > 1                       ? (list_w - tile_gap * static_cast<float>(columns - 1)) / static_cast<float>(columns)
                             : s.mode == tb::ViewMode::list ? list_w
                                                            : std::min(list_w, 260 * k);
        for (std::size_t r = from; r < to; ++r) {
            const auto& row = s.layout.rows[r];
            const float ry = la.y + row.top - s.scroll;
            const auto& g = s.groups[row.group];
            ImGui::PushID(static_cast<int>(row.group));
            if (row.header) {
                shelf_group_row(d, f, g, s.folded.contains(g.key), ImVec2(la.x, ry), ImVec2(la.x + list_w, ry + row.height));
                ImGui::PopID();
                continue;
            }
            for (std::size_t j = 0; j < row.count; ++j) {
                const auto& it = s.items[g.items[row.first + j]];
                const bool across = s.mode != tb::ViewMode::details;
                const ImVec2 ia = across ? ImVec2(la.x + static_cast<float>(j) * (tile_w + tile_gap), ry) : ImVec2(la.x, ry);
                const ImVec2 ib = across ? ImVec2(ia.x + tile_w, ry + row.height) : ImVec2(la.x + list_w, ry + row.height);
                ImGui::PushID(it.name.c_str());
                if (s.mode == tb::ViewMode::icons && card_under_peek(ia, ib)) {
                    ImGui::PopID();
                    continue;
                }
                bool lit = false;
                bool pressed = hit("##item", ia, ib, &lit);
                // Under the hover preview's window the row is hidden: a click there is not on it.
                if (u.peek_hole && ImGui::IsMouseHoveringRect(u.peek_a, u.peek_b, false) && !nav().keys) pressed = lit = false;
                const bool chosen = s.picked.names.contains(it.name);
                // A double click opens it (the first click already chose it).
                if (lit && !nav().keys && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !s.select_mode && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
                    shelf_open(v, it.name);
                else if (pressed) shelf_click(v, it.name);
                const bool on_screen = ib.y > la.y && ia.y < lb.y;
                if (on_screen) {
                    switch (s.mode) {
                    case tb::ViewMode::icons: shelf_tile(d, f, v, it, ia, ib, chosen, lit); break;
                    case tb::ViewMode::list: around_peek(d, ia, ib, [&] { shelf_list_row(d, f, v, it, ia, ib, chosen, lit); }); break;
                    case tb::ViewMode::details: around_peek(d, ia, ib, [&] { shelf_details_row(d, f, v, it, cols, ia, ib, chosen, lit); }); break;
                    }
                    const std::string name = it.name;
                    note_hover("mine|" + name, ia, ib, [name](bool play) { return studio::peek_saved(name, play); }, it.shown);
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        end_region(d, la, lb, s.layout.height + 8 * k, s.scroll, k);
    }
    // ---- beside it: the chosen trick's card, or the card for several ----
    if (drawer) {
        if (several) shelf_bulk(d, f, v, ImVec2(b.x - drawer_w, a.y), b);
        else trick_drawer(d, f, v, ImVec2(b.x - drawer_w, a.y), b);
    }
}
