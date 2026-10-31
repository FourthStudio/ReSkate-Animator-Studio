# Library workspace and trick modifiers

The Library has three independent concepts: the playlist being edited, the normal playlist, and the
modifier playlist. Browsing, adding, removing, copying or clearing content never changes either
playback assignment. Assign playback explicitly with **PLAY AS NORMAL** or **USE WITH MODIFIER**.
The trainer's Animator Studio page exposes the same modifier settings and a shortcut to its Library setup.

## Manage content

- Choose a destination in the left column. Its name and roles remain visible while browsing.
- **PACKS > CHOOSE TRICKS** opens a pack without adding it. Tick one saved animation or use ADD ALL.
  A whole pack can be partially selected: untick a member to exclude it only in this destination.
  ADD REMAINING restores local exclusions; REMOVE PACK removes that source's entries.
- **MY TRICKS** groups your own saves by trick, collapsed initially. Expand individual groups, use
  EXPAND ALL / COLLAPSE ALL, or search names, tricks, makers, source packs, pop heights and stances.
  Search reveals matching groups, but does not reopen groups the user folds afterwards.
- Each animation shows its native trick, pop, stance, modifier hint, and source pack(s). Its membership
  distinguishes an individual selection from inclusion through a whole pack. Preview and edit/view
  remain independent actions. A locked animation can be viewed and selected without being edited.
- The destination column shows source entries in playback-priority order. Expand a whole pack to
  change individual membership. Reordering, removal, membership changes and clearing support one
  level of UNDO / REDO, scoped to that playlist and rejected if a newer edit invalidated the history.
- PLAYLISTS on a saved animation opens a scrollable multi-destination chooser, including playlists
  it already belongs to. It can be added to several lists or removed from just one. No six-list cap.
- Copying preserves entries, exclusions and mode in a new inactive playlist. Renaming preserves its
  playback roles and available content undo. Deleting removes its assignments but keeps source packs
  and animation files. Clear/delete ask for confirmation; hidden controls cannot be activated through
  that confirmation or the modifier setup panel.

Whole-pack entries follow pack updates. Individual entries select only that save. A save included by
multiple sources is loaded once; its first source supplies the playback rules. Unticking a save removes
all paths that include it in that destination, creating local exclusions rather than altering the packs.
An explicit single selection can opt into a variant disabled in its source pack, while retaining that
pack's pop rule. Missing source packs do not silently become personal tricks with different rules.

Local exclusions are stored as `omitted` on whole-pack playlist entries. Existing playlists without
that field keep their previous behaviour. Older Studio builds ignore exclusions: retain a copy of
`ReSkate.Tricks/presets/playlists.json` when testing or rolling back this build. Names/assignments are
written together with rollback on replacement failure. Limits are 200 playlists and 2000 entries per list.

## Modifier setup

The Library's **ONE INPUT. TWO STYLES. > SET UP** panel explains the feature and provides its controls.
Keep a regular Kickflip animation in the normal playlist. Put a Dolphin Flip variation of that same
Kickflip input in another playlist and assign it to the modifier. Choose a controller button, for
example RB / R1. Hold before the move begins to use that playlist; without it, keep the normal animation.

Replace native button reserves that button during gameplay (preventing RB/R1's native darkcatch).
The Steam Overlay front hook and direct XInput share final filtering; Sony HID reports are filtered
selectively too. The button remains available to Studio's own reader and is released in menus, on focus
loss/disconnection, when disabled, or if the client stops refreshing its capture lease.

Tags remain optional author hints. CREATE PLAYLIST FROM MODIFIER HINTS collects saved tagged
animations into a new Modifier picks playlist and assigns it, keeping the local button preference.
It does not turn the bind on, alter the normal role, or continuously rewrite the collection later.

The route is latched per movement:

- Input tricks: at takeoff, as in the tested modifier prototype.
- Grinds: on rail entry. A late native grind identity refines that hold without resampling the button.
  A genuinely different named grind starts another choice.
- Manuals: on entering the native tail/nose/fakie manual context.
- Grabs: when the native grab family begins. A style change may select a better matching clip while
  keeping that hold's route. Releasing the modifier does not replace its animation mid-hold.
- Recognised moving native pieces: source recognition is deduplicated, then eligible saved variants
  of the recognised asset use the chosen route and playlist mode. Unknown or incompatible variants
  fall back to normal/native playback rather than forcing another action's animation.

Held moves keep their clips alive until a move boundary when the library changes. TEST IN GAME has
priority. Normal and modifier variants cycle independently. The trainer feed and diagnostics identify
which route supplied a selected animation. This extends the existing native recognisers; it does not
turn every animation-tree asset into a guaranteed live gameplay replacement.

## Native normal and switch versions

Fourth's native stance feature is integrated from the stance changes in `9523fec5` (#25).
Open a saved grind, manual, grab, invert, fingerflip or other native piece, then use **THIS TRICK >
STANCE > NORMAL / SWITCH** to change it, or **COPY TO** to keep both versions. The saved trick's
stance controls also create/open these versions. Native fakie uses a separate source animation,
so these controls offer two stances; whole tricks retain Normal, Fakie, Switch and Switch Fakie.

Copies mirror their source clip and keyed limb/board edits together, keep their modifier hint and
crossfade, and preserve the source asset and timing. Locked saves remain protected. Playback
prefers a version authored for the current foot within the selected route; it does not choose a
normal-playlist version over a modifier merely because the former has an exact-foot version.
Exact stance falls back to normal/native when no compatible modifier exists. Front foot may mirror
the modifier when its own-foot variant is missing. Native variant cycles are independent per route
and live lead foot. The rest of #25 remains held; see the [integration record](studio-library-integration.md).

## Verification

Run the preset build and CTest aggregate, then from a VS developer shell:

```powershell
python contrib/check_studio_modifier_steam.py
python contrib/check_studio_native_modifiers.py
python contrib/preview_studio_library.py build/library-preview
```

The preview tool compiles the production Library drawing functions with fixture data and runs
controller/keyboard activation checks. It writes PNGs at 1920x1080 and 1366x768 plus content, setup and
confirmation views; fixture covers/backdrop replace installed artwork. No game is launched or modified.
The native playback check compiles the production selectors against controlled pose scores; the CTest
animation suite separately checks the pose matching. These checks do not replace an in-game test.

Live acceptance on the exact DLL: normal versus modifier flips; normal/switch tail/nose manuals;
grind entry and transfers; grabs including a style change during one hold; release/press during a move;
missing modifier variants; a library edit or binding disable during a hold; native menus, F8, focus loss
and reconnection. Verify the original normal animations still play with the modifier OFF. Export
diagnostics after a mismatch so native selection, source clip and route can be compared.
