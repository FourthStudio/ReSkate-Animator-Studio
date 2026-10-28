#include "skate_menu_internal.h"
#include "animation_studio_settings.h"
#include "Extension/AnimationStudio/anim_studio.h"

#include <string>

// ADDONS > ANIMATOR STUDIO: the studio's settings that change how your tricks play in game and the playlist that plays,
// changed without opening it, and a way into it. What only acts while editing stays in the studio. The rows are the studio's own
// (animation_studio_settings.h). This page only draws them as the menu does.
namespace dingosdk::overlay::menu {
namespace {
using studio_settings::Setting;

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
}

void animator_studio_page(SkateMenu& menu, const Model& model, const CallbacksV3&) {
    if (primary_button(menu, "OPEN ANIMATION STUDIO", animator_studio_openable(model))) {
        anim_studio::request_open(true);
        menu.close_requested = true;
    }
    if (const auto blocked = animator_studio_blocked_reason(model); !blocked.empty()) warn(blocked.c_str());
    ImGui::BeginChild("animator-studio", ImVec2(0, page_body_height(menu)), ImGuiChildFlags_NavFlattened);
    const auto now = anim_studio::settings();
    begin_card(menu, "in-game", "IN GAME");
    setting_row(menu, studio_settings::custom_tricks_in_game(now));
    setting_row(menu, studio_settings::crossfade(now));
    setting_row(menu, studio_settings::trick_matching(now));
    setting_row(menu, studio_settings::joint_limits(now));
    end_card();
    const studio_settings::Tell tell = [&menu](const std::string& said) { feedback(menu, said.c_str()); };
    const auto playlists = anim_studio::playlists_now();
    begin_card(menu, "playlist", "PLAYLIST");
    setting_row(menu, studio_settings::playing_playlist(playlists, tell));
    setting_row(menu, studio_settings::which_version_plays(playlists, tell));
    if (playlists.lists.empty()) note("No playlists yet: make one in the studio, PACKS > PLAYLIST.");
    end_card();
    setting_row(menu, studio_settings::report_a_bug(tell)); // a button, not a setting: no card of its own
    ImGui::EndChild();
}
}
