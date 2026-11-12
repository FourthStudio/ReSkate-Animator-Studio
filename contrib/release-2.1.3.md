# animator studio 2.1.3

The Animator Studio on **ReSkate 2.1.4-5 (hotfix 2)**: everything ReSkate's newest update brings, with the studio in it, a new look for the ANIMATOR page, and a fix for Linux. Your tricks, packs and playlists stay as they are.

## how to get it

- **Still on Animator Studio 2.1.2?** SETTINGS > UPDATES > UPDATE NOW installs it.
- **Let the ReSkate launcher update to 2.1.4-5?** That put base ReSkate's DLL back, so the studio is gone. Download `ReSkate.dll` from this release and put it in your ReSkate folder in place of the one there, as when you first installed the studio. Turn the launcher's ReSkate updates off, or it swaps the DLL back on the next start.

## changed

- **The ANIMATOR page in the ReSkate menu has the studio's look.** Switches that slide, a row of dots for each choice, a real bar for Crossfade, a line under every setting saying what it does, and TRICKS, PACKS, LIBRARY and MARKET as tiles with the studio's icons.
- **What a setting does is told in a panel at the foot of the page**, for the mouse and the controller alike, instead of in a hover tip. A long tip used to run across the whole screen.
- **Hover tips on the other ReSkate menu pages wrap** to a width that reads.

## fixed

- **On Linux (Proton) the game no longer eats all your RAM.** Since 1.1, the studio mutes the game's sound while it is open, and to do that it looks at the game's audio sessions every few seconds. Under Proton each of those looks leaked memory, so the game grew to 20 GB within two minutes and kept going to 50 GB and more. Under Proton the studio now leaves the game's sound alone, so on Linux the game's sound keeps playing while the studio is open.
- **On Windows the studio no longer keeps looking for a sound to unmute at every start** after a run where it had nothing to mute.

Everything else in ReSkate 2.1.4-5 and its hotfixes (the menu changes from players' feedback, the tags' backup copy, fewer connection failures, and the rest) comes from the [ReSkate 2.1.4-5 (hotfix 2) release](https://github.com/Dingo-Shenanigans/ReSkate/releases/tag/v2.1.4-5-hotfix-2).

Thanks to the Linux player who sent us their logs and ran a memory test build.
