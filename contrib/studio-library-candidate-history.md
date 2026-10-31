# Library integration candidate, 2026-10-31

This is a test candidate, `1.0.39-library.5+reskate.2.1.3`, on
`22sh/release-library-integration`. It combines the Library/modifier workspace with reviewed team
work and the native normal/switch feature requested separately by Fourth. It is not Studio 1.0.55
and does not claim that every change in #25 is present or verified in game.

## Source reconciliation

GitHub branches, all PR heads and recent PR discussions were fetched on 31 October. Main was
`f3c43590`; #22 was `37a85dbf`; Fourth's #25 was `b5a14969` (19 commits after #22).
A refresh at 17:20 UTC found those heads unchanged and a new #27 at `d218dc40`, based on #25.

| Work | Treatment in this candidate |
|---|---|
| #22 / Studio 1.0.39, ReSkate 2.1.3 | Existing base, retained with its history. |
| #23 / board quaternion winding | Already included through #24; #23 was closed in favour of #24. |
| #24 / grab style and regression aggregate | Already included; melon-to-japan selection and all enabled test dependencies retained. |
| #19 / Studio camera FOV (`6fcd0986`) | Merged locally with Norms' original commit. The Studio uses the game's FOV; the Freecam slider still applies outside it. |
| #20 / named Studio launcher and optional official channel | Four feature commits cherry-picked with Norms' authorship. Resolved duplicate build-name definitions and kept the existing modified-build notice for DLL-only users. |
| #21 / earlier 2.1.3 integration branch | Its runtime compatibility is superseded by the #22 base; do not re-merge its older editor snapshot. Recover the missing font provenance metadata from its audit. |
| #25 / native normal/switch (`9523fec5`, stance portion) | Selectively ported by request. Editing, copying, stance labels and in-game selection reconciled with modifier routes and playlist modes; not a merge of #25. |
| #25 correctness follow-up (`88a17022`, `8f65e226`, `d77b6f1a`, `b5a14969`) | Library .5 reconciles native takeoff flags, source-time playback, pose handoffs and upper-body gameplay limits. The pose fade uses the previous frame's native baseline to avoid freezing untouched native joints. These are targeted ports, not a merge of #25. |
| Remaining #25 features and cleanup | Still separate: magazine Market UI, quarter-pipe preview work, bulk SELECT/COPY screen, loop-copy workflow and unrelated cleanup. No upstream PR/main merge was performed. |
| #27 / Norms' 1.0.55 audit (`d218dc40`) | Fix commit ported and reconciled onto this branch: pack ownership/update protection, invalid clips, failed saves, background reports, installed-pack rename tracking, room reopen, trainer navigation and held-loop stability. The bounded COPY backend and serialization fix are included; its SELECT/COPY screen remains held #25 UI. The unrelated `96db5b74` cleanup was not imported. |

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
- 64/64 CTest suites pass with launcher, Studio and plugin checks enabled.
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

The full #25 cannot be blindly merged: the earlier preflight already reported conflicts in `CMakePresets.json`,
`anim_core_tests.cpp`, `anim_studio.cpp` and `animation_studio_overlay.cpp`. When Fourth clears it,
retain the new Market and editor work, route its playlist actions through the destination APIs, retain
the modifier/held-grab tests, and reconcile native stance preference in the shared selectors instead
of adding a second unfiltered scan over all saved pieces. Native Switch is already ported here.

Fourth's #25 description says its latest lost-air clock fix was not yet checked in game. Library .5
now includes the targeted correctness ports listed above; remaining feature/UI work stays separate.
The combined DLL still needs in-game acceptance before a public-release go-ahead.

## Library .3 and Size 1 report

The Library now uses playlist contents, a dedicated add picker, Collection browsing and separate
settings/setup pages. Its top strip shows what plays independently of what is being edited.
The actual Preview/Back functions bookmark and restore the new route, filters, expanded groups,
scroll and controller focus. Named creation happens before content is added; destructive actions
use an explicit destination and block underlying input. Paused playback has a direct Resume action.

The Size 1 report reproduced a body-scale error in whole-source fallback playback: live scale 0.92
versus source 1, adding roughly 6 cm at landing. The shared core now fits hip placement to live body
dimensions while retaining bone scales, keys and asymmetrical foot poses. See [body-size notes](studio-body-size.md).
Report ZIPs preserve their summary, timeline and skeleton before large pose files exhaust the budget.
The .4 reconciliation also includes #27's asynchronous reporting and log/settings priority.

Validation for .3: 64/64 CTest suites, 21 native Python tests, 13 packaging tests (one optional skip),
production Steam/native selection checks and the new Library interaction harness pass. The latter
executes the production Preview/Back route, failed/locked preview, membership isolation, named
creation, search/folding, setup/confirmation input blocking and paused-playback recovery. Rendered
screens were inspected at 1080p and 768p. The 3,200-save fixture remained responsive in the harness.
Core tests cover five body scales and both live scale representations; the actual core also replays
the private captured poses. These checks do not replace in-game acceptance of this exact DLL.

