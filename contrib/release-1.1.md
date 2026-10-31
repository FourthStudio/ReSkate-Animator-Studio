# animator studio 1.1

A new Library, alternate trick inputs, and the latest editor, Market and playback fixes from Fourth, Norms and 22sh. Built for **ReSkate 2.1.3**.

## what's new

- Redesigned Library: clear editing vs playback assignments, searchable animations and packs, collapsible trick groups, individual pack variations, named playlists and reliable Preview/Back navigation.
- Trick modifiers: assign an alternate playlist to a configurable controller button. Keep a normal kickflip and use button + kickflip for a dolphin flip. Also supports grinds, manuals and grabs, with normal playback fallback when no compatible alternate exists.
- Guided button setup in the Library and shortcuts in the trainer. Reserve the button's game action to avoid the native RB/R1 darkcatch. Modifier tags remain optional author hints; build a playlist from tagged saves or add any animations manually.
- Normal/switch versions for native animations, including manuals, grabs and grinds. Native fakie moves remain separate animations.
- Fourth's magazine-style Market, bulk SELECT/COPY tools and loop-pose copying to its entry/exit animations. Handplants preview on a quarter pipe; grinds preview on a scaled rail.

## fixes

- Source-time playback, late trick recognition, native stance selection and pose handoffs address jumps, wrong-leg edits and mismatches between the editor and gameplay.
- Body-size compensation addresses floating feet in whole-source fallback poses, including the smallest skater size. Gameplay limits retain authored leg bends while constraining the upper body.
- Safer pack updates preserve personal and locally edited tricks. Save/copy operations replace clip and metadata together with rollback on failure.
- Failed playlist writes preserve Undo/Redo. TEST IN GAME restores the previous playback setting. Inspector rotations stay consistent near both 90-degree poles.
- Bug reports save in the background and keep 25 seconds of gameplay context, plus logs and settings, without uploading automatically.

## install or update

1. Use a working **ReSkate 2.1.3** installation. Keep its pinned game build; do not replace Skate.exe with a newer Steam executable.
2. In the launcher, turn **Settings > LAUNCHER > Install ReSkate updates** off if using the official launcher.
3. Close the game and launcher. Back up `ReSkate.dll` and `ReSkate.Tricks`.
4. Download **ReSkate.dll** from this release and replace the DLL beside `ReSkateLauncher.exe`.
5. Launch through `ReSkateLauncher.exe`, then press **F8**. The Studio can build/update its native library from your installed game; no animation-library download is required.

Your saved animations and packs stay in `ReSkate.Tricks`. In **LIBRARY > PLAYLISTS**, use **+ ADD ANIMATIONS**, then **SET AS DEFAULT** and **RESUME** if paused. Use **BUTTON SETUP** for an alternate playlist. The controller opening shortcut, including L3 + R3, can be changed or disabled in **Settings > STUDIO > Controller combo**.

Optional integrity check in PowerShell:

```powershell
Get-FileHash .\ReSkate.dll -Algorithm SHA256
```

Compare with `ReSkate.dll.sha256`. When rolling back, restore your backed-up DLL and playlist/binding files together: older builds do not understand playlist exclusions.

If something looks wrong, reproduce it, then use **REPORT A BUG** and include the animation name, stance, body size and what happened. The ZIP stays local until you share it.

[installation and usage](https://github.com/FourthStudio/ReSkate-Animator-Studio#readme) · [heatz's tutorial](https://www.youtube.com/watch?v=cOBGvT7y45I) · [discord](https://discord.gg/zjg262qvw6)
