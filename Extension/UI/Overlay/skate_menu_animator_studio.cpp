#include "skate_menu_internal.h"
#include "skate_style.h"
#include "animation_studio_images.h"
#include "Extension/AnimationStudio/studio_settings_rows.h"
#include "Extension/AnimationStudio/studio_update.h"
#include "Extension/AnimationStudio/anim_studio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

// ADDONS > ANIMATOR STUDIO: the studio's settings that change how your tricks play in game and the playlist that plays,
// changed without opening it, and a way into it - dressed as the studio is (its backdrop, grain, brush bar, Anton and the
// brush face, the kickflipping board), with a banner that plays in each time the page opens, the last tricks done and who
// played them, and the packs to switch in and out of the playlist. What only acts while editing stays in the studio. The
// rows are the studio's own (Extension/AnimationStudio/studio_settings_rows.h).
namespace dingosdk::overlay::menu {
namespace {
using anim_studio::settings_rows::Setting;
using theme::paper;
using theme::muted;

// A slider's value as its own format: the row has already written it ("0.30 s").
std::string as_format(const std::string& value) {
    std::string format;
    for (const char c : value) format += c == '%' ? std::string("%%") : std::string(1, c);
    return format;
}
void setting_row(SkateMenu& menu, const Setting& row) {
    ImGui::PushID(row.label.c_str());
    switch (row.kind) {
    case Setting::Kind::choice:
        if (row.on_off) {
            bool on = row.selected == 1;
            if (toggle_row(menu, row.label.c_str(), row.about.c_str(), on, row.enabled) && row.pick) row.pick(on ? 1 : 0);
        } else if (!row.options.empty()) {
            int picked = row.selected;
            if (option_row(menu, row.label.c_str(), row.about.c_str(), picked, row.options, row.enabled) && row.pick) row.pick(picked);
        }
        break;
    case Setting::Kind::slider: {
        field(menu, row.label.c_str(), row.about.c_str());
        float value = row.low + row.amount * (row.high - row.low);
        ImGui::BeginDisabled(!row.enabled);
        if (slider_float("##slider", &value, row.low, row.high, as_format(row.value).c_str(), ImGuiSliderFlags_AlwaysClamp) && row.slide)
            row.slide((value - row.low) / (row.high - row.low));
        ImGui::EndDisabled();
        break;
    }
    case Setting::Kind::action:
        if (button_row(menu, "##action", {{row.label.c_str(), row.enabled, false, row.about.c_str()}}) == 0 && row.act) row.act();
        break;
    case Setting::Kind::info: info(menu, row.label.c_str(), row.value); break;
    case Setting::Kind::name:
    case Setting::Kind::folder:
    case Setting::Kind::key:
    case Setting::Kind::heading: break; // the studio's own: what is used while editing, not in game
    }
    ImGui::PopID();
}

// ---- the studio's look ----------------------------------------------------------------------------------------------
constexpr ImU32 blue_light = IM_COL32(110, 190, 255, 255), shadow = IM_COL32(0, 0, 0, 170), red = IM_COL32(150, 34, 36, 245);
float ease(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return 1 - (1 - x) * (1 - x) * (1 - x);
}
// One of the studio's faces at the size it is drawn (made at the screen's scale: the one made at `size` or just above).
ImFont* face(const SkateMenu& menu, const SkateMenu::StudioFace& f, float size, ImFont* fallback) {
    for (int i = 0; i < f.count; ++i)
        if (f.fonts[static_cast<std::size_t>(i)] && f.sizes[static_cast<std::size_t>(i)] * menu.studio_scale >= size * 0.97f)
            return f.fonts[static_cast<std::size_t>(i)];
    return f.count > 0 && f.fonts[static_cast<std::size_t>(f.count - 1)] ? f.fonts[static_cast<std::size_t>(f.count - 1)] : fallback;
}
ImFont* anton(const SkateMenu& menu, float size) { return face(menu, menu.studio_anton, size, menu.heading); }
ImFont* marker(const SkateMenu& menu, float size) { return face(menu, menu.studio_marker, size, menu.bold); }
void words(ImDrawList* d, ImFont* font, float size, ImVec2 at, ImU32 ink, const std::string& text, float alpha = 1) {
    const auto fade = [&](ImU32 c) { return (c & 0x00ffffff) | (static_cast<ImU32>(((c >> 24) & 0xff) * std::clamp(alpha, 0.0f, 1.0f)) << 24); };
    d->AddText(font, size, ImVec2(at.x + px(2), at.y + px(2)), fade(shadow), text.c_str());
    d->AddText(font, size, at, fade(ink), text.c_str());
}
// `text` cut short with "..." to fit `width` drawn in `font` at `size`, whole characters only (a name with an accent is
// never cut between the bytes of one letter). The menu can be a narrow window, and names are as long as people make them.
std::string fit(ImFont* font, float size, const std::string& text, float width) {
    if (width <= 0) return {};
    if (font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x <= width) return text;
    std::string cut = text;
    while (!cut.empty()) {
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back();
        if (!cut.empty()) cut.pop_back();
        if (font->CalcTextSizeA(size, FLT_MAX, 0, (cut + "...").c_str()).x <= width) break;
    }
    return cut + "...";
}
// A chip: a little label on a colour, `alpha` of the way in. Returns its width.
float chip(ImDrawList* d, const SkateMenu& menu, ImVec2 at, const std::string& label, ImU32 fill, float alpha) {
    const float size = px(13);
    const auto t = menu.bold->CalcTextSizeA(size, FLT_MAX, 0, label.c_str());
    const ImVec2 b(at.x + t.x + px(18), at.y + px(26));
    const auto a8 = static_cast<int>(255 * std::clamp(alpha, 0.0f, 1.0f));
    d->AddRectFilled(ImVec2(at.x, at.y + (1 - alpha) * px(8)), ImVec2(b.x, b.y + (1 - alpha) * px(8)), (fill & 0x00ffffff) | (static_cast<ImU32>(a8 * ((fill >> 24) & 0xff) / 255) << 24));
    d->AddText(menu.bold, size, ImVec2(at.x + px(9), at.y + px(6) + (1 - alpha) * px(8)), IM_COL32(245, 245, 245, a8), label.c_str());
    return b.x - at.x;
}

// The page plays in each time it is opened (not drawn for half a second): seconds since.
float since_opened() {
    static double opened = -1, last = -1;
    const double now = ImGui::GetTime();
    if (last < 0 || now - last > 0.5) opened = now;
    last = now;
    return static_cast<float>(now - opened);
}

// The banner: the studio's picture (its own logo: reskate. animator studio) shown whole, settling in from dark each time
// the page opens, with the kickflipping board sliding in at its right and a blue line wiping in under it; under that, what
// plays as chips coming in one by one, and the brush face's line at the right. Nothing is drawn over the picture's logo.
void banner(SkateMenu& menu, const anim_studio::MenuSummary& now, const anim_studio::Settings& settings, float t) {
    auto* d = ImGui::GetWindowDrawList();
    const auto a = ImGui::GetCursorScreenPos();
    // Tall enough for the logo and its splashes (the built-in picture is 3072 x 1296, the logo in its middle 60%).
    const float w = ImGui::GetContentRegionAvail().x, h = std::clamp(w * 0.29f, px(210), px(320));
    const ImVec2 b(a.x + w, a.y + h);
    ImGui::Dummy(ImVec2(w, h));
    d->PushClipRect(a, b, true);
    // The picture comes in from dark and a little close, and settles.
    const float settle = ease(t / 0.7f);
    const float grow = 0.06f * (1 - settle);
    const ImVec2 middle((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const ImVec2 pa(middle.x - (w * 0.5f) * (1 + grow), middle.y - (h * 0.5f) * (1 + grow)), pb(middle.x + (w * 0.5f) * (1 + grow), middle.y + (h * 0.5f) * (1 + grow));
    if (!draw_studio_backdrop_cover(d, pa, pb, IM_COL32(255, 255, 255, 255))) d->AddRectFilled(a, b, IM_COL32(20, 21, 24, 255));
    draw_studio_picture(d, StudioPicture::grain, a, b, IM_COL32(255, 255, 255, 30));
    if (settle < 1) d->AddRectFilled(a, b, IM_COL32(0, 0, 0, static_cast<int>(255 * (1 - settle))));
    // The board kickflipping at the right, sliding in.
    const float board_in = ease((t - 0.25f) / 0.45f);
    const float side = std::min(h * 0.55f, px(150));
    const ImVec2 ka(b.x - side - px(28) + (1 - board_in) * px(80), a.y + (h - side) * 0.5f);
    draw_studio_kickflip(d, ka, ImVec2(ka.x + side, ka.y + side), ImGui::GetTime(), IM_COL32(255, 255, 255, static_cast<int>(255 * board_in)));
    d->PopClipRect();
    // The blue line under it, wiping in from the left.
    const float line = ease((t - 0.15f) / 0.45f);
    d->AddRectFilled(ImVec2(a.x, b.y), ImVec2(a.x + w * line, b.y + px(4)), skate_theme::blue);
    ImGui::Dummy(ImVec2(w, px(4)));

    // What plays, one chip after another, under the picture.
    ImGui::Dummy(ImVec2(0, px(6)));
    const auto row = ImGui::GetCursorScreenPos();
    const float row_h = px(26);
    ImGui::Dummy(ImVec2(w, row_h));
    float x = row.x;
    int i = 0;
    bool full = false;
    // Chips stop at the page's edge rather than running past it (a long playlist name in a narrow window).
    const auto next = [&](const std::string& label, ImU32 fill) {
        if (full) return;
        if (x + menu.bold->CalcTextSizeA(px(13), FLT_MAX, 0, label.c_str()).x + px(18) > row.x + w) {
            full = true;
            return;
        }
        const float alpha = ease((t - 0.45f - 0.07f * static_cast<float>(i++)) / 0.25f);
        if (alpha > 0) x += chip(d, menu, ImVec2(x, row.y), label, fill, alpha) + px(8);
    };
    const std::string playing = fit(menu.bold, px(13), now.playing, w * 0.4f);
    if (!settings.play_in_game) next("YOUR TRICKS ARE OFF IN GAME", red);
    else if (now.playing.empty()) next("NO PLAYLIST IS ON", red);
    else if (now.entries == 0) next(playing + " IS EMPTY", red);
    else next("PLAYING  " + playing, skate_theme::blue);
    if (!now.playing.empty())
        next(now.mode == anim_studio::PlaylistMode::shuffle ? "SHUFFLE" : now.mode == anim_studio::PlaylistMode::turns ? "IN TURNS" : "PRIORITY",
             IM_COL32(40, 42, 48, 235));
    next(std::format("{} TRICKS IN PLAY", now.in_play), IM_COL32(40, 42, 48, 235));
    next(settings.smooth_transitions ? "SMOOTH" : "CLASSIC", IM_COL32(40, 42, 48, 235));
    // The version, and a newer one when it is out (SETTINGS > UPDATES in the studio; the UPDATES card below).
    {
        namespace up = anim_studio::update;
        const auto running = up::running_version();
        const auto st = up::state();
        const bool newer = st.step == up::Step::newer || st.step == up::Step::download_failed || st.step == up::Step::install_failed;
        const bool installed = st.step == up::Step::installed || st.step == up::Step::restarting;
        if (newer) next(st.latest + " IS OUT", skate_theme::blue);
        else if (installed) next(st.latest + " INSTALLED - RESTART", skate_theme::blue);
        else if (!running.empty()) next("V" + running, IM_COL32(40, 42, 48, 235));
    }
    // The brush face's line at the right, when it fits beside the chips.
    const float size = px(20);
    auto* hand = marker(menu, size);
    const char* motto = "your tricks, your way.";
    const auto mw = hand->CalcTextSizeA(size, FLT_MAX, 0, motto);
    if (x + px(16) + mw.x < row.x + w)
        words(d, hand, size, ImVec2(row.x + w - mw.x - px(4), row.y + (row_h - mw.y) * 0.5f), blue_light, motto, ease((t - 0.8f) / 0.35f));
    ImGui::Dummy(ImVec2(0, px(10)));
}

// A big button in the studio's look: its rough tile in blue, the label in Anton, a key hint at its right.
bool studio_button(SkateMenu& menu, const char* id, const char* label, const char* hint, bool enabled, float t) {
    const auto a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = px(58);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    const bool lit = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImGui::EndDisabled();
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 b(a.x + w, a.y + h);
    const float in = ease((t - 0.25f) / 0.3f);
    const ImU32 fill = !enabled ? IM_COL32(60, 62, 66, 255) : lit ? skate_theme::blue_hover : skate_theme::blue;
    const ImVec2 fa(a.x, a.y), fb(a.x + w * in, b.y);
    if (!draw_studio_tile(d, fa, fb, px(8), fill)) d->AddRectFilled(fa, fb, fill);
    if (lit && enabled) d->AddRect(ImVec2(a.x - px(2), a.y - px(2)), ImVec2(b.x + px(2), b.y + px(2)), paper, 0, 0, px(2));
    const float size = px(26);
    auto* font = anton(menu, size);
    const auto tw = font->CalcTextSizeA(size, FLT_MAX, 0, label);
    words(d, font, size, ImVec2(a.x + (w - tw.x) * 0.5f, a.y + (h - tw.y) * 0.5f), paper, label, in);
    if (hint && *hint) {
        const auto hw = menu.bold->CalcTextSizeA(px(12), FLT_MAX, 0, hint);
        d->AddText(menu.bold, px(12), ImVec2(b.x - hw.x - px(14), a.y + (h - hw.y) * 0.5f), IM_COL32(220, 235, 255, static_cast<int>(200 * in)), hint);
    }
    return pressed && enabled;
}

// A card in the studio's look: its rough dark tile behind it (sized from the frame before), its title on a strip of blue
// tape in Anton. Rows go in it as the menu draws them.
struct StudioCard {
    ImGuiID key{};
};
StudioCard studio_card_begin(SkateMenu& menu, const char* id, const char* title, const std::string& aside = {}) {
    ImGui::PushID(id);
    StudioCard card{ImGui::GetID("##height")};
    const float height = ImGui::GetStateStorage()->GetFloat(card.key, 0);
    const auto a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    auto* d = ImGui::GetWindowDrawList();
    if (height > 0) {
        const ImVec2 b(a.x + w, a.y + height);
        if (!draw_studio_tile(d, a, b, px(10), IM_COL32(34, 36, 41, 250))) d->AddRectFilled(a, b, IM_COL32(26, 27, 30, 245));
        draw_studio_picture(d, StudioPicture::grain, a, b, IM_COL32(255, 255, 255, 22));
    }
    // The title on tape.
    const float size = px(20);
    auto* font = anton(menu, size);
    const auto tw = font->CalcTextSizeA(size, FLT_MAX, 0, title);
    const ImVec2 ta(a.x + px(12), a.y + px(10)), tb(ta.x + tw.x + px(24), ta.y + px(30));
    d->AddRectFilled(ta, tb, skate_theme::blue);
    d->AddRectFilled(ImVec2(ta.x, tb.y - px(3)), tb, IM_COL32(0, 90, 190, 255));
    d->AddText(font, size, ImVec2(ta.x + px(12), ta.y + (px(30) - tw.y) * 0.5f), paper, title);
    if (!aside.empty())
        d->AddText(menu.bold, px(13), ImVec2(tb.x + px(12), ta.y + px(8)), muted, fit(menu.bold, px(13), aside, a.x + w - px(12) - (tb.x + px(12))).c_str());
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(w, px(46)));
    ImGui::Indent(px(12));
    return card;
}
void studio_card_end(const StudioCard& card) {
    ImGui::Dummy(ImVec2(0, px(10)));
    ImGui::Unindent(px(12));
    ImGui::EndGroup();
    ImGui::GetStateStorage()->SetFloat(card.key, ImGui::GetItemRectSize().y);
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, px(12)));
}

// The last tricks done, the newest first: yours that played it by the name you gave it (STEEZFLIP TRE), or the game's
// trick when its own played; the pop, and at the right the game's trick yours stood in for.
void last_tricks(SkateMenu& menu, const anim_studio::MenuSummary& now) {
    auto* d = ImGui::GetWindowDrawList();
    if (now.recent.empty()) {
        note("Do a trick: it shows here, with what played it - one of yours, or the game's own.");
        return;
    }
    for (std::size_t i = 0; i < now.recent.size(); ++i) {
        const auto& r = now.recent[i];
        const auto a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x - px(12), h = px(34);
        ImGui::Dummy(ImVec2(w, h));
        const ImVec2 b(a.x + w, a.y + h);
        d->AddRectFilled(a, b, i == 0 ? IM_COL32(18, 40, 70, 235) : IM_COL32(20, 21, 24, 220));
        if (i == 0) d->AddRectFilled(a, ImVec2(a.x + px(4), b.y), skate_theme::blue);
        float x = a.x + px(12);
        if (!r.pop.empty()) x += chip(d, menu, ImVec2(x, a.y + px(4)), r.pop == "low" ? "LOW" : r.pop == "mid" ? "MID" : r.pop == "high" ? "HIGH" : r.pop, IM_COL32(50, 52, 60, 255), 1) + px(10);
        const std::string& title = r.played.empty() ? r.label : r.played;
        d->AddText(menu.bold, px(15), ImVec2(x, a.y + px(8)), paper, fit(menu.bold, px(15), title, b.x - px(140) - x).c_str());
        // Who played it, at the right.
        const std::string who = r.played.empty() ? std::string("THE GAME'S OWN")
                                                 : std::string(r.modifier ? "MODIFIER, FOR " : "NORMAL, FOR ") + r.label + (r.choices > 1 ? std::format("  ({} TO CHOOSE FROM)", r.choices) : std::string{});
        const std::string ago = r.ago < 60 ? std::format("{:.0f}s", r.ago) : std::format("{:.0f}m", r.ago / 60);
        const auto aw = menu.bold->CalcTextSizeA(px(12), FLT_MAX, 0, ago.c_str());
        d->AddText(menu.bold, px(12), ImVec2(b.x - aw.x - px(12), a.y + px(10)), muted, ago.c_str());
        const auto ww = menu.bold->CalcTextSizeA(px(13), FLT_MAX, 0, who.c_str());
        // After the name, however long the name you gave it is.
        const float from = x + std::max(px(120), menu.bold->CalcTextSizeA(px(15), FLT_MAX, 0, title.c_str()).x + px(16));
        d->PushClipRect(ImVec2(from, a.y), ImVec2(b.x - aw.x - px(20), b.y), true);
        d->AddText(menu.bold, px(13), ImVec2(std::max(from, b.x - aw.x - px(24) - ww.x), a.y + px(9)), r.played.empty() ? muted : blue_light, who.c_str());
        d->PopClipRect();
        ImGui::Dummy(ImVec2(0, px(2)));
    }
}

// The installer's steps, as the studio's update card shows them: DOWNLOAD, VERIFY, INSTALL, DONE, each lit as it is
// reached, the one going on in blue (its edge pulsing), a failed one in red.
void install_steps(const SkateMenu& menu, const anim_studio::update::State& st) {
    namespace up = anim_studio::update;
    bool failed = false;
    const int step = up::install_step(st, failed);
    if (step < 0) return;
    static constexpr std::array<const char*, 4> names{"DOWNLOAD", "VERIFY", "INSTALL", "DONE"};
    auto* d = ImGui::GetWindowDrawList();
    const auto a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x - px(12), h = px(34), gap = px(8), sw = (w - 3 * gap) / 4, size = px(13);
    ImGui::Dummy(ImVec2(w, h));
    for (int i = 0; i < 4; ++i) {
        const ImVec2 sa(a.x + static_cast<float>(i) * (sw + gap), a.y), sb(sa.x + sw, a.y + h);
        const bool done = i < step || (i == 3 && step == 3), now = i == step && step < 3, bad = now && failed;
        d->AddRectFilled(sa, sb, bad ? red : done ? skate_theme::blue : now ? IM_COL32(20, 60, 110, 248) : IM_COL32(26, 27, 30, 240));
        if (now && !bad) {
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0f);
            d->AddRect(sa, sb, IM_COL32(110, 190, 255, static_cast<int>(120 + 120 * pulse)), 0, 0, px(2));
        }
        // Its number and name, or the name alone when the tile is too narrow for both.
        std::string label = std::format("{}  {}", i + 1, names[static_cast<std::size_t>(i)]);
        if (menu.bold->CalcTextSizeA(size, FLT_MAX, 0, label.c_str()).x > sw - px(12)) label = names[static_cast<std::size_t>(i)];
        const auto t = menu.bold->CalcTextSizeA(size, FLT_MAX, 0, label.c_str());
        d->PushClipRect(sa, sb, true);
        d->AddText(menu.bold, size, ImVec2(sa.x + std::max(px(4), (sw - t.x) * 0.5f), sa.y + (h - t.y) * 0.5f), done || now ? paper : muted, label.c_str());
        d->PopClipRect();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
}
// How far the download is, as a bar (a short piece running along when its size is not known).
void download_bar(const anim_studio::update::State& st) {
    auto* d = ImGui::GetWindowDrawList();
    const auto a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x - px(12), h = px(8);
    ImGui::Dummy(ImVec2(w, h));
    d->AddRectFilled(a, ImVec2(a.x + w, a.y + h), IM_COL32(255, 255, 255, 30));
    if (st.total) {
        const float frac = std::clamp(static_cast<float>(static_cast<double>(st.received) / static_cast<double>(st.total)), 0.0f, 1.0f);
        d->AddRectFilled(a, ImVec2(a.x + w * frac, a.y + h), st.retry_in ? IM_COL32(236, 214, 96, 255) : skate_theme::blue);
    } else {
        const float run = static_cast<float>(std::fmod(ImGui::GetTime() * 0.6, 1.0));
        d->AddRectFilled(ImVec2(a.x + (w - px(80)) * run, a.y), ImVec2(a.x + (w - px(80)) * run + px(80), a.y + h), skate_theme::blue);
    }
    ImGui::Dummy(ImVec2(0, px(4)));
}

// UPDATES: the version running, the newest on GitHub, and updating from here as from the studio's installer card
// (studio_update.h: DOWNLOAD, VERIFY, INSTALL, DONE; then KEEP PLAYING or RESTART NOW).
void updates_card(SkateMenu& menu, const Model& model) {
    namespace up = anim_studio::update;
    const auto st = up::state();
    const auto running = up::running_version(), base = up::reskate_base();
    const bool newer = st.step == up::Step::newer || st.step == up::Step::download_failed || st.step == up::Step::install_failed;
    const bool installed = st.step == up::Step::installed, restarting = st.step == up::Step::restarting;
    const bool going = st.step == up::Step::downloading || st.step == up::Step::verifying || st.step == up::Step::installing;
    const auto c = studio_card_begin(menu, "updates", "UPDATES",
                                     going ? "installing " + st.latest : newer ? st.latest + " is out" : installed ? st.latest + " is installed"
                                                                                                         : "the version you have, and the newest");
    info(menu, "You have", running.empty() ? std::string("not the studio's own build")
                                           : "Animator Studio " + running + (base.empty() ? std::string{} : "  /  ReSkate " + base));
    if (!up::available()) {
        note("This ReSkate.dll is not one of the Animator Studio's own releases, so it does not look for its updates.");
        studio_card_end(c);
        return;
    }
    info(menu, "Newest on GitHub",
         st.step == up::Step::checking ? std::string("checking...")
         : st.step == up::Step::failed ? "could not check (" + st.error + ")"
         : st.latest.empty()           ? std::string("not checked yet")
         : newer || going              ? st.latest + "  -  new"
                                       : st.latest + "  -  up to date");
    const auto mb = [](std::uint64_t bytes) { return std::format("{:.1f}", static_cast<double>(bytes) / (1024.0 * 1024.0)); };
    install_steps(menu, st);
    if (st.step == up::Step::downloading) {
        note((st.retry_in  ? std::format("The connection dropped at {} MB. Trying again in {} s: it goes on from there.", mb(st.received), st.retry_in)
              : st.total   ? std::format("Downloading {}: {} of {} MB{}", st.latest, mb(st.received), mb(st.total), st.attempt > 1 ? std::format(" (try {})", st.attempt) : std::string{})
              : st.received ? std::format("Downloading {}: {} MB", st.latest, mb(st.received))
                            : std::string("Connecting to GitHub..."))
                 .c_str());
        download_bar(st);
    }
    if (st.step == up::Step::verifying) note("Checking it against GitHub's SHA-256, and that it is a ReSkate runtime...");
    if (st.step == up::Step::installing) note("Putting it in place. The version you have now is kept beside it as ReSkate.dll.previous.");
    if ((going || installed || newer) && !st.installed_to.empty()) {
        const auto folder = st.installed_to.parent_path().u8string();
        note(("Into the folder ReSkate runs from: " + std::string(folder.begin(), folder.end())).c_str());
    }
    if (st.step == up::Step::newer && !st.folder_writable)
        warn("That folder cannot be written to (read-only, or a protected folder such as Program Files). UPDATE NOW still downloads and checks it; "
             "you then copy it in yourself with the game closed, or move ReSkate to a folder of its own.");
    if (restarting)
        note(st.restarting_for < 12 ? "Closing the game. It starts again by itself in a few seconds."
                                    : "Waiting for the game to close. If it asks whether to quit, say yes: it starts again by itself.");
    if (installed)
        note((st.can_restart ? "Installed. KEEP PLAYING and it is there next time, or RESTART NOW to close the game and start it again with " + st.latest + "."
                             : "Installed. Close the game and start it with ReSkateLauncher to use " + st.latest + ".")
                 .c_str());
    if (st.step == up::Step::install_failed) warn(("It could not be installed: " + up::as_sentence(st.error)).c_str());
    else if (st.step == up::Step::download_failed)
        warn(("The download did not work: " + up::as_sentence(st.error) +
              (st.failed_at == 0 ? " TRY AGAIN goes on from where it stopped." : " TRY AGAIN downloads it again."))
                 .c_str());
    // What can be done now: the one to press as the studio's own big blue button, the rest under it.
    if (st.step == up::Step::newer) {
        const auto label = "UPDATE TO " + st.latest + " NOW";
        if (studio_button(menu, "##updatenow", label.c_str(), "DOWNLOAD + INSTALL", true, 10)) up::download();
        ImGui::Dummy(ImVec2(0, px(6)));
        if (button_row(menu, "##whatsnew", {{"WHAT'S NEW", animator_studio_openable(model), false, "Opens the studio with what the new version changes."}}) == 0) {
            up::show_popup();
            anim_studio::request_open(true);
            menu.close_requested = true;
        }
    } else if (st.step == up::Step::download_failed || st.step == up::Step::install_failed) {
        const bool folder = st.step == up::Step::install_failed && !st.saved_to.empty();
        const int pressed = folder ? button_row(menu, "##updatefailed", {{"TRY AGAIN", true, true, "Downloads and installs it again."},
                                                                         {"OPEN FOLDER", true, false, "Where the new ReSkate.dll is, to copy in by hand."}})
                                   : button_row(menu, "##updatefailed", {{"TRY AGAIN", true, true, "Goes on with the download from where it stopped."}});
        if (pressed == 0) up::download();
        if (pressed == 1) up::open_folder();
    } else if (st.step == up::Step::downloading) {
        if (button_row(menu, "##updatestop", {{"CANCEL", true, false, "Stops the download; UPDATE NOW goes on from where it stopped."}}) == 0) up::cancel();
    } else if (installed) {
        if (st.can_restart && studio_button(menu, "##restartnow", "RESTART NOW", "STARTS AGAIN BY ITSELF", true, 10)) up::restart();
        ImGui::Dummy(ImVec2(0, px(6)));
        if (button_row(menu, "##keepplaying", {{"KEEP PLAYING", true, !st.can_restart, "Back to the game; the new version runs from the next start."}}) == 0) {
            up::close_popup();
            menu.close_requested = true;
        }
    } else if (!going && !restarting) {
        if (button_row(menu, "##checknow", {{st.step == up::Step::checking ? "CHECKING..." : "CHECK NOW", st.step != up::Step::checking, false,
                                             "Asks GitHub for the newest release now."}}) == 0)
            up::check_now();
    }
    bool every = up::checking();
    if (toggle_row(menu, "Check at every start", "At each start the studio asks GitHub whether a newer version is out. Nothing is changed until you choose UPDATE NOW.", every))
        up::set_checking(every);
    studio_card_end(c);
}
}

