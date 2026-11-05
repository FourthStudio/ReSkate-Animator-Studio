// ---- PACKS: making your own packs and putting them in the MARKET ----------------------------------------------------
// The user, 2026-11-05: "the PACKS page is only for creating and publishing your own pack". Its front page: CREATE A PACK,
// DOWNLOADED PACKS (the packs you got are in TRICKS > FROM PACKS, and play from the LIBRARY), and your packs as prints on
// the wall. A pack opened is its editor, laid out as the MARKET lays out a pack: its cover on a print, its name in Anton,
// its numbers, PUBLISH in the shop's box, then its rules and its tricks. A pack you got opens in the LIBRARY as before.

// Your own packs (made on this PC, so yours to share), the last edited first.
std::vector<const studio::View::PackView*> own_packs(const studio::View& v) {
    std::vector<const studio::View::PackView*> out;
    for (const auto& pack : v.packs)
        if (!pack.installed) out.push_back(&pack);
    std::stable_sort(out.begin(), out.end(), [](const auto* a, const auto* b) { return a->modified > b->modified; });
    return out;
}
// TRICKS > FROM PACKS: every trick that came in a pack you got.
void show_downloaded_packs() {
    auto& u = ui();
    u.menu_tab = 0;
    u.trick_group = "*packs";
    u.cards_scroll = 0;
    u.trick_page.clear();
    u.piece_card.clear();
}
// A new pack, open in its editor.
void create_own_pack(const studio::View& v) {
    make_pack(v);
    auto& u = ui();
    u.menu_tab = packs_tab;
    u.pack_page = true;
    u.pack_tricks_scroll = 0;
    u.confirm_delete.clear();
}
// SHARE, UPDATE and PUBLISH all open the SHARE card (a zip, a Thunderstore package, or straight into the MARKET). A pack
// holding another maker's tricks is told which, and is not shared (reshare_pack).
void publish_own_pack(const studio::View& v, const studio::View::PackView& pack) {
    auto& u = ui();
    const bool others = std::any_of(pack.members.begin(), pack.members.end(), [&](const std::string& m) { return saved_trick(v, m) && from_a_pack(v, m); });
    if (pack.installed || others) u.reshare_pack = pack.name;
    else {
        u.share_pack = pack.name;
        u.share_description_for.clear();
        u.share_version_for.clear();
    }
}
// Where a pack stands with the MARKET, in a few words for its print and its tag.
std::string pack_standing(const studio::market::View& m, const studio::View::PackView& pack) {
    if (const auto* live = live_package(m, pack)) return "IN THE MARKET / v" + live->version;
    if (!pack.pack.published.empty()) return "PUBLISHED / v" + pack.pack.published;
    return "NOT SHARED YET";
}

