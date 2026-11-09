# animator studio 2.1.3

A fix for Linux, built for **ReSkate 2.1.4-4**. SETTINGS > UPDATES > UPDATE NOW installs it. Your tricks, packs and playlists stay as they are.

## fixed

- **On Linux (Proton) the game no longer eats all your RAM.** Since 1.1, the studio mutes the game's sound while it is open, and to do that it looks at the game's audio sessions every few seconds. Under Proton each of those looks leaked memory, so the game grew to 20 GB within two minutes and kept going to 50 GB and more. Under Proton the studio now leaves the game's sound alone, so on Linux the game's sound keeps playing while the studio is open.
- **On Windows the studio no longer keeps looking for a sound to unmute at every start** after a run where it had nothing to mute.

Thanks to the Linux player who sent us their logs and ran a memory test build.
