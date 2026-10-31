# Studio release acceptance

## Library .5 integration candidate (2026-11-01)

This is an experimental candidate, not a public-release approval. See the [integration record](studio-library-integration.md)
for the reconciled #27 fixes, held #25 work and automated evidence. Record the exact source/hash when checking these in game.

- [ ] Edit a playlist different from the default and button playlists. Its contents change without changing either playback assignment.
- [ ] Add one pack variation, remove it, restore it and Undo/Redo. Other playlists and the source pack remain intact.
- [ ] Preview from the picker and from Collection, including a locked save. Back returns to the same filters, groups, scroll and destination.
- [ ] Create a named playlist from Collection's chooser, cancel another creation, rename/duplicate, and confirm clear/delete. No unrelated list or animation file changes.
- [ ] Use a controller through playlist contents, picker, settings and button setup at 1366x768. No hidden controls activate under setup or confirmation.
- [ ] Resume paused playback, choose distinct default/button playlists and compare the same trick input with/without the bind.
- [ ] Compare `tre_low_wip` at Size 1/default, normal/switch, through catch and landing. Check an intentionally raised foot and an additive-only animation.
- [ ] Repeat modifier manuals, grind entries/transfers and grab style changes; release the button during a hold, edit its list and reconnect the pad. Missing variants fall back correctly.
- [ ] Compare local/remote playback online. Export a mismatch and confirm the ZIP contains summary, timeline and skeleton.
- [ ] Kickflip high and high fakie: lose matching near catch/landing without a jump or body/board time drift. Repeat on a long drop.
- [ ] Exact stance: normal/switch/fakie/nollie inputs select the intended physical leg, including nollie heelflip.
- [ ] Custom grind/manual into a flip and then native riding: the outgoing pose fades without snapping or applying old bends to the new move.
- [ ] Start with Custom tricks OFF, TEST IN GAME twice, reopen F8: it stays OFF. An explicit ON choice during the test stays ON.
- [ ] Inspector near -90 and +90 degrees Y: changing an unrelated channel preserves orientation.
- [ ] SAVE failure on a locked metadata file preserves the matching old clip and leaves the editor's unsaved changes available.

## Norms' #27 audit: checks to repeat on the reconciled candidate (2026-10-31)

The CTest suites cover helpers. Production-function harnesses also check native route selection and
playlist/report failure handling with controlled dependencies; they do not link the complete runtime
or prove visual gameplay. Repeat these checks in a temporary test installation.

- [ ] Pack update: a pack names a member that files as your own trick (`Kickfli/../p`). Update the pack twice. Your trick stays.
- [ ] Edit, then update: edit an editable pack's trick, press SAVE twice, then update the pack. Your edit stays first and the maker's copy is off. A SAVE without any prior edit adds no mark.
- [ ] Pack that names your trick: the pack does not claim the trick. You can share your own pack that holds it, and deleting the pack keeps the trick.
- [ ] Rename, then UPDATE: no second copy.
- [ ] Failed save: make `playlists.json` read-only and flip a pack in the ReSkate menu. A message shows and the overlay keeps running.
- [ ] Name Windows cannot spell: add an emoji folder to the tricks folder. The log says it is passed over and the overlay keeps running.
- [ ] ReSkate menu: set Playing to another playlist, then switch a pack ON. The pack goes in the playlist that is on.
- [ ] Room: switch Studio room off while in the room. The studio closes and opens again on the street, with the trick still open.
- [ ] REPORT A BUG after using the editor: the game stays in the level, and the zip holds `ReSkate.log`, the settings and the timelines. A freeze of about 7 s or more on the game or render thread drops the player out of the level.
- [ ] Shuv-it board: play smonk's Pop Shuv-it low 2 with SMOOTH on. The board does not turn over in the fades.
- [ ] Grab style: with melon and japan loops saved, change the style during a grab. The loop follows after 0.1 s.
- Deferred until #25's screen is integrated: COPY in MY TRICKS > SELECT > ALL > COPY. Copies retain duration, stance and metadata. The backend fix is present here.
- Deferred until that screen is integrated: bulk COPY stops after about 3 s and leaves unprocessed items selected; a second press continues without duplicating completed items.
- [ ] Controller: reach OPEN ANIMATION STUDIO with the D-pad, then try to reach HOW IT WORKS, REPORT A BUG and the rows below it.

## 1.0.10 review (2026-10-27)

Candidate integrates #14 (`2912763`, grind entry and drop timing) and #15 (`6d5aea8`, menu entry buttons), plus rail capture lifetime fixes. A rail lock capture belongs to one fade and one skater generation: starting a saved grind or another fade invalidates it, even if both fades start at full weight. Ground prediction smoothing resets when the selected trick changes.

Release validation must use the combined candidate. Build runtime/launcher, run the Studio/plugin CTest suites and `native_menu_tools`, then native Python and packaging tests. The manual workflow includes the menu tests; automatic push/PR builds remain disabled.