// ---- the front page -------------------------------------------------------------------------------------------------
float packs_front_steps(ImDrawList* d, const Fonts& f, ImVec2 at, float w) {
    const float k = f.k, h = 112 * k;
    const ImVec2 a = at, b(at.x + w, at.y + h);
    d->AddRectFilled(ImVec2(a.x + 6 * k, a.y + 8 * k), ImVec2(b.x + 6 * k, b.y + 8 * k), IM_COL32(0, 0, 0, 80));
    d->AddRectFilled(a, b, IM_COL32(1, 92, 196, 236));
    d->AddRectFilled(a, ImVec2(a.x + 5 * k, b.y), IM_COL32_WHITE);
    halftone_patch(d, ImVec2(b.x - 90 * k, b.y - 30 * k), 90 * k, 9 * k, IM_COL32(0, 30, 90, 70));
    const std::array<std::array<const char*, 3>, 3> steps{{{"01", "CREATE A PACK", "Give it a name and a cover. It is yours: made here, on this PC."},
                                                           {"02", "ADD YOUR TRICKS", "Pick from the tricks you saved, and set the pop they play on."},
                                                           {"03", "PUBLISH IT", "Into the MARKET for everyone, or as a zip to send. Update it any time."}}};
    const float pitch = (w - 60 * k) / 3, para_w = pitch - 40 * k;
    float sx = a.x + 36 * k;
    for (const auto& step : steps) {
        ImFont* num = anton(f, 46 * k);
        const auto [nt, nb] = caps_of(num, 46 * k);
        float sy = a.y + 24 * k;
        d->AddText(sharp(num, 46 * k), 46 * k, ImVec2(sx, sy - nt), shop_ink, step[0]);
        const float nw = text_size(num, 46 * k, step[0]).x + 18 * k;
        ImFont* word = anton(f, 24 * k);
        const auto [wt, wb] = caps_of(word, 24 * k);
        text_spaced(d, word, 24 * k, ImVec2(sx + nw, sy + (nb - nt) - (wb - wt) - wt), IM_COL32_WHITE, step[1], 1 * k);
        sy += (nb - nt) + 16 * k;
        lines_text(d, body(f, 14 * k), 14 * k, ImVec2(sx, sy), para_w, IM_COL32(255, 255, 255, 235), step[2], 3);
        sx += pitch;
    }
    return h;
}
// One of your packs as a print: its cover, its name under it, where it stands with the MARKET. Pressed, its editor.
void own_pack_print(ImDrawList* d, const Fonts& f, const studio::View& v, const studio::market::View& m, const studio::View::PackView& pack, ImVec2 at,
                    float side, int i) {
    auto& u = ui();
    const float k = f.k;
    const ImVec2 a = at, b(at.x + side + 28 * k, at.y + side + 64 * k + 50 * k);
    ImGui::PushID(pack.name.c_str());
    bool lit = false;
    if (hit("##ownpack", a, b, &lit)) {
        studio::open_pack(pack.name);
        u.pack_page = true;
        u.pack_tricks_scroll = 0;
        u.pack_name_for.clear();
        u.confirm_delete.clear();
    }
    ImGui::PopID();
    const float deg = (i % 3 == 0 ? -1.6f : i % 3 == 1 ? 1.2f : -0.6f) + (lit ? 0.8f : 0.0f);
    print_card(d, f, ImVec2(a.x + 6 * k, a.y + (lit ? 0.0f : 6 * k)), ImVec2(side, side), deg, studio::utf8_prefix(upper(pack.name), 26), lit,
               [&](ImVec2 pa, ImVec2 pb) { pack_cover(d, f, pack, pa, pb); }, 20);
    const auto count = saved_count(v, pack);
    const float ty = a.y + side + 64 * k + 10 * k;
    const auto standing = pack_standing(m, pack);
    const bool out = standing != "NOT SHARED YET";
    text_spaced(d, mont(f, 12 * k), 12 * k, ImVec2(a.x + 8 * k, ty), out ? blue_light : soft, standing, 1.4f * k);
    const auto tricks = std::to_string(count) + (count == 1 ? " TRICK" : " TRICKS");
    text_spaced(d, mont(f, 12 * k), 12 * k, ImVec2(a.x + 8 * k, ty + 20 * k), IM_COL32(243, 243, 239, 200), tricks, 1.4f * k);
    if (lit && nav().keys) chosen_outline(d, a, b, k);
}
// The first print on the wall: an empty one to make a new pack with.
void new_pack_print(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 at, float side) {
    const float k = f.k;
    const ImVec2 a(at.x + 6 * k, at.y + 6 * k), b(a.x + side + 28 * k, a.y + side + 64 * k);
    bool lit = false;
    if (hit("##newpackprint", ImVec2(at.x, at.y), ImVec2(b.x, b.y + 50 * k), &lit)) create_own_pack(v);
    d->AddRectFilled(a, b, lit ? IM_COL32(1, 131, 255, 60) : IM_COL32(255, 255, 255, 14));
    // A dashed edge, as a space left for a print.
    const float dash = 14 * k, gap = 9 * k, th = 2.4f * k;
    const ImU32 c = lit ? IM_COL32_WHITE : IM_COL32(243, 243, 239, 150);
    for (float x = a.x; x < b.x; x += dash + gap) {
        d->AddLine(ImVec2(x, a.y), ImVec2(std::min(x + dash, b.x), a.y), c, th);
        d->AddLine(ImVec2(x, b.y), ImVec2(std::min(x + dash, b.x), b.y), c, th);
    }
    for (float y = a.y; y < b.y; y += dash + gap) {
        d->AddLine(ImVec2(a.x, y), ImVec2(a.x, std::min(y + dash, b.y)), c, th);
        d->AddLine(ImVec2(b.x, y), ImVec2(b.x, std::min(y + dash, b.y)), c, th);
    }
    const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float arm = 30 * k, plus_y = mid.y - 20 * k;
    d->AddLine(ImVec2(mid.x - arm, plus_y), ImVec2(mid.x + arm, plus_y), c, 8 * k);
    d->AddLine(ImVec2(mid.x, plus_y - arm), ImVec2(mid.x, plus_y + arm), c, 8 * k);
    ImFont* word = anton(f, 26 * k);
    const float ww = spaced_width(word, 26 * k, "CREATE A PACK", 1.2f * k);
    text_spaced(d, word, 26 * k, ImVec2(mid.x - ww * 0.5f, mid.y + 34 * k), c, "CREATE A PACK", 1.2f * k);
    if (lit && nav().keys) chosen_outline(d, ImVec2(at.x, at.y), ImVec2(b.x, b.y + 50 * k), k);
}
void packs_front(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui();
    const float k = f.k;
    const auto& m = studio::market::view();
    const auto mine = own_packs(v);
    // The head: PACKS on blue paper, a line in marker, and the two ways on from here.
    const ImVec2 head = look_heading(d, f, *look_now, ImVec2(a.x + 4 * k, a.y - 4 * k), "PACKS", 44 * k, 73, PaperKind::blue, -1.6f);
    marker_words(d, f, ImVec2(a.x + head.x + 26 * k, a.y + 10 * k), "make it. pack it. share it.", 26 * k, -3, IM_COL32(243, 243, 239, 230));
    const float bh = 48 * k;
    const float cw = shop_button_width(f, "+ CREATE A PACK", bh), dw = shop_button_width(f, "DOWNLOADED PACKS", bh, 3);
    if (shop_button(d, f, "##createpack", ImVec2(b.x - cw, a.y), ImVec2(cw, bh), "+ CREATE A PACK", ShopLook::accent)) create_own_pack(v);
    if (shop_button(d, f, "##downloadedpacks", ImVec2(b.x - cw - 14 * k - dw, a.y), ImVec2(dw, bh), "DOWNLOADED PACKS", ShopLook::line, true, 3))
        show_downloaded_packs();
    const ImVec2 la(a.x, a.y + 76 * k), lb(b.x, b.y);
    begin_region(311, la, lb, u.own_packs_scroll, k);
    float y = la.y + 6 * k - u.own_packs_scroll;
    const float w = lb.x - la.x - 16 * k;
    y += packs_front_steps(d, f, ImVec2(la.x, y), w) + 40 * k;
    side_heading(d, f, ImVec2(la.x, y), w, mine.empty() ? "YOUR PACKS" : "YOUR PACKS  /  " + std::to_string(mine.size()));
    y += 46 * k;
    // The prints, in rows: as many across as fit, none smaller than 220 at 1080 lines.
    const float least = 220 * k, gap = 34 * k;
    const int across = std::max(1, static_cast<int>((w + gap) / (least + 28 * k + gap)));
    const float side = std::min(300 * k, (w + gap) / static_cast<float>(across) - gap - 28 * k);
    const float cell_w = side + 28 * k + gap, cell_h = side + 64 * k + 50 * k + 34 * k;
    int i = 0;
    const auto place = [&](int n) { return ImVec2(la.x + static_cast<float>(n % across) * cell_w, y + static_cast<float>(n / across) * cell_h); };
    new_pack_print(d, f, v, place(i++), side);
    for (const auto* pack : mine) {
        own_pack_print(d, f, v, m, *pack, place(i), side, i);
        ++i;
    }
    // None of your own yet: a note beside the empty print, saying what a pack is for.
    if (mine.empty() && across > 1) {
        bool pressed = false;
        (void)empty_note(d, f, ImVec2(la.x + cell_w, y + 6 * k), w - cell_w, "no packs of your own yet.",
                         "A pack is how your tricks get to other players: make one, put your saved tricks in it, give it a cover, and "
                         "publish it to the MARKET. The packs you got from others are under DOWNLOADED PACKS.",
                         "CREATE A PACK", &pressed, 31);
        if (pressed) create_own_pack(v);
    }
    y += static_cast<float>((i + across - 1) / across) * cell_h;
    end_region(d, la, lb, y + u.own_packs_scroll - la.y + 20 * k, u.own_packs_scroll, k);
}

