#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// A trick locked by its creator. A creator who shares a pack without "open to edit" keeps its tricks his: anyone who gets
// the pack can play them, test them and preview them, and nothing else. Every way into a trick's keys asks here (opening
// it in the editor, copying it whole or in parts, renaming it, sharing it), so no one path can be missed by another.
namespace dingosdk::anim_studio {
// What is known about whose a saved trick is.
struct TrickOwner {
    bool made_elsewhere{};  // its maker's key is another PC's, and not one of your own team's
    bool editable{};        // its maker let others open it to edit and copy it
    bool locked_clip{};     // its recording is one remembered as a locked trick's (a copy of it, or its key taken out)
    bool in_foreign_pack{}; // a pack made on another PC names it
};
// A trick without a maker's key is its pack's: one that a pack made on another PC names is that pack's maker's work, locked
// unless that pack is open to edit. (A pack zipped by hand has no keys in its tricks; without this they read as yours.)
constexpr TrickOwner owned_by_pack(TrickOwner owner, bool keyless, bool foreign_pack_names_it, bool foreign_pack_editable) noexcept {
    if (keyless && foreign_pack_names_it && !owner.made_elsewhere) {
        owner.made_elsewhere = true;
        owner.editable = foreign_pack_editable;
    }
    owner.in_foreign_pack = owner.in_foreign_pack || foreign_pack_names_it;
    return owner;
}
// Locked: it is only previewed, played and tested.
constexpr bool locked_to_creator(const TrickOwner& owner) noexcept { return (owner.made_elsewhere && !owner.editable) || owner.locked_clip; }
// Someone else's, locked or open: never put in your packs and never shared again.
constexpr bool someone_elses(const TrickOwner& owner) noexcept {
    return owner.made_elsewhere || owner.locked_clip || owner.in_foreign_pack;
}
enum class TrickUse : std::uint8_t {
    preview,    // watched in the studio (its keys are not handed to the editor's screen)
    play,       // played in game from a playlist
    test,       // TEST IN GAME
    choose_pop, // which pop it plays on here (the trick itself is not changed)
    edit,       // opened in the editor to change it
    copy,       // a copy of it made: COPY, COPY TO (pops, stances), COPY TO INS AND OUTS
    copy_keys,  // its keys or a pose taken from it (copy and paste)
    relabel,    // its name changed
    share,      // in a zip, a Thunderstore package or a publish
    put_in_pack // in one of your packs
};
constexpr bool trick_use_allowed(const TrickOwner& owner, TrickUse use) noexcept {
    switch (use) {
    case TrickUse::preview: case TrickUse::play: case TrickUse::test: case TrickUse::choose_pop: return true;
    case TrickUse::edit: case TrickUse::copy: case TrickUse::copy_keys: case TrickUse::relabel: return !locked_to_creator(owner);
    case TrickUse::share: case TrickUse::put_in_pack: return !someone_elses(owner);
    }
    return false;
}
// What the studio shows on a locked trick, and starts every refusal with.
inline constexpr std::string_view locked_preview_only = "Locked by its creator: preview only";
// Why a use was refused, for the status line: `shown` the trick as it is shown, `maker` who made it ("" not known).
inline std::string refused_use_message(std::string_view shown, std::string_view maker, TrickUse use) {
    std::string who(shown);
    if (!maker.empty()) who += " (by " + std::string(maker) + ")";
    switch (use) {
    case TrickUse::share: case TrickUse::put_in_pack:
        return who + " is another maker's: it plays for you, but only your own tricks go in your packs and are shared.";
    case TrickUse::edit: return std::string(locked_preview_only) + ". " + who + " plays, tests and previews, and is never opened in the editor.";
    case TrickUse::copy: return std::string(locked_preview_only) + ". " + who + " plays, tests and previews, and is never copied.";
    case TrickUse::copy_keys: return std::string(locked_preview_only) + ". The keys and poses of " + who + " stay its creator's: nothing is copied from it.";
    case TrickUse::relabel: return std::string(locked_preview_only) + ". " + who + " keeps the name its creator gave it.";
    default: return {};
    }
}
// A clip remembered as a locked trick's (by its hash, with its maker's key) locks a trick that has the same recording - a copy
// of it, or the trick itself with its key taken out - but not one made from a clip of the game's own (the same file for
// everyone who opens that trick), and not one its maker has since opened to edit.
constexpr bool clip_locks(bool game_clip, bool clip_maker_elsewhere, bool same_maker, bool editable) noexcept {
    return !game_clip && clip_maker_elsewhere && !(same_maker && editable);
}
} // namespace dingosdk::anim_studio