void animator_studio_page(SkateMenu& menu, const Model& model, const CallbacksV3&) {
    const float t = since_opened();
    const auto settings = anim_studio::settings();
    const auto now = anim_studio::menu_summary();
    ImGui::BeginChild("animator-studio", ImVec2(0, page_body_height(menu)), ImGuiChildFlags_NavFlattened);
    banner(menu, now, settings, t);
    if (studio_button(menu, "##openstudio", "OPEN ANIMATION STUDIO", "F8  /  BOTH STICKS", animator_studio_openable(model), t)) {
        anim_studio::request_open(true);
        menu.close_requested = true;
    }
    if (const auto blocked = animator_studio_blocked_reason(model); !blocked.empty()) warn(blocked.c_str());
    ImGui::Dummy(ImVec2(0, px(6)));
    const anim_studio::settings_rows::Tell tell = [&menu](const std::string& said) { feedback(menu, said.c_str()); };
    switch (button_row(menu, "##studioactions", {{"HOW IT WORKS", true, false, "The guide from the first start: packs, stances, playlists, pop heights."},
                                                  {"REPORT A BUG", true, false, "The last 15 seconds, the log and your settings in one zip, for us."}})) {
    case 0:
        anim_studio::set_guide_seen(false);
        anim_studio::request_open(true);
        menu.close_requested = true;
        break;
    case 1: anim_studio::settings_rows::report_a_bug(tell).act(); break;
    default: break;
    }
    ImGui::Dummy(ImVec2(0, px(12)));
    // A newer version out, or one installed and waiting for a restart: UPDATES first. Otherwise at the foot of the page.
    namespace up = anim_studio::update;
    const auto update_step = up::state().step;
    const bool update_first = update_step == up::Step::newer || update_step == up::Step::installed || update_step == up::Step::downloading ||
                              update_step == up::Step::verifying || update_step == up::Step::installing || update_step == up::Step::restarting ||
                              update_step == up::Step::install_failed || update_step == up::Step::download_failed;
    if (update_first) updates_card(menu, model);

    // Nothing of yours plays: said, and put right in one press.
    if (settings.play_in_game && (now.playing.empty() || now.entries == 0) && !now.packs.empty()) {
        const auto c = studio_card_begin(menu, "nothing", "DEFAULT PLAYLIST IS EMPTY");
        note("Normal input uses the game's own animations. An assigned modifier playlist can still play while its button is held. Choose a default playlist below, or add packs to one.");
        if (button_row(menu, "##allpacks", {{"PLAY ALL MY PACKS", true, true, "Every pack goes in the playlist that is on (or a new one)."}}) == 0) {
            for (const auto& pack : now.packs)
                if (!pack.in) {
                    std::string said;
                    anim_studio::set_pack_in_playlist_on(pack.name, true, said);
                    feedback(menu, said.c_str());
                }
        }
        studio_card_end(c);
    }

    {
        const auto c = studio_card_begin(menu, "last", "LAST TRICKS", "what the game did, and what played it");
        last_tricks(menu, now);
        studio_card_end(c);
    }
    {
        const auto c = studio_card_begin(menu, "in-game", "IN GAME");
        setting_row(menu, anim_studio::settings_rows::custom_tricks_in_game(settings));
        setting_row(menu, anim_studio::settings_rows::transitions(settings));
        setting_row(menu, anim_studio::settings_rows::trick_matching(settings));
        setting_row(menu, anim_studio::settings_rows::crossfade(settings));
        studio_card_end(c);
    }
    {
        const auto playlists = anim_studio::playlists_now();
        const auto c = studio_card_begin(menu, "playlist", "DEFAULT PLAYLIST",
                                         now.playing.empty() ? std::string("none on") : std::format("{}: {} in it", now.playing, now.entries));
        setting_row(menu, anim_studio::settings_rows::playing_playlist(playlists, tell));
        setting_row(menu, anim_studio::settings_rows::which_version_plays(playlists, tell));
        if (playlists.lists.empty()) note("No playlists yet: + ADD a pack below, or make one in the studio's LIBRARY.");
        studio_card_end(c);
    }
    {
        const auto binding = anim_studio::modifier_binding();
        const auto c = studio_card_begin(menu, "modifier", "TRICK MODIFIER", "same input, another animation when you hold a button");
        note("Kickflip input: your regular animation. Kickflip input + modifier: a variation from your modifier playlist. The same principle applies to grinds, manuals and grabs.");
        setting_row(menu, anim_studio::settings_rows::modifier_button(binding));
        setting_row(menu, anim_studio::settings_rows::modifier_playlist(anim_studio::playlists_now(), binding));
        setting_row(menu, anim_studio::settings_rows::modifier_native_button(binding));
        if (button_row(menu, "##modifierlibrary", {{"SET UP IN LIBRARY", animator_studio_openable(model), true, "Choose animations, edit either playlist, and see how modifiers work."}}) == 0) {
            anim_studio::request_library(true);
            menu.close_requested = true;
        }
        studio_card_end(c);
    }
    if (!now.packs.empty()) {
        const auto c = studio_card_begin(menu, "packs", "PACKS", "ON: in the playlist that is on, and it plays");
        for (const auto& pack : now.packs) {
            ImGui::PushID(pack.name.c_str());
            bool in = pack.in;
            const std::string label = std::format("{}  ({} tricks)", pack.name, pack.tricks);
            if (toggle_row(menu, label.c_str(), "On: the whole pack is in the playlist that is on, and plays in game.", in)) {
                std::string said;
                anim_studio::set_pack_in_playlist_on(pack.name, in, said);
                feedback(menu, said.c_str());
            }
            ImGui::PopID();
        }
        studio_card_end(c);
    }
    if (!update_first) updates_card(menu, model);
    ImGui::EndChild();
}
}