The user reports the original changes work in game. That is not a recorded visual test of the follow-up capture fixes. Before uploading the new DLL, retest flip-to-crooked/noseslide, another trick/fade on the same rail, respawn, normal/switch, flat pops, a long drop with a short catch-to-landing interval, and both new menu buttons. Check remote feet with another player. No live game or market publishing test was performed during this code review.

The evidence below records the earlier 1.0.9 audit; its old CI/upload status and checkboxes are historical, not the status of 1.0.10.

## 1.0.9 baseline

Audit baseline: `studio/ux-round-2` at `d9a3fc5` (PR #12 merged), on ReSkate 2.1.2. At the audit snapshot, main was still `c2b4551` / Studio 1.0.8 and PR #11 was open. Merge status must be checked again when choosing the final public source commit.

This is an acceptance record, not a claim that the game has already passed it. Tick a runtime item only after observing the exact candidate DLL and record its source commit/hash, tester and date in the PR or release notes.

## Automated evidence for the candidate baseline

- [x] Windows Release runtime and launcher compile with warnings treated as errors.
- [x] Eight CTest suites pass, including pack file rollback/cancellation and Fixed/Alternate priority regressions.
- [x] Native Python tests pass (21); packaging tests pass (13 run, one platform-dependent skip).
- [x] An existing saved custom animation loads with the production reader.
- [x] All 4,004 clips in the pinned native preview fixture load and have finite poses.
- [x] Inventory and readable-name checks pass for 19,339 assets; 25,511 inspector documents fit the production reader.
- [x] Current tracked-tree scan found no obvious GitHub-token/private-key/AWS-key patterns or tracked market account files (1,825 text files checked). This is not an exhaustive history/security audit.
- [ ] Validate the final packaged inventory/documents/names if distributing the optional prebuilt library; checks against a fixture are not a hash check of a different archive.
- [ ] Confirm successful upload and download of the final distribution. A quota-blocked CI run is not a successful artifact delivery.

Latest baseline CI: [run 40361192408](https://github.com/FourthStudio/ReSkate-Animator-Studio/actions/runs/40361192408) builds and passes regressions, then fails at artifact upload with the GitHub storage-quota error. The previous 23 generated ZIP artifacts have been removed; current storage being empty does not erase accumulated billing usage. Delivery remains blocked until a successful upload or another explicitly verified release distribution is available.

## In-game acceptance

- [ ] Clean install: official ReSkate 2.1.2 first, install the Studio DLL, launch, open/close F8 repeatedly, and allow the first library build to finish.
- [ ] Old data: back up the library, open an existing trick, edit/save/reopen, undo/redo, and restart without losing edits or selection.
- [ ] Preview: 360 Flip high and 360 Hardflip high; play, pause, scrub and change speed. Compare body, board, feet and landing with the intended animation.
- [ ] Gameplay: an obvious body edit and board edit on low/mid/high; verify the affected limb, board heading, onset and end timing.
- [ ] Stances: normal, fakie, switch and switch fakie; test Exact stance and Front foot separately.
- [ ] Selection: ACTIVE, no playlist on, switching the one active playlist, Fixed pack priority above/below Alternate, In turns and Shuffle over repeated attempts.
- [ ] Pop changes: ANY, an explicit pop with fallbacks, copied pop versions, a refined game grade, flat ground, stairs, gaps and landings.
- [ ] Online/AI: repeat selection and pop tests with other skaters present, including near-simultaneous pops. Check what a remote player actually sees.
- [ ] UI: custom key bindings, previews/back navigation, opening/closing the studio room during respawn/menu changes, and a large saved library.
- [ ] Market: browse and preview without linking an account, install/reinstall/update a pack, activate it through a playlist, and verify ownership after relinking a publishing team.
- [ ] File safety: use a disposable copied library for pack deletion/cancellation and update failure testing. Confirm retained files, Recycle Bin recovery and recovery error messages.
- [ ] Publishing: use an explicitly approved test pack/team; inspect the package and verify the real server result. Automated tests do not perform a live publish.
- [ ] Diagnostics: REPORT A BUG creates an inspectable ZIP with the relevant build/trick/stance context; no account files are bundled.

## Public delivery

- [ ] PR #11 and required follow-up fixes are merged into the intended public source branch; its tree matches the built DLL.
- [ ] Gameplay acceptance above is recorded. No unverified feature is described as stable or implemented.
- [ ] README, generated package instructions and release notes match the shipped controls and limitations.
- [ ] Publish a persistent release download with source commit, version and SHA256. Include license/third-party notices as appropriate to the distribution.
- [ ] Keep the game, user data, credentials and development research mods out of the distribution. Review which native data is included if offering the optional library.
- [ ] Check that a fresh user can obtain the download and corresponding project source with the access intended for the public release.

Until these gates are complete, call the build a release candidate. Keep a known working DLL/library backup for rollback.
