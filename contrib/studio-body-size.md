# Whole-source playback and skater body size

The 31 October Size 1 report reproduced floating feet on `tre_low_wip`. The report selected
`360 Flip mid 2` while playing a low-pop trick. That whole-source fallback keeps the game's live
body scales but takes placement and rotations from the saved clip. The captured render skeleton
had a 0.92 body scale; its source clip used 1. Keeping the source hip placement shortened the reach
of both legs and introduced roughly 5–6 cm of additional height during the landing.

`apply_authored_edit_layer` now fits whole-source body placement to the live dimensions. After
applying the authored pose and keys, it compares the ankles' midpoint against the same pose with
source body scales. The difference translates the hips in their parent frame. It preserves live
scales, bone lengths, rotations, asymmetrical foot poses and board keys. It does not force both feet
onto the board. Pure additive edits are unchanged. The correction fades with source/animation
ownership and is disabled when either leg or the hips has been returned to the game.

Core regressions cover scales 0.85, 0.92, 1, 1.08 and 1.15, root yaw/scale, independent knee edits,
partial/zero weight, released hips, and the game's thigh-scale versus render hip-scale placement.
Replaying the captured poses through the actual corrected core removes the reproduced size offset.
The private capture and native clip remain outside the repository and test package.

This fixes the identified body-size contribution, not every possible preview/gameplay discrepancy.
Joint limits, clip/pop selection, shoe cosmetics and landing timing remain separate factors.
Acceptance on the exact DLL must compare the same save at Size 1 and the default size, normal and
switch, through takeoff, catch, landing and release. Also check an intentionally lifted foot and a
pure additive animation. Do not claim a live-game fix from the offline replay alone.

Large bug reports now reserve space for `summary.json`, `timeline.csv` and `skeleton.csv` before
pose CSVs exhaust the existing ZIP budget. The received ZIP omitted these; diagnosis required
reading the complete local dump. Library .4 integrates Norms' #27 background report generation:
logs and settings are included first, followed by diagnostic files ordered from smallest to largest.
