# animator studio 1.1.1 (hotfix)

Fixes the feet going through the board and the offset joints that came with 1.1, in packs and new tricks alike. Built for **ReSkate 2.1.3**. Your packs and saved tricks need no changes.

## fixed

- **Feet through the board.** In 1.1, when a trick started, every joint it keys (legs, hips) was held at the pose from the frame before the pop for up to half the Crossfade: 0.4 s at 0.8, which many packs use. Meanwhile the game popped and flipped the board, so the board went up while the legs stayed behind. Tricks now fade in on the game's live pose within 0.12 s. The Crossfade only sets how long a trick takes to hand back to the game on the ground.
- **Offset joints in packs and new tricks.** Same cause: the held pose. Joints a trick has no keys for are still left to the game, as before.
- **Board and body in step.** The board fades the same way as the body. Off a rail it no longer runs the grind hand-over while the body fades; on a rail both hand over as before.

Tested in game with the same saved tricks (kickflip, heelflip, ollies, a manual) and a Crossfade of 0.8. On 1.1 the feet were through the deck in 38% of air frames, up to 24 cm deep. On 1.1.1 they never were.

Thanks to Heatz for the bug report and Norms for the analysis.

## install or update

1. Use a working **ReSkate 2.1.3** installation. Coming from a build older than 1.1, follow the full steps in the [1.1 release](https://github.com/FourthStudio/ReSkate-Animator-Studio/releases/tag/1.1) first.
2. Close the game and launcher. Back up `ReSkate.dll`.
3. Download **ReSkate.dll** from this release and replace the DLL beside `ReSkateLauncher.exe`.
4. Launch through `ReSkateLauncher.exe`.

Your saved animations, packs and playlists in `ReSkate.Tricks` stay as they are.

Optional integrity check in PowerShell:

```powershell
Get-FileHash .\ReSkate.dll -Algorithm SHA256
```

Compare with `ReSkate.dll.sha256`.

If something looks wrong, reproduce it, then use **REPORT A BUG** and include the animation name, stance, body size and what happened. The ZIP stays local until you share it.

[installation and usage](https://github.com/FourthStudio/ReSkate-Animator-Studio#readme) · [heatz's tutorial](https://www.youtube.com/watch?v=cOBGvT7y45I) · [discord](https://discord.gg/zjg262qvw6)
