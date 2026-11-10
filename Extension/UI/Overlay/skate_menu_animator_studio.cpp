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
#include <functional>
#include <string>

// The ReSkate menu's ANIMATOR page: the studio's settings that change how your tricks play in game and the playlist that plays,
// changed without opening it, and a way into it - dressed as the studio is (its backdrop, grain, brush bar, Anton and the
// brush face, the kickflipping board, its worn icons), with a banner that plays in each time the page opens, the last tricks
// done and who played them, and the packs to switch in and out of the playlist. What only acts while editing stays in the
// studio. The rows are the studio's own (Extension/AnimationStudio/studio_settings_rows.h), drawn as its SETTINGS draws
// them; what the row under the pointer or the focus does is told in the panel at the page's foot, never in a hover tip.
namespace dingosdk::overlay::menu {
namespace {
using anim_studio::settings_rows::Setting;
using theme::paper;
using theme::muted;

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

// ---- the page's own controls, in the studio's look --------------------------------------------------------------------
// Rows that read as the studio's SETTINGS do: a dark rough tile flooding blue under the focus, the name in bold with what
// it does in a muted line under it, the control at the right (a switch that slides, "< value >" with a dot for each
// choice, a bar with its value). No hover tips anywhere on the page (a long one ran across the whole screen, 2026-11-10):
// what the row under the pointer or the focus does is told in full in the panel at the foot of the page (help_panel).
constexpr ImU32 row_rest = IM_COL32(33, 35, 41, 242), row_lit = IM_COL32(1, 131, 255, 250), row_off = IM_COL32(30, 31, 35, 160);
constexpr ImU32 ink_dark = IM_COL32(8, 8, 10, 255), soft = IM_COL32(205, 210, 220, 255);
// Inside a card the rows keep this far from its right edge, as the indent keeps them from its left.
float card_margin = 0;
float room_x() { return std::max(px(60), ImGui::GetContentRegionAvail().x - card_margin); }

// What the row under the pointer or the focus does, for the panel at the foot of the page. Set as the rows draw.
struct Help {
    std::string title, text;
};
Help& help_now() {
    static Help h;
    return h;
}
void offer_help(bool shown, const std::string& title, const std::string& text) {
    if (shown && !text.empty()) help_now() = {title, text};
}
bool focus_shown() { return ImGui::IsItemFocused() && ImGui::GetIO().NavVisible; }
// A number eased towards where it is going, kept by id (a switch's knob sliding across).
float eased(ImGuiID id, float target, float rate = 16.0f) {
    auto* store = ImGui::GetStateStorage();
    float value = store->GetFloat(id, target);
    value += (target - value) * std::min(1.0f, ImGui::GetIO().DeltaTime * rate);
    if (std::abs(target - value) < 0.001f) value = target;
    store->SetFloat(id, value);
    return value;
}
// What a row does, in the line under its name: its first sentence ("How one trick hands over to the next.").
std::string first_sentence(const std::string& about) {
    for (std::size_t i = 0; i < about.size(); ++i)
        if ((about[i] == '.' || about[i] == ':') && (i + 1 == about.size() || about[i + 1] == ' ')) return about.substr(0, i + (about[i] == '.' ? 1 : 0));
    return about;
}
std::string upper(std::string text) {
    for (auto& c : text)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return text;
}
// The tile under a row: rough-cut like the studio's, blue under the focus, lighter under the pointer, a blue edge at
// its left for one that is ON.
void row_tile(ImDrawList* d, ImVec2 a, ImVec2 b, bool lit, bool hovered, bool enabled, bool marked) {
    const ImU32 fill = !enabled ? row_off : lit ? row_lit : hovered ? IM_COL32(45, 48, 57, 248) : row_rest;
    if (!draw_studio_tile(d, a, b, px(6), fill)) d->AddRectFilled(a, b, fill, px(3));
    if (!lit) d->AddLine(ImVec2(a.x + px(6), a.y + px(1)), ImVec2(b.x - px(6), a.y + px(1)), IM_COL32(255, 255, 255, hovered ? 26 : 14), px(1));
    if (marked && !lit) d->AddRectFilled(ImVec2(a.x, a.y + px(6)), ImVec2(a.x + px(4), b.y - px(6)), skate_theme::blue);
    if (lit && ImGui::GetIO().NavVisible) d->AddRect(ImVec2(a.x - px(2), a.y - px(2)), ImVec2(b.x + px(2), b.y + px(2)), paper, px(3), 0, px(2));
}
// The name and the line under it, between the row's left edge and `right`.
void row_words(SkateMenu& menu, ImDrawList* d, ImVec2 a, float right, float h, const std::string& label, const std::string& about, bool lit,
               bool enabled) {
    const float room = right - a.x - px(18);
    const std::string line = fit(menu.body, px(13), first_sentence(about), room);
    const float name_size = px(16);
    const auto name = fit(menu.bold, name_size, label, room);
    const ImU32 ink = !enabled ? muted : paper;
    if (line.empty()) {
        d->AddText(menu.bold, name_size, ImVec2(a.x + px(16), a.y + (h - name_size) * 0.5f - px(1)), ink, name.c_str());
        return;
    }
    d->AddText(menu.bold, name_size, ImVec2(a.x + px(16), a.y + h * 0.5f - name_size - px(1)), ink, name.c_str());
    d->AddText(menu.body, px(13), ImVec2(a.x + px(16), a.y + h * 0.5f + px(3)), lit ? IM_COL32(225, 238, 255, 235) : muted, line.c_str());
}
float row_height_for(const std::string& about) { return about.empty() ? px(46) : px(58); }

// A setting that is ON or OFF: a switch at the right that slides across. True when it was flipped.
bool studio_toggle(SkateMenu& menu, const std::string& label, const std::string& about, bool& value, bool enabled = true) {
    ImGui::PushID(label.c_str());
    const auto a = ImGui::GetCursorScreenPos();
    const float w = room_x(), h = row_height_for(about);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled), lit = enabled && focus_shown();
    // Left / right on the controller set it too: left OFF, right ON.
    const int step = console_step(enabled && ImGui::IsItemFocused());
    bool flipped = enabled && clicked;
    if (step && enabled && (step > 0) != value) flipped = true;
    if (flipped) value = !value;
    offer_help(hovered || lit, label, about);
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 b(a.x + w, a.y + h);
    row_tile(d, a, b, lit, hovered, enabled, value && enabled);
    // The switch: a track, its knob sliding to ON.
    const float tw = px(50), th = px(26), knob = th * 0.5f - px(3);
    const ImVec2 ta(b.x - px(14) - tw, a.y + (h - th) * 0.5f), tb(ta.x + tw, ta.y + th);
    const float at = eased(ImGui::GetID("##knob"), value ? 1.0f : 0.0f);
    const bool blue_row = lit;
    const ImU32 track = !enabled ? IM_COL32(60, 62, 68, 200) : value ? (blue_row ? IM_COL32(255, 255, 255, 235) : skate_theme::blue) : IM_COL32(16, 17, 20, 230);
    d->AddRectFilled(ta, tb, track, th * 0.5f);
    if (!value) d->AddRect(ta, tb, IM_COL32(255, 255, 255, 46), th * 0.5f, 0, px(1.5f));
    const ImVec2 knob_at(ta.x + th * 0.5f + (tw - th) * at, ta.y + th * 0.5f);
    d->AddCircleFilled(ImVec2(knob_at.x, knob_at.y + px(1.5f)), knob, IM_COL32(0, 0, 0, 90), 24);
    d->AddCircleFilled(knob_at, knob, value ? (blue_row ? skate_theme::blue : paper) : IM_COL32(150, 155, 165, 255), 24);
    // ON / OFF beside it in Anton.
    const char* state = value ? "ON" : "OFF";
    auto* font = anton(menu, px(17));
    const auto sw = font->CalcTextSizeA(px(17), FLT_MAX, 0, state);
    d->AddText(font, px(17), ImVec2(ta.x - px(10) - sw.x, a.y + (h - sw.y) * 0.5f), !enabled ? muted : blue_row ? paper : value ? blue_light : muted, state);
    row_words(menu, d, a, ta.x - px(16) - sw.x, h, label, about, blue_row, enabled);
    ImGui::PopID();
    return flipped;
}

// One of a few: "< value >" at the right with a dot for each, the arrows' halves stepping it back and on. `step` moves it
// one along (-1, 1). True when it moved.
bool studio_choice(SkateMenu& menu, const std::string& label, const std::string& about, const std::string& value, int selected, int count,
                   const std::function<void(int)>& step, bool enabled = true) {
    ImGui::PushID(label.c_str());
    const auto a = ImGui::GetCursorScreenPos();
    const float w = room_x(), h = row_height_for(about);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##choice", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled), lit = enabled && focus_shown();
    const float cw = std::clamp(w * 0.46f, px(150), px(260));
    const ImVec2 ca(a.x + w - px(10) - cw, a.y + px(8)), cb(a.x + w - px(10), a.y + h - px(8));
    int moved = 0;
    // A click on the control's left half steps back; a click anywhere else, or A, steps on.
    const bool mouse = ImGui::GetIO().MouseReleased[0] && hovered;
    if (enabled && clicked) moved = mouse && ImGui::GetIO().MousePos.x < ca.x + cw * 0.5f && ImGui::GetIO().MousePos.x >= ca.x ? -1 : 1;
    if (const int s = console_step(enabled && ImGui::IsItemFocused())) moved = s;
    if (moved && step) step(moved);
    offer_help(hovered || lit, label, about);
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 b(a.x + w, a.y + h);
    row_tile(d, a, b, lit, hovered, enabled, false);
    // The control: a dark well with chevrons at either end, the choice in the middle, the dots under it.
    d->AddRectFilled(ca, cb, lit ? IM_COL32(0, 40, 90, 120) : IM_COL32(0, 0, 0, 95), px(3));
    const ImU32 arrow = !enabled ? IM_COL32(120, 124, 132, 160) : lit ? paper : hovered ? paper : soft;
    const float mid = (ca.y + cb.y) * 0.5f, s = px(5);
    const auto chevron = [&](float x, float dir) {
        const ImVec2 points[3]{ImVec2(x + dir * s, mid - s * 1.4f), ImVec2(x - dir * s * 0.4f, mid), ImVec2(x + dir * s, mid + s * 1.4f)};
        d->AddPolyline(points, 3, arrow, ImDrawFlags_None, px(2.4f));
    };
    chevron(ca.x + px(14), 1);
    chevron(cb.x - px(14), -1);
    const bool dots = count > 1 && count <= 12;
    const float size = px(15);
    const auto shown = fit(menu.bold, size, upper(value), cw - px(56));
    const auto vw = menu.bold->CalcTextSizeA(size, FLT_MAX, 0, shown.c_str());
    d->AddText(menu.bold, size, ImVec2((ca.x + cb.x - vw.x) * 0.5f, mid - vw.y * 0.5f - (dots ? px(4) : 0)), !enabled ? muted : lit ? paper : blue_light,
               shown.c_str());
    if (dots) {
        const float dw = px(9), gap = px(4), total = static_cast<float>(count) * dw + static_cast<float>(count - 1) * gap;
        float x = (ca.x + cb.x - total) * 0.5f;
        for (int i = 0; i < count; ++i, x += dw + gap)
            d->AddRectFilled(ImVec2(x, cb.y - px(8)), ImVec2(x + dw, cb.y - px(5)),
                             i == selected ? (lit ? paper : skate_theme::blue) : IM_COL32(255, 255, 255, lit ? 90 : 40), px(1));
    }
    row_words(menu, d, a, ca.x - px(6), h, label, about, lit, enabled);
    ImGui::PopID();
    return moved != 0;
}

// A value on a bar: the name and its value on top, the bar across the row under them, its knob dragged with the mouse or
// stepped a twentieth at a time with left / right. `slide` takes 0 to 1 of the bar.
void studio_slider(SkateMenu& menu, const std::string& label, const std::string& about, const std::string& value, float amount,
                   const std::function<void(float)>& slide, bool enabled = true) {
    ImGui::PushID(label.c_str());
    const auto a = ImGui::GetCursorScreenPos();
    const float w = room_x(), top = row_height_for(about), h = top + px(26);
    ImGui::BeginDisabled(!enabled);
    ImGui::InvisibleButton("##slider", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled), lit = enabled && focus_shown();
    const float bar_a = a.x + px(18), bar_b = a.x + w - px(18), bar_y = a.y + top + px(6);
    // Dragged: only a press that began on the bar moves it (a click on the name just picks the row).
    const ImGuiID drag = ImGui::GetID("##drag");
    auto* store = ImGui::GetStateStorage();
    bool dragging = store->GetBool(drag, false);
    if (ImGui::IsItemActivated() && ImGui::GetIO().MousePos.y >= bar_y - px(14)) dragging = true;
    if (!ImGui::IsItemActive()) dragging = false;
    store->SetBool(drag, dragging);
    float shown_amount = std::clamp(amount, 0.0f, 1.0f);
    if (enabled && dragging && slide) {
        const float to = std::clamp((ImGui::GetIO().MousePos.x - bar_a) / std::max(1.0f, bar_b - bar_a), 0.0f, 1.0f);
        if (std::abs(to - shown_amount) > 0.0005f) slide(to);
        shown_amount = to;
    }
    if (const int s = console_step(enabled && ImGui::IsItemFocused()); s && slide) {
        shown_amount = std::clamp(shown_amount + 0.05f * static_cast<float>(s), 0.0f, 1.0f);
        slide(shown_amount);
    }
    offer_help(hovered || lit, label, about);
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 b(a.x + w, a.y + h);
    row_tile(d, a, b, lit, hovered, enabled, false);
    // The value at the top right, in Anton.
    auto* font = anton(menu, px(19));
    const auto vw = font->CalcTextSizeA(px(19), FLT_MAX, 0, value.c_str());
    d->AddText(font, px(19), ImVec2(b.x - px(16) - vw.x, a.y + (top - vw.y) * 0.5f), !enabled ? muted : lit ? paper : blue_light, value.c_str());
    row_words(menu, d, a, b.x - px(26) - vw.x, top, label, about, lit, enabled);
    // The bar: its ticks, the part up to the knob filled, the knob.
    const float bh = px(6), x = bar_a + (bar_b - bar_a) * shown_amount;
    for (int i = 0; i <= 10; ++i) {
        const float tx = bar_a + (bar_b - bar_a) * static_cast<float>(i) / 10.0f;
        d->AddRectFilled(ImVec2(tx - px(0.75f), bar_y + bh * 0.5f + px(5)), ImVec2(tx + px(0.75f), bar_y + bh * 0.5f + (i % 5 ? px(8) : px(11))),
                         IM_COL32(255, 255, 255, lit ? 110 : 45));
    }
    d->AddRectFilled(ImVec2(bar_a, bar_y - bh * 0.5f), ImVec2(bar_b, bar_y + bh * 0.5f), lit ? IM_COL32(0, 30, 70, 140) : IM_COL32(0, 0, 0, 120), bh * 0.5f);
    d->AddRectFilled(ImVec2(bar_a, bar_y - bh * 0.5f), ImVec2(x, bar_y + bh * 0.5f), !enabled ? muted : lit ? paper : skate_theme::blue, bh * 0.5f);
    d->AddCircleFilled(ImVec2(x, bar_y + px(1.5f)), px(9), IM_COL32(0, 0, 0, 100), 24);
    d->AddCircleFilled(ImVec2(x, bar_y), px(9), paper, 24);
    d->AddCircle(ImVec2(x, bar_y), px(9), lit ? IM_COL32(0, 40, 90, 255) : skate_theme::blue, 24, px(2.5f));
    ImGui::PopID();
}

// Tiles across the row (a press each), fewer to a row and more rows when they would be too narrow: the name in Anton, a
// muted line under it, and the studio's worn icon running off the corner where there is one. `on` floods one blue: the
// one to press.
struct Tile {
    const char* label;
    std::string about;
    bool enabled = true;
    bool on = false;
    int icon = -1; // a StudioPicture, -1 for none
};
int studio_tiles(SkateMenu& menu, const char* id, std::initializer_list<Tile> tiles, float height = 0) {
    int pressed = -1;
    const int count = static_cast<int>(tiles.size());
    if (!count) return pressed;
    ImGui::PushID(id);
    const float gap = px(8), room = room_x(), least = px(150);
    int across = std::clamp(static_cast<int>((room + gap) / (least + gap)), 1, count);
    if (count == 4 && across == 3) across = 2; // two rows of two, not three and one
    const float tw = (room - gap * static_cast<float>(across - 1)) / static_cast<float>(across);
    const float th = height > 0 ? height : px(62);
    int i = 0;
    for (const auto& tile : tiles) {
        if (i % across) ImGui::SameLine(0, gap);
        else if (i) ImGui::Dummy(ImVec2(0, std::max(0.0f, gap - ImGui::GetStyle().ItemSpacing.y)));
        ImGui::PushID(i);
        const auto a = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!tile.enabled);
        const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(tw, th), ImGuiButtonFlags_EnableNav);
        ImGui::EndDisabled();
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled), lit = tile.enabled && focus_shown();
        offer_help(hovered || lit, tile.label, tile.about);
        if (clicked && tile.enabled) pressed = i;
        auto* d = ImGui::GetWindowDrawList();
        const ImVec2 b(a.x + tw, a.y + th);
        const bool flood = tile.enabled && (lit || tile.on);
        const ImU32 fill = !tile.enabled ? row_off : flood ? (lit && tile.on ? skate_theme::blue_hover : skate_theme::blue)
                                                  : hovered ? IM_COL32(45, 48, 57, 250) : IM_COL32(12, 13, 16, 246);
        if (!draw_studio_tile(d, a, b, px(8), fill)) d->AddRectFilled(a, b, fill, px(3));
        // The icon, big and worn, running off the bottom right corner: the tile is its frame.
        if (tile.icon >= 0) {
            d->PushClipRect(ImVec2(a.x + px(4), a.y + px(4)), ImVec2(b.x - px(4), b.y - px(4)), true);
            const float side = th * 1.25f;
            const ImVec2 ia(b.x - side * 0.78f, a.y + th * 0.02f);
            (void)draw_studio_picture(d, static_cast<StudioPicture>(tile.icon), ia, ImVec2(ia.x + side, ia.y + side),
                                      flood ? IM_COL32(255, 255, 255, 235) : IM_COL32(255, 255, 255, hovered ? 150 : 95));
            d->PopClipRect();
        }
        if (lit && ImGui::GetIO().NavVisible) d->AddRect(ImVec2(a.x - px(2), a.y - px(2)), ImVec2(b.x + px(2), b.y + px(2)), paper, px(3), 0, px(2));
        const float text_room = tw - px(28) - (tile.icon >= 0 ? th * 0.55f : 0);
        const float size = std::min(px(21), th * 0.34f);
        auto* font = anton(menu, size);
        const auto name = fit(font, size, tile.label, text_room);
        const auto nw = font->CalcTextSizeA(size, FLT_MAX, 0, name.c_str());
        const ImU32 ink = !tile.enabled ? muted : flood ? (tile.on && !lit ? paper : ink_dark) : paper;
        const std::string line = tile.about.empty() ? std::string{} : fit(menu.body, px(12), first_sentence(tile.about), text_room);
        const float block = nw.y + (line.empty() ? 0 : px(16));
        const float ty = a.y + (th - block) * 0.5f;
        if (tile.icon >= 0 || !line.empty()) d->AddText(font, size, ImVec2(a.x + px(14), ty), ink, name.c_str());
        else d->AddText(font, size, ImVec2(a.x + (tw - nw.x) * 0.5f, ty), ink, name.c_str());
        if (!line.empty())
            d->AddText(menu.body, px(12), ImVec2(a.x + px(14), ty + nw.y + px(2)),
                       flood ? (tile.on && !lit ? IM_COL32(225, 238, 255, 230) : IM_COL32(8, 8, 10, 200)) : muted, line.c_str());
        ImGui::PopID();
        ++i;
    }
    ImGui::PopID();
    return pressed;
}