## Norms reconciliation in Library .4

GitHub was refreshed at 17:33 UTC: #27 remained `d218dc40`, #25 `b5a14969`, and #22/main were
unchanged. The fix commit was applied independently of #25 and its preceding dead-code cleanup.
Conflicts were resolved against this branch's actual runtime rather than importing missing editor,
Market-category or loop-copy screens. This keeps native stance conversion, body-size placement,
modifier routing, playlist-local exclusions and the new Library/Preview flow intact.

- Failed playlist writes return failure without disabling the overlay. Library edits, Undo/Redo,
  duplicates and legacy additions respect that result before advancing history or opening a copy.
- Pack updates keep locally edited saves and put the maker's update beside them, off. A repeated
  SAVE also keeps the in-memory local-edit marker. Older edits not marked by a supporting build
  cannot be identified retroactively; keep backups when testing updates to existing packs.
- Reports run off the game/render threads, allow one job at a time, and include logs/settings before
  size-sorted diagnostic files. A failed callback, launch or completion notification releases the gate.
- Held grabs keep unnamed pose-selected loops. A compatible better style must persist for 0.1 s;
  filtering for modifier route and stance happens first. An incompatible mirrored candidate leaves
  exact-stance fallback running. Normal and modifier counters and clocks remain independent.
- The trainer explicitly changes the playing/default playlist while preserving the playlist being
  edited. This branch's existing pack PLAY action retains its default-playlist semantics.

64/64 CTest suites pass, including Norms' pack/clip/listing/serialization tests and our size tests.
The production native harness additionally covers style flicker/debounce, exact-stance fallback,
unnamed grabs and modifier handoffs. A new production storage/report harness injects failed writes,
checks Undo/Redo and destination isolation, and runs real background jobs through completion and
exceptions. It is included in the manual tester workflow. Existing Steam, Python and Library checks
also pass. These harnesses use controlled data; the combined DLL still needs live-game acceptance.

The remainder of Fourth's #25 is still held. This is a tested integration candidate, not a merge of
that branch or a public-release approval. The bulk-copy backend is ready for its future screen, but
there is no new SELECT/COPY UI in this build. The #27 in-game checklist marks those rows accordingly.

## Release hardening in Library .5 (2026-11-01)

The 22:29 UTC activity refresh found #25 and #27 heads unchanged. The code audit then reproduced
these remaining correctness issues and reconciled the corresponding Fourth fixes without merging
the held PR:

- **Lost high-pop clock:** extrapolating the game's phase could jump from 0.58 s to the clip's landing
  in one frame. Continuation now uses source seconds and eases short of landing until touchdown.
- **Separate catch slowdown:** gameplay no longer retimes body keys beside the native board's clock
  on drops. Old catch metadata and hidden drop-preview tools remain readable. Raised-ledge ground
  prediction stays active; the obsolete airborne source-to-board magnet is removed.
- **Joint limits:** Studio posing keeps full limits. Gameplay limits the upper body while preserving
  the legs' keyed bends, including outgoing poses and the gameplay foot-reference path.
- **Exact stance:** native mirror/fakie flags determine takeoff stance when readable. Each unavailable
  flag retains its own fallback, including legitimate false values.
- **Handoffs:** outgoing custom edits no longer get applied to another move's native pose. The last
  drawn pose blends into the next pose on edited joints. The previous native baseline determines
  ownership, so untouched native joints keep moving. Interrupted fades remain continuous; native-only
  rail entry retains its board-relative foot handling. Input intent alone no longer releases the legs.
- **Late recognition:** selecting another clip clears the previous clip's sampled time and landing
  history while retaining the real takeoff anchor. Otherwise a late flip could inherit an ollie's
  playhead and skip straight past its early keys.
- **Saved data:** metadata and clip replacements now use the existing rollback transaction together,
  both for SAVE and the bounded COPY backend. Export file replacement keeps the previous file on
  failure. A Windows regression locks metadata after the old clip is backed up and verifies rollback.
- **Temporary tests:** TEST IN GAME enables playback without persisting that override, including
  unrelated settings saves. Returning restores the prior preference; an explicit playback choice wins.
- **Inspector angles:** near +/-90 degrees the old gimbal threshold changed the reconstructed rotation,
  and the negative pole used the wrong sign. Double-precision matrix terms and an atan2 decomposition
  preserve the actual rotation. Regressions cover both sides and both exact poles.

The Library now distinguishes assignment from active playback and reports paused/inactive button
playlists explicitly. Diagnostics record the gameplay clock, limits and pose-transition policies.
Automated checks cover these cases, but do not prove physical controller, rail contacts, native
recognition or remote rendering in a running game. Complete the .5 checklist on the exact source/hash.
