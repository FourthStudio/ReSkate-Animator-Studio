# Studio release acceptance

## 1.0.55 audit: checks to repeat in game (2026-10-31)

No automated test links `anim_studio.cpp`. The tests cover the helpers, but a test still passes if a call to a helper is removed. Repeat these checks in game when you change this code. Use a temporary folder with `RESKATE_ANIMATION_STUDIO_DIR`.

- [ ] Pack update: a pack names a member that files as your own trick (`Kickfli/../p`). Update the pack twice. Your trick stays.
- [ ] Edit, then update: edit an editable pack's trick, press SAVE, then update the pack. Your edit stays first and the maker's copy is off. A SAVE with no change adds no mark.
- [ ] Pack that names your trick: the pack does not claim the trick. You can share your own pack that holds it, and deleting the pack keeps the trick.
- [ ] Rename, then UPDATE: no second copy.
- [ ] Failed save: make `playlists.json` read-only and flip a pack in the ReSkate menu. A message shows and the overlay keeps running.
- [ ] Name Windows cannot spell: add an emoji folder to the tricks folder. The log says it is passed over and the overlay keeps running.
- [ ] ReSkate menu: set Playing to another playlist, then switch a pack ON. The pack goes in the playlist that is on.
- [ ] Room: switch Studio room off while in the room. The studio closes and opens again on the street, with the trick still open.
- [ ] REPORT A BUG after using the editor: the game stays in the level, and the zip holds `ReSkate.log`, the settings and the timelines. A freeze of about 7 s or more on the game or render thread drops the player out of the level.
- [ ] Shuv-it board: play smonk's Pop Shuv-it low 2 with SMOOTH on. The board does not turn over in the fades.
- [ ] Grab style: with melon and japan loops saved, change the style during a grab. The loop follows after 0.1 s.
- [ ] COPY: in MY TRICKS, press SELECT, ALL and COPY. Each copy shows its stance and + PLAYLIST, and the log has no `metadata not loaded`.
- [ ] COPY limit: with several hundred of your own tricks, press SELECT, ALL and COPY. The studio stops after about 3 s and says how many stay ticked. The game stays in the level. COPY again copies those, not the first ones again.
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