// A studio settings row (studio_settings_rows.h) as this page's control.
void setting_row(SkateMenu& menu, const Setting& row) {
    ImGui::PushID(row.label.c_str());
    switch (row.kind) {
    case Setting::Kind::choice:
        if (row.on_off) {
            bool on = row.selected == 1;
            if (studio_toggle(menu, row.label, row.about, on, row.enabled) && row.pick) row.pick(on ? 1 : 0);
        } else {
            const int count = static_cast<int>(row.options.size());
            const std::string value = row.selected >= 0 && row.selected < count ? row.options[static_cast<std::size_t>(row.selected)] : row.value;
            // One along: the row's own step, else its list from the one it is now.
            const std::function<void(int)> step = row.step ? row.step : std::function<void(int)>([&row, count](int by) {
                if (count > 0 && row.pick) row.pick(((row.selected < 0 ? (by > 0 ? -1 : 0) : row.selected) + by + count) % count);
            });
            studio_choice(menu, row.label, row.about, value.empty() ? std::string("CHOOSE") : value, row.selected, count, step, row.enabled);
        }
        break;
    case Setting::Kind::slider: studio_slider(menu, row.label, row.about, row.value, row.amount, row.slide, row.enabled); break;
    case Setting::Kind::action:
        if (studio_tiles(menu, "##action", {{row.label.c_str(), row.about, row.enabled}}, px(52)) == 0 && row.act) row.act();
        break;
    case Setting::Kind::info: info(menu, row.label.c_str(), row.value); break;
    case Setting::Kind::name:
    case Setting::Kind::folder:
    case Setting::Kind::key:
    case Setting::Kind::heading: break; // the studio's own: what is used while editing, not in game
    }
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::PopID();
}

