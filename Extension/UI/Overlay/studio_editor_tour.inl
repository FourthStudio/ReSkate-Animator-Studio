// Included in the Studio's drawing namespace, after the LIBRARY's code: it uses its fonts and buttons.
// ---- the editor's first-open tour ----------------------------------------------------------------------
// The first time the editor is up, a short tour of it: seven steps, each lighting up the real part it is about (the rest of
// the screen dimmed) with a card beside it saying what it is, what it does, and a little to try on the way - choose the left
// hand, turn it, KEY it, go on a few frames, KEY it again, scrub. SKIP TOUR or Esc ends it, BACK and NEXT go through it.
// Gone through or skipped, it is not shown again (studio-settings.json "editor_tour_read") until SETTINGS > STUDIO >
// Editor tour, or EDITOR TOUR on the HOW IT WORKS guide, asks for it. The parts tell it where they were drawn this frame
// (tour_spot), so it points at them wherever they are on any screen.
void begin_card(ImVec2 display, bool dim); // the cards' window, below
void end_card();
struct EditorTour {
    int step{};
    std::array<std::pair<ImVec2, ImVec2>, static_cast<std::size_t>(TourSpot::count)> at{};
    std::array<int, static_cast<std::size_t>(TourSpot::count)> drawn{}; // the frame each was drawn on
    std::optional<bool> seen_last; // the settings' flag as last seen: asked for again, the menu makes way
    ImVec2 from_a{}, from_b{}, shown_a{}, shown_b{}; // the light moving from the last part to this one
    int shown_step{-1};
    double moved_at{};
};
EditorTour& tour() {
    static auto* t = new EditorTour;
    return *t;
}
void tour_spot(TourSpot spot, ImVec2 a, ImVec2 b) {
    auto& t = tour();
    const auto i = static_cast<std::size_t>(spot);
    if (i >= t.at.size()) return;
    t.at[i] = {a, b};
    t.drawn[i] = ImGui::GetFrameCount();
}
// Whether the tour is up: the editor is on screen with a trick in it (not the menu, a preview, the clean view, the loading
// screen) and the first-start guide has been read.
bool tour_wanted(const studio::View& v) {
    const auto& u = ui();
    return !v.editor_tour_seen && v.guide_seen && v.has_clip && !v.previewing && !u.menu && !u.hide_ui && v.pose_seen &&
           studio::loading().alpha <= 0.0f;
}
void tour_close() {
    tour().step = 0;
    tour().shown_step = -1;
    studio::set_editor_tour_seen(true);
}
// Asked for again (SETTINGS, HOW IT WORKS) while the menu is up over a trick: the menu goes, so it shows at once.
void tour_frame(const studio::View& v) {
    auto& t = tour();
    if (t.seen_last && *t.seen_last && !v.editor_tour_seen) {
        t.step = 0;
        t.shown_step = -1;
        if (v.has_clip) ui().menu = false;
    }
    t.seen_last = v.editor_tour_seen;
}
void editor_tour(const Fonts& f, const studio::View& v, ImVec2 display) {
    auto& t = tour();
    if (!tour_wanted(v) || other_cards_open(v)) return;
    const float k = f.k;
    const std::string key = key_of("key"), camera = key_of("camera"), menu = key_of("menu");
    struct Step {
        TourSpot spot;
        const char* title;
        std::string words, try_it;
    };
    const std::array<Step, 7> steps{{
        {TourSpot::skater, "THIS IS YOUR TRICK",
         "He does the trick at the frame the playhead is on. The dots on him are his joints: click one to choose it. Hold the right "
         "mouse button and drag to look round him, the middle button to slide the view, the wheel to zoom.",
         "Click the dot on his left hand."},
        {TourSpot::tools, "POSE HIM",
         "The chosen joint wears a gizmo: drag one of its rings to turn the joint, an arrow to move it. A hand or a foot pulls its "
         "arm or leg along. These tools set how a drag works: TURN or MOVE, one axis only, SOLO, SNAP, LIMITS.",
         "Drag a ring on the left hand: the arm turns."},
        {TourSpot::joint, "THE JOINT CARD",
         "The chosen joint's turn and place as numbers, to drag or type. KEY (" + key + ") keeps the pose at the playhead: that is a "
         "keyframe. DELETE KEY takes it off again, and < > jump between the joint's keys.",
         "Press KEY: a diamond shows on the joint's row in the timeline."},
        {TourSpot::timeline, "THE TIMELINE",
         "A row for each joint, a diamond for each key: between two keys he moves from one pose to the next. Drag a diamond along "
         "its row to move it in time, or drag a box with the right mouse button to choose many. The START ANIM. and END ANIM. "
         "flags mark where your animation starts and ends.",
         "Go ten frames on, turn the arm another way and press KEY again."},
        {TourSpot::scrub, "SCRUB AND PLAY",
         "Drag along the frame numbers to scrub: he moves through your keys. PLAY, LOOP and the speed are at the left, and < > step "
         "one frame. What you key plays in game the way it plays here.",
         "Scrub back and forth: the arm swings from one key to the next."},
        {TourSpot::camera, "THE CAMERA",
         "Where you look from: FREE is your own camera; FRONT, SIDE, BACK, TOP, BOARD and CINEMATIC are set shots, and FILMER looks "
         "through a fisheye. " + camera + " or the arrows change it.",
         {}},
        {TourSpot::save, "SAVE IT, SKATE IT",
         "SAVE keeps it in MY TRICKS. TEST IN GAME saves it and closes the studio so you can skate it. MENU (" + menu + ") goes back "
         "to your tricks, the LIBRARY (the playlists that decide what plays in game) and SETTINGS.",
         "SETTINGS > STUDIO > Editor tour shows this tour again."},
    }};
    t.step = std::clamp(t.step, 0, static_cast<int>(steps.size()) - 1);
    const auto& step = steps[static_cast<std::size_t>(t.step)];
    // The part it is about, with a little room round it, kept on the screen. Not drawn this frame: no light, the card in
    // the middle.
    const auto spot = static_cast<std::size_t>(step.spot);
    const bool have = t.drawn[spot] == ImGui::GetFrameCount();
    ImVec2 sa = t.at[spot].first, sb = t.at[spot].second;
    const float pad = 8 * k;
    sa = ImVec2(std::max(4 * k, sa.x - pad), std::max(4 * k, sa.y - pad));
    sb = ImVec2(std::min(display.x - 4 * k, sb.x + pad), std::min(display.y - 4 * k, sb.y + pad));
    // The light slides from the last part to this one; the card goes straight to where it is going (sliding with the light
    // it could change sides half way).
    const ImVec2 to_a = sa, to_b = sb;
    const double now = ImGui::GetTime();
    const bool new_step = t.shown_step != t.step;
    if (new_step) {
        t.from_a = t.shown_step < 0 ? sa : t.shown_a;
        t.from_b = t.shown_step < 0 ? sb : t.shown_b;
        t.shown_step = t.step;
        t.moved_at = now;
    }
    const float e = std::clamp(static_cast<float>(now - t.moved_at) / 0.28f, 0.0f, 1.0f), ease = 1 - (1 - e) * (1 - e) * (1 - e);
    const auto lerp = [&](ImVec2 p, ImVec2 q) { return ImVec2(p.x + (q.x - p.x) * ease, p.y + (q.y - p.y) * ease); };
    if (have) {
        sa = lerp(t.from_a, sa);
        sb = lerp(t.from_b, sb);
        t.shown_a = sa;
        t.shown_b = sb;
    }
    begin_card(display, false);
    auto* d = ImGui::GetWindowDrawList();
    // A pad's A (Enter) goes on to the next step: NEXT is the one chosen as each step comes up.
    if (new_step) nav().focus = ImGui::GetID("##tournext");
    const ImU32 dim = IM_COL32(0, 0, 0, 178);
    if (have) {
        d->AddRectFilled(ImVec2(0, 0), ImVec2(display.x, sa.y), dim);
        d->AddRectFilled(ImVec2(0, sb.y), display, dim);
        d->AddRectFilled(ImVec2(0, sa.y), ImVec2(sa.x, sb.y), dim);
        d->AddRectFilled(ImVec2(sb.x, sa.y), ImVec2(display.x, sb.y), dim);
        d->AddRect(ImVec2(sa.x - 3 * k, sa.y - 3 * k), ImVec2(sb.x + 3 * k, sb.y + 3 * k), IM_COL32(1, 131, 255, 70), 8 * k, 0, 6 * k);
        d->AddRect(sa, sb, blue_light, 6 * k, 0, 2.5f * k);
    } else d->AddRectFilled(ImVec2(0, 0), display, dim);
    // The card: its size from its words.
    const float cw = std::min(520 * k, display.x - 40 * k), x_in = 26 * k, tw = cw - 2 * x_in;
    ImFont* words_font = body(f, 16 * k);
    ImFont* try_font = body(f, 15 * k);
    const float words_h = sharp(words_font, 16 * k)->CalcTextSizeA(16 * k, FLT_MAX, tw, step.words.c_str()).y;
    const float try_h = step.try_it.empty() ? 0 : sharp(try_font, 15 * k)->CalcTextSizeA(15 * k, FLT_MAX, tw - 30 * k, step.try_it.c_str()).y + 46 * k;
    const float ch = 24 * k + 20 * k + 46 * k + words_h + 18 * k + (try_h > 0 ? try_h + 18 * k : 0) + 44 * k + 24 * k;
    // Beside the part where there is room: right of it, left, below, above; else in the middle.
    const float gap = 26 * k, edge = 20 * k;
    ImVec2 ca((display.x - cw) * 0.5f, (display.y - ch) * 0.5f);
    int side = -1; // where the part is from the card: 0 left of it, 1 right, 2 above, 3 below
    if (have) {
        const float mid_x = (to_a.x + to_b.x) * 0.5f, mid_y = (to_a.y + to_b.y) * 0.5f;
        const auto clamp_x = [&](float x) { return std::clamp(x, edge, std::max(edge, display.x - cw - edge)); };
        const auto clamp_y = [&](float y) { return std::clamp(y, edge, std::max(edge, display.y - ch - edge)); };
        if (to_b.x + gap + cw <= display.x - edge) ca = ImVec2(to_b.x + gap, clamp_y(mid_y - ch * 0.5f)), side = 0;
        else if (to_a.x - gap - cw >= edge) ca = ImVec2(to_a.x - gap - cw, clamp_y(mid_y - ch * 0.5f)), side = 1;
        else if (to_b.y + gap + ch <= display.y - edge) ca = ImVec2(clamp_x(mid_x - cw * 0.5f), to_b.y + gap), side = 2;
        else if (to_a.y - gap - ch >= edge) ca = ImVec2(clamp_x(mid_x - cw * 0.5f), to_a.y - gap - ch), side = 3;
    }
    const ImVec2 cb(ca.x + cw, ca.y + ch);
    d->AddRectFilled(ImVec2(ca.x + 10 * k, ca.y + 12 * k), ImVec2(cb.x + 10 * k, cb.y + 12 * k), IM_COL32(0, 0, 0, 130));
    d->AddRectFilled(ca, cb, IM_COL32(14, 15, 18, 252));
    d->AddRectFilled(ca, ImVec2(cb.x, ca.y + 4 * k), blue);
    // A pointer from the card to the part.
    if (side >= 0) {
        const float s = 12 * k;
        const float py = std::clamp((to_a.y + to_b.y) * 0.5f, ca.y + 30 * k, cb.y - 30 * k), px = std::clamp((to_a.x + to_b.x) * 0.5f, ca.x + 30 * k, cb.x - 30 * k);
        if (side == 0) d->AddTriangleFilled(ImVec2(ca.x, py - s), ImVec2(ca.x - s, py), ImVec2(ca.x, py + s), IM_COL32(14, 15, 18, 252));
        else if (side == 1) d->AddTriangleFilled(ImVec2(cb.x, py - s), ImVec2(cb.x + s, py), ImVec2(cb.x, py + s), IM_COL32(14, 15, 18, 252));
        else if (side == 2) d->AddTriangleFilled(ImVec2(px - s, ca.y), ImVec2(px, ca.y - s), ImVec2(px + s, ca.y), blue);
        else d->AddTriangleFilled(ImVec2(px - s, cb.y), ImVec2(px, cb.y + s), ImVec2(px + s, cb.y), IM_COL32(14, 15, 18, 252));
    }
    const float x = ca.x + x_in;
    float y = ca.y + 24 * k;
    lib_caps(d, f, ImVec2(x, y), 11 * k, lib_blue, std::format("EDITOR TOUR   {} / {}", t.step + 1, steps.size()), 1.8f);
    y += 20 * k;
    title_text(d, f, 36 * k, ImVec2(x, y), white, step.title, tw);
    y += 46 * k;
    y += paragraph(d, words_font, 16 * k, ImVec2(x, y), tw, IM_COL32(226, 226, 230, 255), step.words) + 18 * k;
    if (!step.try_it.empty()) {
        const ImVec2 ta(x, y), tb2(x + tw, y + try_h);
        d->AddRectFilled(ta, tb2, IM_COL32(1, 131, 255, 30));
        d->AddRectFilled(ta, ImVec2(ta.x + 4 * k, tb2.y), blue);
        lib_caps(d, f, ImVec2(x + 18 * k, y + 12 * k), 11 * k, lib_blue, "TRY IT", 1.8f);
        paragraph(d, try_font, 15 * k, ImVec2(x + 18 * k, y + 32 * k), tw - 30 * k, white, step.try_it);
        y += try_h + 18 * k;
    }
    // SKIP TOUR at the left; BACK and NEXT (DONE on the last) at the right.
    const bool last = t.step + 1 == static_cast<int>(steps.size());
    const float bh = 44 * k, nw = 130 * k, bw = 110 * k, by = cb.y - 24 * k - bh;
    if (!last && action(d, f, "##tourskip", ImVec2(x, by), ImVec2(150 * k, bh), "SKIP TOUR", Look::ghost)) tour_close();
    if (t.step > 0 && action(d, f, "##tourback", ImVec2(cb.x - x_in - nw - 10 * k - bw, by), ImVec2(bw, bh), "BACK", Look::dark)) --t.step;
    if (action(d, f, "##tournext", ImVec2(cb.x - x_in - nw, by), ImVec2(nw, bh), last ? "DONE" : "NEXT", Look::blue)) {
        if (last) tour_close();
        else ++t.step;
    }
    end_card();
}
