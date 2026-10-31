#pragma once
#include "anim_core.h"
#include "anim_runtime_rules.h"
#include <stdexcept>
#include <utility>

namespace dingosdk::anim_studio {
inline bool valid_stance_version(std::string_view trick, int version) noexcept {
    return version >= 0 && version <= 3 && (!trick.starts_with("native:") || version % 2 == 0);
}
// Native pieces have separate assets for fakie. A normal/switch copy mirrors its clip and edits together;
// it never turns on the whole-trick fakie layer or changes its source asset, timing or modifier hint.
inline std::pair<Animation, Clip> stance_copy(Animation animation, Clip clip, bool own_right, int version) {
    if (!valid_stance_version(animation.trick, version))
        throw std::invalid_argument("Choose normal or switch for a native piece; its fakie way is a separate animation.");
    const bool piece = animation.trick.starts_with("native:");
    const bool want_right = version >= 2 ? !own_right : own_right;
    const bool authored_right = animation.source.kind == "native_asset" || animation.stance == "goofy";
    const bool lead = clip_leads_right(clip).value_or(leads_right(authored_right, animation.switch_stance.value_or(false)));
    if (lead != want_right) {
        clip = mirror_clip(clip);
        const auto timing = animation.clip;
        animation.clip = {};
        animation = mirror_animation(animation);
        animation.clip = timing;
    }
    animation.switch_stance = (animation.source.kind == "native_asset" || animation.stance == "goofy") != want_right;
    const bool was_fakie = fakie_mirrored(animation);
    if (!piece) animation.fakie = version % 2 == 1;
    if (fakie_mirrored(animation) != was_fakie) mirror_upper_keys(animation);
    animation.keep_leads_right = want_right;
    return {std::move(animation), std::move(clip)};
}
}