// The panel at the foot of the page: what the row under the pointer or the focus does, in full and wrapped to the page
// (the hover tips ran across the whole screen). Its words shrink a step or two before a long one is cut.
void help_panel(SkateMenu& menu, float height) {
    const auto& help = help_now();
    auto* d = ImGui::GetWindowDrawList();
    const auto a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(w, height));
    const ImVec2 b(a.x + w, a.y + height);
    if (!draw_studio_tile(d, a, b, px(8), IM_COL32(10, 11, 14, 250))) d->AddRectFilled(a, b, IM_COL32(10, 11, 14, 250), px(3));
    d->AddRectFilled(ImVec2(a.x + px(4), a.y + px(4)), ImVec2(a.x + px(8), b.y - px(4)), skate_theme::blue);
    const std::string title = help.title.empty() ? std::string("ANIMATOR") : upper(help.title);
    const std::string text = help.text.empty() ? std::string("Point at a setting, or move onto it with the controller: what it does shows here in full.") : help.text;
    auto* font = anton(menu, px(16));
    const float left = a.x + px(20), room = b.x - px(14) - left;
    d->AddText(font, px(16), ImVec2(left, a.y + px(9)), blue_light, fit(font, px(16), title, room).c_str());
    const float text_top = a.y + px(32), text_room = b.y - px(8) - text_top;
    float size = px(14);
    for (const float candidate : {px(14), px(13), px(12)}) {
        size = candidate;
        if (menu.body->CalcTextSizeA(size, FLT_MAX, room, text.c_str()).y <= text_room) break;
    }
    d->PushClipRect(ImVec2(left, text_top), ImVec2(b.x - px(10), b.y - px(6)), true);
    d->AddText(menu.body, size, ImVec2(left, text_top), help.text.empty() ? muted : soft, text.c_str(), nullptr, room);
    d->PopClipRect();
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
StudioCard studio_card_begin(SkateMenu& menu, const char* id, const char* title, const std::string& aside = {}, ImU32 tape = skate_theme::blue) {
    ImGui::PushID(id);
    StudioCard card{ImGui::GetID("##height")};
    const float height = ImGui::GetStateStorage()->GetFloat(card.key, 0);
    const auto a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    auto* d = ImGui::GetWindowDrawList();
    if (height > 0) {
        const ImVec2 b(a.x + w, a.y + height);
        if (!draw_studio_tile(d, a, b, px(10), IM_COL32(24, 26, 31, 250))) d->AddRectFilled(a, b, IM_COL32(24, 26, 31, 248));
        draw_studio_picture(d, StudioPicture::grain, a, b, IM_COL32(255, 255, 255, 22));
        // A wash of the tape's colour down from the top, under the title.
        const ImU32 wash = (tape & 0x00ffffffu) | (34u << 24), clear = tape & 0x00ffffffu;
        d->AddRectFilledMultiColor(ImVec2(a.x + px(6), a.y + px(6)), ImVec2(b.x - px(6), a.y + std::min(height * 0.5f, px(90))), wash, wash, clear, clear);
    }
    // The title on tape.
    const float size = px(20);
    auto* font = anton(menu, size);
    const auto tw = font->CalcTextSizeA(size, FLT_MAX, 0, title);
    const ImVec2 ta(a.x + px(12), a.y + px(10)), tb(ta.x + tw.x + px(24), ta.y + px(30));
    const auto darker = [](ImU32 c) {
        return IM_COL32((c & 0xff) * 6 / 10, ((c >> 8) & 0xff) * 6 / 10, ((c >> 16) & 0xff) * 6 / 10, 255);
    };
    if (!draw_studio_tile(d, ta, tb, px(4), tape)) d->AddRectFilled(ta, tb, tape);
    d->AddRectFilled(ImVec2(ta.x + px(2), tb.y - px(3)), ImVec2(tb.x - px(2), tb.y), darker(tape));
    d->AddText(font, size, ImVec2(ta.x + px(12), ta.y + (px(30) - tw.y) * 0.5f), paper, title);
    if (!aside.empty())
        d->AddText(menu.bold, px(13), ImVec2(tb.x + px(12), ta.y + px(8)), muted, fit(menu.bold, px(13), aside, a.x + w - px(12) - (tb.x + px(12))).c_str());
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(w, px(46)));
    ImGui::Indent(px(12));
    card_margin = px(12); // the rows stop short of the card's right edge as they do of its left
    return card;
}
void studio_card_end(const StudioCard& card) {
    card_margin = 0;
    ImGui::Dummy(ImVec2(0, px(8)));
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
    ImGui::Dummy(ImVec2(0, px(2)));
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
        if (studio_tiles(menu, "##whatsnew", {{"WHAT'S NEW", "Opens the studio with what the new version changes.", animator_studio_openable(model)}}, px(52)) == 0) {
            up::show_popup();
            anim_studio::request_open(true);
            menu.close_requested = true;
        }
    } else if (st.step == up::Step::download_failed || st.step == up::Step::install_failed) {
        const bool folder = st.step == up::Step::install_failed && !st.saved_to.empty();
        const int pressed = folder ? studio_tiles(menu, "##updatefailed", {{"TRY AGAIN", "Downloads and installs it again.", true, true},
                                                                           {"OPEN FOLDER", "Where the new ReSkate.dll is, to copy in by hand."}}, px(52))
                                   : studio_tiles(menu, "##updatefailed", {{"TRY AGAIN", "Goes on with the download from where it stopped.", true, true}}, px(52));
        if (pressed == 0) up::download();
        if (pressed == 1) up::open_folder();
    } else if (st.step == up::Step::downloading) {
        if (studio_tiles(menu, "##updatestop", {{"CANCEL", "Stops the download; UPDATE NOW goes on from where it stopped."}}, px(52)) == 0) up::cancel();
    } else if (installed) {
        if (st.can_restart && studio_button(menu, "##restartnow", "RESTART NOW", "STARTS AGAIN BY ITSELF", true, 10)) up::restart();
        ImGui::Dummy(ImVec2(0, px(6)));
        if (studio_tiles(menu, "##keepplaying", {{"KEEP PLAYING", "Back to the game; the new version runs from the next start.", true, !st.can_restart}}, px(52)) == 0) {
            up::close_popup();
            menu.close_requested = true;
        }
    } else if (!going && !restarting) {
        if (studio_tiles(menu, "##checknow", {{st.step == up::Step::checking ? "CHECKING..." : "CHECK NOW", "Asks GitHub for the newest release now.",
                                               st.step != up::Step::checking}}, px(52)) == 0)
            up::check_now();
    }
    bool every = up::checking();
    if (studio_toggle(menu, "Check at every start", "At each start the studio asks GitHub whether a newer version is out. Nothing is changed until you choose UPDATE NOW.", every))
        up::set_checking(every);
    studio_card_end(c);
}
}

