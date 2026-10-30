# Experimental trick modifiers

This is a local experiment based on the Studio 1.0.39 release-audit fixes and ReSkate 2.1.3.
It is not a public release. Use the DLL in a separate test installation and keep a copy of its
previous DLL and `ReSkate.Tricks` folder. No game assets or animation library are included.

## Kickflip and dolphin flip without replacing each other

1. Keep your regular playlist selected and playing, with Studio gameplay playback enabled.
2. Save your dolphin animation as a variation of **Kickflip**, using its kickflip input slot.
   Giving it the display name Dolphin Flip does not change the input recognised by the game.
3. Put that saved animation in a separate playlist, for example Specials. Any saved input trick can
   go here; the modifier tag is not required. Do not switch Specials on as the regular playlist.
4. In **F8 > SETTINGS > TRICKS**, select Specials under **Modifier playlist** and choose
   **EXPERIMENTAL - Modifier bind**, for example RB / R1. The default is OFF.
5. Enable **Replace native button** if the game should stop using that button during gameplay.
   This is useful for RB / R1, which otherwise also triggers the native darkflip action.
6. Close F8. Do a kickflip normally, then do a kickflip while holding the modifier **before takeoff**.
   The first uses your regular playlist; the second uses the eligible Kickflip variation in Specials.

The modifier route is sampled at takeoff. Releasing the button in the air does not switch it back;
pressing it after takeoff does not switch a normal trick into a modifier trick. A missing or
ineligible modifier variation falls back to the regular playlist, then the native animation.
Pop, entry and stance matching use Studio's existing rules, including Front foot mirroring.
Each playlist keeps its own Fixed / Shuffle / In turns mode and variant history.

## Author hints and automatic playlist creation

In **THIS TRICK**, enable **MODIFIER HINT** and save the animation. The hint travels with that
animation when its pack is shared. It does not prevent normal playback or require a modifier.

**SETTINGS > TRICKS > Modifier playlist from tags** collects saved tagged animations into a new
playlist and assigns that playlist to the modifier. It leaves the regular playlist selected and
keeps your chosen button. If the bind is OFF, choose a button to enable it. This is a collection
of the tags at the time you press the button; later saves do not silently rewrite that playlist.
You can add untagged tricks, remove entries, rename it, or use it as a regular playlist.

## Boundaries of this prototype

- One modifier playlist and one controller button: shoulders, face buttons or stick clicks.
  Keyboard keys, triggers and multiple simultaneous modifiers are not implemented.
- This routes input-recognised air tricks. Native grind/manual/grab loops and micro animations
  stay on the regular playlist. Adding these to Specials does not enable modifier routing for them.
- TEST IN GAME takes priority over modifier playback. Leave test mode to compare both playlists.
- The local bind is stored in `ReSkate.Tricks/presets/modifier-input.local`, outside pack JSON
  discovery and pack exports. Other people's packs do not assign or overwrite your controls.
- Changing the bind releases the old capture while the new playlist loads. Deleting its playlist
  disables capture; renaming it updates the assignment. An empty or native-loop-only modifier
  playlist does not claim a button.
- Native input filtering targets XInput and the existing DualShock 4 / DualSense HID paths.
  Menus, F8, focus loss, disconnection and a stopped client tick should release capture. Physical
  controllers, Steam Input/remappers and the native darkflip action still need live testing.

## Live acceptance test

1. Compare normal Kickflip against Kickflip + modifier using visibly different saved edits.
2. Release during flight, then press only after takeoff on another attempt: neither should swap
   the chosen route halfway through. Repeat from normal and switch stance, with different pops.
3. Try a trick absent from Specials: the regular playlist must still work. Try an empty playlist
   and one containing only native loops: native controls must stay available.
4. With Replace native button ON, verify the native RB darkflip action is suppressed while a
   modifier trick still plays. With it OFF, verify the native action returns.
5. Open native menus and F8, switch windows, disconnect/reconnect the pad, change the bind,
   rename/delete the playlist, and disable the feature. Check buttons are restored each time.
6. Check Shuffle / In turns in each playlist independently. Native grind/grab/manual animations
   must keep the regular playlist behaviour, including a grab-style change during one hold.
7. If playback differs from the editor, export diagnostics from F8 > PLAYBACK after reproducing
   it. Selected modifier tricks include `[modifier playlist]` in the playback decision.

Automated coverage checks route preference/fallback with the real stance matcher, metadata
round trips, bind validation and selective Sony report filtering across USB, Bluetooth simple/full
and padded DS4 reads. `modifier.2` also filters the final Steam Overlay XInput result: Steam may
answer from its own cached pad after an inner XInput hook, which let RB/R1 reach the native darkcatch
in the first prototype. Direct XInput and Steam now use the same final filtering step.

From a Visual Studio developer shell, run `python contrib/check_studio_modifier_steam.py` to compile
the production Steam-front hook against a cached-pad provider. It must suppress R1 for the game,
retain A, analog input and Studio's raw R1 read, and restore native R1 when the modifier is OFF.
The regular `trick_modifiers` CTest suite covers the shared final filtering step and menu capture.

Automated tests do not establish that the native game input is suppressed
on a physical controller or that a visual animation plays correctly in the game.
