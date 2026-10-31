# Animator Studio 1.1 integration

The `22sh/release-1.1` integration brings the tested Library .5 work together with the complete histories of Fourth's #25 and Norms' #27, plus the earlier Studio/launcher work, for a PR into `main`. The release build is Studio `1.1.0+reskate.2.1.3` on ReSkate 2.1.3.

## Team work included

| Work | Integration |
|---|---|
| #22 (`37a85dbf`) | Studio 1.0.39 / ReSkate 2.1.3 base. |
| #19 (`6fcd0986`) | Game camera FOV in the Studio. |
| #20 (`6acc8c47`) | Named Studio launcher channel and optional one-way official ReSkate channel; keep the worker snapshot/stale-result guards. |
| #21 (`ce058bfd`) | Earlier 2.1.3 integration history, with the newer runtime and font provenance retained. |
| #24 (`aba52107`) | Board quaternion winding, held-grab style selection, regression aggregate and release fixes. |
| #25 (`b5a14969`) | Magazine Market, normal/switch native animations, quarter-pipe handplant previews, fixed preview rail, SELECT/COPY, loop-copy-to-ins/outs, quiet studio and longer reports. |
| #27 (`d218dc40`) | Pack ownership/update safety, local-edit preservation, validation, failed-write handling, background reports, installed-pack rename and held-loop stability. |
| Library .5 (`0522ef23`) | Guided Library, exact Preview return, separate default/button assignments, modifiers for tricks/grinds/manuals/grabs, size compensation, timing/stance/handoff fixes and safe saves. |

The earlier prototype clock, gizmo and held-move branches are superseded alternatives. Existing bugfixes-3/open-from-player-tab/release-audit branches are already ancestors. They are not reapplied over their newer replacements.

## Conflict decisions

- Keep the new Library's explicit destination APIs. Editing a playlist never silently changes which list plays; the trainer's PLAY switch edits the default playback list.
- Native stance preference stays inside the selected modifier/default route. No extra all-saves scan can replace a modifier with a normal animation. Family/style/stance filtering precedes route priority and the held-style debounce.
- Keep the tested source-time fallback, late-recognition clock reset, previous-frame pose baseline, body-size placement fit and upper-body gameplay limits.
- Retain the active render contact/handoff helpers and regression coverage used by this runtime. Upstream cleanup cannot delete dependencies still used by the combined implementation.
- All SAVE/COPY/loop-copy clip-and-metadata writes use the paired rollback transaction. Temporary TEST IN GAME never persists its forced playback preference.
- Imported-pack pop fill-ins rank after explicit and ANY-pop matches. An incompatible height remains rejected.
- The manual tester workflow remains opt-in. The release contains the DLL, not game assets, private captures or account files.

## Validation and acceptance

The user reported that Library .5 works in game and authorized this full integration and main merge. That is a smoke-test report for `0522ef23`, not a completed per-scenario check of the newly combined 1.1 DLL.

Build and regression results for the combined source are recorded in the release PR and accompanying build-info. Verification includes the Release runtime/launcher with warnings as errors, all enabled CTest suites, native and packaging Python tests, the production Steam/native/storage harnesses and production Library drawing/navigation at 1080p and 768p. Added core cases cover quarter-pipe alignment and loop entry/exit pose copying.

No game installation is overwritten or started during release preparation. Visual checks for the exact new DLL remain listed in [release acceptance](studio-release-checklist.md). Prior candidate evidence is preserved in [candidate history](studio-library-candidate-history.md).
