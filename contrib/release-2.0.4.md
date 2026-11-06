# animator studio 2.0.4

A fix on 2.0.3, built for **ReSkate 2.1.4-2**. SETTINGS > UPDATES > UPDATE NOW installs it. Your tricks, packs and playlists stay as they are.

## fixed

- **Down a drop, a trick could skip its landing.** When the game's own move drifts away from the trick in the air (a BS 180 kickflip turns you off the kickflip's clip), the trick goes on at its own time. Down a longer drop it then jumped to its end on the touchdown and let go at once, so its landing never played. Its landing plays from the touchdown again.
- **A twitch of the feet as a trick popped.** For the first frames of every pop the studio thought the landing was already there and put the feet back on the board as the game had them. Keyed feet now go where they were made to from the pop.
