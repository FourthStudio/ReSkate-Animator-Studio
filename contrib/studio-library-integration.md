# Library integration candidate, 2026-10-31

This is a test candidate, `1.0.39-library.2+reskate.2.1.3`, on
`22sh/release-library-integration`. It combines the Library/modifier workspace with reviewed team
work and the native normal/switch feature requested separately by Fourth. It is not Studio 1.0.55
and does not claim that every change in #25 is present or verified in game.

## Source reconciliation

GitHub branches, all PR heads and recent PR discussions were fetched on 31 October. Main was
`f3c43590`; #22 was `37a85dbf`; Fourth's #25 was `b5a14969` (19 commits after #22).

| Work | Treatment in this candidate |
|---|---|
| #22 / Studio 1.0.39, ReSkate 2.1.3 | Existing base, retained with its history. |
| #23 / board quaternion winding | Already included through #24; #23 was closed in favour of #24. |
| #24 / grab style and regression aggregate | Already included; melon-to-japan selection and all enabled test dependencies retained. |
| #19 / Studio camera FOV (`6fcd0986`) | Merged locally with Norms' original commit. The Studio uses the game's FOV; the Freecam slider still applies outside it. |
| #20 / named Studio launcher and optional official channel | Four feature commits cherry-picked with Norms' authorship. Resolved duplicate build-name definitions and kept the existing modified-build notice for DLL-only users. |
| #21 / earlier 2.1.3 integration branch | Its runtime compatibility is superseded by the #22 base; do not re-merge its older editor snapshot. Recover the missing font provenance metadata from its audit. |
| #25 / native normal/switch (`9523fec5`, stance portion) | Selectively ported by request. Editing, copying, stance labels and in-game selection reconciled with modifier routes and playlist modes; not a merge of #25. |
| Remainder of #25, including joint limits and lost-air clock | Held following Fourth's request. No remote merge, partial limit fix or clock rewrite was made. |

The older held-moves and game-clock prototype branches, old gizmo test branch and switch-playback
merge branch are historical alternatives, not newly released work. Their changes must be assessed
against the newer #22 implementation rather than merged wholesale over it. Existing merged menu,
rail-feet and release-review branches are already ancestors of this candidate.

## Integration fixes

The launcher used to read mutable update choices on its worker, and a channel change could leave
a stale update result behind the button. It now captures choices before starting the worker. The
install path rejects a result fetched for different channel/pre-release settings, disabled updates,
a build with binary updates disabled, or `--no-update`; it rechecks instead. Status facts retain the
channel that supplied their config. This adds regression coverage for both switch directions.

The experimental preset keeps automatic binary replacement OFF. It points at this project's
releases and disables crash uploads to upstream ReSkate's endpoint. Shipping a functioning automatic
update channel later requires an explicit release manifest and separate acceptance of that launcher;
the DLL test package does not activate it.

Native switch selection stays within the chosen normal/modifier route and prefers an authored
current-foot save over mirroring another save of the same asset. Native variant counters are separate
for each route and foot. Copies retain the existing whole-trick stance behaviour and never apply a
whole-trick fakie layer to a native piece. The native stance work is based on Fourth's `9523fec5`.

## Validation

- Release runtime and launcher build with warnings as errors.
- 63/63 CTest suites pass with launcher, Studio and plugin checks enabled.
- 21 native Python tests pass; 13 packaging tests run, with one optional platform fixture skipped.
- The Steam input reproducer and production native selection harness pass. The latter exercises
  native normal/switch choices, missing variants, route isolation, clock continuity, native family/style
  matching and independent variant counters.
- Production Library drawing/navigation checks pass at 1920x1080 and 1366x768, including a native
  stance copy control and rejection of nonexistent native fakie controls. These use fixture data.
- Core tests exercise native stance copies for manuals, grabs, grinds, inverts, fingerflips and other
  pieces, both rider stances, mirrored limb/board keys, rotation-only ownership, metadata retention,
  normal-to-switch-to-normal and existing four-way whole-trick copies.

No installed game is modified or launched by this integration. The exact candidate DLL still needs
gameplay acceptance: normal/switch manuals, grabs and grinds in both playlists, modifier release and
fallback, grind transfers, existing air tricks, online selection, and Library editing while a move runs.

## Remaining release decision

The full #25 cannot be blindly merged: the preflight reports conflicts in `CMakePresets.json`,
`anim_core_tests.cpp`, `anim_studio.cpp` and `animation_studio_overlay.cpp`. When Fourth clears it,
retain the new Market and editor work, route its playlist actions through the destination APIs, retain
the modifier/held-grab tests, and reconcile native stance preference in the shared selectors instead
of adding a second unfiltered scan over all saved pieces. Native Switch is already ported here.

Fourth's #25 description says its latest lost-air clock fix was not yet checked in game. Its joint-limit
and high-pop fixes are still outside this candidate. Until those held changes are resolved and the
combined DLL is tested in game, this is not the final public-release go-ahead.