void animator_studio_page(SkateMenu& menu, const Model& model, const CallbacksV3&) {
    const float t = since_opened();
    const auto settings = anim_studio::settings();
    const auto now = anim_studio::menu_summary();
    help_now() = {};
    // The help panel's room at the foot: tall enough for the longest row's words at the side sheet's width.
    const float help_height = px(118), help_gap = px(8);
    ImGui::BeginChild("animator-studio", ImVec2(0, std::max(px(140), page_body_height(menu) - help_height - help_gap)), ImGuiChildFlags_NavFlattened);
    banner(menu, now, settings, t);
    if (studio_button(menu, "##openstudio", "OPEN ANIMATION STUDIO", "F8  /  BOTH STICKS", animator_studio_openable(model), t)) {
        anim_studio::request_open(true);
        menu.close_requested = true;
    }
    if (const auto blocked = animator_studio_blocked_reason(model); !blocked.empty()) warn(blocked.c_str());
    ImGui::Dummy(ImVec2(0, px(8)));
    // The studio's own tabs, as its menu has them: straight to the one wanted, as tiles with the studio's worn icons.
    {
        const bool can = animator_studio_openable(model);
        const int picked = studio_tiles(menu, "##studiotabs",
                                        {{"TRICKS", "Your tricks and the game's, to open and edit.", can, false, static_cast<int>(StudioPicture::icon_tricks)},
                                         {"PACKS", "Make your own packs and publish them to the MARKET.", can, false, static_cast<int>(StudioPicture::icon_packs)},
                                         {"LIBRARY", "Your playlists: what plays by default and on a button.", can, false, static_cast<int>(StudioPicture::icon_studio)},
                                         {"MARKET", "Get packs other players made.", can, false, static_cast<int>(StudioPicture::icon_skater)}},
                                        px(72));
        // The studio's numbers for them (animation_studio_overlay.cpp: packs_tab is 5).
        static constexpr std::array<int, 4> tabs{0, 5, 1, 2};
        if (picked >= 0 && picked < static_cast<int>(tabs.size())) {
            anim_studio::request_tab(tabs[static_cast<std::size_t>(picked)]);
            menu.close_requested = true;
        }
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    const anim_studio::settings_rows::Tell tell = [&menu](const std::string& said) { feedback(menu, said.c_str()); };
    switch (studio_tiles(menu, "##studioactions", {{"HOW IT WORKS", "The guide from the first start: packs, stances, playlists, pop heights."},
                                                    {"REPORT A BUG", "Which trick and what looked wrong, then the last 25 seconds, the log and your settings in one zip."}},
                         px(52))) {
    case 0:
        anim_studio::set_guide_seen(false);
        anim_studio::request_open(true);
        menu.close_requested = true;
        break;
    case 1: // its form is in the studio, which opens for it
        anim_studio::settings_rows::report_a_bug(tell).act();
        menu.close_requested = true;
        break;
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
        const auto c = studio_card_begin(menu, "nothing", "DEFAULT PLAYLIST IS EMPTY", "your tricks do not play yet", red);
        note("Normal input uses the game's own animations. An assigned modifier playlist can still play while its button is held. Choose a default playlist below, or add packs to one.");
        ImGui::Dummy(ImVec2(0, px(4)));
        if (studio_tiles(menu, "##allpacks", {{"PLAY ALL MY PACKS", "Every pack goes in the playlist that is on (or a new one).", true, true}}, px(56)) == 0) {
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
        const auto c = studio_card_begin(menu, "in-game", "IN GAME", "how your tricks play");
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
        const auto c = studio_card_begin(menu, "modifier", "PLAYLISTS ON BUTTONS", "same input, another animation while you hold a button");
        note("Kickflip input: your regular animation. Kickflip input while holding a button: the version in that button's playlist. Each "
             "playlist can have a button of its own. Grinds, manuals and grabs work the same way.");
        for (const auto& row : anim_studio::settings_rows::button_playlists(anim_studio::playlists_now(), anim_studio::modifier_bindings()))
            setting_row(menu, row);
        ImGui::Dummy(ImVec2(0, px(2)));
        if (studio_tiles(menu, "##modifierlibrary", {{"SET UP IN LIBRARY", "Choose animations, edit either playlist, and see how modifiers work.",
                                                      animator_studio_openable(model), true}}, px(56)) == 0) {
            anim_studio::request_library(true);
            menu.close_requested = true;
        }
        studio_card_end(c);
    }
    if (!now.packs.empty()) {
        const auto c = studio_card_begin(menu, "packs", "PACKS IN THE DEFAULT PLAYLIST", "ON: in the playlist that plays by default");
        for (const auto& pack : now.packs) {
            ImGui::PushID(pack.name.c_str());
            bool in = pack.in;
            const std::string about = std::format("{} trick{}. On: the whole pack is in the playlist that is on, and plays in game.", pack.tricks,
                                                  pack.tricks == 1 ? "" : "s");
            if (studio_toggle(menu, pack.name, about, in)) {
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
    ImGui::Dummy(ImVec2(0, std::max(0.0f, help_gap - ImGui::GetStyle().ItemSpacing.y)));
    help_panel(menu, help_height);
}
}
