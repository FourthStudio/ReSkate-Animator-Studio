# ReSkate Animator Studio

Edit skate animations in game, build your own trick packs, and choose how they play.

Animator Studio is a modified **ReSkate runtime**, built by 22sh, Fourth Studio, Norms and Steez, and tested by THE CREW. Open it with **F8** to browse native animations, edit body and board keys, make stance variants, and share packs through the market.

> **Version 1.1:** Animator Studio on ReSkate 2.1.3. See the [1.1 release notes](contrib/release-1.1.md) and [integration record](contrib/studio-library-integration.md).
>
> This is a fan project, unaffiliated with Electronic Arts or Full Circle. You need your own copy of skate. on Steam. The game is not included.

## Install

**For now, the Animator Studio is one file: a `ReSkate.dll` that takes the place of ReSkate's own.** You swap the DLL and turn ReSkate's updates off. Once the ReSkate team brings the studio into ReSkate itself, none of this is needed: you will just update ReSkate.

You need Windows, your own copy of skate. on Steam, and a working **ReSkate 2.1.3** install (the game build ReSkate pins is Steam build **25414733**, internal build 20260929; let the ReSkate launcher set it up rather than copying the latest Steam executable over it).

1. Install **ReSkate 2.1.3** from its [release](https://github.com/Dingo-Shenanigans/ReSkate/releases/tag/v2.1.3) and start it once the normal way, so it is set up and up to date.
2. In the launcher, open **Settings > LAUNCHER** and turn **Install ReSkate updates** off. While it is on, the launcher puts the official `ReSkate.dll` back.
3. Close the launcher and the game.
4. Open your ReSkate folder (the one with `Skate.exe` and `ReSkateLauncher.exe`). Back up `ReSkate.dll`, and your `ReSkate.Tricks` folder if you already have one.
5. Download `ReSkate.dll` from the [latest Studio release](https://github.com/FourthStudio/ReSkate-Animator-Studio/releases) and put it in that folder, replacing the old one.
6. Start the game from `ReSkateLauncher.exe` as usual. Once it has loaded it says **Modified build: AnimatorStudio**. Press **F8** (or hold **L3 + R3**) to open the studio. You can also use **Open Animation Studio** under **Mod Options > PLAYER**, or on the ReSkate menu's **SKATER** page, once your skater is in game and Park Editor is closed.

On the first start the studio builds its trick library from your own game: about a minute and 1.6 GB, and the game's files are only read, never changed. **TRICKS** shows how far it has got.

**Updating the studio:** close the game and replace `ReSkate.dll` with the new one. Your tricks, packs and settings in `ReSkate.Tricks` stay.

**Going back to the official ReSkate:** turn **Install ReSkate updates** back on and start the launcher: it puts the official files back by itself (or copy your backed-up `ReSkate.dll` back). Your `ReSkate.Tricks` folder is kept.

## Make your first custom trick

1. Open **TRICKS**, search for an animation, and choose a native trick/pop to edit.
2. Scrub or play the timeline, move or rotate body/board joints, and add keys. Use key blend-in/out to ease edits into the game's pose. Undo/redo and save are available in the editor.
3. Save with a name of your own. Preview speed affects watching the timeline; gameplay uses normal timing.
4. In **LIBRARY > PLAYLISTS**, create or select a playlist and choose **+ ADD ANIMATIONS**. Add your save, choose **SET AS DEFAULT**, and **RESUME** if playback is paused.
5. Close the studio and perform the trick in game.

**Saving alone does not activate a trick.** One default playlist plays normally; an optional button playlist supplies alternate animations while its modifier is held. Also check **Custom tricks in game** in settings. **TEST IN GAME** is a temporary override until you reopen the studio or restart; Library playlists keep the selection for later sessions.

The Library shows which playlist you are editing separately from the default and button playlists.
**SET AS DEFAULT** assigns a list; **RESUME** enables paused playback. **TEST IN GAME** restores your
previous playback setting when you return. See the [Library guide](contrib/library-workspace.md).

### Playlists, packs and stances

- **Fixed:** the highest playlist entry for a trick/pop/stance/takeoff slot wins. If that winning pack's slot uses Alternate, its variants can alternate; lower-priority packs stay out of that slot.
- **In turns:** variants play in playlist order, one attempt at a time.
- **Shuffle:** variants are chosen randomly, avoiding the previous choice when alternatives exist.
- Packs group animations for distribution; playlists choose which packs and tricks play.
- **Exact stance** uses the authored leading-foot and fakie combination. **Front foot** can mirror variants when the other foot leads. Make separate normal, fakie, switch and switch-fakie versions when each should have its own style.
- **ANY pop** can generate low/mid/high versions in memory. An imported pack can supply fallback versions where no selected trick covers another height; your own single-pop saves keep their explicit height. Use **COPY TO LOW, MID AND HIGH** to edit those versions separately.

Editor shortcuts can be changed under **SETTINGS > STUDIO > EDITOR KEYS**. A trick can override the global crossfade through its own settings.

## Market and sharing

Browse and preview animation packs in **MARKET**. Install a pack, then add it to an enabled playlist to use it in game.

To publish your own pack, open **MARKET > ACCOUNT / SIGN IN**, enter your Thunderstore team name and its service account token, and link the team. Publish from the pack's sharing controls. No token is needed to browse or install packs.

The token is stored in `ReSkate.Tricks/market/account.key`, encrypted with Windows DPAPI for that Windows user. `account.json` stores the team name. If the token was deleted or cannot be read after moving PCs, sign out and link the team again. Never include account files when distributing a DLL or pack.

Imported packs retain their maker's ownership settings. A verified publishing team can reclaim its own packs made on another PC. Use the studio's export/share controls rather than zipping your whole `ReSkate.Tricks` folder.

## Scope and current limits

- Native inventory includes clips, graph states, transitions and parameters. An indexed item is not necessarily editable or bound to live gameplay.
- Saved manual, grind and grab loops can play through recognized native movement contexts. Other moving native pieces require successful pose recognition; an indexed transition or graph parameter is not a guaranteed live replacement.
- Gameplay follows the native animation clock, or continues at source pace when matching is lost. There is no separate catch slowdown or airborne foot magnet; **CATCH HOLD** and **DROP PREVIEW** controls are removed. The optional **CATCH** marker remains on the timeline.
- Into an unmodified grind, contact protection hands the lower body back to the native board. A compatible saved grind loop is selected separately from that handoff.
- Older goofy saves with incorrect stance metadata are not automatically repaired in every case; recreate an affected version if its tags or limb behavior are wrong.
- Windows is the tested build target. Studio operation on Proton, Steam Deck and macOS is not release-validated.

## Your files and recovery

Everything below is relative to the folder containing `Skate.exe` unless `RESKATE_ANIMATION_STUDIO_DIR` overrides the Studio library location.

| Data | Location |
|---|---|
| Saved tricks | `ReSkate.Tricks/<name>/<name>.json` and `.clip`; older loose pairs remain readable |
| Packs and playlists | `ReSkate.Tricks/presets/` |
| Studio settings | `ReSkate.Tricks/studio-settings.json` |
| Native library | `ReSkate.Tricks/library/` |
| Market settings and account | `ReSkate.Tricks/market/` |
| Bug report ZIPs | `ReSkate.Tricks/bug reports/` |
| Interrupted update recovery files | `ReSkate.Tricks/.pack-transactions/` |

Failed pack updates restore the old files when possible; failed recovery keeps backups and reports their location. Do not discard recovery files after an interrupted update. Automatic crash recovery during a multi-file commit is not implemented. Back up the library before testing a new build.

Deleting your own pack keeps its tricks. Deleting an imported pack recycles unshared member tricks; failed or cancelled recycling stops the operation and keeps remaining files. Some files may already be in the Recycle Bin after a partial shell operation.

## Report a problem

Use **SETTINGS > STUDIO > REPORT A BUG** and attach the resulting ZIP, with the trick name, pop, stance, and whether the problem occurs in the preview, gameplay, or both. Include whether you were offline or online.

The ZIP is created locally; sending it is your choice. It contains recent diagnostics, relevant animation data, settings, build details and the end of the game log. Standard export does not collect the market's `account.key`. ReSkate's automatic crash reporting is separate: control **Send crash reports** in launcher settings, or use `RESKATE_CRASH_REPORTING=0`.

## Build and contribute

Use Visual Studio 2022 with the C++ desktop workload, CMake and Ninja. Python 3.11+ is used for tests/packaging; an installed Cargo toolchain enables the optional Spotify helper. From a Developer PowerShell:

```powershell
cmake --preset ci-windows
cmake --build build/ci-windows --config Release --target dingosdk_runtime dingosdk_launcher --parallel 4
```

Outputs are in `build/ci-windows/Release/`. The preset stamps Studio 1.1.0 / ReSkate 2.1.3 and disables binary auto-update. Dependencies are vendored; optional helper/test setup is described in the [build and validation guide](contrib/studio-tester-build.md).

See the [Studio implementation notes](contrib/animator-studio.md), [repository structure](STRUCTURE.md), and [release checklist](contrib/studio-release-checklist.md). Keep gameplay changes, tests and UI descriptions together; use focused PRs and wait for review before merging.

## Credits and license

Built on [ReSkate](https://github.com/Dingo-Shenanigans/ReSkate) and its contributors' work. Third-party dependencies and fonts retain their licenses; see [External/README.md](External/README.md).

**Built by** 22sh, Fourth Studio, Norms and Steez.

**THE CREW** (testing): leaf.bro, Setsu, renzoh., smonk, philsteezy, Heatz, Bonksz and Kanga.

Project source is licensed under [GPL-3.0](LICENSE). The license does not grant rights to skate. or EA content.