// ---- the editor -------------------------------------------------------------------------------------------------------
void own_pack_editor(ImDrawList* d, const Fonts& f, const studio::View& v, const studio::View::PackView& pack, ImVec2 a, ImVec2 b) {
    auto& u = ui();
    const float k = f.k;
    const auto& m = studio::market::view();
    const auto* live = live_package(m, pack);
    const bool published = pack_is_published(m, pack);
    // BACK, and where the page is.
    {
        const float bw = shop_button_width(f, "< PACKS", 40 * k) + 10 * k;
        if (shop_button(d, f, "##packsback", a, ImVec2(bw, 40 * k), "< PACKS", ShopLook::line)) {
            u.pack_page = false;
            return;
        }
        ImFont* cf = mont(f, 13 * k);
        float cx = a.x + bw + 24 * k;
        const float cy = a.y + 12 * k;
        d->AddText(sharp(cf, 13 * k), 13 * k, ImVec2(cx, cy), soft, "PACKS");
        cx += text_size(cf, 13 * k, "PACKS").x + 12 * k;
        d->AddText(sharp(cf, 13 * k), 13 * k, ImVec2(cx, cy), IM_COL32(255, 255, 255, 90), "/");
        cx += 18 * k;
        d->AddText(sharp(cf, 13 * k), 13 * k, ImVec2(cx, cy), IM_COL32_WHITE, elide(cf, 13 * k, upper(pack.name), b.x - cx).c_str());
    }
    const ImVec2 ra(a.x, a.y + 58 * k), rb(b.x, b.y);
    begin_region(312, ra, rb, u.pack_tricks_scroll, k);
    const float y0 = ra.y + 16 * k - u.pack_tricks_scroll;
    // ---- the left: its cover on a print, the cover's buttons, who may edit it, DELETE ----
    const float side = std::min(420 * k, (b.x - a.x) * 0.28f), left_w = side + 28 * k;
    float ly = y0;
    const auto made_by = pack.made_by.empty() ? std::string("YOU") : upper(pack.made_by);
    print_card(d, f, ImVec2(a.x + 10 * k, ly + 10 * k), ImVec2(side, side), -2.0f,
               studio::utf8_prefix(made_by, 20) + " / " + (live ? "v" + live->version : pack.pack.published.empty() ? std::string("not shared") : "v" + pack.pack.published),
               true, [&](ImVec2 pa, ImVec2 pb) { pack_cover(d, f, pack, pa, pb); });
    ly += 10 * k + side + 46 * k + 34 * k;
    {
        const bool making = detail::studio_pack_picture_making();
        const float bh = 46 * k, half = (left_w - 12 * k) / 2;
        const auto cover = making ? std::string("MAKING IT...") : pack.thumbnail.empty() ? std::string("MAKE A COVER") : std::string("NEW COVER");
        if (shop_button(d, f, "##packcover", ImVec2(a.x, ly), ImVec2(pack.thumbnail.empty() ? left_w : half, bh), cover, ShopLook::paper, !making))
            open_cover_designer(v, pack.name);
        if (!pack.thumbnail.empty() && shop_button(d, f, "##packnocover", ImVec2(a.x + half + 12 * k, ly), ImVec2(half, bh), "NO PICTURE", ShopLook::line)) {
            std::string said;
            studio::set_pack_thumbnail(pack.name, "", said);
            say(said);
        }
        ly += bh + 30 * k;
    }
    {
        ly += side_heading(d, f, ImVec2(a.x, ly), left_w, "WHO CAN EDIT IT");
        text(d, f.bold, 15 * k, ImVec2(a.x, ly + 8 * k), white, pack.pack.editable ? "OTHERS CAN EDIT IT" : "LOCKED: PLAY AND PREVIEW");
        if (toggle(d, f, "##packeditable", ImVec2(a.x + left_w - 64 * k, ly), pack.pack.editable)) {
            std::string said;
            studio::set_pack_editable(pack.name, !pack.pack.editable, said);
            say(said);
        }
        ly += 40 * k;
        ly += paragraph(d, body(f, 13 * k), 13 * k, ImVec2(a.x, ly), left_w, soft,
                        pack.pack.editable ? "Whoever gets it can open its tricks, change them and copy them."
                                           : "Whoever gets it can play and preview its tricks, but not open, copy or share them again.") +
              30 * k;
    }
    {
        const std::string confirm = "pack:" + pack.name;
        const bool asking = u.confirm_delete == confirm;
        if (shop_button(d, f, "##packdelete", ImVec2(a.x, ly), ImVec2(left_w, 44 * k), asking ? "PRESS AGAIN TO DELETE" : "DELETE THIS PACK",
                        ShopLook::danger)) {
            if (!asking) u.confirm_delete = confirm;
            else {
                std::string said;
                const std::string gone = pack.name;
                if (studio::delete_pack(gone, said)) studio::market::forget_installed(gone);
                say(said);
                u.confirm_delete.clear();
                u.pack_page = false;
            }
        }
        ly += 54 * k;
        ly += paragraph(d, body(f, 13 * k), 13 * k, ImVec2(a.x, ly), left_w, soft,
                        published ? "Deleting it here keeps its tricks (in MY TRICKS) and leaves what is already in the MARKET as it is."
                                  : "Deleting it keeps its tricks: they stay in MY TRICKS.") +
              10 * k;
    }
    // ---- the right: its tags, its name, its numbers, PUBLISH, its rules and its tricks ----
    const float x = a.x + left_w + 64 * k, w = b.x - 16 * k - x;
    float ry = y0;
    {
        float sx = x;
        sx += paper_label(d, f, marker(f, 18 * k), 18 * k, ImVec2(sx, ry), published ? "in the market" : "not shared yet",
                          published ? PaperKind::blue : PaperKind::white, 640, -2, published ? IM_COL32_WHITE : shop_ink, ImVec2(14 * k, 7 * k)).x + 18 * k;
        paper_label(d, f, marker(f, 18 * k), 18 * k, ImVec2(sx, ry), pack.pack.editable ? "open to edit" : "locked", PaperKind::grip, 641, 1, IM_COL32_WHITE,
                    ImVec2(14 * k, 7 * k));
        ry += 50 * k;
    }
    // Its name, as big as fits in two lines; renamed in the field under it.
    {
        float size = 96 * k;
        const auto title = upper(pack.name);
        const auto too_big = [&](float at) {
            const auto set = title_lines(anton(f, at), at, title, w);
            if (set.size() > 2) return true;
            for (const auto& line : set)
                if (text_size(anton(f, at), at, line.c_str()).x > w) return true;
            return false;
        };
        while (size > 44 * k && too_big(size)) size -= 6 * k;
        ImFont* big = anton(f, size);
        const auto [bt, bf] = caps_of(big, size);
        auto lines = title_lines(big, size, title, w);
        if (lines.size() > 2) lines.resize(2);
        for (const auto& l : lines) {
            d->AddText(sharp(big, size), size, ImVec2(x, ry - bt), shop_white, elide(big, size, l, w).c_str());
            ry += (bf - bt) + 14 * k;
        }
        ry += 8 * k;
        if (u.pack_name_for != pack.name && !ImGui::IsAnyItemActive()) {
            std::snprintf(u.pack_name.data(), u.pack_name.size(), "%s", pack.name.c_str());
            u.pack_name_for = pack.name;
        }
        const float fw = std::min(520 * k, w - 140 * k);
        bool edited = false;
        const bool entered = field(f, "##ownpackname", u.pack_name, ImVec2(x, ry), fw, "Pack name", &edited);
        const bool changed = std::string(u.pack_name.data()) != pack.name;
        if ((shop_button(d, f, "##ownpackrename", ImVec2(x + fw + 12 * k, ry - 2 * k), ImVec2(120 * k, 38 * k), "RENAME", ShopLook::line, changed) || entered ||
             edited) &&
            changed) {
            std::string said;
            if (studio::rename_pack(pack.name, u.pack_name.data(), said)) u.pack_name_for.clear();
            say(said);
        }
        ry += 54 * k;
    }
    // by its maker, and its numbers.
    const auto count = saved_count(v, pack);
    {
        d->AddText(sharp(mont(f, 20 * k), 20 * k), 20 * k, ImVec2(x, ry), IM_COL32_WHITE, "by");
        const float byw = text_size(mont(f, 20 * k), 20 * k, "by ").x;
        d->AddText(sharp(mont(f, 20 * k), 20 * k), 20 * k, ImVec2(x + byw + 4 * k, ry), blue_light, elide(mont(f, 20 * k), 20 * k, made_by, w - byw).c_str());
        if (const auto when = edited_words(pack.modified); !when.empty()) {
            const auto edited_at = "LAST EDITED " + upper(when);
            text_spaced(d, mont(f, 12 * k), 12 * k, ImVec2(x + byw + 4 * k + text_size(mont(f, 20 * k), 20 * k, made_by.c_str()).x + 24 * k, ry + 5 * k), soft,
                        edited_at, 1.4f * k);
        }
        ry += 46 * k;
        ImFont* num = anton(f, 40 * k);
        const auto [nt, nb] = caps_of(num, 40 * k);
        std::vector<std::pair<std::string, const char*>> stats{{std::to_string(count), count == 1 ? "TRICK" : "TRICKS"}};
        if (live) {
            stats.emplace_back("v" + live->version, "IN THE MARKET");
            stats.emplace_back(short_count(live->downloads), "DOWNLOADS");
            stats.emplace_back(short_count(static_cast<std::uint64_t>(std::max(0, live->rating))), "LIKES");
        } else if (!pack.pack.published.empty()) stats.emplace_back("v" + pack.pack.published, "PUBLISHED");
        float sx = x;
        for (const auto& [value, word] : stats) {
            const float vw = text_size(num, 40 * k, value.c_str()).x, lw = spaced_width(mont(f, 12 * k), 12 * k, word, 1.6f * k);
            if (sx + std::max(vw, lw) > x + w) break;
            d->AddText(sharp(num, 40 * k), 40 * k, ImVec2(sx, ry - nt), IM_COL32_WHITE, value.c_str());
            text_spaced(d, mont(f, 12 * k), 12 * k, ImVec2(sx, ry + (nb - nt) + 8 * k), soft, word, 1.6f * k);
            sx += std::max(vw, lw) + 40 * k;
        }
        ry += (nb - nt) + 46 * k;
    }
    // The shop's box: PUBLISH (or UPDATE), + ADD TRICKS, and its page in the MARKET once it is there.
    {
        const float pad = 24 * k, bh = 56 * k;
        const ImVec2 ba(x, ry);
        const float inner = w - pad * 2;
        const auto publish = published ? std::string("PUBLISH AN UPDATE") : std::string("PUBLISH TO THE MARKET");
        struct Button {
            const char* id;
            std::string label;
            ShopLook look;
            int icon;
            float w;
        };
        std::vector<Button> buttons{{"##ownpackpublish", publish, ShopLook::accent, 3, std::max(300 * k, shop_button_width(f, publish, bh, 3))},
                                    {"##ownpackadd", "+ ADD TRICKS", ShopLook::paper, 0, shop_button_width(f, "+ ADD TRICKS", bh)}};
        if (live) buttons.push_back({"##ownpackmarket", "SEE IT IN THE MARKET", ShopLook::line, 3, shop_button_width(f, "SEE IT IN THE MARKET", bh, 3)});
        std::vector<ImVec2> spots;
        float bx = 0, rows_h = bh;
        for (auto& button : buttons) {
            button.w = std::min(button.w, inner);
            if (bx > 0 && bx + button.w > inner) bx = 0, rows_h += bh + 12 * k;
            spots.emplace_back(bx, rows_h - bh);
            bx += button.w + 12 * k;
        }
        ImFont* nf = body(f, 14 * k);
        const auto note = !count ? std::string("Empty for now: + ADD TRICKS puts your saved tricks in it. Then PUBLISH puts it in everyone's shop.")
                          : live ? "In the MARKET as v" + live->version + ". Changed it since? PUBLISH AN UPDATE sends the new version; everyone who has it gets it."
                          : published ? std::string("Published before. PUBLISH AN UPDATE sends what it is now.")
                                      : std::string("Only on this PC so far. PUBLISH puts it in the MARKET for everyone in a minute, or saves it as a zip to send.");
        const float note_h = sharp(nf, 14 * k)->CalcTextSizeA(14 * k, FLT_MAX, inner, note.c_str()).y;
        const float box_h = pad + rows_h + 18 * k + note_h + pad;
        const ImVec2 bb(x + w, ry + box_h);
        d->AddRectFilled(ImVec2(ba.x + 6 * k, ba.y + 8 * k), ImVec2(bb.x + 6 * k, bb.y + 8 * k), IM_COL32(0, 0, 0, 90));
        d->AddRectFilled(ba, bb, IM_COL32(14, 15, 18, 240));
        d->AddRectFilled(ba, ImVec2(bb.x, ba.y + 4 * k), look_now->accent);
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const auto& button = buttons[i];
            const ImVec2 at(ba.x + pad + spots[i].x, ba.y + pad + spots[i].y);
            if (!shop_button(d, f, button.id, at, ImVec2(button.w, bh), button.label, button.look, true, button.icon)) continue;
            if (i == 0) publish_own_pack(v, pack);
            else if (i == 1) {
                u.pack_adding = true;
                u.adding_scroll = 0;
            } else if (live) {
                u.menu_tab = 2;
                u.market_open = live->full_name;
                u.market_detail_scroll = 0;
            }
        }
        paragraph(d, nf, 14 * k, ImVec2(ba.x + pad, ba.y + pad + rows_h + 18 * k), inner, IM_COL32(220, 222, 228, 255), note);
        ry += box_h + 44 * k;
    }
    // EVERY TRICK PLAYS ON: the pack's pop rule.
    {
        ry += side_heading(d, f, ImVec2(x, ry), w, "EVERY TRICK PLAYS ON");
        static constexpr std::array<const char*, 5> rules{"", "any", "low", "mid", "high"};
        int chosen = 0;
        for (std::size_t i = 0; i < rules.size(); ++i)
            if (pack.pack.pop_rule == rules[i]) chosen = static_cast<int>(i);
        const float sw = std::min(620 * k, w);
        const int picked = segmented(d, f, "##ownpoprule", ImVec2(x, ry), sw, 40 * k, {"OWN POP", "ANY", "LOW", "MID", "HIGH"}, chosen);
        if (picked >= 0 && picked != chosen) {
            std::string said;
            studio::set_pack_pop_rule(pack.name, rules[static_cast<std::size_t>(picked)], said);
            say(said);
        }
        ry += 52 * k;
        const char* about = chosen == 0   ? "Each trick plays on the pop it was saved for (its tag below)."
                            : chosen == 1 ? "Every trick in the pack plays on any pop: low, mid or high."
                                          : "Every trick in the pack plays on that pop only; the game's own plays on the others.";
        ry += paragraph(d, body(f, 14 * k), 14 * k, ImVec2(x, ry), w, soft, about) + 14 * k;
        ry += paragraph(d, body(f, 13 * k), 13 * k, ImVec2(x, ry), w, faint,
                        "LOW is a quick, short flick (a little hop). MID is an ordinary flick, what most tricks are made on. HIGH is a long, hard "
                        "flick that floats. ANY plays on every pop, each the way COPY TO LOW, MID AND HIGH makes it.") +
              34 * k;
    }
    ry += pack_duplicates(d, f, v, pack, ImVec2(x, ry), w);
    // Its tricks.
    {
        ry += side_heading(d, f, ImVec2(x, ry), w, count ? "TRICKS IN THIS PACK  /  " + std::to_string(count) : std::string("TRICKS IN THIS PACK"));
        ry += paragraph(d, body(f, 13 * k), 13 * k, ImVec2(x, ry), w, faint,
                        v.front_foot ? "To play one: do its trick with its pop. Trick matching is on Front foot, so it plays in any stance, mirrored when the "
                                       "other foot leads."
                                     : "To play one: do its trick with its pop, in the stance on its tags (Trick matching is on Exact stance).") +
              16 * k;
        if (!count) {
            if (shop_button(d, f, "##ownpackaddempty", ImVec2(x, ry), ImVec2(shop_button_width(f, "+ ADD TRICKS", 48 * k), 48 * k), "+ ADD TRICKS", ShopLook::paper)) {
                u.pack_adding = true;
                u.adding_scroll = 0;
            }
            ry += 70 * k;
        } else ry += pack_tricks(d, f, v, pack, ImVec2(x, ry), w) + 20 * k;
    }
    end_region(d, ra, rb, std::max(ly, ry) + u.pack_tricks_scroll - ra.y + 30 * k, u.pack_tricks_scroll, k);
}
void own_packs_page(ImDrawList* d, const Fonts& f, const studio::View& v, ImVec2 a, ImVec2 b) {
    auto& u = ui();
    want_pack_pictures(v);
    if (u.pack_page)
        if (const auto* pack = pack_named(v, v.open_pack)) {
            // A pack you got is not edited here: its page is the LIBRARY's.
            if (pack->installed) {
                u.menu_tab = 1;
                return;
            }
            own_pack_editor(d, f, v, *pack, a, b);
            return;
        }
    u.pack_page = false;
    packs_front(d, f, v, a, b);
}
