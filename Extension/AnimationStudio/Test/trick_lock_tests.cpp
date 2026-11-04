// A trick locked by its creator (trick_lock.h): what may be done with someone else's work, and COPY TO's names for a new
// stance copy (stance_copy_name), which never take the name of a version that is there already.
#include "Extension/AnimationStudio/trick_lock.h"
#include "Extension/AnimationStudio/stance_copy.h"
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>

using namespace dingosdk::anim_studio;

namespace {
void check(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
constexpr TrickUse all_uses[]{TrickUse::preview, TrickUse::play, TrickUse::test, TrickUse::choose_pop, TrickUse::edit,
                              TrickUse::copy, TrickUse::copy_keys, TrickUse::relabel, TrickUse::share, TrickUse::put_in_pack};
bool watch_only(TrickUse use) {
    return use == TrickUse::preview || use == TrickUse::play || use == TrickUse::test || use == TrickUse::choose_pop;
}

void own_trick_may_do_anything() {
    const TrickOwner own{};
    check(!locked_to_creator(own) && !someone_elses(own), "a trick made here is yours");
    for (const auto use : all_uses) check(trick_use_allowed(own, use), "your own trick may be used every way");
}
void locked_trick_is_preview_only() {
    const TrickOwner locked{true, false, false, false};
    check(locked_to_creator(locked), "made elsewhere and not opened to edit: locked");
    for (const auto use : all_uses)
        check(trick_use_allowed(locked, use) == watch_only(use), "a locked trick is previewed, played and tested, nothing else");
    // Every refusal starts the same way, so the studio says the same thing wherever it was asked.
    for (const auto use : {TrickUse::edit, TrickUse::copy, TrickUse::copy_keys, TrickUse::relabel}) {
        const auto said = refused_use_message("Steezy Tre", "Renzo", use);
        check(said.starts_with(locked_preview_only), "a refusal says it is locked by its creator, preview only");
        check(said.find("Steezy Tre") != std::string::npos && said.find("Renzo") != std::string::npos, "a refusal names the trick and its maker");
    }
}
void open_trick_is_edited_but_never_shared() {
    const TrickOwner open{true, true, false, false};
    check(!locked_to_creator(open) && someone_elses(open), "opened to edit by its maker: editable, still his");
    check(trick_use_allowed(open, TrickUse::edit) && trick_use_allowed(open, TrickUse::copy) && trick_use_allowed(open, TrickUse::copy_keys),
          "its maker let others edit and copy it");
    check(!trick_use_allowed(open, TrickUse::share) && !trick_use_allowed(open, TrickUse::put_in_pack), "but it is never reshared");
}
void locked_recording_locks_whatever_its_key_says() {
    // A copy of a locked trick's recording, or the trick with its key edited out of its file, under a key of yours.
    const TrickOwner copied{false, false, true, false};
    check(locked_to_creator(copied) && someone_elses(copied), "a locked recording is locked under any key");
    check(!trick_use_allowed(copied, TrickUse::edit) && !trick_use_allowed(copied, TrickUse::share), "not opened, not shared");
    // The clip rule: never a clip of the game's own (the same file for everyone who opens that trick), never one its maker
    // opened to edit since, and only a clip made on another PC.
    check(clip_locks(false, true, false, false), "another maker's locked recording locks");
    check(!clip_locks(true, true, false, false), "a clip of the game's own never locks");
    check(!clip_locks(false, false, false, false), "a clip remembered from this PC's own maker does not lock");
    check(!clip_locks(false, true, true, true), "its maker opened it to edit since: it does not lock his open trick");
    check(clip_locks(false, true, true, false), "its own maker's trick, still locked: locks");
    check(clip_locks(false, true, false, true), "another maker's open trick on a locked maker's recording: locks (stolen, then opened)");
}
void keyless_trick_belongs_to_its_pack() {
    // A pack zipped by hand has no keys in its tricks: before 1.2 they read as yours, to open, copy and share.
    const auto locked = owned_by_pack({}, true, true, false);
    check(locked_to_creator(locked) && someone_elses(locked), "a keyless trick of a locked pack from another PC is locked");
    const auto open = owned_by_pack({}, true, true, true);
    check(!locked_to_creator(open) && someone_elses(open), "a keyless trick of an open pack is editable, and still not reshared");
    const auto alone = owned_by_pack({}, true, false, false);
    check(!locked_to_creator(alone) && !someone_elses(alone), "a keyless trick no pack from another PC names stays yours");
    // A trick with your key in someone else's pack is still yours to edit; that pack is never yours to share.
    const auto keyed = owned_by_pack({}, false, true, false);
    check(!locked_to_creator(keyed) && someone_elses(keyed), "your keyed trick named by another maker's pack: edited, not shared");
    // One with another maker's key keeps his lock, whatever the pack says.
    const auto theirs = owned_by_pack({true, false, false, false}, false, true, true);
    check(locked_to_creator(theirs), "another maker's locked trick stays locked in an open pack");
}
void stance_copies_are_always_new() {
    std::set<std::string> taken{"Kickflip mid", "Kickflip mid fakie", "Kickflip mid 2", "Kickflip mid switch", "Kickflip mid switch 2"};
    const auto is_taken = [&](const std::string& n) { return taken.contains(n); };
    check(stance_copy_name("Kickflip mid", 1, is_taken) == "Kickflip mid fakie 2", "a fakie copy beside the fakie one there is");
    check(stance_copy_name("Kickflip mid 2", 1, is_taken) == "Kickflip mid 2 fakie", "the second kickflip's fakie copy is its own");
    check(stance_copy_name("Kickflip mid fakie", 2, is_taken) == "Kickflip mid switch 3", "a version of a version is named from the trick");
    check(stance_copy_name("Kickflip mid switch fakie", 0, is_taken) == "Kickflip mid normal", "the stance word is taken off first");
    // Whatever is there already, the copy's name is never one of them, and fits the library's 48 bytes.
    for (int round = 0; round < 40; ++round)
        for (int version = 0; version < 4; ++version) {
            const auto name = stance_copy_name("A rather long trick name that fills the field up", version, is_taken);
            check(!taken.contains(name), "a stance copy never takes a name that is there");
            check(name.size() <= 48, "a stance copy's name fits the library");
            taken.insert(name);
        }
}
} // namespace

int main() {
    try {
        own_trick_may_do_anything();
        locked_trick_is_preview_only();
        open_trick_is_edited_but_never_shared();
        locked_recording_locks_whatever_its_key_says();
        keyless_trick_belongs_to_its_pack();
        stance_copies_are_always_new();
        std::puts("trick lock tests passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "trick lock tests failed: %s\n", e.what());
        return 1;
    }
}
