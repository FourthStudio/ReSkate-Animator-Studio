# animator studio 1.2

A big update: smoother transitions in and out of tricks, a new bail logic, the TRICKS tab as a folder, an easier LIBRARY, the MARKET as a magazine and shop, fixes for the bugs reported on Discord, fewer frame spikes, and an updater that installs itself. Built for **ReSkate 2.1.4**. Your packs and saved tricks need no changes.

## transitions

- **From what you see into what comes next.** Every change of what plays now starts from the pose on screen and its speed, and eases onto the game's live pose. This covers a trick into a grind, slide, manual, landing or the next pop, and a grind or manual into a trick. The game's own pose in between never shows. Your Crossfade still sets how long it takes. SMOOTH only; CLASSIC is unchanged.
- **Feet on the board.** During a change, a standing foot moves in the frame of the board as it is drawn. The soles are checked against the deck, kicks included, and never go into the grip.
- **Ready before the contact.** In the last moment before a landing or a rail, the feet go onto the board where the game wants them, so a lock or a touchdown has little left to cover.
- **Keys before the pop.** A trick can start at the flick, before takeoff, so a wind-up keyed before the pop plays in game. The timeline marks what never plays.
- **Rails.** Body and feet use the same board as the one drawn (a grind loop's tilted board seated the feet on a flat one). Legs and board hand over no earlier than the trick's catch. A lock before the catch takes the game's slower blend for the arms and back.
- Each change has its own switch in `anim_studio.cpp`. Bug reports gain columns for it: how far apart the poses were at each change, and how close each sole came to the deck.

## bails

Rewritten after measuring 70 recorded bails. The game holds a joint that turns slowly where it is, whatever is pushed on it. That is why hands grabbed nothing, arms stayed up in the air, and arms flicked.

- The fall stays physical: heavy limbs, the arm catch from your momentum, the head guard, flinches and the board kick.
- Every reaction steers motion that is still there. The reach to a hurt spot starts while he still slides, nearest hand first, and ends on the spot, where the game's hold keeps it. Bruises are rubbed. Racked means a curl on the rail, then a full curl on the ground.
- Any arm or leg left up is brought down while it still moves.
- SETTINGS > BAILS keeps its sliders.

## tricks tab

- **A folder.** Show your tricks as ICONS (the trick cards, groups folded), a LIST or DETAILS with sortable columns. Group by trick, kind or pack, or not at all.
- **FILTER.** By kind, stance, pop, pack, maker, in a playlist or not, editable or locked, each with a count.
- **Choose several.** Ctrl or Shift click, or Ctrl+A, then copy them, add them to a playlist or pack, set their pop, or delete them.
- Fast with hundreds of tricks: only what is on screen is drawn.
- **Editor tour.** The first time the editor opens, a 7-step tour shows each part. Replay it from SETTINGS > STUDIO or HOW IT WORKS.

## library

- **Drag to reorder.** Drag an entry by its dots: the others shift, nothing swaps. With a pad: A picks it up, up and down move it, A drops it. UNDO puts it back.
- **WHAT PLAYS.** The strip at the top shows what plays by default and on a button, with a CHANGE for each, a PLAYBACK switch and a GUIDE.
- **+ ADD A BUTTON** sets a playlist to play while a button is held.
- **"Add it to a playlist?"** after you get a pack in the MARKET and after you save a new trick.
- The default playlist opens on its own. The setup guide follows what is set.
- One set of words everywhere: the DEFAULT playlist, PRIORITY / IN TURNS / SHUFFLE, and the LIBRARY.

## market

- A magazine and shop front: the month's cover with GET IT, SHOP THE MARKET tiles (trending, new, most liked, verified, the crew), trick types with counts, MAKER OF THE MONTH, all-time lists.
- Every card has a working GET / UPDATE button, likes and downloads.
- **BROWSE** with a side list of sorts and filters, and removable filter tags.
- **Pack pages** like a shop's product page: cover, made by, versions, what is in it, tricks by type, more from the maker, you might also like.
- A page for every maker.
- Names with accents and Cyrillic show as written.

## fixed

- **Locked packs.** A trick its maker locked can only be previewed: it can't be opened to edit, copied (COPY, COPY TO, keys or poses), renamed, saved under your name or shared, by any path.
- **COPY TO** always makes a new copy beside the versions you have, and opens it. Before, it could open an existing version instead.
- **Studio room.** The walk to the room now works on keyboard and mouse and with PlayStation pads. Before, it only worked once the game had seen an Xbox pad.
- **Controller in the editor.** MOVE on a hand or foot bends the limb, like the mouse, instead of pulling the foot off the leg. A drifting stick no longer keys joints by itself.
- **Downloads.** A download only gives up when it truly stalls. It resumes where it stopped, retries a bad connection, and is checked before it is installed. Packs can be up to 512 MB.
- **Frame spikes.** The check that matches game pieces in your playlists ran on every animation update and grew with the playlist: 7-14 ms with 100-200 pieces, now under 1.2 ms. Editing a playlist, pressing the modifier button and installing a pack no longer stall the game.
- Many layout fixes: labels fitted to their buttons, brighter help text, lists that fade at their edges instead of cutting rows, nothing under the shortcuts or THIS TRICK at 16:10 and 4:3.

## updater

- **UPDATE NOW** downloads, verifies and installs the new `ReSkate.dll` in the folder the game loaded it from. The card shows each step. The old DLL is kept as `ReSkate.dll.previous` and put back if anything fails.
- Then **KEEP PLAYING** or **RESTART NOW**.
- The same card is on the ReSkate menu's ANIMATOR STUDIO page and in SETTINGS > UPDATES.

## install or update

1. Use a working **ReSkate 2.1.4** installation.
2. Close the game and launcher. Back up `ReSkate.dll`.
3. Download **ReSkate.dll** from this release and replace the DLL beside `ReSkateLauncher.exe`.
4. Launch through `ReSkateLauncher.exe`.

From 1.2 on, the studio can update itself: SETTINGS > UPDATES.

Your saved animations, packs and playlists in `ReSkate.Tricks` stay as they are.

Optional integrity check in PowerShell:

```powershell
Get-FileHash .\ReSkate.dll -Algorithm SHA256
```

Compare with `ReSkate.dll.sha256`.
